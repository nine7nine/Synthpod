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

#ifndef _SYNTHPOD_METER_H
#define _SYNTHPOD_METER_H

#include <Evas.h>

#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#define METER_UI "/synthpod/meter/ui"

Evas_Object *
meter_object_add(Evas *e);

void
meter_object_color_set(Evas_Object *o, int col);

void
meter_object_peak_set(Evas_Object *o, LV2UI_Peak_Data *peak_data);

#endif // _SYNTHPOD_METER_H
