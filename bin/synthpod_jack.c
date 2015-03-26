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
#include <synthpod_ui.h>
#include <ext_urid.h>
#include <varchunk.h>

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>

#include <jack/jack.h>
#include <jack/midiport.h>

#include <uv.h>

typedef struct _handle_t handle_t;

struct _handle_t {
	ext_urid_t *ext_urid;

	sp_app_t *app;
	sp_app_driver_t app_driver;

	sp_ui_t *ui;
	sp_ui_driver_t ui_driver;

	LV2_Atom_Forge forge;
	LV2_URID midi_MidiEvent;

	jack_client_t *client;
	jack_port_t *midi_in;
	jack_port_t *audio_in[2];
	jack_port_t *midi_out;
	jack_port_t *audio_out[2];

	varchunk_t *to_ui;
	varchunk_t *from_ui;
	varchunk_t *to_worker;
	varchunk_t *from_worker;

	uv_loop_t *loop;
	uv_signal_t quit;

	uv_thread_t worker_thread;
	uv_async_t worker_quit;
	uv_async_t worker_wake;

	Ecore_Animator *ui_anim;
};

// non-rt main-thread
static void
_quit(uv_signal_t *quit, int signal)
{
	uv_signal_stop(quit);
}

// non-rt worker-thread
static void
_worker_quit(uv_async_t *quit)
{
	handle_t *handle = quit->data;

	uv_close((uv_handle_t *)&handle->worker_quit, NULL);
	uv_close((uv_handle_t *)&handle->worker_wake, NULL);
}

// non-rt worker-thread
static void
_worker_wake(uv_async_t *quit)
{
	handle_t *handle = quit->data;

	size_t size;
	const LV2_Atom *atom;
	while((atom = varchunk_read_request(handle->to_worker, &size)))
	{
		sp_worker_from_app(handle->app, atom);
		varchunk_read_advance(handle->to_worker);
	}
}

// non-rt worker-thread
static void
_worker_thread(void *data)
{
	handle_t *handle = data;

	uv_loop_t *loop = uv_loop_new();

	handle->worker_quit.data = handle;
	uv_async_init(loop, &handle->worker_quit, _worker_quit);
	
	handle->worker_wake.data = handle;
	uv_async_init(loop, &handle->worker_wake, _worker_wake);

	uv_run(loop, UV_RUN_DEFAULT);

	uv_loop_close(loop);
}

// non-rt ui-thread
static void
_ui_delete_request(void *data, Evas_Object *obj, void *event)
{
	handle_t *handle = data;

	elm_exit();
}

// non-rt ui-thread
static void
_ui_quit(void *data)
{
	handle_t *handle = data;

	elm_exit();
}

// non-rt ui-thread
static Eina_Bool
_ui_animator(void *data)
{
	handle_t *handle = data;

	size_t size;
	const LV2_Atom *atom;
	while((atom = varchunk_read_request(handle->to_ui, &size)))
	{
		sp_ui_from_app(handle->ui, atom, handle);
		varchunk_read_advance(handle->to_ui);
	}

	return EINA_TRUE; // continue animator
}

// rt
static int
_process(jack_nframes_t nsamples, void *data)
{
	handle_t *handle = data;
	sp_app_t *app = handle->app;

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

	LV2_Atom_Sequence *seq_in = sp_app_get_system_source(app, 0);
	sp_app_set_system_source(app, 1, audio_in_buf[0]);
	sp_app_set_system_source(app, 2, audio_in_buf[1]);
	const LV2_Atom_Sequence *seq_out = sp_app_get_system_sink(app, 0);
	sp_app_set_system_sink(app, 1, audio_out_buf[0]);
	sp_app_set_system_sink(app, 2, audio_out_buf[1]);

	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(forge, (void *)seq_in, 8192);
	lv2_atom_forge_sequence_head(forge, &frame, 0);
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
	lv2_atom_forge_pop(forge, &frame);

	// read events from worker
	{
		size_t size;
		const LV2_Atom *atom;
		while((atom = varchunk_read_request(handle->from_worker, &size)))
		{
			sp_app_from_worker(handle->app, atom);
			varchunk_read_advance(handle->from_worker);
		}
	}

	// run synthpod app pre
	sp_app_run_pre(handle->app, nsamples);

	// read events from UI
	{
		size_t size;
		const LV2_Atom *atom;
		while((atom = varchunk_read_request(handle->from_ui, &size)))
		{
			sp_app_from_ui(handle->app, atom);
			varchunk_read_advance(handle->from_ui);
		}
	}
	
	// run synthpod app pre
	sp_app_run_post(handle->app, nsamples);

	// fill midi output buffer
	jack_midi_clear_buffer(midi_out_buf);
	LV2_ATOM_SEQUENCE_FOREACH(seq_out, ev)
	{
		const LV2_Atom *atom = &ev->body;

		jack_midi_event_write(midi_out_buf, ev->time.frames,
			LV2_ATOM_BODY_CONST(atom), atom->size);
	}

	return 0;
}

// rt
static void *
_app_to_ui_request(size_t size, void *data)
{
	handle_t *handle = data;

	return varchunk_write_request(handle->to_ui, size);
}
static void
_app_to_ui_advance(size_t size, void *data)
{
	handle_t *handle = data;

	varchunk_write_advance(handle->to_ui, size);
}

// rt
static void *
_app_to_worker_request(size_t size, void *data)
{
	handle_t *handle = data;

	return varchunk_write_request(handle->to_worker, size);
}
static void
_app_to_worker_advance(size_t size, void *data)
{
	handle_t *handle = data;

	varchunk_write_advance(handle->to_worker, size);
	uv_async_send(&handle->worker_wake); // wake up worker thread
	//TODO only do once at end of sp_app_run ?
}

// non-rt worker-thread
static void *
_worker_to_app_request(size_t size, void *data)
{
	handle_t *handle = data;

	return varchunk_write_request(handle->from_worker, size);
}
static void
_worker_to_app_advance(size_t size, void *data)
{
	handle_t *handle = data;

	varchunk_write_advance(handle->from_worker, size);
}

// non-rt ui-thread
static void *
_ui_to_app_request(size_t size, void *data)
{
	handle_t *handle = data;

	return varchunk_write_request(handle->from_ui, size);
}
static void
_ui_to_app_advance(size_t size, void *data)
{
	handle_t *handle = data;

	varchunk_write_advance(handle->from_ui, size);
}

int
elm_main(int argc, char **argv)
{
	static handle_t handle;

	// varchunk init
	handle.to_ui = varchunk_new(8192);
	handle.from_ui = varchunk_new(8192);
	handle.to_worker = varchunk_new(8192);
	handle.from_worker = varchunk_new(8192);

	// ext_urid init
	handle.ext_urid = ext_urid_new();
	LV2_URID_Map *map = ext_urid_map_get(handle.ext_urid);
	LV2_URID_Unmap *unmap = ext_urid_unmap_get(handle.ext_urid);

	lv2_atom_forge_init(&handle.forge, map);
	handle.midi_MidiEvent = map->map(map->handle, LV2_MIDI__MidiEvent);

	// synthpod init
	handle.app_driver.sample_rate = 32000;
	handle.app_driver.period_size = 32; //TODO
	handle.app_driver.seq_size = 8192; //TODO
	handle.app_driver.map = map;
	handle.app_driver.unmap = unmap;
	handle.app_driver.schedule = NULL; //TODO
	handle.app_driver.log = NULL; //TODO
	handle.app_driver.to_ui_request = _app_to_ui_request;
	handle.app_driver.to_ui_advance = _app_to_ui_advance;
	handle.app_driver.to_worker_request = _app_to_worker_request;
	handle.app_driver.to_worker_advance = _app_to_worker_advance;
	handle.app_driver.to_app_request = _worker_to_app_request;
	handle.app_driver.to_app_advance = _worker_to_app_advance;
	
	handle.ui_driver.map = map;
	handle.ui_driver.unmap = unmap;
	handle.ui_driver.to_app_request = _ui_to_app_request;
	handle.ui_driver.to_app_advance = _ui_to_app_advance;

	handle.app = sp_app_new(&handle.app_driver, &handle);

	// jack init
	handle.client = jack_client_open("Synthpod", JackNullOption, NULL);
	jack_set_process_callback(handle.client, _process, &handle);
	handle.midi_in = jack_port_register(handle.client, "midi_in",
		JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	handle.audio_in[0] = jack_port_register(handle.client, "audio_in_1",
		JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	handle.audio_in[1] = jack_port_register(handle.client, "audio_in_2",
		JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	handle.midi_out = jack_port_register(handle.client, "midi_out",
		JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	handle.audio_out[0] = jack_port_register(handle.client, "audio_out_1",
		JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	handle.audio_out[1] = jack_port_register(handle.client, "audio_out_2",
		JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	jack_activate(handle.client);

	// uv init
	handle.loop = uv_default_loop();

	handle.quit.data = &handle;
	uv_signal_init(handle.loop, &handle.quit);
	uv_signal_start(&handle.quit, _quit, SIGINT);

	// threads init
	uv_thread_create(&handle.worker_thread, _worker_thread, &handle);

	handle.ui_anim = ecore_animator_add(_ui_animator, &handle);
	Evas_Object *win = elm_win_add(NULL, "Synthpod", ELM_WIN_BASIC);
	evas_object_smart_callback_add(win, "delete,request", _ui_delete_request, &handle);
	evas_object_resize(win, 800, 450);
	evas_object_show(win);

	// ui init
	handle.ui = sp_ui_new(win, &handle.ui_driver, &handle);

	// main loop
	elm_run();

	// ui deinit
	sp_ui_free(handle.ui);

	evas_object_del(win);
	ecore_animator_del(handle.ui_anim);

	// threads deinit
	uv_async_send(&handle.worker_quit);
	uv_thread_join(&handle.worker_thread);

	// jack deinit
	if(handle.client)
	{
		if(handle.midi_in)
			jack_port_unregister(handle.client, handle.midi_in);
		if(handle.audio_in[0])
			jack_port_unregister(handle.client, handle.audio_in[0]);
		if(handle.audio_in[1])
			jack_port_unregister(handle.client, handle.audio_in[1]);
		if(handle.midi_out)
			jack_port_unregister(handle.client, handle.midi_out);
		if(handle.audio_out[0])
			jack_port_unregister(handle.client, handle.audio_out[0]);
		if(handle.audio_out[1])
			jack_port_unregister(handle.client, handle.audio_out[1]);

		jack_deactivate(handle.client);
		jack_client_close(handle.client);
	}
	
	// synthpod deinit
	sp_app_free(handle.app);

	// ext_urid deinit
	ext_urid_free(handle.ext_urid);

	// varchunk deinit
	varchunk_free(handle.to_ui);
	varchunk_free(handle.from_ui);
	varchunk_free(handle.to_worker);
	varchunk_free(handle.from_worker);

	return 0;
}

ELM_MAIN()
