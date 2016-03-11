/*
 * Copyright (c) 2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#define PATCHER_UI "/patcher/ui"

typedef struct _patcher_event_t patcher_event_t;

struct _patcher_event_t {
	int index;
	intptr_t id;
};

Evas_Object *
patcher_object_add(Evas_Object *parent);

void
patcher_object_dimension_set(Evas_Object *o, int sources, int sinks);

void
patcher_object_connected_set(Evas_Object *o, intptr_t source_id, intptr_t sink_id,
	Eina_Bool state, int indirect);

void
patcher_object_source_id_set(Evas_Object *o, int source, intptr_t id);

void
patcher_object_sink_id_set(Evas_Object *o, int sink, intptr_t id);

void
patcher_object_source_color_set(Evas_Object *o, int source, int col);

void
patcher_object_sink_color_set(Evas_Object *o, int sink, int col);

void
patcher_object_source_label_set(Evas_Object *o, int source, const char *label);

void
patcher_object_sink_label_set(Evas_Object *o, int sink, const char *label);

void
patcher_object_source_group_set(Evas_Object *o, int source, const char *group);

void
patcher_object_sink_group_set(Evas_Object *o, int sink, const char *group);

void
patcher_object_realize(Evas_Object *o);

void
patcher_object_zoom_in(Evas_Object *o);

void
patcher_object_zoom_out(Evas_Object *o);

void
patcher_object_zoom_set(Evas_Object *o, float zoom);

#endif // _SYNTHPOD_PATCHER_H
