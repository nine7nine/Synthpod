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
#include <lv2/lv2plug.in/ns/ext/patch/patch.h>

#ifndef LV2_PATCH__Copy
#	define LV2_PATCH__Copy LV2_PATCH_PREFIX "Copy"
#endif

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	struct {
		LV2_URID patch_ack;
		LV2_URID patch_delete;
		LV2_URID patch_copy;
		LV2_URID patch_error;
		LV2_URID patch_get;
		LV2_URID patch_message;
		LV2_URID patch_move;
		LV2_URID patch_patch;
		LV2_URID patch_post;
		LV2_URID patch_put;
		LV2_URID patch_request;
		LV2_URID patch_response;
		LV2_URID patch_set;

		LV2_URID patch_subject;
		LV2_URID patch_add;
		LV2_URID patch_remove;
		LV2_URID patch_writable;
		LV2_URID patch_readable;
	} urid;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;

	LV2_Atom_Forge forge;
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;
	mlock(handle, sizeof(plughandle_t));

	LV2_URID_Map *map = NULL;

	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			map = (LV2_URID_Map *)features[i]->data;
  }

	if(!map)
	{
		free(handle);
		return NULL;
	}

	lv2_atom_forge_init(&handle->forge, map);

	handle->urid.patch_ack = map->map(map->handle, LV2_PATCH__Ack);
	handle->urid.patch_delete = map->map(map->handle, LV2_PATCH__Delete);
	handle->urid.patch_copy = map->map(map->handle, LV2_PATCH__Copy);
	handle->urid.patch_error = map->map(map->handle, LV2_PATCH__Error);
	handle->urid.patch_get = map->map(map->handle, LV2_PATCH__Get);
	handle->urid.patch_message = map->map(map->handle, LV2_PATCH__Message);
	handle->urid.patch_move = map->map(map->handle, LV2_PATCH__Move);
	handle->urid.patch_patch = map->map(map->handle, LV2_PATCH__Patch);
	handle->urid.patch_post = map->map(map->handle, LV2_PATCH__Post);
	handle->urid.patch_put = map->map(map->handle, LV2_PATCH__Put);
	handle->urid.patch_request = map->map(map->handle, LV2_PATCH__Request);
	handle->urid.patch_response = map->map(map->handle, LV2_PATCH__Response);
	handle->urid.patch_set = map->map(map->handle, LV2_PATCH__Set);

	handle->urid.patch_subject = map->map(map->handle, LV2_PATCH__subject);
	handle->urid.patch_add = map->map(map->handle, LV2_PATCH__add);
	handle->urid.patch_remove = map->map(map->handle, LV2_PATCH__remove);
	handle->urid.patch_writable = map->map(map->handle, LV2_PATCH__writable);
	handle->urid.patch_readable = map->map(map->handle, LV2_PATCH__readable);

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
		default:
			break;
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

		if(lv2_atom_forge_is_object_type(forge, obj->atom.type))
		{
			if(  (obj->body.otype == handle->urid.patch_ack)
				|| (obj->body.otype == handle->urid.patch_delete)
				|| (obj->body.otype == handle->urid.patch_copy)
				|| (obj->body.otype == handle->urid.patch_error)
				|| (obj->body.otype == handle->urid.patch_get)
				|| (obj->body.otype == handle->urid.patch_message)
				|| (obj->body.otype == handle->urid.patch_move)
				|| (obj->body.otype == handle->urid.patch_patch)
				|| (obj->body.otype == handle->urid.patch_post)
				|| (obj->body.otype == handle->urid.patch_put)
				|| (obj->body.otype == handle->urid.patch_request)
				|| (obj->body.otype == handle->urid.patch_response)
				|| (obj->body.otype == handle->urid.patch_set) )
			{
				LV2_Atom_Forge_Frame obj_frame;

				if(ref)
					ref = lv2_atom_forge_frame_time(forge, ev->time.frames);
				if(ref)
					ref = lv2_atom_forge_object(forge, &obj_frame, obj->body.id, obj->body.otype); // use id?

				LV2_ATOM_OBJECT_FOREACH(obj, prop)
				{
					if(prop->key == handle->urid.patch_subject)
					{
						if(obj->body.otype == handle->urid.patch_patch)
						{
							const LV2_Atom_Object *add = NULL;
							const LV2_Atom_Object *remove = NULL;

							LV2_Atom_Object_Query q[] = {
								{ handle->urid.patch_add, (const LV2_Atom **)&add },
								{ handle->urid.patch_remove, (const LV2_Atom **)&remove },
								{ 0, NULL }
							};
							lv2_atom_object_query(obj, q);

							if(add)
							{
								const LV2_Atom *writable = NULL;
								const LV2_Atom *readable = NULL;

								LV2_Atom_Object_Query q2[] = {
									{ handle->urid.patch_writable, &writable },
									{ handle->urid.patch_readable, &readable },
									{ 0, NULL }
								};
								lv2_atom_object_query(add, q2);

								if(writable || readable)
									continue; // strip object property
							}

							if(remove)
							{
								const LV2_Atom *writable = NULL;
								const LV2_Atom *readable = NULL;

								LV2_Atom_Object_Query q2[] = {
									{ handle->urid.patch_writable, &writable },
									{ handle->urid.patch_readable, &readable },
									{ 0, NULL }
								};
								lv2_atom_object_query(remove, q2);

								if(writable || readable)
									continue; // strip object property
							}
						}
						else
						{
							continue; // strip object property
						}
					}

					const uint32_t size = lv2_atom_total_size(&prop->value);

					if(ref)
						ref = lv2_atom_forge_key(forge, prop->key);
					if(ref)
						ref = lv2_atom_forge_raw(forge, &prop->value, size);
					if(ref)
						lv2_atom_forge_pad(forge, size);
				}

				if(ref)
					lv2_atom_forge_pop(forge, &obj_frame);	
			}
		}
	}

	if(ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	munlock(handle, sizeof(plughandle_t));
	free(handle);
}

const LV2_Descriptor synthpod_anonymizer = {
	.URI						= SYNTHPOD_ANONYMIZER_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};
