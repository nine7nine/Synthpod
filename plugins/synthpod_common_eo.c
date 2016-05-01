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

#include <assert.h>

#include <synthpod_lv2.h>
#include <synthpod_ui.h>

#include <zero_writer.h>

#include <lv2/lv2plug.in/ns/ext/parameters/parameters.h>

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	const LilvWorld *world;
	sp_ui_t *ui;
	sp_ui_driver_t driver;

	struct {
		LV2_URID float_protocol;
		LV2_URID event_transfer;
	} uri;

	Evas_Object *widget;

	Zero_Writer_Schedule *zero_writer;
	LV2UI_Write_Function write_function;
	LV2UI_Controller controller;

	LV2UI_Port_Map *port_map;
	uint32_t control_port;
	uint32_t notify_port;

	struct {
		uint8_t app [CHUNK_SIZE] _ATOM_ALIGNED;
	} buf;
};

static void
_content_free(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	plughandle_t *handle = data;

	sp_ui_free(handle->ui);
}

static void
_content_del(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	plughandle_t *handle = data;

	sp_ui_del(handle->ui, false);
}

static void *
_to_app_request(size_t size, void *data)
{
	plughandle_t *handle = data;

	// use zero-writer if available
	if(handle->zero_writer)
	{
		return handle->zero_writer->request(handle->zero_writer->handle,
			handle->control_port, size, handle->uri.event_transfer);
	}

	return size <= CHUNK_SIZE
		? handle->buf.app
		: NULL;
}
static void
_to_app_advance(size_t size, void *data)
{
	plughandle_t *handle = data;

	// use zero writer if available
	if(handle->zero_writer)
	{
		handle->zero_writer->advance(handle->zero_writer->handle, size);
		return;
	}
	
	handle->write_function(handle->controller, handle->control_port,
		size, handle->uri.event_transfer, handle->buf.app);
}

static Evas_Object *
_content_get(plughandle_t *handle, Evas_Object *parent)
{
	handle->ui = sp_ui_new(parent, handle->world, &handle->driver, handle, 0);
	if(!handle->ui)
		return NULL;

	sp_ui_refresh(handle->ui); // get everything from app

	Evas_Object *widg = sp_ui_widget_get(handle->ui);
	evas_object_event_callback_add(widg, EVAS_CALLBACK_FREE, _content_free, handle);
	evas_object_event_callback_add(widg, EVAS_CALLBACK_DEL, _content_del, handle);
	evas_object_size_hint_min_set(widg, 1280, 720);

	return widg;
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor, const char *plugin_uri,
	const char *bundle_path, LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	if(  strcmp(plugin_uri, SYNTHPOD_STEREO_URI)
		&& strcmp(plugin_uri, SYNTHPOD_MONOATOM_URI) )
	{
		return NULL;
	}

	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	handle->write_function = write_function;
	handle->controller = controller;
	
	handle->world = NULL;

	Evas_Object *parent = NULL;
	LV2_Options_Option *opts;
	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->driver.map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->driver.unmap = features[i]->data;
		else if(!strcmp(features[i]->URI, XPRESS_VOICE_MAP))
			handle->driver.xmap = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_INSTANCE_ACCESS_URI))
			handle->world = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_UI__portMap))
			handle->port_map = features[i]->data;
		else if(!strcmp(features[i]->URI, ZERO_WRITER__schedule))
			handle->zero_writer = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->driver.log = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_UI__parent))
			parent = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_OPTIONS__options))
			opts = features[i]->data;
  }

	if(!handle->driver.xmap)
		handle->driver.xmap = &voice_map_fallback;

	if(!handle->driver.map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->driver.unmap)
	{
		fprintf(stderr, "%s: Host does not support urid:unmap\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->port_map)
	{
		fprintf(stderr, "%s: Host does not support ui:portMap\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!parent)
	{
		fprintf(stderr, "%s: Host does not support ui:parent\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(handle->zero_writer)
	{
		fprintf(stderr, "%s: Host supports zero-writer:schedule\n", descriptor->URI);
	}

	LV2_URID atom_float = handle->driver.map->map(handle->driver.map->handle,
		LV2_ATOM__Float);
	LV2_URID params_sample_rate = handle->driver.map->map(handle->driver.map->handle,
		LV2_PARAMETERS__sampleRate);

	handle->driver.sample_rate = 44100; // fall-back

	if(opts)
	{
		for(LV2_Options_Option *opt = opts;
			(opt->key != 0) && (opt->value != NULL);
			opt++)
		{
			if( (opt->key == params_sample_rate) && (opt->type == atom_float) )
				handle->driver.sample_rate = *(float*)opt->value;
			//TODO handle more options
		}
	}

	handle->driver.features = SP_UI_FEATURE_NEW
		| SP_UI_FEATURE_IMPORT_FROM | SP_UI_FEATURE_EXPORT_TO;
	
	// query port indeces of "control" and "notify" ports
	handle->control_port = handle->port_map->port_index(handle->port_map->handle, "control");
	handle->notify_port = handle->port_map->port_index(handle->port_map->handle, "notify");

	handle->uri.float_protocol = handle->driver.map->map(handle->driver.map->handle,
		LV2_UI_PREFIX"floatProtocol");
	handle->uri.event_transfer = handle->driver.map->map(handle->driver.map->handle,
		LV2_ATOM__eventTransfer);

	handle->driver.to_app_request = _to_app_request;
	handle->driver.to_app_advance = _to_app_advance;
	handle->driver.opened = NULL; //TODO
	handle->driver.saved = NULL; //TODO
	handle->driver.close = NULL; //TODO

	handle->widget = _content_get(handle, parent);
	if(!handle->widget)
	{
		free(handle);
		return NULL;
	}
	*(Evas_Object **)widget = handle->widget;

	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle->widget)
		evas_object_del(handle->widget);
	free(handle);
}

static void
port_event(LV2UI_Handle instance, uint32_t port_index, uint32_t size,
	uint32_t format, const void *buffer)
{
	plughandle_t *handle = instance;
	
	if(  ( port_index == handle->notify_port)
		&& (format == handle->uri.event_transfer) )
	{
		const LV2_Atom *atom = buffer;
		assert(size == sizeof(LV2_Atom) + atom->size);
		sp_ui_from_app(handle->ui, atom);
	}
}

const LV2UI_Descriptor synthpod_common_3_eo = {
	.URI						= SYNTHPOD_COMMON_EO_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= NULL
};
