/*
 * Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sandbox_slave.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <signal.h>

typedef struct _app_t app_t;

struct _app_t {
	sandbox_slave_t *sb;
	LV2UI_Handle *handle;
	const LV2UI_Idle_Interface *idle_iface;
	const LV2UI_Resize *resize_iface;

	xcb_connection_t *conn;
	xcb_screen_t *screen;
	xcb_drawable_t win;
	xcb_drawable_t widget;
	xcb_intern_atom_cookie_t cookie;
 	xcb_intern_atom_reply_t* reply;
	xcb_intern_atom_cookie_t cookie2;
 	xcb_intern_atom_reply_t* reply2;
	int w;
	int h;
};

static _Atomic bool done = ATOMIC_VAR_INIT(false);

static inline void
_sig(int signum)
{
	atomic_store_explicit(&done, true, memory_order_relaxed);
}

static inline int
_resize(void *data, int w, int h)
{
	app_t *app= data;

	app->w = w;
	app->h = h;

	const uint32_t values [2] = {app->w, app->h};
	xcb_configure_window(app->conn, app->win,
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
	xcb_flush(app->conn);

	return 0;
}

static inline int
_init(sandbox_slave_t *sb, void *data)
{
	app_t *app= data;

	signal(SIGINT, _sig);

  app->conn = xcb_connect(NULL, NULL);
  app->screen = xcb_setup_roots_iterator(xcb_get_setup(app->conn)).data;
  app->win = xcb_generate_id(app->conn);
  const uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  const uint32_t values [2] = {
		app->screen->white_pixel,
		XCB_EVENT_MASK_STRUCTURE_NOTIFY
	};

	app->w = 640;
	app->h = 360;
  xcb_create_window(app->conn, XCB_COPY_FROM_PARENT, app->win, app->screen->root,
		0, 0, app->w, app->h, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, app->screen->root_visual, mask, values);

	const char *title = sandbox_slave_title_get(sb);
	if(title)
		xcb_change_property(app->conn, XCB_PROP_MODE_REPLACE, app->win,
			XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
			strlen(title), title);

	app->cookie = xcb_intern_atom(app->conn, 1, 12, "WM_PROTOCOLS");
 	app->reply = xcb_intern_atom_reply(app->conn, app->cookie, 0);

	app->cookie2 = xcb_intern_atom(app->conn, 0, 16, "WM_DELETE_WINDOW");
	app->reply2 = xcb_intern_atom_reply(app->conn, app->cookie2, 0);

	xcb_change_property(app->conn,
		XCB_PROP_MODE_REPLACE, app->win, (*app->reply).atom, 4, 32, 1, &(*app->reply2).atom);
 
  xcb_map_window(app->conn, app->win);
  xcb_flush(app->conn);

	const LV2_Feature parent_feature = {
		.URI = LV2_UI__parent,
		.data = (void *)(uintptr_t)app->win
	};

	if(!(app->handle = sandbox_slave_instantiate(sb, &parent_feature, (uintptr_t *)&app->widget)))
		return -1;
	if(!app->widget)
		return -1;

	if(sandbox_slave_no_user_resize_get(sb))
	{
		xcb_size_hints_t hints;
		xcb_icccm_size_hints_set_min_size(&hints, app->w, app->h);
		xcb_icccm_size_hints_set_max_size(&hints, app->w, app->h);
		xcb_icccm_set_wm_size_hints(app->conn, app->win, XCB_ATOM_WM_NORMAL_HINTS, &hints);
	}

	app->idle_iface = sandbox_slave_extension_data(sb, LV2_UI__idleInterface);
	app->resize_iface = sandbox_slave_extension_data(sb, LV2_UI__resize);

	return 0;
}

static inline void
_run(sandbox_slave_t *sb, float update_rate, void *data)
{
	app_t *app = data;
	const unsigned ns = 1000000000 / update_rate;
	struct timespec to;
	clock_gettime(CLOCK_REALTIME, &to);

	while(!atomic_load_explicit(&done, memory_order_relaxed))
	{
		to.tv_nsec += ns;
		while(to.tv_nsec >= 1000000000)
		{
			to.tv_nsec -= 1000000000;
			to.tv_sec += 1;
		}

		xcb_generic_event_t *e;
		while((e = xcb_poll_for_event(app->conn)))
		{
			switch(e->response_type & ~0x80)
			{
				case XCB_CONFIGURE_NOTIFY:
				{
					const xcb_configure_notify_event_t *ev = (const xcb_configure_notify_event_t *)e;
					if( (app->w != ev->width) || (app->h != ev->height) )
					{
						app->w = ev->width;
						app->h = ev->height;

						const uint32_t values [2] = {app->w, app->h};
						xcb_configure_window(app->conn, app->widget,
							XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
							&values);
						xcb_flush(app->conn);

						if(app->resize_iface)
						{
							app->resize_iface->ui_resize(app->resize_iface->handle, app->w, app->h);
						}
					}
					break;
				}
				case XCB_CLIENT_MESSAGE:
				{
					const xcb_client_message_event_t *ev = (const xcb_client_message_event_t *)e;
					if(ev->data.data32[0] == (*app->reply2).atom)
						atomic_store_explicit(&done, true, memory_order_relaxed);
					break;
				}
			}
			free(e);
		}

		clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &to, NULL);

		sandbox_slave_recv(sb);
		if(app->idle_iface)
		{
			if(app->idle_iface->idle(app->handle))
				atomic_store_explicit(&done, true, memory_order_relaxed);
		}
		sandbox_slave_flush(sb);
	}
}

static inline void
_deinit(void *data)
{
	app_t *app = data;

	xcb_destroy_subwindows(app->conn, app->win);
	xcb_destroy_window(app->conn, app->win);
	xcb_disconnect(app->conn);
}

static const sandbox_slave_driver_t driver = {
	.init_cb = _init,
	.run_cb = _run,
	.deinit_cb = _deinit,
	.resize_cb = _resize
};

int
main(int argc, char **argv)
{
	static app_t app;

	app.sb = sandbox_slave_new(argc, argv, &driver, &app);
	if(app.sb)
	{
		sandbox_slave_run(app.sb);
		sandbox_slave_free(app.sb);
		return 0;
	}

	return -1;
}
