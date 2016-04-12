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
#include <signal.h>

#include <sandbox_slave.h>
#include <lv2_external_ui.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

typedef struct _app_t app_t;

struct _app_t {
	sandbox_slave_t *sb;

	LV2_External_UI_Host host;
	LV2_External_UI_Widget *widget;
};

static volatile bool done = false;

static inline void
_sig(int signum)
{
	done = true;
}

static inline void
_ui_closed(LV2UI_Controller controller)
{
	sandbox_slave_t *sb = controller;
	(void)sb;

	done = true;
}

static inline int
_init(sandbox_slave_t *sb, void *data)
{
	app_t *app= data;

	signal(SIGINT, _sig);

	app->host.ui_closed = _ui_closed;
	app->host.plugin_human_id = NULL; //FIXME

	const LV2_Feature parent_feature = {
		.URI = LV2_EXTERNAL_UI__Host,
		.data = &app->host
	};

	if(!sandbox_slave_instantiate(sb, &parent_feature, &app->widget))
		return -1;
	if(!app->widget)
		return -1;

	LV2_EXTERNAL_UI_SHOW(app->widget);

	return 0; //success
}

static inline void
_run(sandbox_slave_t *sb, void *data)
{
	app_t *app = data;

	while(!done)
	{
		usleep(40000); // 25 fps

		sandbox_slave_recv(sb);
		LV2_EXTERNAL_UI_RUN(app->widget);
		sandbox_slave_flush(sb);
	}
}

static inline void
_deinit(void *data)
{
	app_t *app = data;

	LV2_EXTERNAL_UI_HIDE(app->widget);
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
