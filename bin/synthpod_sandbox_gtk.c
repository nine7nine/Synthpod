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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sandbox_slave.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop
#include <glib-unix.h>

typedef struct _wrap_t wrap_t;
typedef struct _app_t app_t;

struct _wrap_t {
	sandbox_slave_t *sb;
	app_t *app;
};

struct _app_t {
	sandbox_slave_t *sb;

	GtkWidget *win;
	GtkWidget *widget;
	guint timeout;
	guint signal;
};

static gboolean
_anim(void *data)
{
	wrap_t *wrap = data;

	if(sandbox_slave_recv(wrap->sb))
	{
		gtk_main_quit();
		wrap->app->win = NULL;
	}

	return true;
}

static void
_destroy(GtkWidget *widget, void *data)
{
	app_t *app = data;

  gtk_main_quit();
	app->win = NULL;
}

static gboolean
_sig(void *data)
{
	app_t *app = data;

	gtk_main_quit();
	app->win = NULL;

	return true;
}

static inline int
_init(sandbox_slave_t *sb, void *data)
{
	app_t *app= data;

	app->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if(!app->win)
	{
		fprintf(stderr, "gtk_window_new failed\n");
		goto fail;
	}
	g_signal_connect(G_OBJECT(app->win), "destroy",
		G_CALLBACK(_destroy), app);

	const char *title = sandbox_slave_title_get(sb);
	if(title)
		gtk_window_set_title(GTK_WINDOW(app->win), title);

	const LV2_Feature parent_feature = {
		.URI = LV2_UI__parent,
		.data = app->win
	};

	if(!sandbox_slave_instantiate(sb, &parent_feature, &app->widget))
		goto fail;
	if(!app->widget)
		goto fail;

	gtk_widget_set_can_focus(app->widget, true);
	gtk_widget_grab_focus(app->widget);

	gtk_container_add(GTK_CONTAINER(app->win), app->widget);
	gtk_widget_show_all(app->win);

	app->signal = g_unix_signal_add(SIGINT, _sig, app);
	if(!app->signal)
	{
		fprintf(stderr, "g_unix_signal_add failed\n");
		goto fail;
	}

	return 0; //success

fail:
	return -1;
}

static inline void
_run(sandbox_slave_t *sb, float update_rate, void *data)
{
	app_t *app = data;

	wrap_t wrap = {
		.app = app,
		.sb = sb
	};

	app->timeout = g_timeout_add(1000 / update_rate, _anim, &wrap); //FIXME check
	gtk_main();
}

static inline void
_deinit(void *data)
{
	app_t *app = data;

	if(app->timeout)
		g_source_remove(app->timeout);

	if(app->signal)
		g_source_remove(app->signal);

	if(app->win)
	{
		gtk_widget_hide(app->win);
		gtk_widget_destroy(app->win);
	}
}

static inline int
_request(void *data, LV2_URID key, size_t path_len, char *path)
{
	app_t *app = data;
	(void)app;

	FILE *fin = popen("zenity --file-selection", "r");
	const size_t len = fread(path, sizeof(char), path_len, fin);
	pclose(fin);

	if(len)
	{
		path[len] = '\0';
		return 0;
	}

	return 1;
}

static const sandbox_slave_driver_t driver = {
	.init_cb = _init,
	.run_cb = _run,
	.deinit_cb = _deinit,
	.resize_cb = NULL,
	.request_cb = _request
};

int
main(int argc, char **argv)
{
	static app_t app;
	int res;
	
	gtk_init(&argc, &argv);

	app.sb = sandbox_slave_new(argc, argv, &driver, &app, &res);
	if(app.sb)
	{
		sandbox_slave_run(app.sb);
		sandbox_slave_free(app.sb);
		return res;
	}

	return res;
}
