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

#include <smart_bitmask.h>

#define SMART_BITMASK_TYPE "Smart Bitmask"

#define SMART_BITMASK_CHANGED "changed"
#define SMART_BITMASK_MOUSE_IN "cat,in"
#define SMART_BITMASK_MOUSE_OUT "cat,out"
#define MAX_NBITS 64

typedef struct _smart_bitmask_t smart_bitmask_t;

struct _smart_bitmask_t {
	Evas_Object *theme;
	Evas_Object *self;

	int nbits;
	int64_t value;
	int disabled;
	Evas_Object *bits [MAX_NBITS];
};

static const Evas_Smart_Cb_Description _smart_callbacks [] = {
	{SMART_BITMASK_CHANGED, ""},
	{SMART_BITMASK_MOUSE_IN, ""},
	{SMART_BITMASK_MOUSE_OUT, ""},
	{NULL, NULL}
};

EVAS_SMART_SUBCLASS_NEW(SMART_BITMASK_TYPE, _smart_bitmask,
	Evas_Smart_Class, Evas_Smart_Class,
	evas_object_smart_clipped_class_get, _smart_callbacks);

static inline void
_smart_bitmask_value_flush(Evas_Object *o)
{
	smart_bitmask_t *priv = evas_object_smart_data_get(o);

	for(int i=0; i<priv->nbits; i++)
	{
		const int64_t mask = 1 << i;
		if(priv->value & mask)
			edje_object_signal_emit(priv->bits[i], "on", SMART_BITMASK_UI);
		else
			edje_object_signal_emit(priv->bits[i], "off", SMART_BITMASK_UI);
	}
}

static void
_mouse_in(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	evas_object_smart_callback_call(obj, SMART_BITMASK_MOUSE_IN, NULL);
}

static void
_mouse_out(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	evas_object_smart_callback_call(obj, SMART_BITMASK_MOUSE_OUT, NULL);
}

static void
_mouse_down(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_bitmask_t *priv = data;

	if(priv->disabled)
		return;

	int nbit = -1;
	for(int i=0; i<priv->nbits; i++)
		if(obj == priv->bits[i])
		{
			nbit = i;
			break;
		}

	if(nbit == -1)
		return;

	const int64_t mask = 1 << nbit;
	if(priv->value & mask)
		priv->value &= ~mask;
	else
		priv->value |= mask;
	
	_smart_bitmask_value_flush(priv->self);
	evas_object_smart_callback_call(priv->self, SMART_BITMASK_CHANGED, NULL);
}

static void
_mouse_wheel(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_bitmask_t *priv = data;

	if(priv->disabled)
		return;

	int nbit = -1;
	for(int i=0; i<priv->nbits; i++)
		if(obj == priv->bits[i])
		{
			nbit = i;
			break;
		}

	if(nbit == -1)
		return;

	const int64_t mask = 1 << nbit;
	if(priv->value & mask)
		priv->value &= ~mask;
	else
		priv->value |= mask;

	_smart_bitmask_value_flush(priv->self);
	evas_object_smart_callback_call(priv->self, SMART_BITMASK_CHANGED, NULL);
}

static void
_smart_bitmask_smart_init(Evas_Object *o)
{
	Evas *e = evas_object_evas_get(o);
	smart_bitmask_t *priv = evas_object_smart_data_get(o);

	for(int i=0; i<priv->nbits; i++)
	{
		Evas_Object *bit = edje_object_add(e);
		if(bit)
		{
			edje_object_file_set(bit, SYNTHPOD_DATA_DIR"/synthpod.edj",
				"/synthpod/smart_bitmask/node"); //TODO
			evas_object_size_hint_weight_set(bit, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(bit, EVAS_HINT_FILL, EVAS_HINT_FILL);

			evas_object_event_callback_add(bit, EVAS_CALLBACK_MOUSE_DOWN, _mouse_down, priv);
			evas_object_event_callback_add(bit, EVAS_CALLBACK_MOUSE_WHEEL, _mouse_wheel, priv);

			const unsigned colmax = 16 - 1;
			const unsigned rowmax = (priv->nbits - 1) / 16;
			const unsigned col = i % 16;
			const unsigned row = i / 16;
			edje_object_part_table_pack(priv->theme, "table", bit,
				colmax - col, rowmax - row, 1, 1);
			/*
			const unsigned col = i;
			const unsigned row = 0;
			edje_object_part_table_pack(priv->theme, "table", bit, priv->nbits - 1 - col, row, 1, 1);
			*/

			char label [16];
			snprintf(label, 16, "%"PRIu8, i + 1);
			edje_object_part_text_set(bit, "label", label);

			evas_object_show(bit);
		}

		priv->bits[i] = bit;
	}
}

static void
_smart_bitmask_smart_deinit(Evas_Object *o)
{
	smart_bitmask_t *priv = evas_object_smart_data_get(o);

	for(int i=0; i<priv->nbits; i++)
	{
		edje_object_part_table_unpack(priv->theme, "table", priv->bits[i]);
		evas_object_del(priv->bits[i]);
		priv->bits[i] = NULL;
	}
}

static void
_smart_bitmask_smart_add(Evas_Object *o)
{
	Evas *e = evas_object_evas_get(o);
	EVAS_SMART_DATA_ALLOC(o, smart_bitmask_t);

	_smart_bitmask_parent_sc->add(o);

	priv->self = o;
	priv->nbits = 0;
	priv->value = 0;
	priv->disabled = 0;
	for(unsigned i=0; i<MAX_NBITS; i++)
		priv->bits[i] = NULL;

	priv->theme = edje_object_add(e);
	edje_object_file_set(priv->theme, SYNTHPOD_DATA_DIR"/synthpod.edj",
		"/synthpod/smart_bitmask/theme"); //TODO
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_IN, _mouse_in, priv);
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_OUT, _mouse_out, priv);
	evas_object_show(priv->theme);
	evas_object_smart_member_add(priv->theme, o);

	_smart_bitmask_smart_init(o);
}

static void
_smart_bitmask_smart_del(Evas_Object *o)
{
	smart_bitmask_t *priv = evas_object_smart_data_get(o);

	_smart_bitmask_smart_deinit(o);
	_smart_bitmask_parent_sc->del(o);
}

static void
_smart_bitmask_smart_resize(Evas_Object *o, Evas_Coord w, Evas_Coord h)
{
	Evas_Coord ow, oh;

	evas_object_geometry_get(o, NULL, NULL, &ow, &oh);
	if( (ow == w) && (oh == h) )
		return;

	evas_object_smart_changed(o);
}

static void
_smart_bitmask_smart_calculate(Evas_Object *o)
{
	smart_bitmask_t *priv = evas_object_smart_data_get(o);
	Evas_Coord x, y, w, h;

	evas_object_geometry_get(o, &x, &y, &w, &h);

	evas_object_resize(priv->theme, w, h);
	evas_object_move(priv->theme, x, y);

	_smart_bitmask_value_flush(o);
}

static void
_smart_bitmask_smart_set_user(Evas_Smart_Class *sc)
{
	// function overloading 
	sc->add = _smart_bitmask_smart_add;
	sc->del = _smart_bitmask_smart_del;
	sc->resize = _smart_bitmask_smart_resize;
	sc->calculate = _smart_bitmask_smart_calculate;
}

Evas_Object *
smart_bitmask_add(Evas *e)
{
	return evas_object_smart_add(e, _smart_bitmask_smart_class_new());
}

void
smart_bitmask_value_set(Evas_Object *o, int64_t value)
{
	smart_bitmask_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return;
	
	priv->value = value;

	_smart_bitmask_value_flush(o);
}

int64_t
smart_bitmask_value_get(Evas_Object *o)
{
	smart_bitmask_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return 0;

	return priv->value;
}

void
smart_bitmask_bits_set(Evas_Object *o, int nbits)
{
	smart_bitmask_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return;

	if(priv->nbits == nbits)
		return;

	_smart_bitmask_smart_deinit(o);
	priv->nbits = nbits;
	_smart_bitmask_smart_init(o);
}

int
smart_bitmask_bits_get(Evas_Object *o)
{
	smart_bitmask_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return 0;

	return priv->nbits;
}

void
smart_bitmask_color_set(Evas_Object *o, int col)
{
	smart_bitmask_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return;
	
	char sig[7];
	sprintf(sig, "col,%02i", col);
	edje_object_signal_emit(priv->theme, sig, SMART_BITMASK_UI);
}

void
smart_bitmask_disabled_set(Evas_Object *o, int disabled)
{
	smart_bitmask_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return;

	priv->disabled = disabled;

	if(priv->disabled)
		edje_object_signal_emit(priv->theme, "disabled", SMART_BITMASK_UI);
	else
		edje_object_signal_emit(priv->theme, "enabled", SMART_BITMASK_UI);
}
