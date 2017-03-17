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
#include <lv2/lv2plug.in/ns/ext/time/time.h>

#include <props.h>

#define MAX_NPROPS 1

typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

struct _plugstate_t {
	int32_t alarm;
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	LV2_URID midi_MidiEvent;
	LV2_URID time_Position;
	LV2_URID time_speed;

	PROPS_T(props, MAX_NPROPS);

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;

	plugstate_t state;
	plugstate_t stash;

	struct {
		LV2_URID alarm;
	} urid;

	bool rolling;
};

static inline void
_trigger(plughandle_t *handle, LV2_Atom_Forge *forge, int64_t frames)
{
	uint8_t m [2];
	
	for(uint8_t i=0x0; i<0x10; i++)
	{
		// all notes off
		m[0] = LV2_MIDI_MSG_CONTROLLER | i;
		m[1] = LV2_MIDI_CTL_ALL_NOTES_OFF;

		if(handle->ref)
			handle->ref = lv2_atom_forge_frame_time(forge, frames);
		if(handle->ref)
			handle->ref = lv2_atom_forge_atom(forge, sizeof(m), handle->midi_MidiEvent);
		if(handle->ref)
			handle->ref = lv2_atom_forge_write(forge, m, sizeof(m));

		// all sounds off
		m[0] = LV2_MIDI_MSG_CONTROLLER | i;
		m[1] = LV2_MIDI_CTL_ALL_SOUNDS_OFF;

		if(handle->ref)
			handle->ref = lv2_atom_forge_frame_time(forge, frames);
		if(handle->ref)
			handle->ref = lv2_atom_forge_atom(forge, sizeof(m), handle->midi_MidiEvent);
		if(handle->ref)
			handle->ref = lv2_atom_forge_write(forge, m, sizeof(m));
	}
}

static void
_intercept_alarm(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	if(handle->state.alarm)
	{
		handle->state.alarm = false;
		props_set(&handle->props, forge, frames, handle->urid.alarm, &handle->ref);

		_trigger(handle, forge, frames);
	}
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = SYNTHPOD_PANIC_URI"_alarm",
		.offset = offsetof(plugstate_t, alarm),
		.type = LV2_ATOM__Bool,
		.event_mask = PROP_EVENT_WRITE,
		.event_cb = _intercept_alarm
	}
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
	handle->time_Position = handle->map->map(handle->map->handle, LV2_TIME__Position);
	handle->time_speed = handle->map->map(handle->map->handle, LV2_TIME__speed);

	lv2_atom_forge_init(&handle->forge, handle->map);

	if(!props_init(&handle->props, MAX_NPROPS, descriptor->URI, handle->map, handle))
	{
		fprintf(stderr, "failed to allocate property structure\n");
		free(handle);
		return NULL;
	}

	if(!props_register(&handle->props, defs, MAX_NPROPS, &handle->state, &handle->stash))
	{
		fprintf(stderr, "failed to init property structure\n");
		free(handle);
		return NULL;
	}

	handle->urid.alarm = props_map(&handle->props, defs[0].property);

	handle->rolling = true;

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

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const LV2_Atom *atom = (const LV2_Atom *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(lv2_atom_forge_is_object_type(forge, obj->atom.type))
		{
			if(!props_advance(&handle->props, forge, frames, obj, &handle->ref)
				&& (obj->body.otype == handle->time_Position) )
			{
				const LV2_Atom_Float *speed = NULL;
				lv2_atom_object_get(obj, handle->time_speed, &speed, 0);
				if(speed && (speed->atom.type == forge->Float) )
				{
					if(handle->rolling && (speed->body == 0.f) ) // do not retrigger when already stopped
						_trigger(handle, forge, frames);
					handle->rolling = speed->body != 0.f;
				}
			}
		}
	}

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

static inline LV2_Worker_Status
_work(LV2_Handle instance, LV2_Worker_Respond_Function respond,
LV2_Worker_Respond_Handle worker, uint32_t size, const void *body)
{
	plughandle_t *handle = instance;

	return props_work(&handle->props, respond, worker, size, body);
}

static inline LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	plughandle_t *handle = instance;

	return props_work_response(&handle->props, size, body);
}

static const LV2_Worker_Interface work_iface = {
	.work = _work,
	.work_response = _work_response,
	.end_run = NULL
};

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;
	else if(!strcmp(uri, LV2_WORKER__interface))
		return &work_iface;
	return NULL;
}

const LV2_Descriptor synthpod_panic = {
	.URI						= SYNTHPOD_PANIC_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
