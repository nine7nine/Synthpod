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

#include <lv2_eo_ui.h>

typedef struct _midi_atom_t midi_atom_t;
typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	eo_ui_t eoui;

	struct {
		LV2_URID event_transfer;
		LV2_URID midi_event;
	} uri;

	LV2_URID_Map *map;
	LV2UI_Write_Function write_function;
	LV2UI_Controller controller;

	uint8_t *key;
	Evas_Object *obj;
};

struct _midi_atom_t {
	LV2_Atom atom;
	uint8_t midi [3];
};

static const int octave [12] = {
	0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0
};

static const uint8_t keys [25] = {
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11,
	12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
	24
};

static void
_note_on(plughandle_t *handle)
{
	if(!handle->obj || !handle->key)
		return;

	int r, g, b, a;
	evas_object_color_get(handle->obj, &r, &g, &b, &a);
	if(r == 0xff)
		evas_object_color_set(handle->obj, 0x7f, 0x7f, 0x7f, 0xff);
	else
		evas_object_color_set(handle->obj, 0x80, 0x80, 0x80, 0xff);

	const midi_atom_t midi_atom = {
		.atom = {
			.size = 3,
			.type = handle->uri.midi_event
		},
		.midi[0] = 0x90,
		.midi[1] = *handle->key,
		.midi[2] = 0x7f
	};

	handle->write_function(handle->controller, 0, sizeof(LV2_Atom) + 3,
		handle->uri.event_transfer, &midi_atom);
}

static void
_note_off(plughandle_t *handle)
{
	if(!handle->obj || !handle->key)
		return;

	int r, g, b, a;
	evas_object_color_get(handle->obj, &r, &g, &b, &a);
	if(r == 0x7f)
		evas_object_color_set(handle->obj, 0xff, 0xff, 0xff, 0xff);
	else
		evas_object_color_set(handle->obj, 0x00, 0x00, 0x00, 0xff);

	const midi_atom_t midi_atom = {
		.atom = {
			.size = 3,
			.type = handle->uri.midi_event
		},
		.midi[0] = 0x80,
		.midi[1] = *handle->key,
		.midi[2] = 0x7f
	};

	handle->write_function(handle->controller, 0, sizeof(LV2_Atom) + 3,
		handle->uri.event_transfer, &midi_atom);
}

static void
_note_pressure(plughandle_t *handle, int Y)
{
	if(!handle->obj || !handle->key)
		return;

	int x, y, w, h;
	evas_object_geometry_get(handle->obj, &x, &y, &w, &h);
	const uint8_t touch = (Y - y) * 0x7f / h;

	const midi_atom_t midi_atom = {
		.atom = {
			.size = 3,
			.type = handle->uri.midi_event
		},
		.midi[0] = 0xa0,
		.midi[1] = *handle->key,
		.midi[2] = touch
	};

	handle->write_function(handle->controller, 0, sizeof(LV2_Atom) + 3,
		handle->uri.event_transfer, &midi_atom);
}

static void
_mouse_down(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	plughandle_t *handle = data;
	Evas_Event_Mouse_Down *ev = event_info;

	Evas_Coord y = ev->canvas.y;

	handle->key = evas_object_data_get(obj, "key");
	handle->obj = obj;
	_note_on(handle);
	_note_pressure(handle, y);
}

static void
_mouse_up(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	plughandle_t *handle = data;
	//Evas_Event_Mouse_Up *ev = event_info;

	_note_off(handle);
	handle->key = NULL;
	handle->obj = NULL;
}

static void
_mouse_move(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	plughandle_t *handle = data;
	Evas_Event_Mouse_Move *ev = event_info;

	if(ev->buttons == 0)
		return;

	Evas_Coord x = ev->cur.canvas.x;
	Evas_Coord y = ev->cur.canvas.y;

	Eina_List *objs = evas_tree_objects_at_xy_get(e, NULL, x, y);
	Eina_List *l;
	Evas_Object *itm;
	EINA_LIST_FOREACH(objs, l, itm)
	{
		uint8_t *key = evas_object_data_get(itm, "key");
		if(!key)
			continue;

		if(key != handle->key && itm != handle->obj)
		{
			_note_off(handle);
			handle->key = key;
			handle->obj = itm;
			_note_on(handle);
		}
		else
		{
			_note_pressure(handle, y);
		}

		break;
	}
	eina_list_free(objs);
}

static Evas_Object *
_content_get(eo_ui_t *eoui)
{
	plughandle_t *handle = (void *)eoui - offsetof(plughandle_t, eoui);

	Evas_Object *widg = elm_table_add(eoui->win);
	elm_table_homogeneous_set(widg, EINA_TRUE);
	elm_table_padding_set(widg, 1, 1);
	evas_object_pointer_mode_set(widg, EVAS_OBJECT_POINTER_MODE_NOGRAB);

	// preserve aspect
	evas_object_size_hint_aspect_set(widg, EVAS_ASPECT_CONTROL_BOTH,
		eoui->w, eoui->h);

	for(int i=0, pos=0; i<25; i++)
	{
		if(octave[i % 12] == 0)
		{
			Evas_Object *key = evas_object_rectangle_add(evas_object_evas_get(widg));
			evas_object_data_set(key, "key", &keys[i]);
			evas_object_event_callback_add(key, EVAS_CALLBACK_MOUSE_DOWN, _mouse_down, handle);
			evas_object_event_callback_add(key, EVAS_CALLBACK_MOUSE_UP, _mouse_up, handle);
			evas_object_event_callback_add(key, EVAS_CALLBACK_MOUSE_MOVE, _mouse_move, handle);
			evas_object_size_hint_weight_set(key, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(key, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_pointer_mode_set(key, EVAS_OBJECT_POINTER_MODE_NOGRAB);
			evas_object_show(key);

			evas_object_color_set(key, 0xff, 0xff, 0xff, 0xff);
			elm_table_pack(widg, key, pos, 0, 8, 5);

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
			evas_object_event_callback_add(key, EVAS_CALLBACK_MOUSE_MOVE, _mouse_move, handle);
			evas_object_size_hint_weight_set(key, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(key, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_pointer_mode_set(key, EVAS_OBJECT_POINTER_MODE_NOGRAB);
			evas_object_show(key);

			evas_object_color_set(key, 0x00, 0x00, 0x00, 0xff);
			elm_table_pack(widg, key, pos-3, 0, 6, 3);
		}
	}

	return widg;
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor, const char *plugin_uri,
	const char *bundle_path, LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	if(strcmp(plugin_uri, SYNTHPOD_KEYBOARD_URI))
		return NULL;

	eo_ui_driver_t driver;
	if(descriptor == &synthpod_keyboard_eo)
		driver = EO_UI_DRIVER_EO;
	else if(descriptor == &synthpod_keyboard_ui)
		driver = EO_UI_DRIVER_UI;
	else if(descriptor == &synthpod_keyboard_x11)
		driver = EO_UI_DRIVER_X11;
	else if(descriptor == &synthpod_keyboard_kx)
		driver = EO_UI_DRIVER_KX;
	else
		return NULL;

	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	eo_ui_t *eoui = &handle->eoui;
	eoui->driver = driver;
	eoui->content_get = _content_get;
	eoui->w = 400,
	eoui->h = 100;

	handle->write_function = write_function;
	handle->controller = controller;

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

	handle->uri.event_transfer = handle->map->map(handle->map->handle,
		LV2_ATOM__eventTransfer);
	handle->uri.midi_event = handle->map->map(handle->map->handle,
		LV2_MIDI__MidiEvent);

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
	if(!handle)
		return;

	eoui_cleanup(&handle->eoui);
	free(handle);
}

static void
port_event(LV2UI_Handle instance, uint32_t port_index, uint32_t size,
	uint32_t format, const void *buffer)
{
	//plughandle_t *handle = instance;

	// nothing
}

const LV2UI_Descriptor synthpod_keyboard_eo = {
	.URI						= SYNTHPOD_KEYBOARD_EO_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_eo_extension_data
};

const LV2UI_Descriptor synthpod_keyboard_ui = {
	.URI						= SYNTHPOD_KEYBOARD_UI_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_ui_extension_data
};

const LV2UI_Descriptor synthpod_keyboard_x11 = {
	.URI						= SYNTHPOD_KEYBOARD_X11_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_x11_extension_data
};

const LV2UI_Descriptor synthpod_keyboard_kx = {
	.URI						= SYNTHPOD_KEYBOARD_KX_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_kx_extension_data
};
