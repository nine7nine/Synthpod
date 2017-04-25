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
#include <time.h>

#include <sandbox_slave.h>
#include <lv2_external_ui.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

typedef struct _app_t app_t;

struct _app_t {
	sandbox_slave_t *sb;

	LV2_External_UI_Host host;
	LV2_External_UI_Widget *widget;
};

static _Atomic bool done = ATOMIC_VAR_INIT(false);

static inline void
_sig(int signum)
{
	atomic_store_explicit(&done, true, memory_order_relaxed);
}

static inline void
_ui_closed(LV2UI_Controller controller)
{
	sandbox_slave_t *sb = controller;
	(void)sb;

	atomic_store_explicit(&done, true, memory_order_relaxed);
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

		clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &to, NULL);

		if(sandbox_slave_recv(sb))
			atomic_store_explicit(&done, true, memory_order_relaxed);
		LV2_EXTERNAL_UI_RUN(app->widget);
	}

	LV2_EXTERNAL_UI_HIDE(app->widget);
}

static const sandbox_slave_driver_t driver = {
	.init_cb = _init,
	.run_cb = _run,
	.deinit_cb = NULL,
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
		return 0;
	}

	return -1;
}
