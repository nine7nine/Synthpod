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

#define CROSS_CLOCK_IMPLEMENTATION
#include <cross_clock/cross_clock.h>

#include <sandbox_slave.h>
#include <lv2_external_ui.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

typedef struct _app_t app_t;

struct _app_t {
	sandbox_slave_t *sb;

	LV2_External_UI_Host host;
	LV2_External_UI_Widget *widget;
	cross_clock_t clk_mono;
};

static atomic_bool done = ATOMIC_VAR_INIT(false);

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

	cross_clock_init(&app->clk_mono, CROSS_CLOCK_MONOTONIC);

	return 0; //success
}

static inline void
_run(sandbox_slave_t *sb, float update_rate, void *data)
{
	app_t *app = data;
	const unsigned ns = 1000000000 / update_rate;
	struct timespec to;
	cross_clock_gettime(&app->clk_mono, &to);

	while(!atomic_load_explicit(&done, memory_order_relaxed))
	{
		to.tv_nsec += ns;
		while(to.tv_nsec >= 1000000000)
		{
			to.tv_nsec -= 1000000000;
			to.tv_sec += 1;
		}

		cross_clock_nanosleep(&app->clk_mono, true, &to);

		if(sandbox_slave_recv(sb))
			atomic_store_explicit(&done, true, memory_order_relaxed);
		LV2_EXTERNAL_UI_RUN(app->widget);
	}

	LV2_EXTERNAL_UI_HIDE(app->widget);
}

static inline void
_deinit(void *data)
{
	app_t *app = data;

	cross_clock_deinit(&app->clk_mono);
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
	int res;
	
	app.sb = sandbox_slave_new(argc, argv, &driver, &app, &res);
	if(app.sb)
	{
		sandbox_slave_run(app.sb);
		sandbox_slave_free(app.sb);
		return res;
	}

	return res;
}
