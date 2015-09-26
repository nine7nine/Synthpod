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

#include <smart_spinner.h>

#define SMART_SPINNER_TYPE "Smart Slider"

#define SMART_SPINNER_CHANGED "changed"
#define SMART_SPINNER_MOUSE_IN "cat,in"
#define SMART_SPINNER_MOUSE_OUT "cat,out"

#define SMART_SPINNER_MODIFIER "Control" //TODO make configurable

typedef struct _elmnt_t elmnt_t;
typedef struct _smart_spinner_t smart_spinner_t;

struct _elmnt_t {
	float value;
	char *label;
};

struct _smart_spinner_t {
	Evas_Object *theme;

	Eina_List *elmnts;
	int value;
	int count;

	float drag;
	int disabled;
	int grabbed;
};

static const Evas_Smart_Cb_Description _smart_callbacks [] = {
	{SMART_SPINNER_CHANGED, ""},
	{SMART_SPINNER_MOUSE_IN, ""},
	{SMART_SPINNER_MOUSE_OUT, ""},
	{NULL, NULL}
};

EVAS_SMART_SUBCLASS_NEW(SMART_SPINNER_TYPE, _smart_spinner,
	Evas_Smart_Class, Evas_Smart_Class,
	evas_object_smart_clipped_class_get, _smart_callbacks);

static void
_smart_spinner_smart_init(Evas_Object *o)
{
	smart_spinner_t *priv = evas_object_smart_data_get(o);

	priv->elmnts = NULL;
	priv->value = 0;
	priv->count = 0;

	priv->drag = 0.f;
	priv->disabled = 0;
	priv->grabbed = 0;
	
	edje_object_part_drag_size_set(priv->theme, "drag", 1.f, 1.f);
	edje_object_part_drag_value_set(priv->theme, "drag", 1.f, 0.f);
}

static void
_smart_spinner_smart_deinit(Evas_Object *o)
{
	smart_spinner_t *priv = evas_object_smart_data_get(o);

	elmnt_t *elmnt;
	EINA_LIST_FREE(priv->elmnts, elmnt)
	{
		if(elmnt->label)
			free(elmnt->label);
		free(elmnt);
	}
}

static inline int
_smart_spinner_value_flush(Evas_Object *o)
{
	smart_spinner_t *priv = evas_object_smart_data_get(o);

	// calculate exact value
	float new_value = round(priv->drag * (priv->count - 1));

	double drag_x = new_value / (priv->count - 1);

	// has value changed?
	if(new_value != priv->value)
	{
		priv->value = new_value;

		elmnt_t *elmnt = eina_list_nth(priv->elmnts, priv->value);
		if(elmnt)
		{
			edje_object_part_drag_value_set(priv->theme, "drag", drag_x, 0.f);
			edje_object_part_text_set(priv->theme, "label", elmnt->label);
		}

		return 1;
	}

	return 0;
}

static void
_mouse_in(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	evas_object_smart_callback_call(obj, SMART_SPINNER_MOUSE_IN, NULL);
}

static void
_mouse_out(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	evas_object_smart_callback_call(obj, SMART_SPINNER_MOUSE_OUT, NULL);
}

static void
_mouse_down(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_spinner_t *priv = data;

	if(priv->disabled)
		return;
	
	priv->grabbed = 1;
}

static void
_mouse_up(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_spinner_t *priv = data;

	if(priv->disabled)
		return;

	if(priv->grabbed)
		priv->grabbed = 0;
}

static void
_mouse_move(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_spinner_t *priv = data;
	Evas_Event_Mouse_Move *ev = event_info;

	if(priv->disabled)
		return;

	if(priv->grabbed)
	{
		float dx = ev->cur.output.x - ev->prev.output.x;
		float dy = ev->cur.output.y - ev->prev.output.y;

		float scale = evas_key_modifier_is_set(ev->modifiers, SMART_SPINNER_MODIFIER)
			? 0.0001 // 10000 steps
			: 0.001; // 1000 steps

		float rel = (fabs(dx) > fabs(dy) ? dx : dy) * scale;

		priv->drag += rel;
		if(priv->drag > 1.f)
			priv->drag = 1.f;
		else if(priv->drag < 0.f)
			priv->drag = 0.f;
	
		if(_smart_spinner_value_flush(obj))
			evas_object_smart_callback_call(obj, SMART_SPINNER_CHANGED, NULL);
	}
}

static void
_mouse_wheel(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	smart_spinner_t *priv = data;
	Evas_Event_Mouse_Wheel *ev = event_info;

	if(priv->disabled)
		return;

	float scale = 1.f / (priv->count - 1);

	float rel = scale * ev->z;

	priv->drag += rel;
	if(priv->drag > 1.f)
		priv->drag = 1.f;
	else if(priv->drag < 0.f)
		priv->drag = 0.f;

	if(_smart_spinner_value_flush(obj))
		evas_object_smart_callback_call(obj, SMART_SPINNER_CHANGED, NULL);
}

static void
_smart_spinner_smart_add(Evas_Object *o)
{
	Evas *e = evas_object_evas_get(o);
	EVAS_SMART_DATA_ALLOC(o, smart_spinner_t);

	_smart_spinner_parent_sc->add(o);

	priv->theme = edje_object_add(e);
	edje_object_file_set(priv->theme, SYNTHPOD_DATA_DIR"/synthpod.edj",
		"/synthpod/smart_spinner/theme"); //TODO
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_IN, _mouse_in, priv);
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_OUT, _mouse_out, priv);
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_DOWN, _mouse_down, priv);
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_UP, _mouse_up, priv);
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_MOVE, _mouse_move, priv);
	evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_WHEEL, _mouse_wheel, priv);
	evas_object_show(priv->theme);
	evas_object_smart_member_add(priv->theme, o);

	_smart_spinner_smart_init(o);
}

static void
_smart_spinner_smart_del(Evas_Object *o)
{
	_smart_spinner_smart_deinit(o);
	_smart_spinner_parent_sc->del(o);
}

static void
_smart_spinner_smart_resize(Evas_Object *o, Evas_Coord w, Evas_Coord h)
{
	Evas_Coord ow, oh;

	evas_object_geometry_get(o, NULL, NULL, &ow, &oh);
	if( (ow == w) && (oh == h) )
		return;

	evas_object_smart_changed(o);
}

static void
_smart_spinner_smart_calculate(Evas_Object *o)
{
	smart_spinner_t *priv = evas_object_smart_data_get(o);
	Evas_Coord x, y, w, h;

	evas_object_geometry_get(o, &x, &y, &w, &h);

	evas_object_resize(priv->theme, w, h);
	evas_object_move(priv->theme, x, y);

	_smart_spinner_value_flush(o);
}

static void
_smart_spinner_smart_set_user(Evas_Smart_Class *sc)
{
	// function overloading 
	sc->add = _smart_spinner_smart_add;
	sc->del = _smart_spinner_smart_del;
	sc->resize = _smart_spinner_smart_resize;
	sc->calculate = _smart_spinner_smart_calculate;
}

Evas_Object *
smart_spinner_add(Evas *e)
{
	return evas_object_smart_add(e, _smart_spinner_smart_class_new());
}

static int
_cmp(const void *data1, const void *data2)
{
	const elmnt_t *elmnt1 = data1;
	const elmnt_t *elmnt2 = data2;

	return elmnt1->value < elmnt2->value
		? -1
		: (elmnt1->value > elmnt2->value
			? 1
			: 0);
}

void
smart_spinner_value_add(Evas_Object *o, float value, const char *label)
{
	smart_spinner_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return;

	elmnt_t *elmnt = calloc(1, sizeof(elmnt_t));
	elmnt->value = value;
	elmnt->label = label ? strdup(label) : NULL;

	priv->elmnts = eina_list_sorted_insert(priv->elmnts, _cmp, elmnt);
	priv->count = eina_list_count(priv->elmnts);
			
	double size_x = 1.f / priv->count;
	edje_object_part_drag_size_set(priv->theme, "drag", size_x, 1.f);
	
	//_smart_spinner_value_flush(o);
}

void
smart_spinner_value_set(Evas_Object *o, float value)
{
	smart_spinner_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return;
	
	Eina_List *l;
	elmnt_t *elmnt;
	
	float d = INFINITY;
	int itr = 0;

	EINA_LIST_FOREACH(priv->elmnts, l, elmnt)
	{
		float dd = fabs(value - elmnt->value);

		if(dd <= d)
		{
			d = dd;
			itr += 1;

			continue;
		}

		break;
	}

	itr -= 1;

	priv->drag = 1.f / (priv->count - 1) * itr;
	priv->value = INT_MAX;

	_smart_spinner_value_flush(o);
}

float
smart_spinner_value_get(Evas_Object *o)
{
	smart_spinner_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return 0.f;

	elmnt_t *elmnt = eina_list_nth(priv->elmnts, priv->value);

	return elmnt
		? elmnt->value
		: 0.f;
}

void
smart_spinner_color_set(Evas_Object *o, int col)
{
	smart_spinner_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return;
	
	char sig[7];
	sprintf(sig, "col,%02i", col);
	edje_object_signal_emit(priv->theme, sig, SMART_SPINNER_UI);
}

void
smart_spinner_disabled_set(Evas_Object *o, int disabled)
{
	smart_spinner_t *priv = evas_object_smart_data_get(o);
	if(!priv)
		return;

	priv->disabled = disabled;

	if(priv->disabled)
		edje_object_signal_emit(priv->theme, "disabled", SMART_SPINNER_UI);
	else
		edje_object_signal_emit(priv->theme, "enabled", SMART_SPINNER_UI);
}
