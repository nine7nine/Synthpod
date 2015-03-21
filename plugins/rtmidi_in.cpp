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

#include <cstdio>
#include <cstdlib>

#include <RtMidi.h>

extern "C" {
#include <rtmidi.h>
}

#define BUF_SIZE 2048

typedef struct _handle_t handle_t;

struct _handle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID midi_MidiEvent;
	} uris;
	
	volatile int working;
	LV2_Worker_Schedule *sched;
	
	RtMidiIn *io;

	LV2_Atom_Sequence *midi_in;
	LV2_Atom_Forge forge;
	
	uint8_t buf [BUF_SIZE];
	LV2_Atom_Forge work_forge;
};

// non-rt thread
static LV2_Worker_Status
_work(LV2_Handle instance,
	LV2_Worker_Respond_Function respond,
	LV2_Worker_Respond_Handle target,
	uint32_t size,
	const void *body)
{
	handle_t *handle = (handle_t *)instance;

	//printf("_work: %u %p\n", size, body);

	uint8_t m [BUF_SIZE];
	std::vector<unsigned char> msg;

  try
	{
		while(1)
		{
			double delta = handle->io->getMessage(&msg);

			if(msg.size() > 0)
			{
				uint32_t n = msg.size();
				for(int i=0; i<n; i++)
					m[i] = msg[i];
				msg.clear();
				respond(target, n, m);
			}
			else
				break; // no more messages
		}
	}
	catch(RtMidiError &error)
	{
		error.printMessage();
	}

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	handle_t *handle = (handle_t *)instance;

	//printf("_work_response: %u %p\n", size, body);

	LV2_Atom_Forge *forge = &handle->work_forge;
	lv2_atom_forge_frame_time(forge, 0); //TODO use delta time
	lv2_atom_forge_atom(forge, size, handle->uris.midi_MidiEvent);
	lv2_atom_forge_raw(forge, body, size);
	lv2_atom_forge_pad(forge, size);

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_end_run(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	//printf("_end_run\n");
	
	handle->working = 0;

	return LV2_WORKER_SUCCESS;
}

static const LV2_Worker_Interface work_ext = {
	.work = _work,
	.work_response = _work_response,
	.end_run = _end_run
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	int i;
	handle_t *handle = (handle_t *)calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	for(i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->sched = (LV2_Worker_Schedule *)features[i]->data;

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	
	if(!handle->sched)
	{
		fprintf(stderr, "%s: Host does not support worker:schedule\n",
			descriptor->URI);
		free(handle);
		return NULL;
	}
	
	handle->uris.midi_MidiEvent = handle->map->map(handle->map->handle,
		LV2_MIDI__MidiEvent);
	
	//RtMidi::Api api = RtMidi::LINUX_ALSA;
	RtMidi::Api api = RtMidi::UNIX_JACK;
  try
	{
		handle->io = new RtMidiIn(api, "LV2 RtMidi");
	}
	catch(RtMidiError &error)
	{
		error.printMessage();
	}

	lv2_atom_forge_init(&handle->forge, handle->map);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	handle_t *handle = (handle_t *)instance;

	switch(port)
	{
		case 0:
			handle->midi_in = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;
	
	lv2_atom_forge_set_buffer(&handle->work_forge, handle->buf, BUF_SIZE);

	try
	{
		handle->io->openVirtualPort("midi_in");
	}
	catch(RtMidiError &error)
	{
		error.printMessage();
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;
	
	// prepare midi atom forge
	const uint32_t capacity = handle->midi_in->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->midi_in, capacity);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_sequence_head(forge, &frame, 0);
		
	if(!handle->working)
	{
		if(handle->work_forge.offset > 0)
		{
			// copy forge buffer
			lv2_atom_forge_raw(forge, handle->buf, handle->work_forge.offset);

			// reset forge buffer
			lv2_atom_forge_set_buffer(&handle->work_forge, handle->buf, BUF_SIZE);
		}
	
		// schedule new work
		uint32_t dummy = 0;
		if(handle->sched->schedule_work(handle->sched->handle,
			sizeof(uint32_t), &dummy) == LV2_WORKER_SUCCESS)
		{
			handle->working = 1;
		}
	}

	// end sequence
	lv2_atom_forge_pop(forge, &frame);
}

static void
deactivate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	try
	{
		handle->io->closePort();
	}
	catch(RtMidiError &error)
	{
		error.printMessage();
	}
}

static void
cleanup(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;
  
	try
	{
		delete handle->io;
	}
	catch(RtMidiError &error)
	{
		error.printMessage();
	}

	free(handle);
}

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_WORKER__interface))
		return &work_ext;
	else
		return NULL;
}

extern "C" {
const LV2_Descriptor rtmidi_in = {
	.URI						= RTMIDI_IN_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
}
