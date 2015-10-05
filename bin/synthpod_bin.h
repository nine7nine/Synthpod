/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#ifndef SYNTHPOD_COMMON_H
#define SYNTHPOD_COMMON_H

#include <synthpod_app.h>

#	include <synthpod_ui.h>

#include <Eina.h>

#include <ext_urid.h>
#include <varchunk.h>
#include <lv2_osc.h>
#include <osc.h>

#include <synthpod_nsm.h>

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/time/time.h>

#define SEQ_SIZE 0x2000
#define CHUNK_SIZE 0x10000
#define JAN_1970 (uint64_t)0x83aa7e80
#define MAX_MSGS 10 //FIXME limit to how many events?

typedef enum _save_state_t save_state_t;

enum _save_state_t {
	SAVE_STATE_INTERNAL = 0,
	SAVE_STATE_NSM,
	SAVE_STATE_JACK
};

typedef struct _bin_t bin_t;

struct _bin_t {
	ext_urid_t *ext_urid;
	
	sp_app_t *app;
	sp_app_driver_t app_driver;
	
	varchunk_t *app_to_worker;
	varchunk_t *app_from_worker;
	varchunk_t *app_to_log;

	varchunk_t *app_from_com;

	char *path;
	synthpod_nsm_t *nsm;

	bool has_gui;
	sp_ui_t *ui;
	sp_ui_driver_t ui_driver;
	
	varchunk_t *app_to_ui;
	varchunk_t *app_from_ui;
	
	Ecore_Animator *ui_anim;
	Evas_Object *win;
	
	volatile int worker_dead;
	Eina_Thread worker_thread;
	Eina_Semaphore worker_sem;
	bool worker_sem_needs_release;

	LV2_URID log_entry;
	LV2_URID log_error;
	LV2_URID log_note;
	LV2_URID log_trace;
	LV2_URID log_warning;
};

// non-rt ui-thread
static void
_ui_delete_request(void *data, Evas_Object *obj, void *event)
{
	elm_exit();	
}

static void
_ui_close(void *data)
{
	//printf("_ui_close\n");
	elm_exit();
}

	static void
_ui_opened(void *data, int status)
{
	bin_t *bin = data;

	//printf("_ui_opened: %i\n", status);
	synthpod_nsm_opened(bin->nsm, status);
}

// non-rt ui-thread
static Eina_Bool
_ui_animator(void *data)
{
	bin_t *bin = data;

	size_t size;
	const LV2_Atom *atom;
	unsigned n = 0;
	while((atom = varchunk_read_request(bin->app_to_ui, &size))
		&& (n++ < MAX_MSGS) )
	{
		sp_ui_from_app(bin->ui, atom);
		varchunk_read_advance(bin->app_to_ui);
	}

	return EINA_TRUE; // continue animator
}

// non-rt worker-thread
static void *
_worker_thread(void *data, Eina_Thread thread)
{
	bin_t *bin = data;
	
	pthread_t self = pthread_self();

	struct sched_param schedp;
	memset(&schedp, 0, sizeof(struct sched_param));
	schedp.sched_priority = 60; //TODO make configurable
	
	if(schedp.sched_priority)
	{
		if(pthread_setschedparam(self, SCHED_RR, &schedp))
			fprintf(stderr, "pthread_setschedparam error\n");
	}

	while(!bin->worker_dead)
	{
		eina_semaphore_lock(&bin->worker_sem);

		size_t size;
		const void *body;
		while((body = varchunk_read_request(bin->app_to_worker, &size)))
		{
			sp_worker_from_app(bin->app, size, body);
			varchunk_read_advance(bin->app_to_worker);
		}

		const char *trace;
		while((trace = varchunk_read_request(bin->app_to_log, &size)))
		{
			fprintf(stderr, "[Trace] %s\n", trace);

			varchunk_read_advance(bin->app_to_log);
		}
	}

	return NULL;
}

// rt
static void *
_app_to_ui_request(size_t size, void *data)
{
	bin_t *bin = data;

	return varchunk_write_request(bin->app_to_ui, size);
}
static void
_app_to_ui_advance(size_t size, void *data)
{
	bin_t *bin = data;

	// copy com events to com buffer
	const LV2_Atom_Object *obj = bin->app_to_ui->buf + bin->app_to_ui->head
		+ sizeof(varchunk_elmnt_t);
	if(sp_app_com_event(bin->app, obj->body.id))
	{
		void *dst;
		if((dst = varchunk_write_request(bin->app_from_com, size)))
		{
			memcpy(dst, obj, size);
			varchunk_write_advance(bin->app_from_com, size);
		}
	}

	varchunk_write_advance(bin->app_to_ui, size);
}

// non-rt ui-thread
static void *
_ui_to_app_request(size_t size, void *data)
{
	bin_t *bin = data;

	void *ptr;
	do
	{
		ptr = varchunk_write_request(bin->app_from_ui, size);
	}
	while(!ptr); // wait until there is enough space

	return ptr;
}
static void
_ui_to_app_advance(size_t size, void *data)
{
	bin_t *bin = data;

	varchunk_write_advance(bin->app_from_ui, size);
}

// rt
static void *
_app_to_worker_request(size_t size, void *data)
{
	bin_t *bin = data;

	return varchunk_write_request(bin->app_to_worker, size);
}
static void
_app_to_worker_advance(size_t size, void *data)
{
	bin_t *bin = data;

	varchunk_write_advance(bin->app_to_worker, size);
	bin->worker_sem_needs_release = true;
}

// non-rt worker-thread
static void *
_worker_to_app_request(size_t size, void *data)
{
	bin_t *bin = data;

	void *ptr;
	do
	{
		ptr = varchunk_write_request(bin->app_from_worker, size);
	}
	while(!ptr); // wait until there is enough space

	return ptr;
}
static void
_worker_to_app_advance(size_t size, void *data)
{
	bin_t *bin = data;

	varchunk_write_advance(bin->app_from_worker, size);
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_vprintf(void *data, LV2_URID type, const char *fmt, va_list args)
{
	bin_t *bin = data;

	if(type == bin->log_trace)
	{
		char *trace;
		if((trace = varchunk_write_request(bin->app_to_log, 1024)))
		{
			vsprintf(trace, fmt, args);

			size_t written = strlen(trace) + 1;
			varchunk_write_advance(bin->app_to_log, written);
			bin->worker_sem_needs_release = true;
		}
	}
	else // !log_trace
	{
		const char *type_str = NULL;
		if(type == bin->log_entry)
			type_str = "Entry";
		else if(type == bin->log_error)
			type_str = "Error";
		else if(type == bin->log_note)
			type_str = "Note";
		else if(type == bin->log_warning)
			type_str = "Warning";

		//TODO send to UI?

		fprintf(stderr, "[%s] ", type_str);
		vfprintf(stderr, fmt, args);
		fputc('\n', stderr);

		return 0;
	}

	return -1;
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_printf(void *data, LV2_URID type, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(data, type, fmt, args);
  va_end(args);

	return ret;
}

static int
_show(void *data)
{
	bin_t *bin = data;

	if(bin->win)
		evas_object_show(bin->win);
	
	return 0;
}

static int
_hide(void *data)
{
	bin_t *bin = data;

	if(bin->win)
		evas_object_hide(bin->win);

	return 0;
}

static void
bin_init(bin_t *bin)
{
	// varchunk init
	bin->app_to_ui = varchunk_new(CHUNK_SIZE);
	bin->app_from_ui = varchunk_new(CHUNK_SIZE);
	bin->app_to_worker = varchunk_new(CHUNK_SIZE);
	bin->app_from_worker = varchunk_new(CHUNK_SIZE);
	bin->app_to_log = varchunk_new(CHUNK_SIZE);
	bin->app_from_com = varchunk_new(CHUNK_SIZE);

	bin->ext_urid = ext_urid_new();
	LV2_URID_Map *map = ext_urid_map_get(bin->ext_urid);
	LV2_URID_Unmap *unmap = ext_urid_unmap_get(bin->ext_urid);

	bin->log_entry = map->map(map->handle, LV2_LOG__Entry);
	bin->log_error = map->map(map->handle, LV2_LOG__Error);
	bin->log_note = map->map(map->handle, LV2_LOG__Note);
	bin->log_trace = map->map(map->handle, LV2_LOG__Trace);
	bin->log_warning = map->map(map->handle, LV2_LOG__Warning);
	
	bin->app_driver.map = map;
	bin->app_driver.unmap = unmap;
	bin->app_driver.log_printf = _log_printf;
	bin->app_driver.log_vprintf = _log_vprintf;
	bin->app_driver.to_ui_request = _app_to_ui_request;
	bin->app_driver.to_ui_advance = _app_to_ui_advance;
	bin->app_driver.to_worker_request = _app_to_worker_request;
	bin->app_driver.to_worker_advance = _app_to_worker_advance;
	bin->app_driver.to_app_request = _worker_to_app_request;
	bin->app_driver.to_app_advance = _worker_to_app_advance;

	bin->ui_driver.map = map;
	bin->ui_driver.unmap = unmap;
	bin->ui_driver.to_app_request = _ui_to_app_request;
	bin->ui_driver.to_app_advance = _ui_to_app_advance;
	bin->ui_driver.instance_access = 1; // enabled

	bin->ui_driver.opened = _ui_opened;
	bin->ui_driver.close = _ui_close;
}

static void
bin_run(bin_t *bin, char **argv, const synthpod_nsm_driver_t *nsm_driver)
{
	// create main window
	bin->ui_anim = ecore_animator_add(_ui_animator, bin);

	bin->win = NULL;
	if(bin->has_gui)
	{
		bin->win = elm_win_util_standard_add("synthpod", "Synthpod");
		if(bin->win)
		{
			evas_object_smart_callback_add(bin->win, "delete,request", _ui_delete_request, NULL);
			evas_object_resize(bin->win, 1280, 720);
			evas_object_show(bin->win);
		}
	}

	// ui init
	bin->ui = sp_ui_new(bin->win, NULL, &bin->ui_driver, bin, 1);

	// NSM init
	const char *exe = strrchr(argv[0], '/');
	exe = exe ? exe + 1 : argv[0]; // we only want the program name without path
	bin->nsm = synthpod_nsm_new(exe, argv[optind], nsm_driver, bin); //TODO check

	// init semaphores
	eina_semaphore_new(&bin->worker_sem, 0);

	// threads init
	Eina_Bool status = eina_thread_create(&bin->worker_thread,
		EINA_THREAD_URGENT, -1, _worker_thread, bin);
	if(!status)
		fprintf(stderr, "creation of worker thread failed\n");

	// main loop
	elm_run();

	// ui deinit
	sp_ui_free(bin->ui);

	if(bin->win)
		evas_object_del(bin->win);

	if(bin->ui_anim)
		ecore_animator_del(bin->ui_anim);
}

static void
bin_stop(bin_t *bin)
{
	// threads deinit
	bin->worker_dead = 1; // atomic operation
	eina_semaphore_release(&bin->worker_sem, 1);
	eina_thread_join(bin->worker_thread);

	// NSM deinit
	synthpod_nsm_free(bin->nsm);

	// deinit semaphores
	eina_semaphore_free(&bin->worker_sem);

	if(bin->path)
		free(bin->path);
}

static void
bin_deinit(bin_t *bin)
{
	// synthpod deinit
	sp_app_free(bin->app);

	// ext_urid deinit
	ext_urid_free(bin->ext_urid);

	// varchunk deinit
	varchunk_free(bin->app_to_ui);
	varchunk_free(bin->app_from_ui);
	varchunk_free(bin->app_to_log);
	varchunk_free(bin->app_to_worker);
	varchunk_free(bin->app_from_worker);
	varchunk_free(bin->app_from_com);
}

static inline void
bin_process_pre(bin_t *bin, uint32_t nsamples, int paused)
{
	// read events from worker
	if(!paused) // aka not saving state
	{
		size_t size;
		const void *body;
		unsigned n = 0;
		while((body = varchunk_read_request(bin->app_from_worker, &size))
			&& (n++ < MAX_MSGS) )
		{
			sp_app_from_worker(bin->app, size, body);
			varchunk_read_advance(bin->app_from_worker);
		}
	}

	// run synthpod app pre
	sp_app_run_pre(bin->app, nsamples);

	// read events from UI
	if(!paused) // aka not saving state
	{
		size_t size;
		const LV2_Atom *atom;
		unsigned n = 0;
		while((atom = varchunk_read_request(bin->app_from_ui, &size))
			&& (n++ < MAX_MSGS) )
		{
			sp_app_from_ui(bin->app, atom);
			varchunk_read_advance(bin->app_from_ui);
		}
	}
	
	// run synthpod app post
	sp_app_run_post(bin->app, nsamples);
}

static inline void
bin_process_post(bin_t *bin)
{
	if(bin->worker_sem_needs_release)
	{
		eina_semaphore_release(&bin->worker_sem, 1);
		bin->worker_sem_needs_release = false;
	}
}

#endif
