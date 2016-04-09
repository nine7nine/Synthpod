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

#ifndef _SYNTHPOD_SMART_BITMASK_H
#define _SYNTHPOD_SMART_BITMASK_H

#include <Evas.h>

#define SMART_BITMASK_UI "/synthpod/smart_bitmask/ui"

Evas_Object *
smart_bitmask_add(Evas *e);

void
smart_bitmask_value_set(Evas_Object *o, int64_t value);
int64_t
smart_bitmask_value_get(Evas_Object *o);

void
smart_bitmask_bits_set(Evas_Object *o, int nbits);
int
smart_bitmask_bits_get(Evas_Object *o);

void
smart_bitmask_color_set(Evas_Object *o, int col);

void
smart_bitmask_disabled_set(Evas_Object *o, int disabled);

#endif // _SYNTHPOD_SMART_BITMASK_H
