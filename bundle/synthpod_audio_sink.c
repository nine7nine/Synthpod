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

#include <synthpod_bundle.h>
#include <system_port.h>

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	float *audio_in [4];
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
	plughandle_t *handle = instance;

	if(port < 4)
		handle->audio_in[port] = (float *)data;
}

static System_Port_Type
query(LV2_Handle instance, uint32_t port)
{
	if(port < 4)
		return SYSTEM_PORT_AUDIO;

	return SYSTEM_PORT_NONE;
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = instance;
	
	// nothing
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	// nothing
}

static void
deactivate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	// nothing
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	free(handle);
}

static const System_Port_Interface sys = {
	.query = query
};

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, SYSTEM_PORT__interface))
		return &sys;

	return NULL;
}

const LV2_Descriptor synthpod_audio_sink = {
	.URI						= SYNTHPOD_AUDIO_SINK_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
