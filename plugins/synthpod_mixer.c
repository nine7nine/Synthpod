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
#include <math.h>

#include <synthpod_lv2.h>

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	const float *audio_in [4];
	const float *dBFS_in [4];
	float *audio_out [4];

	float dBFS_old [4];
	float gain [4];
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
			handle->audio_in[0] = (const float *)data;
			break;
		case 1:
			handle->audio_in[1] = (const float *)data;
			break;
		case 2:
			handle->audio_in[2] = (const float *)data;
			break;
		case 3:
			handle->audio_in[3] = (const float *)data;
			break;
		case 4:
			handle->dBFS_in[0] = (const float *)data;
			break;
		case 5:
			handle->dBFS_in[1] = (const float *)data;
			break;
		case 6:
			handle->dBFS_in[2] = (const float *)data;
			break;
		case 7:
			handle->dBFS_in[3] = (const float *)data;
			break;
		case 8:
			handle->audio_out[0] = (float *)data;
			break;
		case 9:
			handle->audio_out[1] = (float *)data;
			break;
		case 10:
			handle->audio_out[2] = (float *)data;
			break;
		case 11:
			handle->audio_out[3] = (float *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	for(int i=0; i<4; i++)
	{
		handle->dBFS_old[i] = 0.f;
		handle->gain[i] = 1.f;
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	for(int i=0; i<4; i++)
	{
		if(handle->dBFS_old[i] != *handle->dBFS_in[i])
		{
			handle->gain[i] = exp(*handle->dBFS_in[i] / 20.f);

			handle->dBFS_old[i] = *handle->dBFS_in[i];
		}
	}

	for(int i=0; i<nsamples; i++)
	{
		handle->audio_out[0][i] = handle->audio_in[0][i] * handle->gain[0];
		handle->audio_out[1][i] = handle->audio_in[1][i] * handle->gain[1];
		handle->audio_out[2][i] = handle->audio_in[2][i] * handle->gain[2];
		handle->audio_out[3][i] = handle->audio_in[3][i] * handle->gain[3];
	}
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

const LV2_Descriptor synthpod_mixer = {
	.URI						= SYNTHPOD_MIXER_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
