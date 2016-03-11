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
#include <math.h>

#include <synthpod_lv2.h>

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/event/event.h>
#include <lv2/lv2plug.in/ns/ext/event/event-helpers.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	const LV2_Event_Buffer *event_in;
	LV2_Atom_Sequence *atom_out;

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
			handle->event_in = (const LV2_Event_Buffer *)data;
			break;
		case 1:
			handle->atom_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	const uint32_t capacity = handle->atom_out->atom.size;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(&handle->forge, (uint8_t *)handle->atom_out, capacity);
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_sequence_head(&handle->forge, &frame, 0);

	LV2_Event_Iterator itr;
	if(lv2_event_begin(&itr, (LV2_Event_Buffer *)handle->event_in))
	{
		uint8_t *data = NULL;

		for(const LV2_Event *ev = NULL;
			(ev = lv2_event_get(&itr, &data));
			lv2_event_increment(&itr))
		{
			if(ref)
				ref = lv2_atom_forge_frame_time(&handle->forge, ev->frames);
			if(ref)
				ref = lv2_atom_forge_atom(&handle->forge, ev->size, ev->type);
			if(ref)
				ref = lv2_atom_forge_raw(&handle->forge, data, ev->size);
			if(ref)
				lv2_atom_forge_pad(&handle->forge, ev->size);
		}
	}

	if(ref)
		lv2_atom_forge_pop(&handle->forge, &frame);
	else
		lv2_atom_sequence_clear(handle->atom_out);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	free(handle);
}

const LV2_Descriptor synthpod_event2atom = {
	.URI						= SYNTHPOD_EVENT2ATOM_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};
