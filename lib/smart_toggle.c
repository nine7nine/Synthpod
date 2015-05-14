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

#include <Edje.h>

#include <smart_toggle.h>

#define SMART_TOGGLE_TYPE "Smart Slider"

#define SMART_TOGGLE_CHANGED "changed"
#define SMART_TOGGLE_MOUSE_IN "mouse,in"
#define SMART_TOGGLE_MOUSE_OUT "mouse,out"

#define SMART_TOGGLE_MODIFIER "Control" //TODO make configurable

typedef struct _smart_toggle_t smart_toggle_t;

struct _smart_toggle_t {
	Evas_Object *theme;

	int value;
	int disabled;
};

static const Evas_Smart_Cb_Description _smart_callbacks [] = {
	{SMART_TOGGLE_CHANGED, NULL},
	{SMART_TOGGLE_MOUSE_IN, NULL},
	{SMART_TOGGLE_MOUSE_OUT, NULL},
	{NULL, NULL}
};

EVAS_SMART_SUBCLASS_NEW(SMART_TOGGLE_TYPE, _smart_toggle,
	Evas_Smart_Class, Evas_Smart_Class,
	evas_object_smart_clipped_class_get, _smart_callbacks);

static void
_smart_toggle_smart_init(Evas_Object *o)
{
	Evas *e = evas_object_evas_get(o);
	smart_toggle_t *priv = evas_object_smart_data_get(o);

	priv->value = 0;
	priv->disabled = 0;
	
	edje_object_part_drag_size_set(priv->theme, "drag", 0.f, 1.f);
	edje_object_part_drag_value_set(priv->theme, "drag", 0.f, 0.f);
}

static void
_smart_toggle_smart_deinit(Evas_Object *o)
{
	smart_toggle_t *priv = evas_object_smart_data_get(o);

	//TODO
}

static inline void
_smart_toggle_value_flush(Evas_Object *o)
{
	smart_toggle_t *priv = evas_object_smart_data_get(o);

	float size_x = priv->value;
	edje_object_part_drag_size_set(priv->theme, "drag", size_x, 1.f);

	evas_object_smart_callback_call(o, SMART_TOGGLE_CHANGED, NULL);
}

static void
_mouse_in(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_toggle_t *priv = data;

	evas_object_smart_callback_call(obj, SMART_TOGGLE_CHANGED, NULL);
}

static void
_mouse_out(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_toggle_t *priv = data;

	evas_object_smart_callback_call(obj, SMART_TOGGLE_CHANGED, NULL);
}

static void
_mouse_down(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_toggle_t *priv = data;
	Evas_Event_Mouse_Down *ev = event_info;

	if(priv->disabled)
		return;

	// toggle value
	priv->value ^= 1;
	
	_smart_toggle_value_flush(obj);
}

static void
_mouse_wheel(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_toggle_t *priv = data;
	Evas_Event_Mouse_Wheel *ev = event_info;

	if(priv->disabled)
		return;

	// toggle value
	priv->value ^= 1;

	_smart_toggle_value_flush(obj);
}

static void
_smart_toggle_smart_add(Evas_Object *o)
{
	Evas *e = evas_object_evas_get(o);
	EVAS_SMART_DATA_ALLOC(o, smart_toggle_t);

	_smart_toggle_parent_sc->add(o);

	priv->theme = edje_object_add(e);
	edje_object_file_set(priv->theme, "/usr/local/share/synthpod/synthpod.edj",
		"/synthpod/smart_toggle/theme"); //TODO
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_IN, _mouse_in, priv);
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_OUT, _mouse_out, priv);
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_DOWN, _mouse_down, priv);
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_WHEEL, _mouse_wheel, priv);
	evas_object_show(priv->theme);
	evas_object_smart_member_add(priv->theme, o);

	_smart_toggle_smart_init(o);
}

static void
_smart_toggle_smart_del(Evas_Object *o)
{
	smart_toggle_t *priv = evas_object_smart_data_get(o);

	_smart_toggle_smart_deinit(o);
	_smart_toggle_parent_sc->del(o);
}

static void
_smart_toggle_smart_resize(Evas_Object *o, Evas_Coord w, Evas_Coord h)
{
	smart_toggle_t *priv = evas_object_smart_data_get(o);
	Evas_Coord ow, oh;

	evas_object_geometry_get(o, NULL, NULL, &ow, &oh);
	if( (ow == w) && (oh == h) )
		return;

	evas_object_smart_changed(o);
}

static void
_smart_toggle_smart_calculate(Evas_Object *o)
{
	smart_toggle_t *priv = evas_object_smart_data_get(o);
	Evas_Coord x, y, w, h;

	evas_object_geometry_get(o, &x, &y, &w, &h);

	evas_object_resize(priv->theme, w, h);
	evas_object_move(priv->theme, x, y);

	_smart_toggle_value_flush(o);
}

static void
_smart_toggle_smart_set_user(Evas_Smart_Class *sc)
{
	// function overloading 
	sc->add = _smart_toggle_smart_add;
	sc->del = _smart_toggle_smart_del;
	sc->resize = _smart_toggle_smart_resize;
	sc->calculate = _smart_toggle_smart_calculate;
}

Evas_Object *
smart_toggle_add(Evas *e)
{
	return evas_object_smart_add(e, _smart_toggle_smart_class_new());
}

void
smart_toggle_value_set(Evas_Object *o, int value)
{
	smart_toggle_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return;
	
	priv->value = value ? 1 : 0;

	_smart_toggle_value_flush(o);
}

int
smart_toggle_value_get(Evas_Object *o)
{
	smart_toggle_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return 0;

	return priv->value;
}

void
smart_toggle_color_set(Evas_Object *o, int col)
{
	smart_toggle_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return;
	
	char sig[7];
	sprintf(sig, "col,%02i", col);
	edje_object_signal_emit(priv->theme, sig, SMART_TOGGLE_UI);
}

void
smart_toggle_disabled_set(Evas_Object *o, int disabled)
{
	smart_toggle_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return;

	priv->disabled = disabled;

	if(priv->disabled)
		edje_object_signal_emit(priv->theme, "disabled", SMART_TOGGLE_UI);
	else
		edje_object_signal_emit(priv->theme, "enabled", SMART_TOGGLE_UI);
}
