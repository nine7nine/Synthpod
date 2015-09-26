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

#include <stdlib.h>

#include <synthpod_bundle.h>

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	int dummy;
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
	// nothing
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	// nothing
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	free(handle);
}

static const LV2_Descriptor synthpod_audio_sink = {
	.URI						= SYNTHPOD_AUDIO_SINK_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};

static const LV2_Descriptor synthpod_audio_source = {
	.URI						= SYNTHPOD_AUDIO_SOURCE_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};

static const LV2_Descriptor synthpod_cv_sink = {
	.URI						= SYNTHPOD_CV_SINK_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};

static const LV2_Descriptor synthpod_cv_source = {
	.URI						= SYNTHPOD_CV_SOURCE_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};

static const LV2_Descriptor synthpod_midi_sink = {
	.URI						= SYNTHPOD_MIDI_SINK_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};

static const LV2_Descriptor synthpod_midi_source = {
	.URI						= SYNTHPOD_MIDI_SOURCE_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};

static const LV2_Descriptor synthpod_osc_sink = {
	.URI						= SYNTHPOD_OSC_SINK_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};

static const LV2_Descriptor synthpod_osc_source = {
	.URI						= SYNTHPOD_OSC_SOURCE_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};

static const LV2_Descriptor synthpod_sink = {
	.URI						= SYNTHPOD_SINK_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};

static const LV2_Descriptor synthpod_source = {
	.URI						= SYNTHPOD_SOURCE_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch(index)
	{
		case 0:
			return &synthpod_source;
		case 1:
			return &synthpod_sink;

		case 2:
			return &synthpod_osc_source;
		case 3:
			return &synthpod_osc_sink;

		case 4:
			return &synthpod_cv_source;
		case 5:
			return &synthpod_cv_sink;

		case 6:
			return &synthpod_audio_source;
		case 7:
			return &synthpod_audio_sink;

		case 8:
			return &synthpod_midi_source;
		case 9:
			return &synthpod_midi_sink;

		default:
			return NULL;
	}
}
