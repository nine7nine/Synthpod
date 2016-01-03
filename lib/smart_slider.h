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

#ifndef _SYNTHPOD_SMART_SLIDER_H
#define _SYNTHPOD_SMART_SLIDER_H

#include <Evas.h>

#define SMART_SLIDER_UI "/synthpod/smart_slider/ui"

Evas_Object *
smart_slider_add(Evas *e);

void
smart_slider_range_set(Evas_Object *o, float min, float max, float dflt);
void
smart_slider_range_get(Evas_Object *o, float *min, float *max, float *dflt);

void
smart_slider_value_set(Evas_Object *o, float value);
float
smart_slider_value_get(Evas_Object *o);

void
smart_slider_format_set(Evas_Object *o, const char *format);

void
smart_slider_unit_set(Evas_Object *o, const char *unit);

void
smart_slider_color_set(Evas_Object *o, int col);

void
smart_slider_integer_set(Evas_Object *o, int integer);

void
smart_slider_logarithmic_set(Evas_Object *o, int logarithmic);

void
smart_slider_disabled_set(Evas_Object *o, int disabled);

#endif // _SYNTHPOD_SMART_SLIDER_H
