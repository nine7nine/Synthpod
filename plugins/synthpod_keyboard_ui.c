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

#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"

#include <Elementary.h>

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	struct {
		LV2_URID event_transfer;
		LV2_URID midi_event;
	} uri;

	LV2_URID_Map *map;

	LV2UI_Write_Function write_function;
	LV2UI_Controller controller;

	int w, h;
	Ecore_Evas *ee;
	Evas_Object *bg;
	Evas_Object *parent;
};

// Idle interface
static int
idle_cb(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	ecore_main_loop_iterate();
	
	return 0;
}

static const LV2UI_Idle_Interface idle_ext = {
	.idle = idle_cb
};

// Show Interface
static int
_show_cb(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle && handle->ee)
		ecore_evas_show(handle->ee);

	return 0;
}

static int
_hide_cb(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

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
	plughandle_t *handle = instance;

	if(!handle)
		return -1;

	handle->w = w;
	handle->h = h;

	if(handle->ee)
	{
		ecore_evas_resize(handle->ee, handle->w, handle->h);
		//evas_object_resize(handle->parent, handle->w, handle->h);
		evas_object_resize(handle->bg, handle->w, handle->h);
	}
  
  return 0;
}

static void
_delete(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	//plughandle_t *handle = data;

	// nothing
}

const int octave [12] = {
	0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0
};

const uint8_t keys [25] = {
	24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
	36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
	48
};

typedef struct _midi_atom_t midi_atom_t;

struct _midi_atom_t {
	LV2_Atom atom;
	uint8_t midi [3];
};

static void
_mouse_down(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	plughandle_t *handle = data;
	Evas_Event_Mouse_Down *ev = event_info;
	uint8_t *key = evas_object_data_get(obj, "key");

	midi_atom_t midi_atom = {
		.atom.size = 3,
		.atom.type = handle->uri.midi_event,
		.midi[0] = 0x90,
		.midi[1] = *key,
		.midi[2] = 0x7f
	};

	int r, g, b, a;
	evas_object_color_get(obj, &r, &g, &b, &a);
	if(r)
		evas_object_color_set(obj, 0x3f, 0x3f, 0x3f, 0x3f);
	else
		evas_object_color_set(obj, 0x00, 0x00, 0x00, 0x3f);

	handle->write_function(handle->controller, 0, sizeof(LV2_Atom) + 3,
		handle->uri.event_transfer, &midi_atom);
}

static void
_mouse_up(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	plughandle_t *handle = data;
	Evas_Event_Mouse_Up *ev = event_info;
	uint8_t *key = evas_object_data_get(obj, "key");

	midi_atom_t midi_atom = {
		.atom.size = 3,
		.atom.type = handle->uri.midi_event,
		.midi[0] = 0x80,
		.midi[1] = *key,
		.midi[2] = 0x7f
	};
	
	int r, g, b, a;
	evas_object_color_get(obj, &r, &g, &b, &a);
	if(r)
		evas_object_color_set(obj, 0xff, 0xff, 0xff, 0xff);
	else
		evas_object_color_set(obj, 0x00, 0x00, 0x00, 0xff);

	handle->write_function(handle->controller, 0, sizeof(LV2_Atom) + 3,
		handle->uri.event_transfer, &midi_atom);
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor, const char *plugin_uri,
	const char *bundle_path, LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	if(strcmp(plugin_uri, SYNTHPOD_KEYBOARD_URI))
		return NULL;

	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	handle->w = 400;
	handle->h = 400;
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
			handle->map = (LV2_URID_Map *)features[i]->data;
  }

	if(!parent || !handle->map)
	{
		free(handle);
		return NULL;
	}

	handle->uri.event_transfer = handle->map->map(handle->map->handle,
		LV2_ATOM__eventTransfer);
	handle->uri.midi_event = handle->map->map(handle->map->handle,
		LV2_MIDI__MidiEvent);

	if(descriptor == &synthpod_keyboard_ui)
	{
		_elm_startup_time = ecore_time_unix_get();
		elm_init(0, NULL);

		Ecore_X_Window xwin = (Ecore_X_Window)parent;
		handle->ee = ecore_evas_gl_x11_new(NULL, xwin, 0, 0, handle->w, handle->h);
		if(!handle->ee)
			handle->ee = ecore_evas_software_x11_new(NULL, xwin, 0, 0, handle->w, handle->h);
		if(!handle->ee)
		{
			free(handle);
			return NULL;
		}
		ecore_evas_show(handle->ee);
	
#if defined(ELM_HAS_FAKE)
		handle->parent = elm_win_fake_add(handle->ee);
		evas_object_resize(handle->parent, handle->w, handle->h);
		evas_object_show(handle->parent);
#else
		Evas *e = ecore_evas_get(handle->ee);
		handle->parent = evas_object_rectangle_add(e);
#endif

		handle->bg = elm_bg_add(handle->parent);
		evas_object_resize(handle->bg, handle->w, handle->w);
		evas_object_show(handle->bg);
	}
	else if(descriptor == &synthpod_keyboard_eo)
	{
		handle->ee = NULL;
		handle->parent = (Evas_Object *)parent;
	}

	if(resize)
    resize->ui_resize(resize->handle, handle->w, handle->h);

	Evas_Object *widg = elm_table_add(handle->parent);
	elm_table_homogeneous_set(widg, EINA_TRUE);
	elm_table_padding_set(widg, 1, 1);
	evas_object_event_callback_add(widg, EVAS_CALLBACK_DEL, _delete, handle);
	evas_object_resize(widg, handle->w, handle->h);
	evas_object_show(widg);

	for(int i=0, pos=0; i<25; i++)
	{
		if(octave[i % 12] == 0)
		{
			Evas_Object *key = evas_object_rectangle_add(evas_object_evas_get(widg));
			evas_object_data_set(key, "key", &keys[i]);
			evas_object_event_callback_add(key, EVAS_CALLBACK_MOUSE_DOWN, _mouse_down, handle);
			evas_object_event_callback_add(key, EVAS_CALLBACK_MOUSE_UP, _mouse_up, handle);
			evas_object_size_hint_weight_set(key, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(key, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(key);

			evas_object_color_set(key, 0xff, 0xff, 0xff, 0xff);
			elm_table_pack(widg, key, pos, 0, 8, 2);

			pos += 8;
		}
	}

	for(int i=0, pos=0; i<25; i++)
	{
		if(octave[i % 12] == 0)
		{
			pos += 8;
		}
		else
		{
			Evas_Object *key = evas_object_rectangle_add(evas_object_evas_get(widg));
			evas_object_data_set(key, "key", &keys[i]);
			evas_object_event_callback_add(key, EVAS_CALLBACK_MOUSE_DOWN, _mouse_down, handle);
			evas_object_event_callback_add(key, EVAS_CALLBACK_MOUSE_UP, _mouse_up, handle);
			evas_object_size_hint_weight_set(key, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(key, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(key);

			evas_object_color_set(key, 0x00, 0x00, 0x00, 0xff);
			elm_table_pack(widg, key, pos-3, 0, 6, 1);
		}
	}

	if(handle->ee) // X11 UI
		*(Evas_Object **)widget = NULL;
	else // Eo UI
		*(Evas_Object **)widget = widg;

	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;
	
	if(handle)
	{
		if(handle->ee)
		{
			ecore_evas_hide(handle->ee);

			evas_object_del(handle->bg);
			evas_object_del(handle->parent);

			//ecore_evas_free(handle->ee);
			//elm_shutdown();
		}
		
		free(handle);
	}
}

static void
port_event(LV2UI_Handle instance, uint32_t port_index, uint32_t size,
	uint32_t format, const void *buffer)
{
	//plughandle_t *handle = instance;

	// nothing
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

const LV2UI_Descriptor synthpod_keyboard_ui = {
	.URI						= SYNTHPOD_KEYBOARD_UI_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= extension_data
};

const LV2UI_Descriptor synthpod_keyboard_eo = {
	.URI						= SYNTHPOD_KEYBOARD_EO_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= NULL
};
