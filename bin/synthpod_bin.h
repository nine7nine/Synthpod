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

#include <stdatomic.h>

#include <symap.h>
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

typedef struct _light_sem_t light_sem_t;
typedef struct _bin_t bin_t;

struct _light_sem_t {
	Eina_Semaphore sem;
	_Atomic int count;
	int spin;
};

struct _bin_t {
	Symap *symap;
	LV2_URID_Map map;
	LV2_URID_Unmap unmap;
	atomic_flag map_lock;
	
	sp_app_t *app;
	sp_app_driver_t app_driver;
	
	varchunk_t *app_to_worker;
	varchunk_t *app_from_worker;
	varchunk_t *app_to_log;

	varchunk_t *app_from_com;

	bool advance_ui;
	varchunk_t *app_from_app;

	char *path;
	synthpod_nsm_t *nsm;

	bool has_gui;
	sp_ui_t *ui;
	sp_ui_driver_t ui_driver;
	
	varchunk_t *app_to_ui;
	varchunk_t *app_from_ui;
	
	Ecore_Animator *ui_anim;
	Evas_Object *win;
	
	_Atomic int worker_dead;
	Eina_Thread worker_thread;
	light_sem_t worker_sem;

	LV2_URID log_entry;
	LV2_URID log_error;
	LV2_URID log_note;
	LV2_URID log_trace;
	LV2_URID log_warning;

	LV2_Log_Log log;

	Eina_Thread self;
};

static inline void
_light_sem_init(light_sem_t *lsem, int count)
{
	assert(count >= 0);
	atomic_init(&lsem->count, 0);
	eina_semaphore_new(&lsem->sem, count);
	lsem->spin = 10000; //TODO make this configurable or self-adapting
}

static inline void
_light_sem_deinit(light_sem_t *lsem)
{
	eina_semaphore_free(&lsem->sem);
}

static inline void
_light_sem_wait_partial_spinning(light_sem_t *lsem)
{
	int old_count;
	int spin = lsem->spin;

	while(spin--)
	{
		old_count = atomic_load_explicit(&lsem->count, memory_order_relaxed);

		if(  (old_count > 0) && atomic_compare_exchange_strong_explicit(
			&lsem->count, &old_count, old_count - 1, memory_order_acquire, memory_order_acquire) )
		{
			return; // immediately return from wait as there was a new signal while spinning
		}

		atomic_signal_fence(memory_order_acquire); // prevent compiler from collapsing the loop
	}

	old_count = atomic_fetch_sub_explicit(&lsem->count, 1, memory_order_acquire);

	if(old_count <= 0)
		eina_semaphore_lock(&lsem->sem);
}

static inline bool
_light_sem_trywait(light_sem_t *lsem)
{
	int old_count = atomic_load_explicit(&lsem->count, memory_order_relaxed);

	return (old_count > 0) && atomic_compare_exchange_strong_explicit(
		&lsem->count, &old_count, old_count - 1, memory_order_acquire, memory_order_acquire);
}

static inline void
_light_sem_wait(light_sem_t *lsem)
{
	if(!_light_sem_trywait(lsem))
		_light_sem_wait_partial_spinning(lsem);
}

static inline void
_light_sem_signal(light_sem_t *lsem, int count)
{
	int old_count = atomic_fetch_add_explicit(&lsem->count, count, memory_order_release);
	int to_release = -old_count < count ? -old_count : count;

	if(to_release > 0) // old_count changed from (-1) to (0)
		eina_semaphore_release(&lsem->sem, to_release);
}

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
#if 0
		&& (n++ < MAX_MSGS) )
#else
		)
#endif
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

	while(!atomic_load_explicit(&bin->worker_dead, memory_order_relaxed))
	{
		_light_sem_wait(&bin->worker_sem);

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
			fprintf(stderr, "[Trace] %s", trace);

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
	_light_sem_signal(&bin->worker_sem, 1);
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

	Eina_Thread this = eina_thread_self();

	// check for trace mode AND DSP thread ID
	if( (type == bin->log_trace)
		&& !eina_thread_equal(this, bin->self) // not UI thread ID
		&& !eina_thread_equal(this, bin->worker_thread) ) // not worker thread ID
	{
		char *trace;
		if((trace = varchunk_write_request(bin->app_to_log, 1024)))
		{
			vsnprintf(trace, 1024, fmt, args);

			size_t written = strlen(trace) + 1;
			varchunk_write_advance(bin->app_to_log, written);
			_light_sem_signal(&bin->worker_sem, 1);
		}
	}
	else // !log_trace OR not DSP thread ID
	{
		const char *prefix = "[Log]   ";
		if(type == bin->log_trace)
			prefix = "[Trace] ";
		else if(type == bin->log_entry)
			prefix = "[Entry] ";
		else if(type == bin->log_error)
			prefix = "[Error] ";
		else if(type == bin->log_note)
			prefix = "[Note]  ";
		else if(type == bin->log_warning)
			prefix = "[Warn]  ";

		//TODO send to UI?

		fprintf(stderr, "%s", prefix);
		vfprintf(stderr, fmt, args);

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

static inline void
_map_lock(bin_t *bin)
{
	while(atomic_flag_test_and_set_explicit(&bin->map_lock, memory_order_acquire))
	{
		// spin
	}
}

static inline void
_map_unlock(bin_t *bin)
{
	atomic_flag_clear_explicit(&bin->map_lock, memory_order_release);
}

static uint32_t
_map(void *data, const char *uri)
{
	bin_t *bin = data;

	_map_lock(bin);
	const uint32_t urid = symap_map(bin->symap, uri);
	_map_unlock(bin);
	
	return urid;
}

static const char *
_unmap(void *data, uint32_t urid)
{
	bin_t *bin = data;

	_map_lock(bin);
	const char *uri = symap_unmap(bin->symap, urid);
	_map_unlock(bin);
	
	return uri;
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
	bin->app_from_app = varchunk_new(CHUNK_SIZE);

	bin->symap = symap_new();
	atomic_flag_clear_explicit(&bin->map_lock, memory_order_relaxed);

	bin->map.map = _map;
	bin->map.handle = bin;
	LV2_URID_Map *map = &bin->map;

	bin->unmap.unmap = _unmap;
	bin->unmap.handle = bin;

	bin->log_entry = map->map(map->handle, LV2_LOG__Entry);
	bin->log_error = map->map(map->handle, LV2_LOG__Error);
	bin->log_note = map->map(map->handle, LV2_LOG__Note);
	bin->log_trace = map->map(map->handle, LV2_LOG__Trace);
	bin->log_warning = map->map(map->handle, LV2_LOG__Warning);

	bin->log.handle = bin;
	bin->log.printf = _log_printf;
	bin->log.vprintf = _log_vprintf;
	
	bin->app_driver.map = &bin->map;
	bin->app_driver.unmap = &bin->unmap;
	bin->app_driver.log = &bin->log;
	bin->app_driver.to_ui_request = _app_to_ui_request;
	bin->app_driver.to_ui_advance = _app_to_ui_advance;
	bin->app_driver.to_worker_request = _app_to_worker_request;
	bin->app_driver.to_worker_advance = _app_to_worker_advance;
	bin->app_driver.to_app_request = _worker_to_app_request;
	bin->app_driver.to_app_advance = _worker_to_app_advance;

	bin->ui_driver.map = &bin->map;
	bin->ui_driver.unmap = &bin->unmap;
	bin->ui_driver.log = &bin->log;
	bin->ui_driver.to_app_request = _ui_to_app_request;
	bin->ui_driver.to_app_advance = _ui_to_app_advance;
	bin->ui_driver.instance_access = 1; // enabled

	bin->ui_driver.opened = _ui_opened;
	bin->ui_driver.close = _ui_close;

	bin->self = eina_thread_self(); // thread ID of UI thread
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
	atomic_init(&bin->worker_dead, 0);
	_light_sem_init(&bin->worker_sem, 0);

	// threads init
	Eina_Bool status = eina_thread_create(&bin->worker_thread,
		EINA_THREAD_URGENT, -1, _worker_thread, bin);
	if(!status)
		fprintf(stderr, "creation of worker thread failed\n");

	// main loop
	elm_run();

	// ui deinit
	sp_ui_del(bin->ui, true);
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
	atomic_store_explicit(&bin->worker_dead, 1, memory_order_relaxed);
	_light_sem_signal(&bin->worker_sem, 1);
	eina_thread_join(bin->worker_thread);

	// NSM deinit
	synthpod_nsm_free(bin->nsm);

	// deinit semaphores
	_light_sem_deinit(&bin->worker_sem);

	if(bin->path)
		free(bin->path);
}

static void
bin_deinit(bin_t *bin)
{
	// synthpod deinit
	sp_app_free(bin->app);

	// symap deinit
	symap_free(bin->symap);

	// varchunk deinit
	varchunk_free(bin->app_to_ui);
	varchunk_free(bin->app_from_ui);
	varchunk_free(bin->app_to_log);
	varchunk_free(bin->app_to_worker);
	varchunk_free(bin->app_from_worker);
	varchunk_free(bin->app_from_com);
	varchunk_free(bin->app_from_app);
}

static inline void
bin_process_pre(bin_t *bin, uint32_t nsamples, bool bypassed)
{
	// read events from worker
	{
		size_t size;
		const void *body;
		unsigned n = 0;
		while((body = varchunk_read_request(bin->app_from_worker, &size))
			&& (n++ < MAX_MSGS) )
		{
			bool advance = sp_app_from_worker(bin->app, size, body);
			if(!advance)
			{
				//fprintf(stderr, "worker is blocked\n");
				break;
			}
			varchunk_read_advance(bin->app_from_worker);
		}
	}

	// run synthpod app pre
	if(!bypassed)
		sp_app_run_pre(bin->app, nsamples);

	// read events from UI ringbuffer
	{
		size_t size;
		const LV2_Atom *atom;
		unsigned n = 0;
		while((atom = varchunk_read_request(bin->app_from_ui, &size))
			&& (n++ < MAX_MSGS) )
		{
			bin->advance_ui = sp_app_from_ui(bin->app, atom);
			if(!bin->advance_ui)
			{
				//fprintf(stderr, "ui is blocked\n");
				break;
			}
			varchunk_read_advance(bin->app_from_ui);
		}
	}

	// read events from feedback ringbuffer
	{
		size_t size;
		const LV2_Atom *atom;
		unsigned n = 0;
		while((atom = varchunk_read_request(bin->app_from_app, &size))
			&& (n++ < MAX_MSGS) )
		{
			bin->advance_ui = sp_app_from_ui(bin->app, atom);
			if(!bin->advance_ui)
			{
				//fprintf(stderr, "ui is blocked\n");
				break;
			}
			varchunk_read_advance(bin->app_from_app);
		}
	}
	
	// run synthpod app post
	if(!bypassed)
		sp_app_run_post(bin->app, nsamples);
}

static inline void
bin_process_post(bin_t *bin)
{
}

#endif
