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

#include <Edje.h>

#include <smart_meter.h>

#define SMART_METER_TYPE "Smart Slider"

typedef struct _smart_meter_t smart_meter_t;

struct _smart_meter_t {
	Evas_Object *theme;
	float value;
};

static const Evas_Smart_Cb_Description _smart_callbacks [] = {
	{NULL, NULL}
};

EVAS_SMART_SUBCLASS_NEW(SMART_METER_TYPE, _smart_meter,
	Evas_Smart_Class, Evas_Smart_Class,
	evas_object_smart_clipped_class_get, _smart_callbacks);

static void
_smart_meter_smart_init(Evas_Object *o)
{
	smart_meter_t *priv = evas_object_smart_data_get(o);

	priv->value = 0.f;

	edje_object_part_drag_value_set(priv->theme, "drag", 0.f, 0.f);
}

static void
_smart_meter_smart_deinit(Evas_Object *o)
{
	//TODO
}

static inline void
_smart_meter_value_flush(Evas_Object *o)
{
	smart_meter_t *priv = evas_object_smart_data_get(o);

	// calculate exact value
	edje_object_part_drag_size_set(priv->theme, "drag", priv->value, 1.f);
}

static void
_smart_meter_smart_add(Evas_Object *o)
{
	Evas *e = evas_object_evas_get(o);
	EVAS_SMART_DATA_ALLOC(o, smart_meter_t);

	_smart_meter_parent_sc->add(o);

	priv->theme = edje_object_add(e);
	edje_object_file_set(priv->theme, SYNTHPOD_DATA_DIR"/synthpod.edj",
		"/synthpod/smart_meter/theme"); //TODO
	evas_object_show(priv->theme);
	evas_object_smart_member_add(priv->theme, o);

	_smart_meter_smart_init(o);
}

static void
_smart_meter_smart_del(Evas_Object *o)
{
	_smart_meter_smart_deinit(o);
	_smart_meter_parent_sc->del(o);
}

static void
_smart_meter_smart_resize(Evas_Object *o, Evas_Coord w, Evas_Coord h)
{
	Evas_Coord ow, oh;

	evas_object_geometry_get(o, NULL, NULL, &ow, &oh);
	if( (ow == w) && (oh == h) )
		return;

	evas_object_smart_changed(o);
}

static void
_smart_meter_smart_calculate(Evas_Object *o)
{
	smart_meter_t *priv = evas_object_smart_data_get(o);
	Evas_Coord x, y, w, h;

	evas_object_geometry_get(o, &x, &y, &w, &h);

	evas_object_resize(priv->theme, w, h);
	evas_object_move(priv->theme, x, y);

	_smart_meter_value_flush(o);
}

static void
_smart_meter_smart_set_user(Evas_Smart_Class *sc)
{
	// function overloading 
	sc->add = _smart_meter_smart_add;
	sc->del = _smart_meter_smart_del;
	sc->resize = _smart_meter_smart_resize;
	sc->calculate = _smart_meter_smart_calculate;
}

Evas_Object *
smart_meter_add(Evas *e)
{
	return evas_object_smart_add(e, _smart_meter_smart_class_new());
}

void
smart_meter_value_set(Evas_Object *o, float value)
{
	smart_meter_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return;

	float new_value = value < 0.f
		? 0.f
		: (value > 1.f
			? 1.f
			: value);

	if(new_value != priv->value)
	{
		priv->value = new_value;

		_smart_meter_value_flush(o);
	}
}

float
smart_meter_value_get(Evas_Object *o)
{
	smart_meter_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return 0.f;

	return priv->value;
}

void
smart_meter_color_set(Evas_Object *o, int col)
{
	smart_meter_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return;
	
	char sig[7];
	sprintf(sig, "col,%02i", col);
	edje_object_signal_emit(priv->theme, sig, SMART_METER_UI);
}
