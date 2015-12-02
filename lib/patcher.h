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

#ifndef _SYNTHPOD_PATCHER_H
#define _SYNTHPOD_PATCHER_H

#include <Evas.h>

#define PATCHER_UI "/synthpod/patcher/ui"

typedef struct _patcher_event_t patcher_event_t;

struct _patcher_event_t {
	int index;
	void *ptr;
};

Evas_Object *
patcher_object_add(Evas *e);

void
patcher_object_dimension_set(Evas_Object *o, int sources, int sinks);

void
patcher_object_connected_set(Evas_Object *o, void *source_data, void *sink_data,
	Eina_Bool state, int indirect);

void
patcher_object_source_data_set(Evas_Object *o, int source, void *data);

void
patcher_object_sink_data_set(Evas_Object *o, int sink, void *data);

void
patcher_object_source_color_set(Evas_Object *o, int source, int col);

void
patcher_object_sink_color_set(Evas_Object *o, int sink, int col);

void
patcher_object_source_label_set(Evas_Object *o, int source, const char *label);

void
patcher_object_sink_label_set(Evas_Object *o, int sink, const char *label);

void
patcher_object_realize(Evas_Object *o);

#endif // _SYNTHPOD_PATCHER_H
