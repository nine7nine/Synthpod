/*
 * Copyright (c) 2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the voiceied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#include <stdio.h>

#include <xpress.h>

#include <lv2/lv2plug.in/ns/ext/log/log.h>

#define XPRESS_TEST_URI	XPRESS_PREFIX"test"

#define MAX_NVOICES 32

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Log_Log *log;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	LV2_URID log_trace;

	struct {
		XPRESS_T(xpress, MAX_NVOICES);
	} in;
	struct {
		XPRESS_T(xpress, MAX_NVOICES);
		LV2_URID targets [MAX_NVOICES];
	} out;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;
};

static int
_log_vprintf(plughandle_t *handle, LV2_URID type, const char *fmt, va_list args)
{
	return handle->log->vprintf(handle->log->handle, type, fmt, args);
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_printf(plughandle_t *handle, LV2_URID type, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(handle, type, fmt, args);
  va_end(args);

	return ret;
}

static void
_intercept_in(void *data, LV2_Atom_Forge *forge, int64_t frames,
	xpress_event_t event, xpress_voice_t *voice)
{
	plughandle_t *handle = data;

	switch(event)
	{
		case XPRESS_EVENT_ALLOC:
		{
			_log_printf(handle, handle->log_trace, "IN  ALLOC: %u", voice->subject);

			LV2_URID *target = &handle->out.targets[voice->pos];
			*target = xpress_alloc(&handle->out.xpress);
			if(*target)
			{
				xpress_voice_t *dst = _xpress_voice_search(&handle->out.xpress, *target);
				if(dst)
				{
					dst->zone = voice->zone;
					dst->pitch = voice->pitch * 2;
					dst->pressure = voice->pressure;
					dst->timbre = voice->timbre;
				}
				
				if(handle->ref)
					handle->ref = xpress_put(&handle->out.xpress, forge, frames, *target);
			}

			break;
		}
		case XPRESS_EVENT_FREE:
		{
			_log_printf(handle, handle->log_trace, "IN  FREE : %u", voice->subject);

			LV2_URID *target = &handle->out.targets[voice->pos];
			if(*target)
			{
				if(handle->ref)
					handle->ref = xpress_del(&handle->out.xpress, forge, frames, *target);

				xpress_free(&handle->out.xpress, *target);
			}

			break;
		}
		case XPRESS_EVENT_PUT:
		{
			_log_printf(handle, handle->log_trace, "IN  PUT  : %u", voice->subject);

			LV2_URID *target = &handle->out.targets[voice->pos];
			if(*target)
			{
				xpress_voice_t *dst = _xpress_voice_search(&handle->out.xpress, *target);
				if(dst)
				{
					dst->zone = voice->zone;
					dst->pitch = voice->pitch * 2;
					dst->pressure = voice->pressure;
					dst->timbre = voice->timbre;
				}

				if(handle->ref)
					handle->ref = xpress_put(&handle->out.xpress, forge, frames, *target);
			}

			break;
		}
	}
}

static void
_intercept_out(void *data, LV2_Atom_Forge *forge, int64_t frames,
	xpress_event_t event, xpress_voice_t *voice)
{
	plughandle_t *handle = data;

	switch(event)
	{
		case XPRESS_EVENT_ALLOC:
		{
			_log_printf(handle, handle->log_trace, "OUT ALLOC: %u", voice->subject);

			break;
		}
		case XPRESS_EVENT_FREE:
		{
			_log_printf(handle, handle->log_trace, "OUT FREE : %u", voice->subject);

			break;
		}
		case XPRESS_EVENT_PUT:
		{
			_log_printf(handle, handle->log_trace, "OUT PUT  : %u", voice->subject);

			break;
		}
	}
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	for(unsigned i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->log)
	{
		fprintf(stderr,
			"%s: Host does not support log:log\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->log_trace = handle->map->map(handle->map->handle, LV2_LOG__Trace);

	lv2_atom_forge_init(&handle->forge, handle->map);
	if(  !xpress_init(&handle->in.xpress, MAX_NVOICES, descriptor->URI, handle->map,
			XPRESS_EVENT_ALL, _intercept_in, handle)
		|| !xpress_init(&handle->out.xpress, MAX_NVOICES, descriptor->URI, handle->map,
			XPRESS_EVENT_ALL, _intercept_out, handle))
	{
		fprintf(stderr, "failed to initialize xpress structure\n");
		free(handle);
		return NULL;
	}

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = (plughandle_t *)instance;

	switch(port)
	{
		case 0:
			handle->event_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->event_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(&handle->forge, (uint8_t *)handle->event_out, capacity);
	handle->ref = lv2_atom_forge_sequence_head(&handle->forge, &frame, 0);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

		if(handle->ref)
			xpress_advance(&handle->in.xpress, &handle->forge, ev->time.frames, obj, &handle->ref); //TODO handle return
	}
	if(handle->ref)
		lv2_atom_forge_pop(&handle->forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	free(handle);
}

const LV2_Descriptor xpress_test = {
	.URI						= XPRESS_TEST_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch(index)
	{
		case 0:
			return &xpress_test;
		default:
			return NULL;
	}
}
