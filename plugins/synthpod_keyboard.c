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
	struct {
		LV2_URID midi_event;
	} uri;

	LV2_URID_Map *map;
	LV2_Atom_Forge forge;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;
	const float *octave;
	const float *channel;
	const float *velocity;
	const float *controller_id;
	const float *controller_val;
	const float *program_change;
	const float *channel_pressure;
	const float *bender;

	uint8_t _controller_id;
	uint8_t _controller_val;
	uint8_t _program_change;
	uint8_t _channel_pressure;
	uint16_t _bender;
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

	handle->uri.midi_event = handle->map->map(handle->map->handle,
		LV2_MIDI__MidiEvent);

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
			handle->event_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->event_out = (LV2_Atom_Sequence *)data;
			break;
		case 2:
			handle->octave = (const float *)data;
			break;
		case 3:
			handle->channel = (const float *)data;
			break;
		case 4:
			handle->velocity = (const float *)data;
			break;
		case 5:
			handle->controller_id = (const float *)data;
			break;
		case 6:
			handle->controller_val = (const float *)data;
			break;
		case 7:
			handle->program_change = (const float *)data;
			break;
		case 8:
			handle->channel_pressure = (const float *)data;
			break;
		case 9:
			handle->bender = (const float *)data;
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

	const uint8_t offset = floor(*handle->octave) * 12;
	const uint8_t channel = floor(*handle->channel);
	const uint8_t velocity = floor(*handle->velocity);
	const uint8_t controller_id = floor(*handle->controller_id);
	const uint8_t controller_val = floor(*handle->controller_val);
	const uint8_t program_change = floor(*handle->program_change);
	const uint8_t channel_pressure = floor(*handle->channel_pressure);
	const uint16_t bender = floor(*handle->bender);

	uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	lv2_atom_forge_sequence_head(forge, &frame, 0);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom *atom = &ev->body;

		if(atom->type == handle->uri.midi_event)
		{
			const uint8_t *midi = LV2_ATOM_BODY_CONST(atom);

			const uint8_t cmnd = midi[0] & 0xf0;
			const uint8_t sys = cmnd | channel;

			lv2_atom_forge_frame_time(forge, ev->time.frames);
			lv2_atom_forge_atom(forge, atom->size, handle->uri.midi_event);
			if( (cmnd == 0x90) || (cmnd == 0x80) )
			{
				const uint8_t offset_midi [3] = {
					[0] = sys,
					[1] = midi[1] + offset,
					[2] = velocity
				};
				lv2_atom_forge_raw(forge, offset_midi, atom->size);
			}
			else if(cmnd == 0xa0)
			{
				const uint8_t offset_midi [3] = {
					[0] = sys,
					[1] = midi[1] + offset,
					[2] = midi[2] 
				};
				lv2_atom_forge_raw(forge, offset_midi, atom->size);
			}
			else
			{
				lv2_atom_forge_raw(forge, midi, atom->size);
			}
			lv2_atom_forge_pad(forge, atom->size);
		}
	}

	if(  (handle->_controller_id != controller_id)
		|| (handle->_controller_val != controller_val) )
	{
		handle->_controller_id = controller_id;
		handle->_controller_val = controller_val;

		const uint8_t offset_midi [3] = {
			[0] = 0xb0 | channel,
			[1] = controller_id,
			[2] = controller_val
		};

		lv2_atom_forge_frame_time(forge, nsamples);
		lv2_atom_forge_atom(forge, 3, handle->uri.midi_event);
		lv2_atom_forge_raw(forge, offset_midi, 3);
		lv2_atom_forge_pad(forge, 3);
	}
	
	if(handle->_program_change != program_change)
	{
		handle->_program_change = program_change;

		const uint8_t offset_midi [2] = {
			[0] = 0xc0 | channel,
			[1] = program_change
		};

		lv2_atom_forge_frame_time(forge, nsamples);
		lv2_atom_forge_atom(forge, 2, handle->uri.midi_event);
		lv2_atom_forge_raw(forge, offset_midi, 2);
		lv2_atom_forge_pad(forge, 2);
	}
	
	if(handle->_channel_pressure != channel_pressure)
	{
		handle->_channel_pressure = channel_pressure;

		const uint8_t offset_midi [2] = {
			[0] = 0xd0 | channel,
			[1] = channel_pressure
		};

		lv2_atom_forge_frame_time(forge, nsamples);
		lv2_atom_forge_atom(forge, 2, handle->uri.midi_event);
		lv2_atom_forge_raw(forge, offset_midi, 2);
		lv2_atom_forge_pad(forge, 2);
	}

	if(handle->_bender != bender)
	{
		handle->_bender = bender;

		const uint8_t offset_midi [3] = {
			[0] = 0xe0 | channel,
			[1] = bender & 0x7f,
			[2] = bender >> 7
		};

		lv2_atom_forge_frame_time(forge, nsamples);
		lv2_atom_forge_atom(forge, 3, handle->uri.midi_event);
		lv2_atom_forge_raw(forge, offset_midi, 3);
		lv2_atom_forge_pad(forge, 3);
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

const LV2_Descriptor synthpod_keyboard = {
	.URI						= SYNTHPOD_KEYBOARD_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
