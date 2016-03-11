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

typedef struct _plughandle_t plughandle_t;

#define MAX_OUTPUTS 8

struct _plughandle_t {
	const LV2_Atom_Sequence *atom_in;
	const float *offset;
	float *cv_out [MAX_OUTPUTS];
	float last [MAX_OUTPUTS];

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
			handle->atom_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->offset = (const float *)data;
			break;
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
			handle->cv_out[port - 2] = (float *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	for(int i=0; i<MAX_OUTPUTS; i++)
		handle->last[i] = 0.f;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	int offset = floor(*handle->offset);
	int bound = offset + MAX_OUTPUTS;

	uint32_t f [MAX_OUTPUTS];
	for(int i=0; i<MAX_OUTPUTS; i++)
		f[i] = 0;

	LV2_ATOM_SEQUENCE_FOREACH(handle->atom_in, ev)
	{
		const LV2_Atom *atom = &ev->body;

		if(atom->type == handle->forge.Tuple)
		{
			int i = -1;
			float val;

			const LV2_Atom_Tuple *tup = (const LV2_Atom_Tuple *)atom;
			const LV2_Atom *itm = lv2_atom_tuple_begin(tup);

			if(!itm || lv2_atom_tuple_is_end(LV2_ATOM_BODY(tup), tup->atom.size, itm))
				continue; // skip

			if(itm->type == handle->forge.Int)
				i = ((const LV2_Atom_Int *)itm)->body;
			else if(itm->type == handle->forge.Long)
				i = ((const LV2_Atom_Long *)itm)->body;
			else
				continue; // skip

			itm = lv2_atom_tuple_next(itm);

			if( !itm || (i <= offset) || (i > bound) )
				continue; // skip

			if(itm->type == handle->forge.Float)
				val = ((const LV2_Atom_Float *)itm)->body;
			else if(itm->type == handle->forge.Double)
				val = ((const LV2_Atom_Double *)itm)->body;
			else
				continue; //skip

			// valid atom
			i -= offset + 1;
			for( ; f[i]<ev->time.frames; f[i]++)
				handle->cv_out[i][f[i]] = handle->last[i];

			handle->last[i] = val;
		}
		else
			continue; // unsupported type
	}

	for(int i=0; i<MAX_OUTPUTS; i++)
	{
		for( ; f[i]<nsamples; f[i]++)
			handle->cv_out[i][f[i]] = handle->last[i];
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

const LV2_Descriptor synthpod_atom2cv = {
	.URI						= SYNTHPOD_ATOM2CV_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
