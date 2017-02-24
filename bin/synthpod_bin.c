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

#include <synthpod_bin.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define CHUNK_SIZE 0x10000
#define MAX_MSGS 10 //FIXME limit to how many events?

static _Atomic xpress_uuid_t voice_uuid = ATOMIC_VAR_INIT(1);

__realtime static xpress_uuid_t
_voice_map_new_uuid(void *handle)
{
	_Atomic xpress_uuid_t *uuid = handle;

	return atomic_fetch_add_explicit(uuid, 1, memory_order_relaxed);
}

static inline void
_light_sem_init(light_sem_t *lsem, int count)
{
	assert(count >= 0);
	atomic_init(&lsem->count, 0);
	uv_sem_init(&lsem->sem, count);
	lsem->spin = 10000; //TODO make this configurable or self-adapting
}

static inline void
_light_sem_deinit(light_sem_t *lsem)
{
	uv_sem_destroy(&lsem->sem);
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
		uv_sem_wait(&lsem->sem);
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
		uv_sem_post(&lsem->sem);
}

__realtime static void
_close_request(void *data)
{
	bin_t *bin = data;

	if(bin->has_gui) // UI has actually been started by app
		atomic_store_explicit(&bin->ui_is_done, true, memory_order_relaxed);
	else
		bin_quit(bin);
}

//FIXME handle this somewhere
__non_realtime static void
_ui_opened(void *data, int status)
{
	bin_t *bin = data;

	//printf("_ui_opened: %i\n", status);
	synthpod_nsm_opened(bin->nsm, status);
}

__non_realtime static void
_ui_animator(uv_timer_t* handle)
{
	bin_t *bin = handle->data;

	if(atomic_load_explicit(&bin->ui_is_done, memory_order_relaxed))
		uv_stop(&bin->loop);

	size_t size;
	const LV2_Atom *atom;
	while( (atom = varchunk_read_request(bin->app_to_ui, &size)) )
	{
		if(bin->sb)
		{
			sandbox_master_send(bin->sb, 15, size, bin->atom_eventTransfer, atom); //TODO check
			sandbox_master_flush(bin->sb); //TODO check
		}

		varchunk_read_advance(bin->app_to_ui);
	}
}

__non_realtime static void
_worker_thread(void *data)
{
	bin_t *bin = data;
	
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
			fprintf(stderr, "["ANSI_COLOR_BLUE"Trace"ANSI_COLOR_RESET"] %s", trace);

			varchunk_read_advance(bin->app_to_log);
		}
	}
}

__realtime static void *
_app_to_ui_request(size_t size, void *data)
{
	bin_t *bin = data;

	return varchunk_write_request(bin->app_to_ui, size);
}
__realtime static void
_app_to_ui_advance(size_t size, void *data)
{
	bin_t *bin = data;

	// copy com events to com buffer
	const LV2_Atom_Object *obj = bin->app_to_ui->buf + bin->app_to_ui->head
		+ sizeof(varchunk_elmnt_t);
	if(sp_app_com_event(bin->app, obj->body.otype))
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

__non_realtime static void *
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
__non_realtime static void
_ui_to_app_advance(size_t size, void *data)
{
	bin_t *bin = data;

	varchunk_write_advance(bin->app_from_ui, size);
}

__realtime static void *
_app_to_worker_request(size_t size, void *data)
{
	bin_t *bin = data;

	return varchunk_write_request(bin->app_to_worker, size);
}
__realtime static void
_app_to_worker_advance(size_t size, void *data)
{
	bin_t *bin = data;

	varchunk_write_advance(bin->app_to_worker, size);
	_light_sem_signal(&bin->worker_sem, 1);
}

__non_realtime static void *
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
__non_realtime static void
_worker_to_app_advance(size_t size, void *data)
{
	bin_t *bin = data;

	varchunk_write_advance(bin->app_from_worker, size);
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
		&& !uv_thread_equal(&this, &bin->self) // not UI thread ID
		&& !uv_thread_equal(&this, &bin->worker_thread) ) // not worker thread ID
	{
		_atomic_spin_lock(&bin->trace_lock);
		char *trace;
		if((trace = varchunk_write_request(bin->app_to_log, 1024)))
		{
			vsnprintf(trace, 1024, fmt, args);

			size_t written = strlen(trace) + 1;
			varchunk_write_advance(bin->app_to_log, written);
			_atomic_unlock(&bin->trace_lock);
			_light_sem_signal(&bin->worker_sem, 1);

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

__non_realtime static uint32_t
_map(void *data, const char *uri)
{
	bin_t *bin = data;

	_atomic_spin_lock(&bin->map_lock);
	const uint32_t urid = symap_map(bin->symap, uri);
	_atomic_unlock(&bin->map_lock);
	
	return urid;
}

__non_realtime static const char *
_unmap(void *data, uint32_t urid)
{
	bin_t *bin = data;

	_atomic_spin_lock(&bin->map_lock);
	const char *uri = symap_unmap(bin->symap, urid);
	_atomic_unlock(&bin->map_lock);
	
	return uri;
}

__non_realtime static void
_sb_recv_cb(void *data, uint32_t index, uint32_t size, uint32_t format,
	const void *buf)
{
	bin_t *bin = data;

	if(index == 14) // control for synthpod:stereo
	{
		void *dst;
		if( (dst = _ui_to_app_request(size, bin)) )
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

__non_realtime static void
_sb_recv(uv_poll_t* handle, int status, int events)
{
	bin_t *bin = handle->data;

	sandbox_master_recv(bin->sb);
}

__non_realtime static void
_exit_cb(uv_process_t *process, int64_t exit_status, int term_signal)
{
	bin_t *bin = process->data;

	uv_stop(&bin->loop);
}

void
bin_init(bin_t *bin)
{
	uv_loop_init(&bin->loop);

	// varchunk init
	bin->app_to_ui = varchunk_new(CHUNK_SIZE, true);
	bin->app_from_ui = varchunk_new(CHUNK_SIZE, true);
	bin->app_to_worker = varchunk_new(CHUNK_SIZE, true);
	bin->app_from_worker = varchunk_new(CHUNK_SIZE, true);
	bin->app_to_log = varchunk_new(CHUNK_SIZE, true);
	bin->app_from_com = varchunk_new(CHUNK_SIZE, false);
	bin->app_from_app = varchunk_new(CHUNK_SIZE, false);

	bin->symap = symap_new();
	atomic_flag_clear_explicit(&bin->map_lock, memory_order_relaxed);

	bin->map.map = _map;
	bin->map.handle = bin;
	LV2_URID_Map *map = &bin->map;

	bin->unmap.unmap = _unmap;
	bin->unmap.handle = bin;

	bin->xmap.new_uuid = _voice_map_new_uuid;
	bin->xmap.handle = &voice_uuid;

	bin->log_error = map->map(map->handle, LV2_LOG__Error);
	bin->log_note = map->map(map->handle, LV2_LOG__Note);
	bin->log_trace = map->map(map->handle, LV2_LOG__Trace);
	bin->log_warning = map->map(map->handle, LV2_LOG__Warning);
	bin->atom_eventTransfer = map->map(map->handle, LV2_ATOM__eventTransfer);

	bin->log.handle = bin;
	bin->log.printf = _log_printf;
	bin->log.vprintf = _log_vprintf;
	bin->trace_lock = (atomic_flag)ATOMIC_FLAG_INIT;
	
	bin->app_driver.map = &bin->map;
	bin->app_driver.unmap = &bin->unmap;
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
	bin->sb_driver.map = &bin->map;
	bin->sb_driver.unmap = &bin->unmap;
	bin->sb_driver.recv_cb = _sb_recv_cb;
	bin->sb_driver.subscribe_cb = _sb_subscribe_cb;

	bin->sb = sandbox_master_new(&bin->sb_driver, bin);
	if(bin->sb)
	{
		int fd;
		sandbox_master_fd_get(bin->sb, &fd);

		if(fd)
		{
			// automatically start gui in separate process
			if(bin->has_gui && !strncmp(bin->socket_path, "ipc://", 6))
			{
//#define USE_NK
#ifdef USE_NK
				const char *cmd = SYNTHPOD_BIN_DIR"synthpod_sandbox_x11";
#else
				const char *cmd = SYNTHPOD_BIN_DIR"synthpod_sandbox_efl";
#endif

				size_t sz = 128;
				char cwd [128];
				uv_cwd(cwd, &sz);

				char window_title [128];
				snprintf(window_title, 128, "Synthpod - %s", bin->socket_path);

				char update_rate [128];
				snprintf(update_rate, 128, "%i", bin->update_rate);

				char *args [] = {
					(char *)cmd,
					"-p", SYNTHPOD_PREFIX"stereo",
					"-b", SYNTHPOD_PLUGIN_DIR, //FIXME look up dynamically
#ifdef USE_NK
					"-u", SYNTHPOD_PREFIX"root_4_nk",
#else
					"-u", SYNTHPOD_PREFIX"root_3_eo",
#endif
					"-s", (char *)bin->socket_path,
					"-w", window_title,
					"-f", update_rate,
					NULL
				};
#ifdef USE_NK
#	undef USE_NK
#endif

				uv_stdio_container_t stdio [3] = {
					[0] = {
						.flags = UV_INHERIT_FD | UV_READABLE_PIPE,
						.data.fd = fileno(stdin)
					},
					[1] = {
						.flags = UV_INHERIT_FD | UV_WRITABLE_PIPE,
						.data.fd = fileno(stdout)
					},
					[2] = {
						.flags = UV_INHERIT_FD | UV_WRITABLE_PIPE,
						.data.fd = fileno(stderr)
					},
				};

				const uv_process_options_t opts = {
					.exit_cb = _exit_cb,
					.file = cmd,
					.args = args,
					.env = NULL,
					.cwd = cwd,
					.flags = UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS
						| UV_PROCESS_WINDOWS_HIDE,
					.stdio_count = 3,
					.stdio = stdio,
					.uid = 0,
					.gid = 0
				};
				bin->exe.data = bin;
				if(uv_spawn(&bin->loop, &bin->exe, &opts))
					fprintf(stderr, "uv_spawn failed\n");
			}

			bin->hndl.data = bin;
			uv_poll_init(&bin->loop, &bin->hndl, fd);
			uv_poll_start(&bin->hndl, UV_READABLE, _sb_recv);
		}
	}
}

void
bin_run(bin_t *bin, char **argv, const synthpod_nsm_driver_t *nsm_driver)
{
	uv_timer_init(&bin->loop, &bin->ui_anim);
	bin->ui_anim.data = bin;
	const unsigned ms = 1000 / bin->update_rate;
	uv_timer_start(&bin->ui_anim, _ui_animator, ms, ms);

	// NSM init
	const char *exe = strrchr(argv[0], '/');
	exe = exe ? exe + 1 : argv[0]; // we only want the program name without path
	bin->nsm = synthpod_nsm_new(exe, argv[optind], &bin->loop, nsm_driver, bin); //TODO check

	// init semaphores
	atomic_init(&bin->worker_dead, 0);
	_light_sem_init(&bin->worker_sem, 0);

	// threads init
	if(uv_thread_create(&bin->worker_thread, _worker_thread, bin))
		fprintf(stderr, "creation of worker thread failed\n");

	atomic_init(&bin->ui_is_done , false);

	// main loop
	uv_run(&bin->loop, UV_RUN_DEFAULT);

	if(uv_is_active((uv_handle_t *)&bin->ui_anim))
		uv_timer_stop(&bin->ui_anim);
	uv_close((uv_handle_t *)&bin->ui_anim, NULL);
}

void
bin_stop(bin_t *bin)
{
	// threads deinit
	atomic_store_explicit(&bin->worker_dead, 1, memory_order_relaxed);
	_light_sem_signal(&bin->worker_sem, 1);
	uv_thread_join(&bin->worker_thread);

	// NSM deinit
	synthpod_nsm_free(bin->nsm);

	// deinit semaphores
	_light_sem_deinit(&bin->worker_sem);

	if(bin->path)
		free(bin->path);
}

void
bin_deinit(bin_t *bin)
{
	if(uv_is_active((uv_handle_t *)&bin->hndl))
		uv_poll_stop(&bin->hndl);
	uv_close((uv_handle_t *)&bin->hndl, NULL);

	if(bin->has_gui)
	{
		if(uv_is_active((uv_handle_t *)&bin->exe))
			uv_process_kill(&bin->exe, SIGINT);
		uv_close((uv_handle_t *)&bin->exe, NULL);
	}
	if(bin->sb)
		sandbox_master_free(bin->sb);

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

	uv_loop_close(&bin->loop);
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
	uv_stop(&bin->loop);
}
