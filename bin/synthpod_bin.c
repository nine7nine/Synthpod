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

#include <getopt.h>
#include <inttypes.h>

#include <synthpod_bin.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define CHUNK_SIZE 0x100000 // 1M
#define MAX_MSGS 10 //FIXME limit to how many events?

#define CONTROL_PORT_INDEX 14
#define NOTIFY_PORT_INDEX 15

static atomic_bool done = ATOMIC_VAR_INIT(false);

static atomic_long voice_uuid = ATOMIC_VAR_INIT(1);

__realtime static xpress_uuid_t
_voice_map_new_uuid(void *handle)
{
	atomic_long *uuid = handle;

	return atomic_fetch_add_explicit(uuid, 1, memory_order_relaxed);
}

__realtime static void
_close_request(void *data)
{
	bin_t *bin = data;

	bin_quit(bin);
}

__realtime static void *
_app_to_ui_request(size_t minimum, size_t *maximum, void *data)
{
	bin_t *bin = data;

	return varchunk_write_request_max(bin->app_to_ui, minimum, maximum);
}
__realtime static void
_app_to_ui_advance(size_t written, void *data)
{
	bin_t *bin = data;

	/* FIXME
	// copy com events to com buffer
	const LV2_Atom_Object *obj = bin->app_to_ui.buf[0];
	size_t sz;
	if(sp_app_com_event(bin->app, obj->body.otype))
	{
		void *dst;
		if((dst = varchunk_write_request(bin->app_from_com, size)))
		{
			memcpy(dst, src, size);
			varchunk_write_advance(bin->app_from_com, size);
		}
	}
	*/

	varchunk_write_advance(bin->app_to_ui, written);
	sandbox_master_signal(bin->sb);
}

__realtime static void *
_ui_to_app_request(size_t minimum, size_t *maximum, void *data)
{
	bin_t *bin = data;

	return varchunk_write_request_max(bin->app_from_ui, minimum, maximum);
}
__realtime static void
_ui_to_app_advance(size_t written, void *data)
{
	bin_t *bin = data;

	varchunk_write_advance(bin->app_from_ui, written);
}

__realtime static void *
_app_to_worker_request(size_t minimum, size_t *maximum, void *data)
{
	bin_t *bin = data;

	return varchunk_write_request_max(bin->app_to_worker, minimum, maximum);
}
__realtime static void
_app_to_worker_advance(size_t written, void *data)
{
	bin_t *bin = data;

	varchunk_write_advance(bin->app_to_worker, written);
	sandbox_master_signal(bin->sb);
}

__non_realtime static void *
_worker_to_app_request(size_t minimum, size_t *maximum, void *data)
{
	bin_t *bin = data;

	void *ptr;
	do
	{
		ptr = varchunk_write_request_max(bin->app_from_worker, minimum, maximum);
	}
	while(!ptr); // wait until there is enough space

	return ptr;
}
__non_realtime static void
_worker_to_app_advance(size_t written, void *data)
{
	bin_t *bin = data;

	varchunk_write_advance(bin->app_from_worker, written);
}

static inline void
_atomic_spin_lock(atomic_flag *flag)
{
	while(atomic_flag_test_and_set_explicit(flag, memory_order_acquire))
	{
		// spin
	}
}

static inline void
_atomic_unlock(atomic_flag *flag)
{
	atomic_flag_clear_explicit(flag, memory_order_release);
}

__non_realtime static int
_log_vprintf(void *data, LV2_URID type, const char *fmt, va_list args)
{
	bin_t *bin = data;

	uv_thread_t this = uv_thread_self();

	// check for trace mode AND DSP thread ID
	if( (type == bin->log_trace)
		&& !uv_thread_equal(&this, &bin->self) ) // not worker thread ID
	{
		_atomic_spin_lock(&bin->trace_lock); //FIXME use per-dsp-thread ringbuffer
		char *trace;
		if((trace = varchunk_write_request(bin->app_to_log, 1024)))
		{
			vsnprintf(trace, 1024, fmt, args);

			size_t written = strlen(trace) + 1;
			varchunk_write_advance(bin->app_to_log, written);
			_atomic_unlock(&bin->trace_lock);
			sandbox_master_signal(bin->sb);

			return written;
		}
		_atomic_unlock(&bin->trace_lock);
	}
	else // !log_trace OR not DSP thread ID
	{
		const char *prefix = "["ANSI_COLOR_MAGENTA"Log"ANSI_COLOR_RESET"]   ";
		if(type == bin->log_trace)
			prefix = "["ANSI_COLOR_BLUE"Trace"ANSI_COLOR_RESET"] ";
		else if(type == bin->log_error)
			prefix = "["ANSI_COLOR_RED"Error"ANSI_COLOR_RESET"] ";
		else if(type == bin->log_note)
			prefix = "["ANSI_COLOR_GREEN"Note"ANSI_COLOR_RESET"]  ";
		else if(type == bin->log_warning)
			prefix = "["ANSI_COLOR_YELLOW"Warn"ANSI_COLOR_RESET"]  ";

		//TODO send to UI?

		fprintf(stderr, "%s", prefix);
		return vfprintf(stderr, fmt, args);
	}

	return -1;
}

__non_realtime static int
_log_printf(void *data, LV2_URID type, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(data, type, fmt, args);
  va_end(args);

	return ret;
}

__non_realtime static void
_sb_recv_cb(void *data, uint32_t index, uint32_t size, uint32_t format,
	const void *buf)
{
	bin_t *bin = data;

	if(index == CONTROL_PORT_INDEX) // control for synthpod:stereo
	{
		void *dst;
		if((dst = _ui_to_app_request(size, NULL, bin)))
		{
			memcpy(dst, buf, size);

			_ui_to_app_advance(size, bin);
		}
	}
}

__non_realtime static void
_sb_subscribe_cb(void *data, uint32_t index, uint32_t protocol, bool state)
{
	bin_t *bin = data;

	// nothing
}

static bin_t *bin_ptr = NULL;
__non_realtime static void
_sig(int sig)
{
	atomic_store_explicit(&done, true, memory_order_relaxed);
	if(bin_ptr)
		sandbox_master_signal(bin_ptr->sb);
}

void
bin_init(bin_t *bin)
{
	bin_ptr = bin;

	// varchunk init
	bin->app_to_ui = varchunk_new(CHUNK_SIZE, true);
	bin->app_from_ui = varchunk_new(CHUNK_SIZE, true);
	bin->app_to_worker = varchunk_new(CHUNK_SIZE, true);
	bin->app_from_worker = varchunk_new(CHUNK_SIZE, true);
	bin->app_to_log = varchunk_new(CHUNK_SIZE, true);
	bin->app_from_com = varchunk_new(CHUNK_SIZE, false);
	bin->app_from_app = varchunk_new(CHUNK_SIZE, false);

	bin->mapper = mapper_new(0x10000); // 64K
	mapper_pool_init(&bin->mapper_pool, bin->mapper, NULL, NULL, NULL);

	bin->map = mapper_pool_get_map(&bin->mapper_pool);
	bin->unmap = mapper_pool_get_unmap(&bin->mapper_pool);

	bin->xmap.new_uuid = _voice_map_new_uuid;
	bin->xmap.handle = &voice_uuid;

	bin->log_error = bin->map->map(bin->map->handle, LV2_LOG__Error);
	bin->log_note = bin->map->map(bin->map->handle, LV2_LOG__Note);
	bin->log_trace = bin->map->map(bin->map->handle, LV2_LOG__Trace);
	bin->log_warning = bin->map->map(bin->map->handle, LV2_LOG__Warning);
	bin->atom_eventTransfer = bin->map->map(bin->map->handle, LV2_ATOM__eventTransfer);

	bin->log.handle = bin;
	bin->log.printf = _log_printf;
	bin->log.vprintf = _log_vprintf;
	bin->trace_lock = (atomic_flag)ATOMIC_FLAG_INIT;
	
	bin->app_driver.map = bin->map;
	bin->app_driver.unmap = bin->unmap;
	bin->app_driver.xmap = &bin->xmap;
	bin->app_driver.log = &bin->log;
	bin->app_driver.to_ui_request = _app_to_ui_request;
	bin->app_driver.to_ui_advance = _app_to_ui_advance;
	bin->app_driver.to_worker_request = _app_to_worker_request;
	bin->app_driver.to_worker_advance = _app_to_worker_advance;
	bin->app_driver.to_app_request = _worker_to_app_request;
	bin->app_driver.to_app_advance = _worker_to_app_advance;
	bin->app_driver.num_slaves = bin->num_slaves;

	bin->app_driver.audio_prio = bin->audio_prio;
	bin->app_driver.bad_plugins = bin->bad_plugins;
	bin->app_driver.close_request = _close_request;

	bin->self = uv_thread_self(); // thread ID of UI thread

	bin->sb_driver.socket_path = bin->socket_path;
	bin->sb_driver.map = bin->map;
	bin->sb_driver.unmap = bin->unmap;
	bin->sb_driver.recv_cb = _sb_recv_cb;
	bin->sb_driver.subscribe_cb = _sb_subscribe_cb;

	bin->sb = sandbox_master_new(&bin->sb_driver, bin); //FIXME check

	signal(SIGTERM, _sig);
	signal(SIGQUIT, _sig);
	signal(SIGINT, _sig);

	uv_loop_init(&bin->loop);
}

void
bin_run(bin_t *bin, char **argv, const synthpod_nsm_driver_t *nsm_driver)
{
	// NSM init
	const char *exe = strrchr(argv[0], '/');
	exe = exe ? exe + 1 : argv[0]; // we only want the program name without path
	bin->nsm = synthpod_nsm_new(exe, argv[optind], &bin->loop, nsm_driver, bin); //TODO check

	pthread_t self = pthread_self();

	if(bin->worker_prio)
	{
		struct sched_param schedp;
		memset(&schedp, 0, sizeof(struct sched_param));
		schedp.sched_priority = bin->worker_prio;
		
		if(schedp.sched_priority)
		{
			if(pthread_setschedparam(self, SCHED_RR, &schedp))
				fprintf(stderr, "pthread_setschedparam error\n");
		}
	}

	while(!atomic_load_explicit(&done, memory_order_relaxed))
	{
		sandbox_master_wait(bin->sb);

		// read events from UI shared mem
		if(sandbox_master_recv(bin->sb))
		{
			bin_quit(bin);
		}

		// route events from app to UI
		{
			size_t size;
			const LV2_Atom_Object *obj;
			while((obj = varchunk_read_request(bin->app_to_ui, &size)))
			{
				sandbox_master_send(bin->sb, NOTIFY_PORT_INDEX, size, bin->atom_eventTransfer, obj);
				varchunk_read_advance(bin->app_to_ui);
			}
		}

		// read events from worker
		{
			size_t size;
			const void *body;
			while((body = varchunk_read_request(bin->app_to_worker, &size)))
			{
				sp_worker_from_app(bin->app, size, body);
				varchunk_read_advance(bin->app_to_worker);
			}
		}

		// read events from logger
		{
			size_t size;
			const char *trace;
			while((trace = varchunk_read_request(bin->app_to_log, &size)))
			{
				fprintf(stderr, "["ANSI_COLOR_BLUE"Trace"ANSI_COLOR_RESET"] %s", trace);

				varchunk_read_advance(bin->app_to_log);
			}
		}
	}
}

void
bin_stop(bin_t *bin)
{
	// NSM deinit
	synthpod_nsm_free(bin->nsm);

	if(bin->path)
		free(bin->path);
}

void
bin_deinit(bin_t *bin)
{
	if(bin->sb)
		sandbox_master_free(bin->sb);

	// synthpod deinit
	sp_app_free(bin->app);

	// mapper deinit
	mapper_pool_deinit(&bin->mapper_pool);
	mapper_free(bin->mapper);

	// varchunk deinit
	varchunk_free(bin->app_to_ui);
	varchunk_free(bin->app_from_ui);
	varchunk_free(bin->app_to_log);
	varchunk_free(bin->app_to_worker);
	varchunk_free(bin->app_from_worker);
	varchunk_free(bin->app_from_com);
	varchunk_free(bin->app_from_app);

	uv_loop_close(&bin->loop);

	fprintf(stderr, "bye\n");
}

void
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

void
bin_process_post(bin_t *bin)
{
	// nothing
}

//FIXME
void
bin_bundle_new(bin_t *bin)
{
	// simply load the default state
	bin_bundle_load(bin, SYNTHPOD_PREFIX"stereo");
}

void
bin_bundle_load(bin_t *bin, const char *bundle_path)
{
	sp_app_bundle_load(bin->app, bundle_path,
		_ui_to_app_request, _ui_to_app_advance, bin);
}

void
bin_bundle_save(bin_t *bin, const char *bundle_path)
{
	sp_app_bundle_save(bin->app, bundle_path,
		_ui_to_app_request, _ui_to_app_advance, bin);
}

void
bin_quit(bin_t *bin)
{
	atomic_store_explicit(&done, true, memory_order_relaxed);
}
