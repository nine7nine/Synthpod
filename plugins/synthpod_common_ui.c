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

#include <assert.h>

#include <synthpod_lv2.h>
#include <synthpod_ui.h>

#include <lv2_eo_ui.h>
#include <zero_writer.h>

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	eo_ui_t eoui;

	const LilvWorld *world;
	sp_ui_t *ui;
	sp_ui_driver_t driver;

	struct {
		LV2_URID float_protocol;
		LV2_URID event_transfer;
	} uri;

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
_content_get(eo_ui_t *eoui)
{
	plughandle_t *handle = (void *)eoui - offsetof(plughandle_t, eoui);
	
	handle->ui = sp_ui_new(eoui->win, handle->world, &handle->driver, handle, 0);
	if(!handle->ui)
		return NULL;

	sp_ui_refresh(handle->ui); // get everything from app

	Evas_Object *widg = sp_ui_widget_get(handle->ui);
	evas_object_event_callback_add(widg, EVAS_CALLBACK_FREE, _content_free, handle);
	evas_object_event_callback_add(widg, EVAS_CALLBACK_DEL, _content_del, handle);

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

	eo_ui_driver_t driver;
	if(descriptor == &synthpod_common_eo)
		driver = EO_UI_DRIVER_EO;
	else if(descriptor == &synthpod_common_ui)
		driver = EO_UI_DRIVER_UI;
	else if(descriptor == &synthpod_common_x11)
		driver = EO_UI_DRIVER_X11;
	else if(descriptor == &synthpod_common_kx)
		driver = EO_UI_DRIVER_KX;
	else
		return NULL;

	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	eo_ui_t *eoui = &handle->eoui;
	eoui->driver = driver;
	eoui->content_get = _content_get;
	eoui->w = 1280,
	eoui->h = 720;

	handle->write_function = write_function;
	handle->controller = controller;
	
	handle->world = NULL;

	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->driver.map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->driver.unmap = (LV2_URID_Unmap *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_INSTANCE_ACCESS_URI))
			handle->driver.instance_access = features[i]->data != NULL;
		else if(!strcmp(features[i]->URI, SYNTHPOD_PREFIX"world"))
			handle->world = (const LilvWorld *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_UI__portMap))
			handle->port_map = (LV2UI_Port_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, ZERO_WRITER__schedule))
			handle->zero_writer = (Zero_Writer_Schedule *)features[i]->data;
  }

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
	if(handle->zero_writer)
	{
		fprintf(stderr, "%s: Host supports zero-writer:schedule\n", descriptor->URI);
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

	if(eoui_instantiate(eoui, descriptor, plugin_uri, bundle_path, write_function,
		controller, widget, features))
	{
		free(handle);
		return NULL;
	}

	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	eoui_cleanup(&handle->eoui);
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

const LV2UI_Descriptor synthpod_common_eo = {
	.URI						= SYNTHPOD_COMMON_EO_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_eo_extension_data
};

const LV2UI_Descriptor synthpod_common_ui = {
	.URI						= SYNTHPOD_COMMON_UI_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_ui_extension_data
};

const LV2UI_Descriptor synthpod_common_x11 = {
	.URI						= SYNTHPOD_COMMON_X11_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_x11_extension_data
};

const LV2UI_Descriptor synthpod_common_kx = {
	.URI						= SYNTHPOD_COMMON_KX_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_kx_extension_data
};
