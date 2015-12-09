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

#define PATCHER_TYPE "Matrix Patcher"

#define PATCHER_CONNECT_REQUEST "connect,request"
#define PATCHER_DISCONNECT_REQUEST "disconnect,request"
#define PATCHER_REALIZE_REQUEST "realize,request"

#define LEN 12

typedef struct _patcher_t patcher_t;

struct _patcher_t {
	Evas_Object *matrix;
	Eina_Bool **state;
	struct {
		void **source;
		void **sink;
	} data;
	int sources;
	int sinks;
	int max;

	Evas_Object *src_over;
	Evas_Object *snk_over;
};

static const Evas_Smart_Cb_Description _smart_callbacks [] = {
	{PATCHER_CONNECT_REQUEST, "(ip)(ip)"},
	{PATCHER_DISCONNECT_REQUEST, "(ip)(ip)"},
	{PATCHER_REALIZE_REQUEST, "(ip)(ip)"},
	{NULL, NULL}
};

EVAS_SMART_SUBCLASS_NEW(PATCHER_TYPE, _patcher,
	Evas_Smart_Class, Evas_Smart_Class,
	evas_object_smart_clipped_class_get, _smart_callbacks);

static inline int 
_patcher_object_source_idx_get(Evas_Object *o, void *data)
{
	patcher_t *priv = evas_object_smart_data_get(o);

	for(int i=0; i<priv->sources; i++)
	{
		if(priv->data.source[i] == data)
			return i;
	}

	return -1;
}

static inline int 
_patcher_object_sink_idx_get(Evas_Object *o, void *data)
{
	patcher_t *priv = evas_object_smart_data_get(o);

	for(int i=0; i<priv->sinks; i++)
	{
		if(priv->data.sink[i] == data)
			return i;
	}

	return -1;
}

static inline void
_abs_to_rel(patcher_t *priv, int x_abs, int y_abs, int *x_rel, int *y_rel)
{
	if(x_rel)
		*x_rel = x_abs + priv->sources - priv->max;
	if(y_rel)
		*y_rel = y_abs + priv->sinks - priv->max;
}

static inline void
_rel_to_abs(patcher_t *priv, int x_rel, int y_rel, int *x_abs, int *y_abs)
{
	if(x_abs)
		*x_abs = x_rel - priv->sources + priv->max;
	if(y_abs)
		*y_abs = y_rel - priv->sinks + priv->max;
}

static inline Evas_Object *
_rel_child_get(patcher_t *priv, int src_idx, int snk_idx)
{
	int src_abs, snk_abs;
	_rel_to_abs(priv, src_idx, snk_idx, &src_abs, &snk_abs);

	return evas_object_table_child_get(priv->matrix, src_abs, snk_abs);
}

static void
_node_in(void *data, Evas_Object *edj, const char *emission, const char *source)
{
	Evas_Object *o = data;
	patcher_t *priv = evas_object_smart_data_get(o);
	unsigned short src, snk;
	evas_object_table_pack_get(priv->matrix, edj, &src, &snk, NULL, NULL);

	Evas_Object *tar = NULL;

	for(int j=snk+1; j<priv->max; j++)
	{
		tar = evas_object_table_child_get(priv->matrix, src, j);
		edje_object_signal_emit(tar, "vertical", PATCHER_UI);
	}
	for(int i=src+1; i<priv->max; i++)
	{
		tar = evas_object_table_child_get(priv->matrix, i, snk);
		edje_object_signal_emit(tar, "horizontal", PATCHER_UI);
	}

	tar = evas_object_table_child_get(priv->matrix, src, snk);
	edje_object_signal_emit(tar, "edge", PATCHER_UI);

	tar = evas_object_table_child_get(priv->matrix, src, priv->max);
	edje_object_signal_emit(tar, "on", PATCHER_UI);

	tar = evas_object_table_child_get(priv->matrix, priv->max, snk);
	edje_object_signal_emit(tar, "on", PATCHER_UI);

	tar = evas_object_table_child_get(priv->matrix, src, priv->max+1);
	edje_object_signal_emit(tar, "on", PATCHER_UI);

	tar = evas_object_table_child_get(priv->matrix, priv->max+1, snk);
	edje_object_signal_emit(tar, "on", PATCHER_UI);
}

static void
_node_out(void *data, Evas_Object *edj, const char *emission, const char *source)
{
	Evas_Object *o = data;
	patcher_t *priv = evas_object_smart_data_get(o);
	unsigned short src, snk;
	evas_object_table_pack_get(priv->matrix, edj, &src, &snk, NULL, NULL);

	Evas_Object *tar = NULL;

	for(int j=snk+1; j<priv->max; j++)
	{
		tar = evas_object_table_child_get(priv->matrix, src, j);
		edje_object_signal_emit(tar, "clear", PATCHER_UI);
	}
	for(int i=src+1; i<priv->max; i++)
	{
		tar = evas_object_table_child_get(priv->matrix, i, snk);
		edje_object_signal_emit(tar, "clear", PATCHER_UI);
	}

	tar = evas_object_table_child_get(priv->matrix, src, snk);
	edje_object_signal_emit(tar, "clear", PATCHER_UI);

	tar = evas_object_table_child_get(priv->matrix, src, priv->max);
	edje_object_signal_emit(tar, "off", PATCHER_UI);

	tar = evas_object_table_child_get(priv->matrix, priv->max, snk);
	edje_object_signal_emit(tar, "off", PATCHER_UI);

	tar = evas_object_table_child_get(priv->matrix, src, priv->max+1);
	edje_object_signal_emit(tar, "off", PATCHER_UI);

	tar = evas_object_table_child_get(priv->matrix, priv->max+1, snk);
	edje_object_signal_emit(tar, "off", PATCHER_UI);
}

static void
_source_in(void *data, Evas_Object *edj, const char *emission, const char *source)
{
	Evas_Object *o = data;
	patcher_t *priv = evas_object_smart_data_get(o);
	unsigned short src;
	evas_object_table_pack_get(priv->matrix, edj, &src, NULL, NULL, NULL);
	int src_idx;
	_abs_to_rel(priv, src, 0, &src_idx, NULL);
	
	edje_object_signal_emit(edj, "on", PATCHER_UI);

	Evas_Object *tar = evas_object_table_child_get(priv->matrix, src, priv->max+1);
	edje_object_signal_emit(tar, "on", PATCHER_UI);

	int first = 1;
	for(int j=0; j<priv->sinks; j++)
	{
		int snk;
		_rel_to_abs(priv, 0, j, NULL, &snk);

		if(priv->state[src_idx][j]) // connected
		{
			for(int i=src+1; i<priv->max; i++)
			{
				tar = evas_object_table_child_get(priv->matrix, i, snk);
				edje_object_signal_emit(tar, "horizontal", PATCHER_UI);
			}

			tar = evas_object_table_child_get(priv->matrix, src, snk);
			if(first)
			{
				edje_object_signal_emit(tar, "edge", PATCHER_UI);
				first = 0;
			}
			else
				edje_object_signal_emit(tar, "edge,vertical", PATCHER_UI);

			tar = evas_object_table_child_get(priv->matrix, priv->max, snk);
			edje_object_signal_emit(tar, "on", PATCHER_UI);
			
			tar = evas_object_table_child_get(priv->matrix, priv->max+1, snk);
			edje_object_signal_emit(tar, "on", PATCHER_UI);
		}
		else if(!first)
		{
			tar = evas_object_table_child_get(priv->matrix, src, snk);
			edje_object_signal_emit(tar, "vertical", PATCHER_UI);
		}
	}

	priv->src_over = edj;
}

static void
_source_out(void *data, Evas_Object *edj, const char *emission, const char *source)
{
	Evas_Object *o = data;
	patcher_t *priv = evas_object_smart_data_get(o);
	unsigned short src;
	evas_object_table_pack_get(priv->matrix, edj, &src, NULL, NULL, NULL);
	int src_idx;
	_abs_to_rel(priv, src, 0, &src_idx, NULL);
	
	edje_object_signal_emit(edj, "off", PATCHER_UI);

	Evas_Object *tar = evas_object_table_child_get(priv->matrix, src, priv->max+1);
	edje_object_signal_emit(tar, "off", PATCHER_UI);
	
	for(int j=0; j<priv->sinks; j++)
	{
		int snk;
		_rel_to_abs(priv, 0, j, NULL, &snk);

		if(priv->state[src_idx][j]) // connected
		{
			for(int i=src+1; i<priv->max; i++)
			{
				tar = evas_object_table_child_get(priv->matrix, i, snk);
				edje_object_signal_emit(tar, "clear", PATCHER_UI);
			}

			tar = evas_object_table_child_get(priv->matrix, src, snk);
			edje_object_signal_emit(tar, "clear", PATCHER_UI);
			
			tar = evas_object_table_child_get(priv->matrix, priv->max, snk);
			edje_object_signal_emit(tar, "off", PATCHER_UI);
			
			tar = evas_object_table_child_get(priv->matrix, priv->max+1, snk);
			edje_object_signal_emit(tar, "off", PATCHER_UI);
		}
		else
		{
			tar = evas_object_table_child_get(priv->matrix, src, snk);
			edje_object_signal_emit(tar, "clear", PATCHER_UI);
		}
	}

	priv->src_over = NULL;
}

static void
_source_toggled(void *data, Evas_Object *edj, const char *emission, const char *source)
{
	Evas_Object *o = data;
	patcher_t *priv = evas_object_smart_data_get(o);
	unsigned short src_abs;
	evas_object_table_pack_get(priv->matrix, edj, &src_abs, NULL, NULL, NULL);
	int src_idx;
	_abs_to_rel(priv, src_abs, 0, &src_idx, NULL);

	int has_connections = 0;
	for(int snk_idx=0; snk_idx<priv->sinks; snk_idx++)
	{
		if(priv->state[src_idx][snk_idx])
		{
			has_connections = 1;
			break;
		}
	}

	for(int snk_idx=0; snk_idx<priv->sinks; snk_idx++)
	{
		patcher_event_t ev [2] = {
			{
				.index = src_idx,
				.ptr = priv->data.source[src_idx]
			},
			{
				.index = snk_idx,
				.ptr = priv->data.sink[snk_idx]
			}
		};

		if(has_connections)
			evas_object_smart_callback_call(o, PATCHER_DISCONNECT_REQUEST, (void *)ev);
		else
			evas_object_smart_callback_call(o, PATCHER_CONNECT_REQUEST, (void *)ev);
	}

	//FIXME update view
}

static void
_sink_in(void *data, Evas_Object *edj, const char *emission, const char *source)
{
	Evas_Object *o = data;
	patcher_t *priv = evas_object_smart_data_get(o);
	unsigned short snk;
	evas_object_table_pack_get(priv->matrix, edj, NULL, &snk, NULL, NULL);
	int snk_idx;
	_abs_to_rel(priv, 0, snk, NULL, &snk_idx);
	
	edje_object_signal_emit(edj, "on", PATCHER_UI);

	Evas_Object *tar = evas_object_table_child_get(priv->matrix, priv->max+1, snk);
	edje_object_signal_emit(tar, "on", PATCHER_UI);
	
	int first = 1;
	for(int i=0; i<priv->sources; i++)
	{
		int src;
		_rel_to_abs(priv, i, 0, &src, NULL);

		if(priv->state[i][snk_idx]) // connected
		{
			for(int j=snk+1; j<priv->max; j++)
			{
				tar = evas_object_table_child_get(priv->matrix, src, j);
				edje_object_signal_emit(tar, "vertical", PATCHER_UI);
			}

			tar = evas_object_table_child_get(priv->matrix, src, snk);
			if(first)
			{
				edje_object_signal_emit(tar, "edge", PATCHER_UI);
				first = 0;
			}
			else
				edje_object_signal_emit(tar, "edge,horizontal", PATCHER_UI);

			tar = evas_object_table_child_get(priv->matrix, src, priv->max);
			edje_object_signal_emit(tar, "on", PATCHER_UI);
			
			tar = evas_object_table_child_get(priv->matrix, src, priv->max+1);
			edje_object_signal_emit(tar, "on", PATCHER_UI);
		}
		else if(!first)
		{
			tar = evas_object_table_child_get(priv->matrix, src, snk);
			edje_object_signal_emit(tar, "horizontal", PATCHER_UI);
		}
	}

	priv->snk_over = edj;
}

static void
_sink_out(void *data, Evas_Object *edj, const char *emission, const char *source)
{
	Evas_Object *o = data;
	patcher_t *priv = evas_object_smart_data_get(o);
	unsigned short snk;
	evas_object_table_pack_get(priv->matrix, edj, NULL, &snk, NULL, NULL);
	int snk_idx;
	_abs_to_rel(priv, 0, snk, NULL, &snk_idx);
	
	edje_object_signal_emit(edj, "off", PATCHER_UI);

	Evas_Object *tar = evas_object_table_child_get(priv->matrix, priv->max+1, snk);
	edje_object_signal_emit(tar, "off", PATCHER_UI);
	
	for(int i=0; i<priv->sources; i++)
	{
		int src;
		_rel_to_abs(priv, i, 0, &src, NULL);

		if(priv->state[i][snk_idx]) // connected
		{
			for(int j=snk+1; j<priv->max; j++)
			{
				tar = evas_object_table_child_get(priv->matrix, src, j);
				edje_object_signal_emit(tar, "clear", PATCHER_UI);
			}

			tar = evas_object_table_child_get(priv->matrix, src, snk);
			edje_object_signal_emit(tar, "clear", PATCHER_UI);

			tar = evas_object_table_child_get(priv->matrix, src, priv->max);
			edje_object_signal_emit(tar, "off", PATCHER_UI);
			
			tar = evas_object_table_child_get(priv->matrix, src, priv->max+1);
			edje_object_signal_emit(tar, "off", PATCHER_UI);
		}
		else
		{
			tar = evas_object_table_child_get(priv->matrix, src, snk);
			edje_object_signal_emit(tar, "clear", PATCHER_UI);
		}
	}

	priv->snk_over = NULL;
}

static void
_sink_toggled(void *data, Evas_Object *edj, const char *emission, const char *source)
{
	Evas_Object *o = data;
	patcher_t *priv = evas_object_smart_data_get(o);
	unsigned short snk_abs;
	evas_object_table_pack_get(priv->matrix, edj, NULL, &snk_abs, NULL, NULL);
	int snk_idx;
	_abs_to_rel(priv, 0, snk_abs, NULL, &snk_idx);

	int has_connections = 0;
	for(int src_idx=0; src_idx<priv->sources; src_idx++)
	{
		if(priv->state[src_idx][snk_idx])
		{
			has_connections = 1;
			break;
		}
	}

	for(int src_idx=0; src_idx<priv->sources; src_idx++)
	{
		patcher_event_t ev [2] = {
			{
				.index = src_idx,
				.ptr = priv->data.source[src_idx]
			},
			{
				.index = snk_idx,
				.ptr = priv->data.sink[snk_idx]
			}
		};

		if(has_connections)
			evas_object_smart_callback_call(o, PATCHER_DISCONNECT_REQUEST, (void *)ev);
		else
			evas_object_smart_callback_call(o, PATCHER_CONNECT_REQUEST, (void *)ev);
	}

	//FIXME update view
}

static void
_node_toggled(void *data, Evas_Object *edj, const char *emission, const char *source)
{
	Evas_Object *o = data;
	patcher_t *priv = evas_object_smart_data_get(o);
	unsigned short src_abs, snk_abs;

	evas_object_table_pack_get(priv->matrix, edj, &src_abs, &snk_abs, NULL, NULL);
	int src_idx, snk_idx;
	_abs_to_rel(priv, src_abs, snk_abs, &src_idx, &snk_idx);

	patcher_event_t ev [2] = {
		{
			.index = src_idx,
			.ptr = priv->data.source[src_idx]
		},
		{
			.index = snk_idx,
			.ptr = priv->data.sink[snk_idx]
		}
	};

	if(priv->state[src_idx][snk_idx]) // is on currently
		evas_object_smart_callback_call(o, PATCHER_DISCONNECT_REQUEST, (void *)ev);
	else // is off currently
		evas_object_smart_callback_call(o, PATCHER_CONNECT_REQUEST, (void *)ev);
}

static void
_patcher_smart_init(Evas_Object *o)
{
	Evas *e = evas_object_evas_get(o);
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Object *elmnt;

	if( !(priv->sinks && priv->sources) )
		return;

	priv->src_over = NULL;
	priv->snk_over = NULL;

	priv->data.source = calloc(priv->sources, sizeof(void *));
	priv->data.sink = calloc(priv->sinks, sizeof(void *));

	// create state
	priv->state = calloc(priv->sources, sizeof(Eina_Bool *));
	for(int src=0; src<priv->sources; src++)
		priv->state[src] = calloc(priv->sinks, sizeof(Eina_Bool));

	// create nodes
	for(int src_idx = 0; src_idx < priv->sources; src_idx++)
	{
		for(int snk_idx = 0; snk_idx < priv->sinks; snk_idx++)
		{
			int src_abs, snk_abs;
			_rel_to_abs(priv, src_idx, snk_idx, &src_abs, &snk_abs);

			elmnt = edje_object_add(e);
			edje_object_file_set(elmnt, SYNTHPOD_DATA_DIR"/synthpod.edj",
				"/synthpod/patcher/node"); //TODO
			edje_object_signal_callback_add(elmnt, "in", PATCHER_UI, _node_in, o);
			edje_object_signal_callback_add(elmnt, "out", PATCHER_UI, _node_out, o);
			edje_object_signal_callback_add(elmnt, "toggled", PATCHER_UI, _node_toggled, o);
			evas_object_size_hint_weight_set(elmnt, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(elmnt, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(elmnt);
			evas_object_table_pack(priv->matrix, elmnt, src_abs, snk_abs, 1, 1);
		}
	}

	// create source ports & labels
	for(int src_idx = 0; src_idx < priv->sources; src_idx++)
	{
		int src_abs, snk_abs;
		_rel_to_abs(priv, src_idx, priv->sinks, &src_abs, &snk_abs);

		elmnt = edje_object_add(e);
		edje_object_file_set(elmnt, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/patcher/port");
		edje_object_signal_callback_add(elmnt, "in", PATCHER_UI, _source_in, o);
		edje_object_signal_callback_add(elmnt, "out", PATCHER_UI, _source_out, o);
		edje_object_signal_callback_add(elmnt, "toggled", PATCHER_UI, _source_toggled, o);
		edje_object_signal_emit(elmnt, "source", PATCHER_UI);
		evas_object_size_hint_weight_set(elmnt, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(elmnt, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(elmnt);
		evas_object_table_pack(priv->matrix, elmnt, src_abs, snk_abs, 1, 1);

		elmnt = edje_object_add(e);
		edje_object_file_set(elmnt, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/patcher/label/vertical");
		evas_object_size_hint_weight_set(elmnt, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(elmnt, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(elmnt);
		evas_object_table_pack(priv->matrix, elmnt, src_abs, snk_abs + 1, 1, LEN);
	}

	// create sink ports & labels
	for(int snk_idx = 0; snk_idx < priv->sinks; snk_idx++)
	{
		int src_abs, snk_abs;
		_rel_to_abs(priv, priv->sources, snk_idx, &src_abs, &snk_abs);

		elmnt = edje_object_add(e);
		edje_object_file_set(elmnt, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/patcher/port");
		edje_object_signal_callback_add(elmnt, "in", PATCHER_UI, _sink_in, o);
		edje_object_signal_callback_add(elmnt, "out", PATCHER_UI, _sink_out, o);
		edje_object_signal_callback_add(elmnt, "toggled", PATCHER_UI, _sink_toggled, o);
		edje_object_signal_emit(elmnt, "sink", PATCHER_UI);
		evas_object_size_hint_weight_set(elmnt, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(elmnt, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(elmnt);
		evas_object_table_pack(priv->matrix, elmnt, src_abs, snk_abs, 1, 1);
		
		elmnt = edje_object_add(e);
		edje_object_file_set(elmnt, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/patcher/label/horizontal");
		evas_object_size_hint_weight_set(elmnt, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(elmnt, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(elmnt);
		evas_object_table_pack(priv->matrix, elmnt, src_abs + 1, snk_abs, LEN, 1);
	}
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
	//patcher_t *priv = evas_object_smart_data_get(o);

	_patcher_smart_deinit(o);

	_patcher_parent_sc->del(o);
}

static void
_patcher_smart_move(Evas_Object *o, Evas_Coord ox, Evas_Coord oy)
{
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Coord ix, iy;

	evas_object_geometry_get(o, &ix, &iy, NULL, NULL);
	if( (ix == ox) && (iy == oy) )
		return;

	evas_object_move(priv->matrix, ox, oy);
}

static void
_patcher_smart_resize(Evas_Object *o, Evas_Coord ow, Evas_Coord oh)
{
	//patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Coord iw, ih;

	evas_object_geometry_get(o, NULL, NULL, &iw, &ih);
	if( (iw == ow) && (ih == oh) )
		return;

	evas_object_smart_changed(o);
}

static void
_patcher_smart_calculate(Evas_Object *o)
{
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Coord ix, iy, iw, ih;
	Evas_Coord ow, oh;

	evas_object_geometry_get(o, &ix, &iy, &iw, &ih);
	float dw = (float)iw / priv->max;
	float dh = (float)ih / priv->max;
	if(dw < dh)
	{
		ow = iw;
		oh = dw * priv->max;
	}
	else // dw >= dh
	{
		ow = dh * priv->max;
		oh = ih;
	}

	if( (iw == ow) && (ih == oh) )
		return;

	evas_object_resize(priv->matrix, ow, oh);
}

static void
_patcher_smart_set_user(Evas_Smart_Class *sc)
{
	// function overloading 
	sc->add = _patcher_smart_add;
	sc->del = _patcher_smart_del;
	sc->move = _patcher_smart_move;
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
	priv->max = sources >= sinks
		? sources
		: sinks;
	if(priv->max < LEN)
		priv->max = LEN;

	_patcher_smart_init(o);
}

static inline void 
_patcher_object_connected_idx_set(Evas_Object *o, int src_idx, int snk_idx,
	Eina_Bool state)
{
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Object *edj = _rel_child_get(priv, src_idx, snk_idx);
	
	if(priv->state[src_idx][snk_idx] == state)
		return; // no change, thus nothing to do
	
	if(state) // enable node
		edje_object_signal_emit(edj, "on", PATCHER_UI);
	else // disable node
		edje_object_signal_emit(edj, "off", PATCHER_UI);

	priv->state[src_idx][snk_idx] = state;
}

static inline void
_patcher_object_indirected_idx_set(Evas_Object *o, int src_idx, int snk_idx,
	int indirect)
{
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Object *edj = _rel_child_get(priv, src_idx, snk_idx);

	/* TODO
	if(priv->indirect[source][sink] == indirect)
		return; // no change, thus nothing to do
	*/

	switch(indirect)
	{
		case 1:
			edje_object_signal_emit(edj, "indirect", PATCHER_UI);
			break;
		case 0:
			edje_object_signal_emit(edj, "direct", PATCHER_UI);
			break;
		case -1:
			edje_object_signal_emit(edj, "feedback", PATCHER_UI);
			break;
	}

	/* TODO
	priv->indirect[source][sink] = indirect;
	*/
}

void
patcher_object_connected_set(Evas_Object *o, void *src_data,
	void *snk_data, Eina_Bool state, int indirect)
{
	patcher_t *priv = evas_object_smart_data_get(o);
	int src_idx = _patcher_object_source_idx_get(o, src_data);
	int snk_idx = _patcher_object_sink_idx_get(o, snk_data);
	if( (src_idx == -1) || (snk_idx == -1) )
		return;
		
	Evas_Object *snk_over = priv->snk_over;
	Evas_Object *src_over = priv->src_over;

	// clear connections if hovering over sink|source node
	if(src_over)
		_source_out(o, src_over, NULL, NULL);
	if(snk_over)
		_sink_out(o, snk_over, NULL, NULL);

	_patcher_object_connected_idx_set(o, src_idx, snk_idx, state);
	_patcher_object_indirected_idx_set(o, src_idx, snk_idx, indirect);

	// update connections if hovering over sink|source node
	if(src_over)
		_source_in(o, src_over, NULL, NULL);
	if(snk_over)
		_sink_in(o, snk_over, NULL, NULL);
}

void
patcher_object_source_data_set(Evas_Object *o, int src_idx, void *data)
{
	patcher_t *priv = evas_object_smart_data_get(o);

	if(priv->data.source)
		priv->data.source[src_idx] = data;
}

void
patcher_object_sink_data_set(Evas_Object *o, int snk_idx, void *data)
{
	patcher_t *priv = evas_object_smart_data_get(o);

	if(priv->data.sink)
		priv->data.sink[snk_idx] = data;
}

void
patcher_object_source_color_set(Evas_Object *o, int src_idx, int col)
{
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Object *edj = _rel_child_get(priv, src_idx, priv->sinks);

	char msg [7];
	sprintf(msg, "col,%02i", col);
	edje_object_signal_emit(edj, msg, PATCHER_UI);
}

void
patcher_object_sink_color_set(Evas_Object *o, int snk_idx, int col)
{
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Object *edj = _rel_child_get(priv, priv->sources, snk_idx);

	char msg [7];
	sprintf(msg, "col,%02i", col);
	edje_object_signal_emit(edj, msg, PATCHER_UI);
}

void
patcher_object_source_label_set(Evas_Object *o, int src_idx, const char *label)
{
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Object *edj = _rel_child_get(priv, src_idx, priv->sinks+1);

	edje_object_part_text_set(edj, "default", label);
}

void
patcher_object_sink_label_set(Evas_Object *o, int snk_idx, const char *label)
{
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Object *edj = _rel_child_get(priv, priv->sources+1, snk_idx);

	edje_object_part_text_set(edj, "default", label);
}

void
patcher_object_realize(Evas_Object *o)
{
	patcher_t *priv = evas_object_smart_data_get(o);

	for(int src_idx=0; src_idx<priv->sources; src_idx++)
		for(int snk_idx=0; snk_idx<priv->sinks; snk_idx++)
		{
			patcher_event_t ev [2] = {
				{
					.index = src_idx,
					.ptr = priv->data.source[src_idx]
				},
				{
					.index = snk_idx,
					.ptr = priv->data.sink[snk_idx]
				}
			};

			evas_object_smart_callback_call(o, PATCHER_REALIZE_REQUEST, (void *)ev);
		}

	evas_object_smart_changed(o);
}
