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
#include <math.h>

#include <synthpod_lv2.h>

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>

#include <props.h>

#define MAX_SLOTS 8
#define MAX_NPROPS (5 * MAX_SLOTS)
#define MIN_VAL 0
#define MAX_VAL 127

typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

struct _plugstate_t {
	int32_t learn [MAX_SLOTS];
	int32_t cntrl [MAX_SLOTS];
	int32_t min [MAX_SLOTS];
	int32_t max [MAX_SLOTS];
	int32_t raw [MAX_SLOTS];
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	LV2_URID midi_MidiEvent;

	PROPS_T(props, MAX_NPROPS);

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;
	float *output [MAX_SLOTS];

	bool learning;
	plugstate_t state;
	plugstate_t stash;

	struct {
		LV2_URID learn [MAX_SLOTS];
		LV2_URID cntrl [MAX_SLOTS];
		LV2_URID min [MAX_SLOTS];
		LV2_URID max [MAX_SLOTS];
		LV2_URID raw [MAX_SLOTS];
	} urid;

	float value [MAX_SLOTS];
	float divider [MAX_SLOTS];
};

static inline void
_update_divider(plughandle_t *handle, int i)
{
	if(handle->state.max[i] != handle->state.min[i])
		handle->divider[i] = 1.f / (handle->state.max[i] - handle->state.min[i]);
	else
		handle->divider[i] = 1.f;
}

static inline void
_update_value(plughandle_t *handle, int i)
{
	handle->value[i] = (handle->state.raw[i] - handle->state.min[i]) * handle->divider[i];
}

static void
_intercept_learn(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	const int i = (int32_t *)impl->value.body - handle->state.learn;

	if(handle->state.learn[i]) // set to learn
	{
		handle->learning = true;

		handle->state.min[i] = MAX_VAL;
		props_set(&handle->props, &handle->forge, frames, handle->urid.min[i], &handle->ref);

		handle->state.max[i] = MIN_VAL;
		props_set(&handle->props, &handle->forge, frames, handle->urid.max[i], &handle->ref);
	}
}

static void
_intercept_min(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	const int i = (int32_t *)impl->value.body - handle->state.min;
	_update_divider(handle, i);
	_update_value(handle, i);
}

static void
_intercept_max(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	const int i = (int32_t *)impl->value.body - handle->state.max;
	_update_divider(handle, i);
	_update_value(handle, i);
}

static void
_intercept_raw(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	const int i = (int32_t *)impl->value.body - handle->state.raw;
	_update_value(handle, i);
}

#define STAT_LEARN(NUM) \
{ \
	.property = SYNTHPOD_MIDI2CONTROL_URI"_learn_"#NUM, \
	.offset = offsetof(plugstate_t, learn) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Bool, \
	.event_cb = _intercept_learn \
}

#define STAT_CNTRL(NUM) \
{ \
	.property = SYNTHPOD_MIDI2CONTROL_URI"_cntrl_"#NUM, \
	.offset = offsetof(plugstate_t, cntrl) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Int, \
}

#define STAT_MIN(NUM) \
{ \
	.property = SYNTHPOD_MIDI2CONTROL_URI"_min_"#NUM, \
	.offset = offsetof(plugstate_t, min) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Int, \
	.event_cb = _intercept_min \
}

#define STAT_MAX(NUM) \
{ \
	.property = SYNTHPOD_MIDI2CONTROL_URI"_max_"#NUM, \
	.offset = offsetof(plugstate_t, max) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Int, \
	.event_cb = _intercept_max \
}

#define STAT_RAW(NUM) \
{ \
	.property = SYNTHPOD_MIDI2CONTROL_URI"_raw_"#NUM, \
	.offset = offsetof(plugstate_t, raw) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Int, \
	.event_cb = _intercept_raw \
}

static const props_def_t defs [MAX_NPROPS] = {
	STAT_LEARN(1),
	STAT_LEARN(2),
	STAT_LEARN(3),
	STAT_LEARN(4),
	STAT_LEARN(5),
	STAT_LEARN(6),
	STAT_LEARN(7),
	STAT_LEARN(8),

	STAT_CNTRL(1),
	STAT_CNTRL(2),
	STAT_CNTRL(3),
	STAT_CNTRL(4),
	STAT_CNTRL(5),
	STAT_CNTRL(6),
	STAT_CNTRL(7),
	STAT_CNTRL(8),

	STAT_MIN(1),
	STAT_MIN(2),
	STAT_MIN(3),
	STAT_MIN(4),
	STAT_MIN(5),
	STAT_MIN(6),
	STAT_MIN(7),
	STAT_MIN(8),

	STAT_MAX(1),
	STAT_MAX(2),
	STAT_MAX(3),
	STAT_MAX(4),
	STAT_MAX(5),
	STAT_MAX(6),
	STAT_MAX(7),
	STAT_MAX(8),

	STAT_RAW(1),
	STAT_RAW(2),
	STAT_RAW(3),
	STAT_RAW(4),
	STAT_RAW(5),
	STAT_RAW(6),
	STAT_RAW(7),
	STAT_RAW(8),
};

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
	}

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->midi_MidiEvent = handle->map->map(handle->map->handle, LV2_MIDI__MidiEvent);

	lv2_atom_forge_init(&handle->forge, handle->map);

	if(!props_init(&handle->props, descriptor->URI,
		defs, MAX_NPROPS, &handle->state, &handle->stash,
		handle->map, handle))
	{
		fprintf(stderr, "failed to allocate property structure\n");
		free(handle);
		return NULL;
	}

	for(unsigned i=0; i<MAX_SLOTS; i++)
	{
		handle->urid.learn[i] = props_map(&handle->props, defs[0*MAX_SLOTS + i].property);
		handle->urid.cntrl[i] = props_map(&handle->props, defs[1*MAX_SLOTS + i].property);
		handle->urid.min[i] = props_map(&handle->props, defs[2*MAX_SLOTS + i].property);
		handle->urid.max[i] = props_map(&handle->props, defs[3*MAX_SLOTS + i].property);
		handle->urid.raw[i] = props_map(&handle->props, defs[4*MAX_SLOTS + i].property);
	}

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = instance;

	if(port == 0)
		handle->event_in = (const LV2_Atom_Sequence *)data;
	else if(port == 1)
		handle->event_out = (LV2_Atom_Sequence *)data;
	else if(port < 2 + MAX_SLOTS)
		handle->output[port - 2] = (float *)data;
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	handle->learning = false;
}

__realtime static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	const uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Frame frame;
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	props_idle(&handle->props, &handle->forge, 0, &handle->ref);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const LV2_Atom *atom = (const LV2_Atom *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(atom->type == handle->midi_MidiEvent)
		{
			const uint8_t *msg = LV2_ATOM_BODY_CONST(atom);

			if( (msg[0] & 0xf0) != LV2_MIDI_MSG_CONTROLLER)
				continue; // ignore non-controller messages

			const int32_t cntrl = msg[1];

			if(handle->learning)
			{

				for(unsigned i=0; i<MAX_SLOTS; i++)
				{
					if(handle->state.learn[i])
					{
						handle->state.learn[i] = false;
						props_set(&handle->props, forge, frames, handle->urid.learn[i], &handle->ref);

						handle->state.cntrl[i] = cntrl;
						props_set(&handle->props, forge, frames, handle->urid.cntrl[i], &handle->ref);
					}
				}

				handle->learning = false;
			}

			for(unsigned i=0; i<MAX_SLOTS; i++) //FIXME use bsearch?
			{
				if(handle->state.cntrl[i] == cntrl)
				{
					const int32_t value = msg[2];

					if(value < handle->state.min[i])
					{
						handle->state.min[i] = value;
						_update_divider(handle, i);
						props_set(&handle->props, forge, frames, handle->urid.min[i], &handle->ref);
					}

					if(value > handle->state.max[i])
					{
						handle->state.max[i] = value;
						_update_divider(handle, i);
						props_set(&handle->props, forge, frames, handle->urid.max[i], &handle->ref);
					}

					handle->state.raw[i] = value;
					_update_value(handle, i);
					props_stash(&handle->props, handle->urid.raw[i]);
				}
			}
		}
		else if(!props_advance(&handle->props, forge, frames, obj, &handle->ref))
		{
			// do nothing
		}
	}

	for(unsigned i=0; i<MAX_SLOTS; i++)
		*handle->output[i] = handle->value[i];

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle)
		free(handle);
}

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;

	return props_save(&handle->props, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;

	return props_restore(&handle->props, retrieve, state, flags, features);
}

static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;

	return NULL;
}

const LV2_Descriptor synthpod_midi2control = {
	.URI						= SYNTHPOD_MIDI2CONTROL_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
