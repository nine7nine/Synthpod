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

#include <patcher.h>

#define PATCHER_UI "/synthpod/patcher/ui"
#define PATCHER_TYPE "Matrix Patcher"
#define PATCHER_CONNECT "connect"
#define PATCHER_DISCONNECT "disconnect"

typedef struct _patcher_t patcher_t;
typedef struct _event_t event_t;

struct _patcher_t {
	Evas_Object *matrix;
	int **state;
	struct {
		void **source;
		void **sink;
	} data;
	int sources;
	int sinks;
};

struct _event_t {
	void *source;
	void *sink;
};

static const Evas_Smart_Cb_Description _smart_callbacks [] = {
	{PATCHER_CONNECT, "(ii)"},
	{PATCHER_DISCONNECT, "(ii)"},
	{NULL, NULL}
};

EVAS_SMART_SUBCLASS_NEW(PATCHER_TYPE, _patcher,
	Evas_Smart_Class, Evas_Smart_Class,
	evas_object_smart_clipped_class_get, _smart_callbacks);

static void
_node_in(void *data, Evas_Object *edj, const char *emission, const char *source)
{
	//TODO
}

static void
_node_out(void *data, Evas_Object *edj, const char *emission, const char *source)
{
	//TODO
}

static void
_node_toggled(void *data, Evas_Object *edj, const char *emission, const char *source)
{
	Evas_Object *o = data;
	patcher_t *priv = evas_object_smart_data_get(o);
	int max = priv->sinks > priv->sources ? priv->sinks : priv->sources;
	unsigned short src, snk;

	evas_object_table_pack_get(priv->matrix, edj, &src, &snk, NULL, NULL);

	event_t ev = {
		.source = priv->data.source[src + priv->sources - max],
		.sink = priv->data.sink[snk + priv->sinks - max]
	};
	
	if(priv->state[src][snk]) // is on
	{
		evas_object_smart_callback_call(o, PATCHER_DISCONNECT, (void *)&ev);
		edje_object_signal_emit(edj, "off", PATCHER_UI);
	}
	else // is off
	{
		evas_object_smart_callback_call(o, PATCHER_CONNECT, (void *)&ev);
		edje_object_signal_emit(edj, "on", PATCHER_UI);
	}

	priv->state[src][snk] ^= 1; // toggle state
}

static void
_patcher_smart_init(Evas_Object *o)
{
	Evas *e = evas_object_evas_get(o);
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Object *elmnt;

	if( !(priv->sinks && priv->sources) )
		return;

	priv->data.source = calloc(priv->sources, sizeof(void *));
	priv->data.sink = calloc(priv->sinks, sizeof(void *));

	// create state
	priv->state = calloc(priv->sources, sizeof(int *));
	for(int src=0; src<priv->sources; src++)
		priv->state[src] = calloc(priv->sinks, sizeof(int));

	int max = priv->sinks > priv->sources ? priv->sinks : priv->sources;

	for(int src=max-priv->sources; src<=max; src++)
	{
		for(int snk=max-priv->sinks; snk<=max; snk++)
		{
			if( (src == max) && (snk == max) )
				continue;

			elmnt = edje_object_add(e);
			if( (src == max) || (snk == max) ) // is port
			{
				edje_object_file_set(elmnt, "/usr/local/share/synthpod/synthpod.edj",
					"/synthpod/patcher/port"); //TODO
			}
			else // is node
			{
				edje_object_file_set(elmnt, "/usr/local/share/synthpod/synthpod.edj",
					"/synthpod/patcher/node"); //TODO
				edje_object_signal_callback_add(elmnt, "in", PATCHER_UI, _node_in, o);
				edje_object_signal_callback_add(elmnt, "out", PATCHER_UI, _node_out, o);
				edje_object_signal_callback_add(elmnt, "toggled", PATCHER_UI, _node_toggled, o);
			}
			evas_object_size_hint_weight_set(elmnt, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(elmnt, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(elmnt);
			evas_object_table_pack(priv->matrix, elmnt, src, snk, 1, 1);
		}
	}
	
	// source label
	elmnt = edje_object_add(e);
	edje_object_file_set(elmnt, "/usr/local/share/synthpod/synthpod.edj",
		"/synthpod/patcher/label/horizontal"); //TODO
	evas_object_size_hint_weight_set(elmnt, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(elmnt, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(elmnt);
	evas_object_table_pack(priv->matrix, elmnt, 0, max+1, max, 1);

	// sink label
	elmnt = edje_object_add(e);
	edje_object_file_set(elmnt, "/usr/local/share/synthpod/synthpod.edj",
		"/synthpod/patcher/label/vertical"); //TODO
	evas_object_size_hint_weight_set(elmnt, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(elmnt, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(elmnt);
	evas_object_table_pack(priv->matrix, elmnt, max+1, 0, 1, max);
}

static void
_patcher_smart_deinit(Evas_Object *o)
{
	patcher_t *priv = evas_object_smart_data_get(o);

	if(priv->data.source)
		free(priv->data.source);
	if(priv->data.sink)
		free(priv->data.sink);

	// free state
	if(priv->state)
	{
		for(int src=0; src<priv->sources; src++)
		{
			if(priv->state[src])
				free(priv->state[src]);
		}
		free(priv->state);
	}

	priv->data.source = NULL;
	priv->data.sink = NULL;
	priv->state = NULL;

	evas_object_table_clear(priv->matrix, EINA_TRUE);
}

static void
_patcher_smart_add(Evas_Object *o)
{
	Evas *e = evas_object_evas_get(o);
	EVAS_SMART_DATA_ALLOC(o, patcher_t);

	_patcher_parent_sc->add(o);

	priv->matrix = evas_object_table_add(e);
	evas_object_table_homogeneous_set(priv->matrix, EINA_TRUE);
	evas_object_table_padding_set(priv->matrix, 0, 0);
	evas_object_show(priv->matrix);
	evas_object_smart_member_add(priv->matrix, o);

	priv->state = NULL;
	priv->data.source = NULL;
	priv->data.sink = NULL;
	priv->sources = 0;
	priv->sinks = 0;

	_patcher_smart_init(o);
}

static void
_patcher_smart_del(Evas_Object *o)
{
	patcher_t *priv = evas_object_smart_data_get(o);

	_patcher_smart_deinit(o);

	_patcher_parent_sc->del(o);
}

static void
_patcher_smart_resize(Evas_Object *o, Evas_Coord w, Evas_Coord h)
{
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Coord ow, oh;

	evas_object_geometry_get(o, NULL, NULL, &ow, &oh);
	if( (ow == w) && (oh == h) )
		return;

	evas_object_smart_changed(o);
}

static void
_patcher_smart_calculate(Evas_Object *o)
{
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Coord x, y, w, h;

	int max = priv->sinks > priv->sources ? priv->sinks : priv->sources;
	evas_object_geometry_get(o, &x, &y, &w, &h);
	float dw = (float)w / (max + 2); // + port number + axis label
	float dh = (float)h / (max + 2);
	if(dw < dh)
		h = dw * (max + 2);
	else // dw >= dh
		w = dh * (max + 2);
	evas_object_resize(priv->matrix, w, h);
	evas_object_move(priv->matrix, x, y);
}

static void
_patcher_smart_set_user(Evas_Smart_Class *sc)
{
	// function overloading 
	sc->add = _patcher_smart_add;
	sc->del = _patcher_smart_del;
	sc->resize = _patcher_smart_resize;
	sc->calculate = _patcher_smart_calculate;
}

Evas_Object *
patcher_object_add(Evas *e)
{
	return evas_object_smart_add(e, _patcher_smart_class_new());
}

void
patcher_object_dimension_set(Evas_Object *o, int sources, int sinks)
{
	patcher_t *priv = evas_object_smart_data_get(o);

	if( (priv->sources == sources) && (priv->sinks == sinks) )
		return;

	_patcher_smart_deinit(o);

	priv->sources = sources;
	priv->sinks = sinks;

	_patcher_smart_init(o);
}

void
patcher_object_dimension_get(Evas_Object *o, int *sources, int *sinks)
{
	patcher_t *priv = evas_object_smart_data_get(o);

	if(sources)
		*sources = priv->sources;
	if(sinks)
		*sinks = priv->sinks;
}

void
patcher_object_state_set(Evas_Object *o, int source, int sink, Eina_Bool state)
{
	patcher_t *priv = evas_object_smart_data_get(o);

	priv->state[source][sink] = state;
	Evas_Object *child = evas_object_table_child_get(priv->matrix, source, sink);
	edje_object_signal_emit(child, state ? "on" : "off", PATCHER_UI);
}

void
patcher_object_source_data_set(Evas_Object *o, int source, void *data)
{
	patcher_t *priv = evas_object_smart_data_get(o);

	if(priv->data.source)
		priv->data.source[source] = data;
}

void
patcher_object_sink_data_set(Evas_Object *o, int sink, void *data)
{
	patcher_t *priv = evas_object_smart_data_get(o);

	if(priv->data.sink)
		priv->data.sink[sink] = data;
}
