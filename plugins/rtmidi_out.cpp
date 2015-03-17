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

typedef struct _handle_t handle_t;

struct _handle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID midi_MidiEvent;
	} uris;
	
	LV2_Worker_Schedule *sched;
	
	RtMidiOut *io;

	const LV2_Atom_Sequence *midi_in;
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

	const uint8_t *m = (const uint8_t *)body;
	
	std::vector<unsigned char> msg(size);
	for(int i=0; i<size; i++)
		msg[i] = m[i];

  try
	{
		handle->io->sendMessage(&msg);
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

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_end_run(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	//printf("_end_run\n");

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
		handle->io = new RtMidiOut(api, "LV2 RtMidi");
	}
	catch(RtMidiError &error)
	{
		error.printMessage();
	}

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	handle_t *handle = (handle_t *)instance;

	switch(port)
	{
		case 0:
			handle->midi_in = (const LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	try
	{
		handle->io->openVirtualPort("midi_out");
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
	
	const LV2_Atom_Event *ev = NULL;
	LV2_ATOM_SEQUENCE_FOREACH(handle->midi_in, ev)
	{
		const LV2_Atom *atom = &ev->body;
		if(atom->type == handle->uris.midi_MidiEvent)
		{
			int64_t frames = ev->time.frames;
			size_t len = ev->body.size;

			if(handle->sched->schedule_work(handle->sched->handle,
				atom->size, LV2_ATOM_BODY_CONST(atom)) == LV2_WORKER_SUCCESS)
			{
				//TODO
			}
		}
	}
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
const LV2_Descriptor rtmidi_out = {
	.URI						= RTMIDI_OUT_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
}
