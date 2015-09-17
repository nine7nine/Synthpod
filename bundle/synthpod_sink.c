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
#include <string.h>

#include <synthpod_bundle.h>
#include <system_port.h>

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	LV2_Atom_Sequence *event_in;
	float *audio_in[2];
	float *input[4];
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = instance;

	switch(port)
	{
		case 0:
			handle->event_in = (LV2_Atom_Sequence *)data;
			break;

		case 1:
			handle->audio_in[0] = (float *)data;
			break;
		case 2:
			handle->audio_in[1] = (float *)data;
			break;

		case 3:
			handle->input[0] = (float *)data;
			break;
		case 4:
			handle->input[1] = (float *)data;
			break;
		case 5:
			handle->input[2] = (float *)data;
			break;
		case 6:
			handle->input[3] = (float *)data;
			break;

		default:
			break;
	}
}

static System_Port_Type
query(LV2_Handle instance, uint32_t port)
{
	switch(port)
	{
		case 0:
			return SYSTEM_PORT_MIDI;

		case 1:
		case 2:
			return SYSTEM_PORT_AUDIO;

		case 3:
		case 4:
		case 5:
		case 6:
			return SYSTEM_PORT_CONTROL;

		default:
			return SYSTEM_PORT_NONE;
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	// nothing
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	free(handle);
}

static const System_Port_Interface sys = {
	.query = query
};

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, SYSTEM_PORT__interface))
		return &sys;

	return NULL;
}

const LV2_Descriptor synthpod_sink = {
	.URI						= SYNTHPOD_SINK_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
