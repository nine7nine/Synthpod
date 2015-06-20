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

#define MAX_PORTS 8

struct _plughandle_t {
	const float *mode;
	const float *cv_in [MAX_PORTS];
	float *output [MAX_PORTS];
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
			handle->mode = (const float *)data;
			break;
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
			handle->cv_in[port-1] = (const float *)data;
			break;
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
		case 16:
			handle->output[port-9] = (float *)data;
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

	int mode = floor(*handle->mode);

	for(int i=0; i<MAX_PORTS; i++)
	{
		switch(mode)
		{
			case 0:
			{
				float min = 1.f;

				for(int f=0; f<nsamples; f++)
				{
					const float *val = &handle->cv_in[i][f];
					if(*val < min)
						min = *val;
				}

				*handle->output[i] = min;

				break;
			}
			case 1:
			{
				float sum = 0.f;

				for(int f=0; f<nsamples; f++)
				{
					const float *val = &handle->cv_in[i][f];
					sum += *val;
				}

				*handle->output[i] = sum / nsamples;

				break;
			}
			case 2:
			{
				float max = 0.f;

				for(int f=0; f<nsamples; f++)
				{
					const float *val = &handle->cv_in[i][f];
					if(*val > max)
						max = *val;
				}

				*handle->output[i] = max;

				break;
			}
		}
	}
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

const LV2_Descriptor synthpod_cv2control = {
	.URI						= SYNTHPOD_CV2CONTROL_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
