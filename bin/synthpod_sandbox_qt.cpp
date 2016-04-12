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

#if (SYNTHPOD_SANDBOX_QT == 4)
#	include <QtGui/QApplication>
#	include <QtGui/QMainWindow>
#elif (SYNTHPOD_SANDBOX_QT == 5)
#	include <QtWidgets/QApplication>
#	include <QtWidgets/QMainWindow>
#else
#	error "SYNTHPOD_QT is invalid"
#endif

typedef struct _app_t app_t;

struct _app_t {
	sandbox_slave_t *sb;

	QApplication *a;
	QMainWindow *win;
	QWidget *widget;
};

//TODO handle SIGINT and gracefully terminate

static inline int
_init(sandbox_slave_t *sb, void *data)
{
	app_t *app= (app_t *)data;

	int argc = 0;
	app->a = new QApplication(argc, NULL, true);
	app->win = new QMainWindow();

	const LV2_Feature parent_feature = {
		.URI = LV2_UI__parent,
		.data = (void *)app->win
	};

	if(!sandbox_slave_instantiate(sb, &parent_feature, (void *)&app->widget))
		return -1;
	if(!app->widget)
		return -1;

	app->widget->show();
	app->win->setCentralWidget(app->widget);

	app->win->adjustSize();
	app->win->show();

	return 0;
}

static inline void
_run(sandbox_slave_t *sb, void *data)
{
	app_t *app = (app_t *)data;
	(void)sb;

	app->a->exec();
}

static inline void
_deinit(void *data)
{
	app_t *app = (app_t *)data;

	app->win->hide();
	delete app->win;
	delete app->a;
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
