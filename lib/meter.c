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

#include <meter.h>

#define METER_TYPE "Meter"

#define METER_CLIP "clip"

typedef struct _meter_t meter_t;

struct _meter_t {
	Evas_Object *column;
	float value;
};

static const Evas_Smart_Cb_Description _smart_callbacks [] = {
	{METER_CLIP, ""},
	{NULL, NULL}
};

EVAS_SMART_SUBCLASS_NEW(METER_TYPE, _meter,
	Evas_Smart_Class, Evas_Smart_Class,
	evas_object_smart_clipped_class_get, _smart_callbacks);

static void
_meter_smart_add(Evas_Object *o)
{
	Evas *e = evas_object_evas_get(o);
	EVAS_SMART_DATA_ALLOC(o, meter_t);

	_meter_parent_sc->add(o);

	priv->column = evas_object_rectangle_add(e);
	evas_object_show(priv->column);
	evas_object_smart_member_add(priv->column, o);

	priv->value = 0.f;
}

static void
_meter_smart_del(Evas_Object *o)
{
	meter_t *priv = evas_object_smart_data_get(o);

	_meter_parent_sc->del(o);
}

static void
_meter_smart_resize(Evas_Object *o, Evas_Coord w, Evas_Coord h)
{
	meter_t *priv = evas_object_smart_data_get(o);
	Evas_Coord ow, oh;

	evas_object_geometry_get(o, NULL, NULL, &ow, &oh);
	if( (ow == w) && (oh == h) )
		return;

	evas_object_smart_changed(o);
}

static void
_meter_smart_calculate(Evas_Object *o)
{
	meter_t *priv = evas_object_smart_data_get(o);
	Evas_Coord x, y, w, h;

	evas_object_geometry_get(o, &x, &y, &w, &h);
	evas_object_resize(priv->column, w, h);
	evas_object_move(priv->column, x, y);
}

static void
_meter_smart_set_user(Evas_Smart_Class *sc)
{
	// function overloading 
	sc->add = _meter_smart_add;
	sc->del = _meter_smart_del;
	sc->resize = _meter_smart_resize;
	sc->calculate = _meter_smart_calculate;
}

Evas_Object *
meter_object_add(Evas *e)
{
	return evas_object_smart_add(e, _meter_smart_class_new());
}

void
meter_object_color_set(Evas_Object *o, int col)
{
	meter_t *priv = evas_object_smart_data_get(o);
	//TODO
}

void
meter_object_peak_set(Evas_Object *o, LV2UI_Peak_Data *peak_data)
{
	meter_t *priv = evas_object_smart_data_get(o);
	//TODO
}
