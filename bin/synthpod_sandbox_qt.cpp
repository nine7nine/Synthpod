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

#include <atomic>

#include <sandbox_slave.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#if (SYNTHPOD_SANDBOX_QT == 4)
#	include <QtGui/QApplication>
#	include <QtGui/QMainWindow>
#elif (SYNTHPOD_SANDBOX_QT == 5)
#	include <QtWidgets/QApplication>
#	include <QtWidgets/QMainWindow>
#else
#	error "SYNTHPOD_SANDBOX_QT is invalid"
#endif

typedef struct _app_t app_t;

class MyWindow : public QMainWindow
{
	Q_OBJECT

public:
	MyWindow(sandbox_slave_t *_sb);
	~MyWindow();
	void start(float update_rate);

protected:
	void timerEvent(QTimerEvent *event);

protected:
	sandbox_slave_t *sb;
	int timer_id;
};


struct _app_t {
	sandbox_slave_t *sb;

	MyWindow *win;
	QWidget *widget;
};

static QApplication *a;
static std::atomic<bool> done = ATOMIC_VAR_INIT(false);

MyWindow::MyWindow(sandbox_slave_t *_sb)
	: QMainWindow(), sb(_sb)
{
}

MyWindow::~MyWindow()
{
	killTimer(timer_id);
}

void
MyWindow::start(float update_rate)
{
	timer_id = startTimer(1000 / update_rate);
}

void
MyWindow::timerEvent(QTimerEvent *event)
{
	(void)event;

	if(sandbox_slave_recv(sb))
		done.store(true, std::memory_order_relaxed);

	if(done.load(std::memory_order_relaxed))
		a->quit();
}

static inline void
_sig(int signum)
{
	(void)signum;
	done.store(true, std::memory_order_relaxed);
}

static inline int
_init(sandbox_slave_t *sb, void *data)
{
	app_t *app= (app_t *)data;

	signal(SIGINT, _sig);

	int argc = 0;
	a = new QApplication(argc, NULL, true);
	app->win = new MyWindow(sb);
	if(!app->win)
		return -1;

	const char *title = sandbox_slave_title_get(sb);
	if(title)
		app->win->setWindowTitle(title);

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
_run(sandbox_slave_t *sb, float update_rate, void *data)
{
	app_t *app = (app_t *)data;
	(void)sb;

	app->win->start(update_rate);
	a->exec();
}

static inline void
_deinit(void *data)
{
	app_t *app = (app_t *)data;

	app->win->hide();
	delete app->win;

	delete a;
}

static inline int
_request(void *data, LV2_URID key, size_t path_len, char *path)
{
	app_t *app = (app_t *)data;
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

	app.sb = sandbox_slave_new(argc, argv, &driver, &app, &res);
	if(app.sb)
	{
		sandbox_slave_run(app.sb);
		sandbox_slave_free(app.sb);
		return res;
	}

	return res;
}

#include <synthpod_sandbox_qt.moc>
