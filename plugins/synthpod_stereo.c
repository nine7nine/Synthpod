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
#include <lv2/lv2plug.in/ns/ext/buf-size/buf-size.h>
#include <lv2/lv2plug.in/ns/ext/options/options.h>

#include <Eina.h>

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	sp_app_t *app;
	sp_app_driver_t driver;

	LV2_Worker_Schedule *schedule;
	LV2_Log_Log *log;
	LV2_Options_Option *opts;

	volatile int working;

	struct {
		struct {
			LV2_URID entry;
			LV2_URID error;
			LV2_URID note;
			LV2_URID trace;
			LV2_URID warning;
		} log;
		struct {
			LV2_URID max_block_length;
			LV2_URID min_block_length;
			LV2_URID sequence_size;
		} bufsz;
		struct {
			LV2_URID event;
		} synthpod;
	} uri;

	struct {
		LV2_Atom_Forge event_out;
		LV2_Atom_Forge notify;
		LV2_Atom_Forge work;
	} forge;

	struct {
		const LV2_Atom_Sequence *event_in;
		const LV2_Atom_Sequence *control;
		const float *audio_in[2];
		LV2_Atom_Sequence *event_out;
		LV2_Atom_Sequence *notify;
		float *audio_out[2];
	} port;

	// non-rt worker-thread
	struct {
		LV2_Worker_Respond_Function respond;
		LV2_Worker_Respond_Handle target;
	} worker;

	struct {
		uint8_t ui [CHUNK_SIZE] _ATOM_ALIGNED;
		uint8_t worker [CHUNK_SIZE] _ATOM_ALIGNED;
		uint8_t app [CHUNK_SIZE] _ATOM_ALIGNED;
		uint8_t tmp [CHUNK_SIZE];
	} buf;
};

static int
_log_vprintf(void *data, LV2_URID type, const char *fmt, va_list args)
{
	plughandle_t *handle = data;

	if(handle->log)
	{
		return handle->log->vprintf(handle->log->handle, type, fmt, args);
	}
	else if(type != handle->uri.log.trace)
	{
		const char *type_str = NULL;
		if(type == handle->uri.log.entry)
			type_str = "Entry";
		else if(type == handle->uri.log.error)
			type_str = "Error";
		else if(type == handle->uri.log.note)
			type_str = "Note";
		else if(type == handle->uri.log.trace)
			type_str = "Trace";
		else if(type == handle->uri.log.warning)
			type_str = "Warning";

		fprintf(stderr, "[%s]", type_str);
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

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;
	sp_app_t *app = handle->app;

	return sp_app_save(app, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;
	sp_app_t *app = handle->app;

	return sp_app_restore(app, retrieve, state, flags, features);
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
	plughandle_t *handle = instance;
	
	//printf("_work: %u\n", size);
	
	handle->worker.respond = respond;
	handle->worker.target = target;

	sp_worker_from_app(handle->app, size, body);

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	plughandle_t *handle = instance;

	//printf("_work_response: %u\n", size);

	sp_app_from_worker(handle->app, size, body);

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_end_run(LV2_Handle instance)
{
	plughandle_t *handle = instance;
	
	handle->working = 0;

	return LV2_WORKER_SUCCESS;
}

static const LV2_Worker_Interface work_iface = {
	.work = _work,
	.work_response = _work_response,
	.end_run = _end_run
};

// rt-thread
static void *
_to_ui_request(size_t size, void *data)
{
	plughandle_t *handle = data;

	return handle->buf.ui;
}
static void
_to_ui_advance(size_t size, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge.work;

	//printf("_to_ui_advance: %zu\n", size);

	if(forge->offset + size > forge->size)
		return; // buffer overflow

	lv2_atom_forge_frame_time(forge, 0);
	lv2_atom_forge_raw(forge, handle->buf.ui, size);
	lv2_atom_forge_pad(forge, size);
}

// rt-thread
static void *
_to_worker_request(size_t size, void *data)
{
	plughandle_t *handle = data;
	
	//TODO test capacity

	return handle->buf.worker;
}
static void
_to_worker_advance(size_t size, void *data)
{
	plughandle_t *handle = data;
	
	//printf("_to_worker_advance: %zu\n", size);
	
	handle->working = 1;
	handle->schedule->schedule_work(handle->schedule->handle,
		size, handle->buf.worker);
	//TODO check return result
}

// non-rt worker-thread
static void *
_to_app_request(size_t size, void *data)
{
	plughandle_t *handle = data;

	//TODO test capacity

	return handle->buf.app;
}
static void
_to_app_advance(size_t size, void *data)
{
	plughandle_t *handle = data;

	//printf("_to_app_advance: %zu\n", size);

	handle->worker.respond(handle->worker.target, size, handle->buf.app);
	//TODO check return result
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	eina_init();

	int i;
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	handle->driver.sample_rate = rate;
	handle->driver.seq_size = SEQ_SIZE;
	handle->driver.log_printf = _log_printf;
	handle->driver.log_vprintf = _log_vprintf;

	const LilvWorld *world = NULL;

	for(i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->driver.map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->driver.unmap = (LV2_URID_Unmap *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = (LV2_Log_Log *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->schedule = (LV2_Worker_Schedule *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_OPTIONS__options))
			handle->opts = (LV2_Options_Option *)features[i]->data;
		else if(!strcmp(features[i]->URI, "http://open-music-kontrollers.ch/lv2/synthpod#world"))
			world = (const LilvWorld *)features[i]->data;

	if(!handle->driver.map)
	{
		_log_printf(handle, handle->uri.log.error,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	
	if(!handle->schedule)
	{
		_log_printf(handle, handle->uri.log.error,
			"%s: Host does not support worker:schedule\n",
			descriptor->URI);
		free(handle);
		return NULL;
	}
	
	if(!handle->opts)
	{
		_log_printf(handle, handle->uri.log.error,
			"%s: Host does not support options:option\n",
			descriptor->URI);
		free(handle);
		return NULL;
	}

	// map URIs
	handle->uri.log.entry = handle->driver.map->map(handle->driver.map->handle,
		LV2_LOG__Entry);
	handle->uri.log.error = handle->driver.map->map(handle->driver.map->handle,
		LV2_LOG__Error);
	handle->uri.log.note = handle->driver.map->map(handle->driver.map->handle,
		LV2_LOG__Note);
	handle->uri.log.trace = handle->driver.map->map(handle->driver.map->handle,
		LV2_LOG__Trace);
	handle->uri.log.warning = handle->driver.map->map(handle->driver.map->handle,
		LV2_LOG__Warning);
			
	handle->uri.bufsz.max_block_length = handle->driver.map->map(handle->driver.map->handle,
		LV2_BUF_SIZE__maxBlockLength);
	handle->uri.bufsz.min_block_length = handle->driver.map->map(handle->driver.map->handle,
		LV2_BUF_SIZE__minBlockLength);
	handle->uri.bufsz.sequence_size = handle->driver.map->map(handle->driver.map->handle,
		LV2_BUF_SIZE__sequenceSize);

	handle->uri.synthpod.event = handle->driver.map->map(handle->driver.map->handle,
		SYNTHPOD_EVENT_URI);

	for(LV2_Options_Option *opt = handle->opts;
		(opt->key != 0) && (opt->value != NULL);
		opt++)
	{
		if(opt->key == handle->uri.bufsz.max_block_length)
			handle->driver.max_block_size = *(int32_t *)opt->value;
		else if(opt->key == handle->uri.bufsz.sequence_size)
			handle->driver.min_block_size = *(int32_t *)opt->value;
		//TODO handle more options
	}

	handle->driver.to_ui_request = _to_ui_request;
	handle->driver.to_ui_advance = _to_ui_advance;
	handle->driver.to_worker_request = _to_worker_request;
	handle->driver.to_worker_advance = _to_worker_advance;
	handle->driver.to_app_request = _to_app_request;
	handle->driver.to_app_advance = _to_app_advance;

	handle->app = sp_app_new(world, &handle->driver, handle);
	if(!handle->app)
	{
		_log_printf(handle, handle->uri.log.error,
			"%s: creation of app failed\n",
			descriptor->URI);
		free(handle);
		return NULL;
	}

	lv2_atom_forge_init(&handle->forge.event_out, handle->driver.map);
	lv2_atom_forge_init(&handle->forge.notify, handle->driver.map);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = instance;
	sp_app_t *app = handle->app;

	switch(port)
	{
		case 0:
			handle->port.event_in = (const LV2_Atom_Sequence *)data;
			sp_app_set_system_source(app, 0, data);
			break;
		case 1:
			handle->port.event_out = (LV2_Atom_Sequence *)data;
			sp_app_set_system_sink(app, 0, data);
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
			handle->port.audio_out[0] = (float *)data;
			sp_app_set_system_sink(app, 1, data);
			break;
		case 5:
			handle->port.audio_out[1] = (float *)data;
			sp_app_set_system_sink(app, 2, data);
			break;
		case 6:
			handle->port.control = (const LV2_Atom_Sequence *)data;
			break;
		case 7:
			handle->port.notify = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	lv2_atom_forge_set_buffer(&handle->forge.work, handle->buf.tmp, CHUNK_SIZE);
	sp_app_activate(handle->app);
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;
	sp_app_t *app = handle->app;

	struct {
		LV2_Atom_Forge_Frame event_out;
		LV2_Atom_Forge_Frame notify;
	} frame;

	// prepare forge(s) & sequence(s)
	lv2_atom_forge_set_buffer(&handle->forge.event_out,
		(uint8_t *)handle->port.event_out, handle->port.event_out->atom.size);
	lv2_atom_forge_sequence_head(&handle->forge.event_out, &frame.event_out, 0);
	
	lv2_atom_forge_set_buffer(&handle->forge.notify,
		(uint8_t *)handle->port.notify, handle->port.notify->atom.size);
	lv2_atom_forge_sequence_head(&handle->forge.notify, &frame.notify, 0);

	if(!handle->working)
	{
		if(handle->forge.work.offset > 0)
		{
			// copy forge buffer
			lv2_atom_forge_raw(&handle->forge.notify, handle->buf.tmp, handle->forge.work.offset);

			// reset forge buffer
			lv2_atom_forge_set_buffer(&handle->forge.work, handle->buf.tmp, CHUNK_SIZE);
		}
	}

	// run app pre
	sp_app_run_pre(app, nsamples);

	// handle events from UI
	LV2_ATOM_SEQUENCE_FOREACH(handle->port.control, ev)
	{
		const LV2_Atom *atom = &ev->body;
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)atom;

		if(  (atom->type == handle->forge.notify.Object)
			&& (obj->body.otype == handle->uri.synthpod.event) )
		{
			//printf("control: %u\n", atom->size);
			sp_app_from_ui(app, atom);
		}
	}
	
	// run app post
	sp_app_run_post(app, nsamples);
		
	// end sequence(s)
	lv2_atom_forge_pop(&handle->forge.event_out, &frame.event_out);
	lv2_atom_forge_pop(&handle->forge.notify, &frame.notify);
}

static void
deactivate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	sp_app_deactivate(handle->app);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	sp_app_free(handle->app);
	free(handle);

	eina_shutdown();
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
