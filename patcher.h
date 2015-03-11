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

Evas_Object *
patcher_object_add(Evas *e);

void
patcher_object_dimension_set(Evas_Object *o, int cols, int rows);

void
patcher_object_dimension_get(Evas_Object *o, int *cols, int *rows);

#endif // _SYNTHPOD_PATCHER_H
