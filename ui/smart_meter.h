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

#ifndef _SYNTHPOD_SMART_METER_H
#define _SYNTHPOD_SMART_METER_H

#include <Evas.h>

#define SMART_METER_UI "/synthpod/smart_meter/ui"

Evas_Object *
smart_meter_add(Evas *e);

void
smart_meter_value_set(Evas_Object *o, float value);
float
smart_meter_value_get(Evas_Object *o);

void
smart_meter_color_set(Evas_Object *o, int col);

#endif // _SYNTHPOD_SMART_METER_H
