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
	const float *cv_in;
	const float *type;
	LV2_Atom_Sequence *atom_out;
	float last;

	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;
  }

	if(!handle->map)
	{
		free(handle);
		return NULL;
	}

	lv2_atom_forge_init(&handle->forge, handle->map);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = instance;

	switch(port)
	{
		case 0:
			handle->cv_in = (const float *)data;
			break;
		case 1:
			handle->type = (const float *)data;
			break;
		case 2:
			handle->atom_out = (LV2_Atom_Sequence *)data;
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

	int type = floor(*handle->type);

	uint32_t capacity = handle->atom_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->atom_out, capacity);
	lv2_atom_forge_sequence_head(forge, &frame, 0);

	for(int i=0; i<nsamples; i++)
	{
		const float *val = &handle->cv_in[i];

		if(*val != handle->last)
		{
			lv2_atom_forge_frame_time(forge, i);
			switch(type)
			{
				case 0:
					lv2_atom_forge_int(forge, floor(*val));
					break;
				case 1:
					lv2_atom_forge_long(forge, floor(*val));
					break;
				case 2:
					lv2_atom_forge_float(forge, *val);
					break;
				case 3:
					lv2_atom_forge_double(forge, *val);
					break;
			}

			handle->last = *val;
		}
	}

	lv2_atom_forge_pop(forge, &frame);
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

const LV2_Descriptor synthpod_cv2atom = {
	.URI						= SYNTHPOD_CV2ATOM_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
