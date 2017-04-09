/*
 * Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
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
#include <time.h>

#include <synthpod_lv2.h>

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	double srate_1;
	const float *audio_in;
	float *audio_out;
	const float *load;
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;
	mlock(handle, sizeof(plughandle_t));

	handle->srate_1 = 1e-2 / rate; // (seconds per sample) * 1%

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = instance;

	switch(port)
	{
		case 0:
			handle->audio_in = (const float *)data;
			break;
		case 1:
			handle->audio_out = (float *)data;
			break;
		case 2:
			handle->load= (const float *)data;
			break;
		default:
			break;
	}
}

__realtime static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	const unsigned thresh = *handle->load * 10000;

	for(unsigned j=0; j<nsamples; j++)
		handle->audio_out[j] = handle->audio_in[j];

	for(unsigned i=0; i<thresh; i++)
	{
		for(unsigned j=0; j<nsamples; j++)
			handle->audio_out[j] *= 0.9f;
	}
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	munlock(handle, sizeof(plughandle_t));
	free(handle);
}

static const void*
extension_data(const char* uri)
{
	return NULL;
}

const LV2_Descriptor synthpod_heavyload = {
	.URI						= SYNTHPOD_HEAVYLOAD_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
