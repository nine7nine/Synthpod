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

#ifndef _SYNTHPOD_SMART_SPINNER_H
#define _SYNTHPOD_SMART_SPINNER_H

#include <Evas.h>

#define SMART_SPINNER_UI "/synthpod/smart_spinner/ui"

Evas_Object *
smart_spinner_add(Evas *e);

void
smart_spinner_value_add(Evas_Object *o, float value, const char *label);
void
smart_spinner_value_set(Evas_Object *o, float value);
float
smart_spinner_value_get(Evas_Object *o);

void
smart_spinner_key_add(Evas_Object *o, const char *key, const char *label);
void
smart_spinner_key_set(Evas_Object *o, const char *key);
const char *
smart_spinner_key_get(Evas_Object *o);

void
smart_spinner_color_set(Evas_Object *o, int col);

void
smart_spinner_disabled_set(Evas_Object *o, int disabled);

#endif // _SYNTHPOD_SMART_SPINNER_H
