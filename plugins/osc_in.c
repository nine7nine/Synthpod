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

#define BUF_SIZE 2048

typedef struct _handle_t handle_t;

struct _handle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID osc_OscEvent;
	} uris;
	LV2_Atom_Forge forge;

	volatile int working;
	LV2_Worker_Schedule *sched;

	LV2_Atom_Sequence *osc_out;

	// non-rt
	LV2_Worker_Respond_Function respond;
	LV2_Worker_Respond_Handle target;

	uv_loop_t *loop;
	osc_stream_t stream;
	uint8_t buf [BUF_SIZE];
	LV2_Atom_Forge work_forge;
};

static uint32_t cnt = 0;

// non-rt thread
static LV2_Worker_Status
_work(LV2_Handle instance,
	LV2_Worker_Respond_Function respond,
	LV2_Worker_Respond_Handle target,
	uint32_t size,
	const void *body)
{
	handle_t *handle = (handle_t *)instance;

	//printf("_work: %u %u %p\n", cnt++, size, body);

	handle->respond = respond;
	handle->target = target;

	uv_run(handle->loop, UV_RUN_NOWAIT);

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	handle_t *handle = (handle_t *)instance;

	//printf("_work_response: %u %p\n", size, body);

	LV2_Atom_Forge * forge = &handle->work_forge;
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
	handle_t *handle = (handle_t *)instance;

	//printf("_end_run\n");
		
	handle->working = 0;

	return LV2_WORKER_SUCCESS;
}

static const LV2_Worker_Interface work_ext = {
	.work = _work,
	.work_response = _work_response,
	.end_run = _end_run
};

static void
_recv_cb(osc_stream_t *stream, osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	//printf("_recv_cb: %zu\n", size);

	handle->respond(handle->target, size, buf);
}

static void
_send_cb(osc_stream_t *stream, size_t size, void *data)
{
	//TODO
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	int i;
	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	for(i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->sched = (LV2_Worker_Schedule *)features[i]->data;

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	
	if(!handle->sched)
	{
		fprintf(stderr, "%s: Host does not support worker:schedule\n",
			descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->uris.osc_OscEvent = handle->map->map(handle->map->handle,
		LV2_OSC__OscEvent);
	lv2_atom_forge_init(&handle->forge, handle->map);

	handle->loop = uv_default_loop();
	osc_stream_init(handle->loop, &handle->stream, "osc.udp4://:3333",
		_recv_cb, _send_cb, handle);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	handle_t *handle = (handle_t *)instance;

	switch(port)
	{
		case 0:
			handle->osc_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	// reset forge buffer
	lv2_atom_forge_set_buffer(&handle->work_forge, handle->buf, BUF_SIZE);
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;

	const uint8_t trig [4] = {'t', 'r', 'g', '\0'};
		
	// prepare osc atom forge
	const uint32_t capacity = handle->osc_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_out, capacity);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_sequence_head(forge, &frame, 0);
		
	if(!handle->working)
	{

		if(handle->work_forge.offset > 0)
		{
			// copy forge buffer
			lv2_atom_forge_raw(forge, handle->buf, handle->work_forge.offset);

			// reset forge buffer
			lv2_atom_forge_set_buffer(&handle->work_forge, handle->buf, BUF_SIZE);
		}
	
		// schedule new work
		if(handle->sched->schedule_work(handle->sched->handle,
			sizeof(trig), trig) == LV2_WORKER_SUCCESS)
		{
			handle->working = 1;
		}
	}

	// end sequence
	lv2_atom_forge_pop(forge, &frame);
}

static void
deactivate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;
	//nothing
}

static void
cleanup(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	osc_stream_deinit(&handle->stream);
	free(handle);
}

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_WORKER__interface))
		return &work_ext;
	else
		return NULL;
}

const LV2_Descriptor osc_io_in = {
	.URI						= OSC_IO_IN_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
