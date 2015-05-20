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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <synthpod_app.h>
#if defined(BUILD_UI)
#	include <synthpod_ui.h>
#endif
#include <ext_urid.h>
#include <varchunk.h>

#include <synthpod_nsm.h>

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>

#include <portaudio.h>

#include <uv.h>

#define CHUNK_SIZE 0x10000
#define SEQ_SIZE 0x2000

typedef struct _prog_t prog_t;

struct _prog_t {
	ext_urid_t *ext_urid;

	sp_app_t *app;
	sp_app_driver_t app_driver;
	
	varchunk_t *app_to_worker;
	varchunk_t *app_from_worker;

	varchunk_t *app_to_log;

	char *path;
	char *exe;
	synthpod_nsm_t *nsm;

#if defined(BUILD_UI)
	sp_ui_t *ui;
	sp_ui_driver_t ui_driver;

	varchunk_t *app_to_ui;
	varchunk_t *app_from_ui;

	varchunk_t *ui_to_app;
	varchunk_t *ui_from_app;

	Ecore_Animator *ui_anim;
	Evas_Object *win;
#else
	uv_loop_t *loop;
	uv_signal_t sigterm;
	uv_signal_t sigint;
	uv_async_t async;
#endif

	LV2_Atom_Forge forge;
	LV2_URID midi_MidiEvent;
	LV2_URID log_entry;
	LV2_URID log_error;
	LV2_URID log_note;
	LV2_URID log_trace;
	LV2_URID log_warning;

	PaStream *stream;
	PaStreamParameters input_params;
	PaStreamParameters output_params;
	uint32_t sample_rate;
	uint32_t min_block_size;
	uint32_t max_block_size;

	uv_thread_t worker_thread;
	uv_async_t worker_quit;
	uv_async_t worker_wake;
};

static const synthpod_nsm_driver_t nsm_driver; // forwared-declaration

static int
_process(const void *inputs, void *outputs, unsigned long nsamples,
	const PaStreamCallbackTimeInfo *time_info,
	PaStreamCallbackFlags status_flags,
	void *data); // forward-declaration

static int
_pa_init(prog_t *handle, const char *id)
{
	int num_in = 2; //TODO
	int num_out = 2; //TODO
	uint32_t sample_rate = 48000; //TODO
	uint32_t block_size = 64; //TODO

	// portaudio init
	if(Pa_Initialize() != paNoError)
		fprintf(stderr, "Pa_Initialize failed\n");
	for(int d=0; d<Pa_GetDeviceCount(); d++)
	{
		//TODO fill a device list with this
		const PaDeviceInfo *info = Pa_GetDeviceInfo(d);
		printf("device: %i\n", d);
		printf("\tname: %s\n", info->name);

		handle->input_params.device = d;
		handle->input_params.channelCount = num_in;
		handle->input_params.sampleFormat = paFloat32 | paNonInterleaved;
		handle->input_params.suggestedLatency = Pa_GetDeviceInfo(d)->defaultLowInputLatency ;
		handle->input_params.hostApiSpecificStreamInfo = NULL;
		
		handle->output_params.device = d;
		handle->output_params.channelCount = num_out;
		handle->output_params.sampleFormat = paFloat32 | paNonInterleaved;
		handle->output_params.suggestedLatency = Pa_GetDeviceInfo(d)->defaultLowInputLatency ;
		handle->output_params.hostApiSpecificStreamInfo = NULL;

		// open first device that supports num_in, num_out @sample_rate
		if(Pa_IsFormatSupported(&handle->input_params, &handle->output_params, sample_rate)
			== paFormatIsSupported)
		{
			printf("\tsupported: OK\n");

			handle->sample_rate = sample_rate;
			handle->min_block_size = block_size;
			handle->max_block_size = block_size;

			if(Pa_OpenStream(&handle->stream, &handle->input_params, &handle->output_params,
					sample_rate, block_size, paNoFlag, _process, handle) != paNoError)
			{
				fprintf(stderr, "Pa_OpenStream failed\n");
			}
			else
				return 0;
		}
	}

	return -1;
}

static void
_pa_deinit(prog_t *handle)
{
	// portaudio deinit
	if(Pa_StopStream(handle->stream) != paNoError)
		fprintf(stderr, "Pa_StopStream failed\n");
	if(Pa_CloseStream(handle->stream) != paNoError)
		fprintf(stderr, "Pa_CloseStream failed\n");
	if(Pa_Terminate() != paNoError)
		fprintf(stderr, "Pa_Terminate failed\n");
}

// non-rt worker-thread
static void
_worker_quit(uv_async_t *quit)
{
	prog_t *handle = quit->data;

	uv_close((uv_handle_t *)&handle->worker_quit, NULL);
	uv_close((uv_handle_t *)&handle->worker_wake, NULL);
	if(handle->nsm)
		synthpod_nsm_free(handle->nsm);
}

// non-rt worker-thread
static void
_worker_wake(uv_async_t *quit)
{
	prog_t *handle = quit->data;

	size_t size;
	const void *body;
	while((body = varchunk_read_request(handle->app_to_worker, &size)))
	{
		sp_worker_from_app(handle->app, size, body);
		varchunk_read_advance(handle->app_to_worker);
	}
}

// non-rt worker-thread
static void
_worker_thread(void *data)
{
	prog_t *handle = data;

	uv_loop_t loop;
	uv_loop_init(&loop);

	handle->worker_quit.data = handle;
	uv_async_init(&loop, &handle->worker_quit, _worker_quit);
	
	handle->worker_wake.data = handle;
	uv_async_init(&loop, &handle->worker_wake, _worker_wake);

	handle->nsm = synthpod_nsm_new(&loop, handle->exe,
		&nsm_driver, handle); //TODO check

	uv_run(&loop, UV_RUN_DEFAULT);

	uv_loop_close(&loop);

	if(handle->path)
		free(handle->path);

	_pa_deinit(handle);
}

// non-rt / rt
static LV2_State_Status
_state_store(LV2_State_Handle state, uint32_t key, const void *value,
	size_t size, uint32_t type, uint32_t flags)
{
	prog_t *handle = state;

	if(type != handle->forge.String)
		return LV2_STATE_ERR_BAD_TYPE;

	if(!(flags & LV2_STATE_IS_POD) || !(flags & LV2_STATE_IS_PORTABLE))
		return LV2_STATE_ERR_BAD_FLAGS;

	FILE *f = fopen(handle->path, "wb");
	if(f)
	{
		int written = fwrite(value, size, 1, f);
		fflush(f);
		fclose(f);

		if(written == size)
			return LV2_STATE_SUCCESS;
	}

	return LV2_STATE_ERR_UNKNOWN;
}

static const void*
_state_retrieve(LV2_State_Handle state, uint32_t key, size_t *size,
	uint32_t *type, uint32_t *flags)
{
	prog_t *handle = state;
	
	*type = handle->forge.String;
	*flags = LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE;

	FILE *f = fopen(handle->path, "rb");
	if(f)
	{
		fseek(f, 0, SEEK_END);
		size_t fsize = ftell(f);
		fseek(f, 0, SEEK_SET);
				
		char *value = malloc(fsize + 1);
		if(value)
		{
			if(fread(value, fsize, 1, f) == 1)
			{
				value[fsize] = '\0';

				*size = fsize + 1;
				return value; //TODO needs to be freed
			}
		}
		fclose(f);
	}

	*size = 0;
	return NULL;
}

// non-rt ui-thread
static void
_trace(prog_t *handle)
{
	size_t size;
	const char *trace;
	while((trace = varchunk_read_request(handle->app_to_log, &size)))
	{
		fprintf(stderr, "[Trace] %s\n", trace);

		varchunk_read_advance(handle->app_to_log);
	}
}

#if defined(BUILD_UI)
// non-rt ui-thread
static void
_ui_delete_request(void *data, Evas_Object *obj, void *event)
{
	prog_t *handle = data;

	elm_exit();
}

// non-rt ui-thread
static void
_ui_quit(void *data)
{
	prog_t *handle = data;

	elm_exit();
}

// non-rt ui-thread
static Eina_Bool
_ui_animator(void *data)
{
	prog_t *handle = data;

	size_t size;
	const LV2_Atom *atom;
	while((atom = varchunk_read_request(handle->app_to_ui, &size)))
	{
		sp_ui_from_app(handle->ui, atom);
		varchunk_read_advance(handle->app_to_ui);
	}

	_trace(handle);

	return EINA_TRUE; // continue animator
}
#else
// non-rt main-thread
static void
_quit(uv_signal_t *quit, int signal)
{
	prog_t *handle = quit->data;

	uv_signal_stop(&handle->sigterm);
	uv_signal_stop(&handle->sigint);
	uv_close((uv_handle_t *)&handle->async, NULL);
}

// non-rt ui-thread
static void
_async(uv_async_t *async)
{
	prog_t *handle = async->data;

	_trace(handle);
}
#endif // BUILD_UI

// rt
static int
_process(const void *inputs, void *outputs, unsigned long nsamples,
	const PaStreamCallbackTimeInfo *time_info,
	PaStreamCallbackFlags status_flags,
	void *data)
{
	prog_t *handle = data;
	sp_app_t *app = handle->app;

	float *const *audio_in_buf = inputs;
	float **audio_out_buf = outputs;

	/*
	void *midi_in_buf = jack_port_get_buffer(handle->midi_in, nsamples);
	float *audio_in_buf[2] = {
		jack_port_get_buffer(handle->audio_in[0], nsamples),
		jack_port_get_buffer(handle->audio_in[1], nsamples)
	};
	void *midi_out_buf = jack_port_get_buffer(handle->midi_out, nsamples);
	float *audio_out_buf[2] = {
		jack_port_get_buffer(handle->audio_out[0], nsamples),
		jack_port_get_buffer(handle->audio_out[1], nsamples)
	};
	*/

	LV2_Atom_Sequence *seq_in = sp_app_get_system_source(app, 0);
	sp_app_set_system_source(app, 1, audio_in_buf[0]);
	sp_app_set_system_source(app, 2, audio_in_buf[1]);
	const LV2_Atom_Sequence *seq_out = sp_app_get_system_sink(app, 0);
	sp_app_set_system_sink(app, 1, audio_out_buf[0]);
	sp_app_set_system_sink(app, 2, audio_out_buf[1]);

	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(forge, (void *)seq_in, SEQ_SIZE);
	lv2_atom_forge_sequence_head(forge, &frame, 0);
	/*
	int n = jack_midi_get_event_count(midi_in_buf);	
	for(int i=0; i<n; i++)
	{
		jack_midi_event_t mev;
		jack_midi_event_get(&mev, midi_in_buf, i);

		//add jack midi event to seq_in
		lv2_atom_forge_frame_time(forge, mev.time);
		lv2_atom_forge_atom(forge, mev.size, handle->midi_MidiEvent);
		lv2_atom_forge_raw(forge, mev.buffer, mev.size);
		lv2_atom_forge_pad(forge, mev.size);
	}
	*/
	lv2_atom_forge_pop(forge, &frame);

	// read events from worker
	{
		size_t size;
		const void *body;
		while((body = varchunk_read_request(handle->app_from_worker, &size)))
		{
			sp_app_from_worker(handle->app, size, body);
			varchunk_read_advance(handle->app_from_worker);
		}
	}

	// run synthpod app pre
	sp_app_run_pre(handle->app, nsamples);

#if defined(BUILD_UI)
	// read events from UI
	{
		size_t size;
		const LV2_Atom *atom;
		while((atom = varchunk_read_request(handle->app_from_ui, &size)))
		{
			sp_app_from_ui(handle->app, atom);
			varchunk_read_advance(handle->app_from_ui);
		}
	}
#endif // BUILD_UI
	
	// run synthpod app post
	sp_app_run_post(handle->app, nsamples);

	// fill midi output buffer
	/*
	jack_midi_clear_buffer(midi_out_buf);
	LV2_ATOM_SEQUENCE_FOREACH(seq_out, ev)
	{
		const LV2_Atom *atom = &ev->body;

		jack_midi_event_write(midi_out_buf, ev->time.frames,
			LV2_ATOM_BODY_CONST(atom), atom->size);
	}
	*/

	return 0;
}

#if defined(BUILD_UI)
// rt
static void *
_app_to_ui_request(size_t size, void *data)
{
	prog_t *handle = data;

	return varchunk_write_request(handle->app_to_ui, size);
}
static void
_app_to_ui_advance(size_t size, void *data)
{
	prog_t *handle = data;

	varchunk_write_advance(handle->app_to_ui, size);
}

// non-rt ui-thread
static void *
_ui_to_app_request(size_t size, void *data)
{
	prog_t *handle = data;

	void *ptr;
	do
		ptr = varchunk_write_request(handle->app_from_ui, size);
	while(!ptr); // wait until there is enough space

	return ptr;
}
static void
_ui_to_app_advance(size_t size, void *data)
{
	prog_t *handle = data;

	varchunk_write_advance(handle->app_from_ui, size);
}
#endif // BUILD_UI

// rt
static void *
_app_to_worker_request(size_t size, void *data)
{
	prog_t *handle = data;

	return varchunk_write_request(handle->app_to_worker, size);
}
static void
_app_to_worker_advance(size_t size, void *data)
{
	prog_t *handle = data;

	varchunk_write_advance(handle->app_to_worker, size);
	uv_async_send(&handle->worker_wake); // wake up worker thread
	//TODO only do once at end of sp_app_run_post ?
}

// non-rt worker-thread
static void *
_worker_to_app_request(size_t size, void *data)
{
	prog_t *handle = data;

	void *ptr;
	do
		ptr = varchunk_write_request(handle->app_from_worker, size);
	while(!ptr); // wait until there is enough space

	return ptr;
}
static void
_worker_to_app_advance(size_t size, void *data)
{
	prog_t *handle = data;

	varchunk_write_advance(handle->app_from_worker, size);
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_vprintf(void *data, LV2_URID type, const char *fmt, va_list args)
{
	prog_t *handle = data;

	if(type == handle->log_trace)
	{
		char *trace;
		if((trace = varchunk_write_request(handle->app_to_log, 1024)))
		{
			vsprintf(trace, fmt, args);

			size_t written = strlen(trace) + 1;
			varchunk_write_advance(handle->app_to_log, written);

#if !defined(BUILD_UI)
			uv_async_send(&handle->async);
#endif
		}
	}
	else // !log_trace
	{
		const char *type_str = NULL;
		if(type == handle->log_entry)
			type_str = "Entry";
		else if(type == handle->log_error)
			type_str = "Error";
		else if(type == handle->log_note)
			type_str = "Note";
		else if(type == handle->log_warning)
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
_open(const char *path, const char *name, const char *id, void *data)
{
	(void)name;
	prog_t *handle = data;

	if(handle->path)
		free(handle->path);
	handle->path = strdup(path);

	// jack init
	if(_pa_init(handle, id))
		return -1;

	// synthpod init
	handle->app_driver.sample_rate = handle->sample_rate;
	handle->app_driver.min_block_size = handle->min_block_size;
	handle->app_driver.max_block_size = handle->max_block_size;
	handle->app_driver.seq_size = SEQ_SIZE; //TODO
	
	// app init
	handle->app = sp_app_new(NULL, &handle->app_driver, handle);

	// restore state
	sp_app_restore(handle->app, _state_retrieve, handle,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, NULL);
	
	// pa activate
	if(Pa_StartStream(handle->stream) != paNoError)
		fprintf(stderr, "Pa_StartStream failed\n");

	return 0; // success
}

static int
_save(void *data)
{
	prog_t *handle = data;
	
	sp_app_save(handle->app, _state_store, handle,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, NULL);

	return 0; // success
}

static const synthpod_nsm_driver_t nsm_driver = {
	.open = _open,
	.save = _save
};

#if defined(BUILD_UI)
EAPI_MAIN int
elm_main(int argc, char **argv)
#else
int
main(int argc, char **argv)
#endif
{
	static prog_t handle;

	// varchunk init
#if defined(BUILD_UI)
	handle.app_to_ui = varchunk_new(CHUNK_SIZE);
	handle.app_from_ui = varchunk_new(CHUNK_SIZE);
#endif
	handle.app_to_worker = varchunk_new(CHUNK_SIZE);
	handle.app_from_worker = varchunk_new(CHUNK_SIZE);

	handle.app_to_log = varchunk_new(CHUNK_SIZE);

	// ext_urid init
	handle.ext_urid = ext_urid_new();
	LV2_URID_Map *map = ext_urid_map_get(handle.ext_urid);
	LV2_URID_Unmap *unmap = ext_urid_unmap_get(handle.ext_urid);

	lv2_atom_forge_init(&handle.forge, map);
	handle.midi_MidiEvent = map->map(map->handle, LV2_MIDI__MidiEvent);
	handle.log_entry = map->map(map->handle, LV2_LOG__Entry);
	handle.log_error = map->map(map->handle, LV2_LOG__Error);
	handle.log_note = map->map(map->handle, LV2_LOG__Note);
	handle.log_trace = map->map(map->handle, LV2_LOG__Trace);
	handle.log_warning = map->map(map->handle, LV2_LOG__Warning);

	handle.app_driver.map = map;
	handle.app_driver.unmap = unmap;
	handle.app_driver.log_printf = _log_printf;
	handle.app_driver.log_vprintf = _log_vprintf;
#if defined(BUILD_UI)
	handle.app_driver.to_ui_request = _app_to_ui_request;
	handle.app_driver.to_ui_advance = _app_to_ui_advance;
#else
	handle.app_driver.to_ui_request = NULL;
	handle.app_driver.to_ui_advance = NULL;
#endif
	handle.app_driver.to_worker_request = _app_to_worker_request;
	handle.app_driver.to_worker_advance = _app_to_worker_advance;
	handle.app_driver.to_app_request = _worker_to_app_request;
	handle.app_driver.to_app_advance = _worker_to_app_advance;

#if defined(BUILD_UI)
	handle.ui_driver.map = map;
	handle.ui_driver.unmap = unmap;
	handle.ui_driver.to_app_request = _ui_to_app_request;
	handle.ui_driver.to_app_advance = _ui_to_app_advance;

	// create main window
	handle.ui_anim = ecore_animator_add(_ui_animator, &handle);
	handle.win = elm_win_util_standard_add("synthpod", "Synthpod");
	evas_object_smart_callback_add(handle.win, "delete,request", _ui_delete_request, &handle);
	evas_object_resize(handle.win, 1280, 720);
	evas_object_show(handle.win);

	// ui init
	handle.ui = sp_ui_new(handle.win, NULL, &handle.ui_driver, &handle);
#endif

	// threads init
	handle.exe = argv[0];
	uv_thread_create(&handle.worker_thread, _worker_thread, &handle);

#if defined(BUILD_UI)
	// main loop
	elm_run();

	// ui deinit
	sp_ui_free(handle.ui);

	evas_object_del(handle.win);
	ecore_animator_del(handle.ui_anim);
#else
	// uv init
	handle.loop = uv_default_loop();

	handle.sigterm.data = &handle;
	uv_signal_init(handle.loop, &handle.sigterm);
	uv_signal_start(&handle.sigterm, _quit, SIGTERM);

	handle.sigint.data = &handle;
	uv_signal_init(handle.loop, &handle.sigint);
	uv_signal_start(&handle.sigint, _quit, SIGINT);

	handle.async.data = &handle;
	uv_async_init(handle.loop, &handle.async, _async);

	uv_run(handle.loop, UV_RUN_DEFAULT);
#endif // BUILD_UI

	// threads deinit
	uv_async_send(&handle.worker_quit);
	uv_thread_join(&handle.worker_thread);

	// synthpod deinit
	sp_app_free(handle.app);

	// ext_urid deinit
	ext_urid_free(handle.ext_urid);

	// varchunk deinit
#if defined(BUILD_UI)
	varchunk_free(handle.app_to_ui);
	varchunk_free(handle.app_from_ui);
#endif
	varchunk_free(handle.app_to_log);
	varchunk_free(handle.app_to_worker);
	varchunk_free(handle.app_from_worker);

	return 0;
}

#if defined(BUILD_UI)
ELM_MAIN()
#endif
