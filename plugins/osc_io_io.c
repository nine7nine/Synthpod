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

#include <uv.h>

#include <osc.h>
#include <osc_stream.h>

#include <osc_io.h>
#include <varchunk.h>

#define BUF_SIZE 4096

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID osc_OscEvent;
		LV2_URID state_default;
		LV2_URID osc_io_url;
		LV2_URID osc_io_trig;
		LV2_URID osc_io_dirty;
		
		LV2_URID log_note;
		LV2_URID log_error;
		LV2_URID log_trace;
	} uris;
	LV2_Atom_Forge forge;

	char *osc_url;
	volatile int dirty;

	volatile int working;
	LV2_Worker_Schedule *sched;

	LV2_Log_Log *log;

	const LV2_Atom_Sequence *osc_sink;
	LV2_Atom_Sequence *osc_source;
	float *connected;

	// non-rt
	LV2_Worker_Respond_Function respond;
	LV2_Worker_Respond_Handle target;

	varchunk_t *recv_buf;
	varchunk_t *send_buf;

	uv_loop_t loop;
	osc_stream_driver_t driver;
	osc_stream_t *stream;
	void *tmp;

	uint8_t work_buf [BUF_SIZE];
	LV2_Atom_Forge work_forge;
};

static void
lprintf(plughandle_t *handle, LV2_URID type, const char *fmt, ...)
{
	if(handle->log)
	{
		va_list args;
		va_start(args, fmt);
		handle->log->vprintf(handle->log->handle, type, fmt, args);
		va_end(args);
	}
	else if(type != handle->uris.log_trace)
	{
		const char *type_str = NULL;
		if(type == handle->uris.log_note)
			type_str = "Note";
		else if(type == handle->uris.log_error)
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
	plughandle_t *handle = (plughandle_t *)instance;

	return store(
		state,
		handle->uris.osc_io_url,
		handle->osc_url,
		strlen(handle->osc_url) + 1,
		handle->forge.String,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = (plughandle_t *)instance;

	size_t size;
	uint32_t type;
	uint32_t flags2;
	const char *osc_url = retrieve(
		state,
		handle->uris.osc_io_url,
		&size,
		&type,
		&flags2
	);

	// check type
	if(type != handle->forge.String)
		return LV2_STATE_ERR_BAD_TYPE;

	// check flags
	/* no need to check, as string IS POD and IS PORTABLE everywhere
	if(  !(flags2 & LV2_STATE_IS_POD)
		|| !(flags2 & LV2_STATE_IS_PORTABLE) )
		return LV2_STATE_ERR_BAD_FLAGS;
	*/

	if(osc_url && size)
	{
		if(handle->osc_url)
			free(handle->osc_url);
		handle->osc_url = strdup(osc_url);
		//handle->dirty = 1; // atomic instruction //FIXME
	}

	return LV2_STATE_SUCCESS;
}
	
static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

// non-rt thread
static LV2_Worker_Status
_work(LV2_Handle instance,
	LV2_Worker_Respond_Function respond,
	LV2_Worker_Respond_Handle target,
	uint32_t size,
	const void *body)
{
	plughandle_t *handle = (plughandle_t *)instance;
	
	const LV2_Atom *atom = body;

	if(atom->type == handle->uris.osc_OscEvent)
	{
		// add data to send queue
		osc_data_t *ptr = varchunk_write_request(handle->send_buf, atom->size);
		if(ptr)
		{
			memcpy(ptr, LV2_ATOM_BODY_CONST(atom), atom->size);
			varchunk_write_advance(handle->send_buf, atom->size);

			osc_stream_flush(handle->stream);
		}
	}
	else if(atom->type == handle->uris.osc_io_dirty)
	{
		printf("_dirty: %s\n", handle->osc_url);

		// reinitialize OSC stream
		osc_stream_free(handle->stream);
		handle->stream = osc_stream_new(&handle->loop, handle->osc_url,
			&handle->driver, handle);
	}
	
	// run main loop
	handle->respond = respond;
	handle->target = target;
	uv_run(&handle->loop, UV_RUN_NOWAIT);
	
	// reply received data to worker host
	{
		const osc_data_t *ptr;
		size_t toread;
		while((ptr = varchunk_read_request(handle->recv_buf, &toread)))
		{
			handle->respond(handle->target, toread, ptr);
			varchunk_read_advance(handle->recv_buf);
		}
	}
	
	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	plughandle_t *handle = (plughandle_t *)instance;

	//printf("_work_response: %u %s\n", size, body);

	if(!strcmp(body, "/stream/connect"))
		*handle->connected = 1.f;
	else if(!strcmp(body, "/stream/disconnect"))
		*handle->connected = 0.f;

	LV2_Atom_Forge *forge = &handle->work_forge;
	lv2_atom_forge_frame_time(forge, 0);
	lv2_atom_forge_atom(forge, size, handle->uris.osc_OscEvent);
	lv2_atom_forge_raw(forge, body, size);
	lv2_atom_forge_pad(forge, size);

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_end_run(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	//printf(handle, handle->uris.log_trace, "_end_run\n");
		
	handle->working = 0;

	return LV2_WORKER_SUCCESS;
}

static const LV2_Worker_Interface work_iface = {
	.work = _work,
	.work_response = _work_response,
	.end_run = _end_run
};

static void *
_recv_req(size_t size, void *data)
{
	plughandle_t *handle = data;

	printf("_recv_req: %zu\n", size);
	return varchunk_write_request(handle->recv_buf, size);
}

static void
_recv_adv(size_t written, void *data)
{
	plughandle_t *handle = data;

	printf("_recv_adv: %zu\n", written);
	varchunk_write_advance(handle->recv_buf, written);
}

static const void *
_send_req(size_t *len, void *data)
{
	plughandle_t *handle = data;

	printf("_send_req\n");
	return varchunk_read_request(handle->send_buf, len);
}

static void
_send_adv(void *data)
{
	plughandle_t *handle = data;

	printf("_send_adv\n");
	return varchunk_read_advance(handle->send_buf);
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	int i;
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	for(i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->sched = (LV2_Worker_Schedule *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = (LV2_Log_Log *)features[i]->data;

	if(!handle->map)
	{
		lprintf(handle, handle->uris.log_error,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	
	if(!handle->sched)
	{
		lprintf(handle, handle->uris.log_error,
			"%s: Host does not support worker:schedule\n",
			descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->uris.osc_OscEvent = handle->map->map(handle->map->handle,
		LV2_OSC__OscEvent);
	handle->uris.state_default = handle->map->map(handle->map->handle,
		LV2_STATE__loadDefaultState);
	handle->uris.osc_io_url = handle->map->map(handle->map->handle,
		OSC_IO_URL_URI);
	handle->uris.osc_io_trig = handle->map->map(handle->map->handle,
		OSC_IO_TRIG_URI);
	handle->uris.osc_io_dirty = handle->map->map(handle->map->handle,
		OSC_IO_DIRTY_URI);
	handle->uris.log_note = handle->map->map(handle->map->handle,
		LV2_LOG__Note);
	handle->uris.log_error = handle->map->map(handle->map->handle,
		LV2_LOG__Error);
	handle->uris.log_trace = handle->map->map(handle->map->handle,
		LV2_LOG__Trace);
	lv2_atom_forge_init(&handle->forge, handle->map);

	uv_loop_init(&handle->loop);

	//handle->osc_url = strdup("osc.udp4://:3333");
	//handle->osc_url = strdup("osc.udp6://:3333");
	handle->osc_url = strdup("osc.tcp4://:3333");
	//handle->osc_url = strdup("osc.tcp6://:3333");
	//handle->osc_url = strdup("osc.slip.tcp4://:3333");
	//handle->osc_url = strdup("osc.slip.tcp6://:3333");

	handle->recv_buf = varchunk_new(0x10000);
	handle->send_buf = varchunk_new(0x10000);
	if(!handle->recv_buf || !handle->send_buf)
	{
		free(handle);
		return NULL;
	}

	handle->driver.recv_req = _recv_req;
	handle->driver.recv_adv = _recv_adv;
	handle->driver.send_req = _send_req;
	handle->driver.send_adv = _send_adv;

	handle->stream = osc_stream_new(&handle->loop, handle->osc_url,
		&handle->driver, handle);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = (plughandle_t *)instance;

	switch(port)
	{
		case 0:
			handle->osc_sink = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->osc_source = (LV2_Atom_Sequence *)data;
			break;
		case 2:
			handle->connected = (float *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	// reset forge buffer
	lv2_atom_forge_set_buffer(&handle->work_forge, handle->work_buf, BUF_SIZE);
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = (plughandle_t *)instance;

	if(handle->dirty)
	{
		LV2_Atom atom = {
			.type = handle->uris.osc_io_dirty,
			.size = 0
		};

		if(handle->sched->schedule_work(handle->sched->handle,
			sizeof(LV2_Atom), &atom) == LV2_WORKER_SUCCESS)
		{
			handle->dirty = 1;
		}
	}
		
	// prepare osc atom forge
	const uint32_t capacity = handle->osc_source->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_source, capacity);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_sequence_head(forge, &frame, 0);
		
	if(!handle->working)
	{
		if(handle->work_forge.offset > 0)
		{
			// copy forge buffer
			lv2_atom_forge_raw(forge, handle->work_buf, handle->work_forge.offset);

			// reset forge buffer
			lv2_atom_forge_set_buffer(&handle->work_forge, handle->work_buf, BUF_SIZE);
		}
		
		LV2_Atom atom = {
			.type = handle->uris.osc_io_trig,
			.size = 0
		};
	
		// schedule new work
		if(handle->sched->schedule_work(handle->sched->handle,
			sizeof(LV2_Atom), &atom) == LV2_WORKER_SUCCESS)
		{
			handle->working = 1;
		}
	}

	// send sinked messages to worker
	//if(*handle->connected == 1.f)
	{
		LV2_ATOM_SEQUENCE_FOREACH(handle->osc_sink, ev)
		{
			const LV2_Atom *atom = &ev->body;
			if(atom->type == handle->uris.osc_OscEvent)
			{
				uint32_t size = sizeof(LV2_Atom) + atom->size;
				if(handle->sched->schedule_work(handle->sched->handle,
					size, atom) == LV2_WORKER_SUCCESS)
				{
					//TODO
				}
			}
		}
	}

	// end sequence
	lv2_atom_forge_pop(forge, &frame);
}

static void
deactivate(LV2_Handle instance)
{
	//plughandle_t *handle = (plughandle_t *)instance;
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	osc_stream_free(handle->stream);
	uv_loop_close(&handle->loop);
	varchunk_free(handle->recv_buf);
	varchunk_free(handle->send_buf);
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

const LV2_Descriptor osc_io_io = {
	.URI						= OSC_IO_IO_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
