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
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>

#define MAX_OUTPUTS 16

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	const LV2_Atom_Sequence *atom_in;
	LV2_Atom_Sequence *atom_out [MAX_OUTPUTS];

	LV2_URID MIDI_MidiEvent;
	LV2_URID_Map *map;
	LV2_Atom_Forge forge [MAX_OUTPUTS];
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;
	mlock(handle, sizeof(plughandle_t));

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

	handle->MIDI_MidiEvent = handle->map->map(handle->map->handle, LV2_MIDI__MidiEvent);

	for(unsigned i=0; i<MAX_OUTPUTS; i++)
		lv2_atom_forge_init(&handle->forge[i], handle->map);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = instance;

	if(port == 0)
		handle->atom_in = (const LV2_Atom_Sequence *)data;
	else if(port <= MAX_OUTPUTS)
		handle->atom_out[port-1] = (LV2_Atom_Sequence *)data;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	LV2_Atom_Forge_Frame frame [MAX_OUTPUTS];
	LV2_Atom_Forge_Ref ref [MAX_OUTPUTS];

	for(unsigned i=0; i<MAX_OUTPUTS; i++)
	{
		const uint32_t capacity = handle->atom_out[i]->atom.size;
		lv2_atom_forge_set_buffer(&handle->forge[i], (uint8_t *)handle->atom_out[i], capacity);
		ref[i] = lv2_atom_forge_sequence_head(&handle->forge[i], &frame[i], 0);
	}

	LV2_ATOM_SEQUENCE_FOREACH(handle->atom_in, ev)
	{
		const LV2_Atom *atom = &ev->body;

		if(atom->type == handle->MIDI_MidiEvent)
		{
			const uint8_t *buf = LV2_ATOM_BODY_CONST(atom);
			if( (buf[0] & 0xf0) == 0xf0) // system message
			{
				//TODO
			}
			else // voice message
			{
				const unsigned i = buf[0] & 0x0f; // channel
				if(ref[i])
					ref[i] = lv2_atom_forge_frame_time(&handle->forge[i], ev->time.frames);
				if(ref[i])
					ref[i] = lv2_atom_forge_raw(&handle->forge[i], atom, lv2_atom_total_size(atom));
				if(ref[i])
					lv2_atom_forge_pad(&handle->forge[i], atom->size);
			}
		}
	}

	for(unsigned i=0; i<MAX_OUTPUTS; i++)
	{
		if(ref[i])
			lv2_atom_forge_pop(&handle->forge[i], &frame[i]);
		else
			lv2_atom_sequence_clear(handle->atom_out[i]);
	}
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	munlock(handle, sizeof(plughandle_t));
	free(handle);
}

const LV2_Descriptor synthpod_midisplitter = {
	.URI						= SYNTHPOD_MIDISPLITTER_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};
