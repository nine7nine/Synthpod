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

#include <synthpod_lv2.h>

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	const LV2_Atom_Sequence *event_out;
	const float *audio_out[2];
	const float *output[4];

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
			handle->event_out = (const LV2_Atom_Sequence *)data;
			break;

		case 1:
			handle->audio_out[0] = (const float *)data;
			break;
		case 2:
			handle->audio_out[1] = (const float *)data;
			break;

		case 3:
			handle->output[0] = (const float *)data;
			break;
		case 4:
			handle->output[1] = (const float *)data;
			break;
		case 5:
			handle->output[2] = (const float *)data;
			break;
		case 6:
			handle->output[3] = (const float *)data;
			break;

		case 7:
			handle->event_in = (LV2_Atom_Sequence *)data;
			break;

		case 8:
			handle->audio_in[0] = (float *)data;
			break;
		case 9:
			handle->audio_in[1] = (float *)data;
			break;

		case 10:
			handle->input[0] = (float *)data;
			break;
		case 11:
			handle->input[1] = (float *)data;
			break;
		case 12:
			handle->input[2] = (float *)data;
			break;
		case 13:
			handle->input[3] = (float *)data;
			break;

		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	//plughandle_t *handle = instance;
	
	// nothing
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	size_t audio_size = nsamples * sizeof(float);
	memcpy(handle->audio_in[0], handle->audio_out[0], audio_size);
	memcpy(handle->audio_in[1], handle->audio_out[1], audio_size);

	size_t seq_size = sizeof(LV2_Atom) + handle->event_out->atom.size;
	memcpy(handle->event_in, handle->event_out, seq_size);

	*handle->input[0] = *handle->output[0];
	*handle->input[1] = *handle->output[1];
	*handle->input[2] = *handle->output[2];
	*handle->input[3] = *handle->output[3];
}

static void
deactivate(LV2_Handle instance)
{
	//plughandle_t *handle = instance;

	// nothing
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	free(handle);
}

static const void*
extension_data(const char* uri)
{
	return NULL;
}

const LV2_Descriptor synthpod_source = {
	.URI						= SYNTHPOD_SOURCE_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};

const LV2_Descriptor synthpod_sink = {
	.URI						= SYNTHPOD_SINK_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
