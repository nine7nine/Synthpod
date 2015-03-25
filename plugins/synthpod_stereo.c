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

#include <synthpod_lv2.h>
#include <synthpod_app.h>

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>

typedef struct _handle_t handle_t;

struct _handle_t {
	sp_app_t *app;
	sp_app_driver_t driver;

	struct {
		struct {
			LV2_URID note;
			LV2_URID error;
			LV2_URID trace;
		} log;
	} uri;

	struct {
		LV2_Atom_Forge event_source;
		LV2_Atom_Forge notify;
	} forge;

	struct {
		const LV2_Atom_Sequence *event_sink;
		const LV2_Atom_Sequence *control;
		const float *audio_in[2];
		LV2_Atom_Sequence *event_source;
		LV2_Atom_Sequence *notify;
		float *audio_out[2];
	} port;

	// non-rt worker-thread
	struct {
		LV2_Worker_Respond_Function respond;
		LV2_Worker_Respond_Handle target;
	} worker;
};

static void
lprintf(handle_t *handle, LV2_URID type, const char *fmt, ...)
{
	if(handle->driver.log)
	{
		va_list args;
		va_start(args, fmt);
		handle->driver.log->vprintf(handle->driver.log->handle, type, fmt, args);
		va_end(args);
	}
	else if(type != handle->uri.log.trace)
	{
		const char *type_str = NULL;
		if(type == handle->uri.log.note)
			type_str = "Note";
		else if(type == handle->uri.log.error)
			type_str = "Error";

		fprintf(stderr, "[%s]", type_str);
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		fputc('\n', stderr);
	}
}

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	handle_t *handle = instance;

	//TODO

	return LV2_STATE_SUCCESS;
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	handle_t *handle = instance;

	//TODO

	return LV2_STATE_SUCCESS;
}
	
static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

// non-rt worker-thread
static LV2_Worker_Status
_work(LV2_Handle instance,
	LV2_Worker_Respond_Function respond,
	LV2_Worker_Respond_Handle target,
	uint32_t size,
	const void *body)
{
	handle_t *handle = instance;
	
	handle->worker.respond = respond;
	handle->worker.target = target;

	const LV2_Atom *atom = body;
	assert(size == sizeof(LV2_Atom) + atom->size);
	sp_worker_from_app(handle->app, atom, handle);

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	handle_t *handle = instance;

	const LV2_Atom *atom = body;
	assert(size == sizeof(LV2_Atom) + atom->size);
	sp_app_from_worker(handle->app, atom, handle);

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_end_run(LV2_Handle instance)
{
	handle_t *handle = instance;

	return LV2_WORKER_SUCCESS;
}

static const LV2_Worker_Interface work_iface = {
	.work = _work,
	.work_response = _work_response,
	.end_run = _end_run
};

// rt-thread
static int
_to_ui_cb(LV2_Atom *atom, void *data)
{
	handle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge.notify;

	if(forge->offset + sizeof(LV2_Atom_Event) + atom->size > forge->size)
		return -1; // buffer overflow

	uint32_t size = sizeof(LV2_Atom_Forge) + atom->size;
	lv2_atom_forge_frame_time(forge, 0);
	lv2_atom_forge_raw(forge, atom, size);
	lv2_atom_forge_pad(forge, size);

	return 0;
}

// rt-thread
static int
_to_worker_cb(LV2_Atom *atom, void *data)
{
	handle_t *handle = data;

	return handle->driver.schedule->schedule_work(handle->driver.schedule->handle,
		sizeof(LV2_Atom) + atom->size, atom) == LV2_WORKER_SUCCESS ? 0 : -1;
}

// non-rt worker-thread
static int
_to_app_cb(LV2_Atom *atom, void *data)
{
	handle_t *handle = data;

	return handle->worker.respond(handle->worker.target,
		sizeof(LV2_Atom) + atom->size, atom) == LV2_WORKER_SUCCESS ? 0 : -1;
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	int i;
	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	handle->driver.sample_rate = rate;
	handle->driver.period_size = 32; //TODO
	handle->driver.seq_size = 8192; //TODO

	for(i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->driver.map = (LV2_URID_Map *)features[i]->data;
		if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->driver.unmap = (LV2_URID_Unmap *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->driver.schedule = (LV2_Worker_Schedule *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->driver.log = (LV2_Log_Log *)features[i]->data;

	if(!handle->driver.map)
	{
		lprintf(handle, handle->uri.log.error,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	
	if(!handle->driver.schedule)
	{
		lprintf(handle, handle->uri.log.error,
			"%s: Host does not support worker:schedule\n",
			descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->driver.to_ui_cb = _to_ui_cb;
	handle->driver.to_worker_cb = _to_worker_cb;
	handle->driver.to_app_cb = _to_app_cb;

	handle->app = sp_app_new(&handle->driver, handle);
	if(!handle->app)
	{
		lprintf(handle, handle->uri.log.error,
			"%s: creatio of app failed\n",
			descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->uri.log.note = handle->driver.map->map(handle->driver.map->handle,
		LV2_LOG__Note);
	handle->uri.log.error = handle->driver.map->map(handle->driver.map->handle,
		LV2_LOG__Error);
	handle->uri.log.trace = handle->driver.map->map(handle->driver.map->handle,
		LV2_LOG__Trace);

	lv2_atom_forge_init(&handle->forge.event_source, handle->driver.map);
	lv2_atom_forge_init(&handle->forge.notify, handle->driver.map);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	handle_t *handle = instance;
	sp_app_t *app = handle->app;

	switch(port)
	{
		case 0:
			handle->port.event_sink = (const LV2_Atom_Sequence *)data;
			sp_app_set_system_source(app, 0, data);
			break;
		case 1:
			handle->port.control = (const LV2_Atom_Sequence *)data;
			break;
		case 2:
			handle->port.audio_in[0] = (const float *)data;
			sp_app_set_system_source(app, 1, data);
			break;
		case 3:
			handle->port.audio_in[1] = (const float *)data;
			sp_app_set_system_source(app, 2, data);
			break;
		case 4:
			handle->port.event_source = (LV2_Atom_Sequence *)data;
			sp_app_set_system_sink(app, 0, data);
			break;
		case 5:
			handle->port.notify = (LV2_Atom_Sequence *)data;
			break;
		case 6:
			handle->port.audio_out[0] = (float *)data;
			sp_app_set_system_sink(app, 1, data);
			break;
		case 7:
			handle->port.audio_out[1] = (float *)data;
			sp_app_set_system_sink(app, 2, data);
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	handle_t *handle = instance;

	sp_app_activate(handle->app);
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = instance;
	sp_app_t *app = handle->app;

	struct {
		LV2_Atom_Forge_Frame event_source;
		LV2_Atom_Forge_Frame notify;
	} frame;

	// prepare forge(s) & sequence(s)
	lv2_atom_forge_set_buffer(&handle->forge.event_source,
		(uint8_t *)handle->port.event_source, handle->port.event_source->atom.size);
	lv2_atom_forge_sequence_head(&handle->forge.event_source, &frame.event_source, 0);
	
	lv2_atom_forge_set_buffer(&handle->forge.notify,
		(uint8_t *)handle->port.event_source, handle->port.notify->atom.size);
	lv2_atom_forge_sequence_head(&handle->forge.notify, &frame.notify, 0);

	// handle events from UI
	LV2_ATOM_SEQUENCE_FOREACH(handle->port.control, ev)
	{
		const LV2_Atom *atom = &ev->body;
		//TODO check atom type
		sp_app_from_ui(app, atom, handle);
	}

	// run app
	sp_app_run(app, nsamples);
		
	// end sequence(s)
	lv2_atom_forge_pop(&handle->forge.event_source, &frame.event_source);
	lv2_atom_forge_pop(&handle->forge.notify, &frame.notify);
}

static void
deactivate(LV2_Handle instance)
{
	handle_t *handle = instance;

	sp_app_deactivate(handle->app);
}

static void
cleanup(LV2_Handle instance)
{
	handle_t *handle = instance;

	sp_app_free(handle->app);
	free(handle);
}

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_WORKER__interface))
		return &work_iface;
	else if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;
	else
		return NULL;
}

const LV2_Descriptor synthpod_stereo = {
	.URI						= SYNTHPOD_STEREO_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
