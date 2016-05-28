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
#include <signal.h>

#include <sandbox_slave.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

typedef struct _app_t app_t;

struct _app_t {
	sandbox_slave_t *sb;
	LV2UI_Handle *handle;
	const LV2UI_Idle_Interface *idle_iface;
	const LV2UI_Show_Interface *show_iface;
};

static _Atomic bool done = ATOMIC_VAR_INIT(false);

static inline void
_sig(int signum)
{
	atomic_store_explicit(&done, true, memory_order_relaxed);
}

static inline int
_init(sandbox_slave_t *sb, void *data)
{
	app_t *app= data;

	signal(SIGINT, _sig);

	void *widget = NULL;
	if(!(app->handle = sandbox_slave_instantiate(sb, NULL, &widget)))
		return -1;

	app->idle_iface = sandbox_slave_extension_data(sb, LV2_UI__idleInterface);
	app->show_iface = sandbox_slave_extension_data(sb, LV2_UI__showInterface);

	if(app->show_iface)
		app->show_iface->show(app->handle);

	return 0; //success
}

static inline void
_run(sandbox_slave_t *sb, void *data)
{
	app_t *app = data;

	while(!atomic_load_explicit(&done, memory_order_relaxed))
	{
		usleep(40000); // 25 fps

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

	if(app->show_iface)
		app->show_iface->hide(app->handle);
}

static const sandbox_slave_driver_t driver = {
	.init_cb = _init,
	.run_cb = _run,
	.deinit_cb = _deinit,
	.resize_cb = NULL
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
		printf("bye from %s\n", argv[0]);
		return 0;
	}

	printf("fail from %s\n", argv[0]);
	return -1;
}
