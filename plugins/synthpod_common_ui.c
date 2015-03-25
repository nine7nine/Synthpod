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

#include <Elementary.h>

typedef struct _handle_t handle_t;

struct _handle_t {
	sp_ui_t *ui;
	sp_ui_driver_t driver;

	struct {
		LV2_URID float_protocol;
		LV2_URID event_transfer;
	} uri;

	LV2UI_Write_Function write_function;
	LV2UI_Controller controller;

	int w, h;
	Ecore_Evas *ee;
	Evas *e;
	Evas_Object *parent;
};

// Idle interface
static int
idle_cb(LV2UI_Handle instance)
{
	handle_t *handle = instance;

	sp_ui_iterate(handle->ui);
	
	return 0;
}

static const LV2UI_Idle_Interface idle_ext = {
	.idle = idle_cb
};

// Show Interface
static int
_show_cb(LV2UI_Handle instance)
{
	handle_t *handle = instance;

	if(handle && handle->ee)
		ecore_evas_show(handle->ee);

	return 0;
}

static int
_hide_cb(LV2UI_Handle instance)
{
	handle_t *handle = instance;

	if(handle && handle->ee)
		ecore_evas_hide(handle->ee);

	return 0;
}

static const LV2UI_Show_Interface show_ext = {
	.show = _show_cb,
	.hide = _hide_cb
};

// Resize Interface
static int
resize_cb(LV2UI_Feature_Handle instance, int w, int h)
{
	handle_t *handle = instance;

	if(!handle)
		return -1;

	handle->w = w;
	handle->h = h;

	if(handle->ee)
	{
		ecore_evas_resize(handle->ee, handle->w, handle->h);
		evas_object_resize(handle->parent, handle->w, handle->h);
	}

	sp_ui_resize(handle->ui, handle->w, handle->h);
  
  return 0;
}

static void
_delete(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	handle_t *handle = data;

	sp_ui_free(handle->ui);
}

static int
_to_app_cb(LV2_Atom *atom, void *data)
{
	handle_t *handle = data;

	uint32_t port_index = 1; // control port
	handle->write_function(handle->controller, port_index,
		sizeof(LV2_Atom) + atom->size, handle->uri.event_transfer, atom);

	return 0;
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor, const char *plugin_uri,
	const char *bundle_path, LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	elm_init(1, (char **)&plugin_uri);

	if(strcmp(plugin_uri, SYNTHPOD_STEREO_URI))
		return NULL;

	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	handle->w = 800;
	handle->h = 450;
	handle->write_function = write_function;
	handle->controller = controller;

	void *parent = NULL;
	LV2UI_Resize *resize = NULL;
	
	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_UI__parent))
			parent = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_UI__resize))
			resize = (LV2UI_Resize *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->driver.map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->driver.unmap = (LV2_URID_Unmap *)features[i]->data;
  }

	//TODO check for parent/map/unmap feature

	handle->uri.float_protocol = handle->driver.map->map(handle->driver.map->handle,
		LV2_UI_PREFIX"floatProtocol");
	handle->uri.event_transfer = handle->driver.map->map(handle->driver.map->handle,
		LV2_ATOM__eventTransfer);

	if(descriptor == &synthpod_common_ui)
	{
		handle->ee = ecore_evas_gl_x11_new(NULL, (Ecore_X_Window)parent, 0, 0, handle->w, handle->h);
		if(!handle->ee)
			handle->ee = ecore_evas_software_x11_new(NULL, (Ecore_X_Window)parent, 0, 0, handle->w, handle->h);
		if(!handle->ee)
			printf("could not start evas\n");
		handle->e = ecore_evas_get(handle->ee);
		ecore_evas_show(handle->ee);
		
		handle->parent = evas_object_rectangle_add(handle->e);
		evas_object_color_set(handle->parent, 48, 48, 48, 255);
		evas_object_resize(handle->parent, handle->w, handle->h);
		evas_object_show(handle->parent);
	}
	else if(descriptor == &synthpod_common_eo)
	{
		handle->ee = NULL;
		handle->parent = (Evas_Object *)parent;
		handle->e = evas_object_evas_get((Evas_Object *)parent);
	}

	if(resize)
    resize->ui_resize(resize->handle, handle->w, handle->h);

	handle->driver.to_app_cb = _to_app_cb;
	handle->ui = sp_ui_new(handle->parent, &handle->driver, handle);
	//TODO check handle->ui
	
	if(handle->ee) // X11 UI
		*(Evas_Object **)widget = NULL;
	else // Eo UI
		*(Evas_Object **)widget = sp_ui_widget_get(handle->ui);

	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	handle_t *handle = instance;
	
	if(handle)
	{
		if(handle->ee)
		{
			ecore_evas_hide(handle->ee);
			evas_object_del(handle->parent);
			ecore_evas_free(handle->ee);
		}
		
		free(handle);
	}

	elm_shutdown();
}

static void
port_event(LV2UI_Handle instance, uint32_t port_index, uint32_t size,
	uint32_t format, const void *buffer)
{
	handle_t *handle = instance;

	if(  ( port_index == 5) // notify port
		&& (format == handle->uri.event_transfer) )
	{
		const LV2_Atom *atom = buffer;
		assert(size == sizeof(LV2_Atom) + atom->size);
		sp_ui_from_app(handle->ui, atom, handle);
	}
	else
		; //TODO subscribe to audio/event ports?
}

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_UI__idleInterface))
		return &idle_ext;
	else if(!strcmp(uri, LV2_UI__showInterface))
		return &show_ext;
		
	return NULL;
}

const LV2UI_Descriptor synthpod_common_ui = {
	.URI						= SYNTHPOD_COMMON_UI_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= extension_data
};

const LV2UI_Descriptor synthpod_common_eo = {
	.URI						= SYNTHPOD_COMMON_EO_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= NULL
};
