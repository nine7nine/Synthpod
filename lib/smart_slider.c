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

#include <smart_slider.h>

#define SMART_SLIDER_TYPE "Smart Slider"

#define SMART_SLIDER_CHANGED "changed"

#define SMART_SLIDER_MODIFIER "Control" //TODO make configurable

typedef struct _smart_slider_t smart_slider_t;

struct _smart_slider_t {
	Evas_Object *theme;
	float min;
	float max;
	float dflt;
	float value;

	float drag;

	int integer;
	int disabled;
	char format [64];

	int grabbed;
};

static const Evas_Smart_Cb_Description _smart_callbacks [] = {
	{SMART_SLIDER_CHANGED, "f"},
	{NULL, NULL}
};

EVAS_SMART_SUBCLASS_NEW(SMART_SLIDER_TYPE, _smart_slider,
	Evas_Smart_Class, Evas_Smart_Class,
	evas_object_smart_clipped_class_get, _smart_callbacks);

static void
_smart_slider_smart_init(Evas_Object *o)
{
	Evas *e = evas_object_evas_get(o);
	smart_slider_t *priv = evas_object_smart_data_get(o);
	Evas_Object *elmnt;

	priv->min = 0.f;
	priv->max = 1.f;
	priv->dflt = 0.f;
	priv->value = priv->dflt;

	priv->drag = 0.f;

	priv->integer = 0;
	strncpy(priv->format, "%.4f", 32);

	priv->grabbed = 0;
	
	edje_object_part_drag_value_set(priv->theme, "drag", 0.f, 0.f);
}

static void
_smart_slider_smart_deinit(Evas_Object *o)
{
	smart_slider_t *priv = evas_object_smart_data_get(o);

	//TODO
}

static inline void
_smart_slider_value_flush(Evas_Object *o)
{
	smart_slider_t *priv = evas_object_smart_data_get(o);

	// calculate exact value
	float new_value = priv->min + (priv->drag * (priv->max - priv->min));

	double drag_x;
	if(priv->integer)
	{
		new_value = floor(new_value);
		drag_x = (new_value - priv->min) / (priv->max - priv->min);
	}
	else
		drag_x = priv->drag;

	// has value changed?
	if(new_value != priv->value)
	{
		priv->value = new_value;

		edje_object_part_drag_size_set(priv->theme, "drag", drag_x, 1.f);

		char label [64];
		sprintf(label, priv->format, priv->value);
		edje_object_part_text_set(priv->theme, "label", label);

		evas_object_smart_callback_call(o, SMART_SLIDER_CHANGED, NULL);
	}
}

static void
_mouse_down(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_slider_t *priv = data;
	Evas_Event_Mouse_Down *ev = event_info;

	if(priv->disabled)
		return;
	
	priv->grabbed = 1;
}

static void
_mouse_up(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_slider_t *priv = data;
	Evas_Event_Mouse_Up *ev = event_info;

	if(priv->disabled)
		return;

	if(priv->grabbed)
		priv->grabbed = 0;
}

static void
_mouse_move(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_slider_t *priv = data;
	Evas_Event_Mouse_Move *ev = event_info;

	if(priv->disabled)
		return;

	if(priv->grabbed)
	{
		float dx = ev->cur.output.x - ev->prev.output.x;
		float dy = ev->cur.output.y - ev->prev.output.y;

		float scale;
		if(priv->integer)
		{
			scale = 1.f / (priv->max - priv->min);
			scale *= evas_key_modifier_is_set(ev->modifiers, SMART_SLIDER_MODIFIER)
				? 0.01 // 100 steps
				: 0.1; // 10 steps
		}
		else
		{
			scale = evas_key_modifier_is_set(ev->modifiers, SMART_SLIDER_MODIFIER)
				? 0.0001 // 10000 steps
				: 0.001; // 1000 steps
		}

		float rel = (fabs(dx) > fabs(dy) ? dx : dy) * scale;

		priv->drag += rel;
		if(priv->drag > 1.f)
			priv->drag = 1.f;
		else if(priv->drag < 0.f)
			priv->drag = 0.f;
	
		_smart_slider_value_flush(obj);
	}
}

static void
_mouse_wheel(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_slider_t *priv = data;
	Evas_Event_Mouse_Wheel *ev = event_info;

	if(priv->disabled)
		return;

	float scale;
	if(priv->integer)
	{
		scale = 1.f / (priv->max - priv->min);
	}
	else
	{
		scale = evas_key_modifier_is_set(ev->modifiers, SMART_SLIDER_MODIFIER)
			? 0.001 // 1000 steps
			: 0.01; // 100 steps
	}

	float rel = scale * ev->z;

	priv->drag += rel;
	if(priv->drag > 1.f)
		priv->drag = 1.f;
	else if(priv->drag < 0.f)
		priv->drag = 0.f;

	_smart_slider_value_flush(obj);
}

static void
_smart_slider_smart_add(Evas_Object *o)
{
	Evas *e = evas_object_evas_get(o);
	EVAS_SMART_DATA_ALLOC(o, smart_slider_t);

	_smart_slider_parent_sc->add(o);

	priv->theme = edje_object_add(e);
	edje_object_file_set(priv->theme, "/usr/local/share/synthpod/synthpod.edj",
		"/synthpod/smart_slider/theme"); //TODO
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_DOWN, _mouse_down, priv);
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_UP, _mouse_up, priv);
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_MOVE, _mouse_move, priv);
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_WHEEL, _mouse_wheel, priv);
	evas_object_show(priv->theme);
	evas_object_smart_member_add(priv->theme, o);

	_smart_slider_smart_init(o);
}

static void
_smart_slider_smart_del(Evas_Object *o)
{
	smart_slider_t *priv = evas_object_smart_data_get(o);

	_smart_slider_smart_deinit(o);
	_smart_slider_parent_sc->del(o);
}

static void
_smart_slider_smart_resize(Evas_Object *o, Evas_Coord w, Evas_Coord h)
{
	smart_slider_t *priv = evas_object_smart_data_get(o);
	Evas_Coord ow, oh;

	evas_object_geometry_get(o, NULL, NULL, &ow, &oh);
	if( (ow == w) && (oh == h) )
		return;

	evas_object_smart_changed(o);
}

static void
_smart_slider_smart_calculate(Evas_Object *o)
{
	smart_slider_t *priv = evas_object_smart_data_get(o);
	Evas_Coord x, y, w, h;

	evas_object_geometry_get(o, &x, &y, &w, &h);

	evas_object_resize(priv->theme, w, h);
	evas_object_move(priv->theme, x, y);

	_smart_slider_value_flush(o);
}

static void
_smart_slider_smart_set_user(Evas_Smart_Class *sc)
{
	// function overloading 
	sc->add = _smart_slider_smart_add;
	sc->del = _smart_slider_smart_del;
	sc->resize = _smart_slider_smart_resize;
	sc->calculate = _smart_slider_smart_calculate;
}

Evas_Object *
smart_slider_add(Evas *e)
{
	return evas_object_smart_add(e, _smart_slider_smart_class_new());
}

void
smart_slider_range_set(Evas_Object *o, float min, float max, float dflt)
{
	smart_slider_t *priv = evas_object_smart_data_get(o);

	priv->min = min;
	priv->max = max;
	priv->dflt = dflt;
	
	_smart_slider_value_flush(o);
}

void
smart_slider_range_get(Evas_Object *o, float *min, float *max, float *dflt)
{
	smart_slider_t *priv = evas_object_smart_data_get(o);

	if(min)
		*min = priv->min;
	if(max)
		*max = priv->max;
	if(dflt)
		*dflt = priv->dflt;
}

void
smart_slider_value_set(Evas_Object *o, float value)
{
	smart_slider_t *priv = evas_object_smart_data_get(o);

	float new_value = value < priv->min
		? priv->min
		: (value > priv->max
			? priv->max
			: value);

	priv->value = INFINITY;
	priv->drag = (new_value - priv->min) / (priv->max - priv->min);

	_smart_slider_value_flush(o);
}

float
smart_slider_value_get(Evas_Object *o)
{
	smart_slider_t *priv = evas_object_smart_data_get(o);

	return priv->value;
}

void
smart_slider_format_set(Evas_Object *o, const char *format)
{
	smart_slider_t *priv = evas_object_smart_data_get(o);

	if(!format)
		return;

	strncpy(priv->format, format, 32);
	_smart_slider_value_flush(o);
}

void
smart_slider_color_set(Evas_Object *o, int col)
{
	smart_slider_t *priv = evas_object_smart_data_get(o);
	
	char sig[7];
	sprintf(sig, "col,%02i", col);
	edje_object_signal_emit(priv->theme, sig, SMART_SLIDER_UI);
}

void
smart_slider_integer_set(Evas_Object *o, int integer)
{
	smart_slider_t *priv = evas_object_smart_data_get(o);

	priv->integer = integer ? 1 : 0;
	
	_smart_slider_value_flush(o);
}

void
smart_slider_disabled_set(Evas_Object *o, int disabled)
{
	smart_slider_t *priv = evas_object_smart_data_get(o);

	priv->disabled = disabled;

	if(priv->disabled)
		edje_object_signal_emit(priv->theme, "disabled", SMART_SLIDER_UI);
	else
		edje_object_signal_emit(priv->theme, "enabled", SMART_SLIDER_UI);
}
