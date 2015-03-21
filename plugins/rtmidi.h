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

#ifndef _RTMIDI_LV2_H
#define _RTMIDI_LV2_H

#include <stdint.h>

#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/worker/worker.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
	
#define RTMIDI_URI										"http://open-music-kontrollers.ch/lv2/rtmidi"

#define RTMIDI_OUT_URI								RTMIDI_URI"#out"
#define RTMIDI_IN_URI									RTMIDI_URI"#in"
//#define RTMIDI_ATOM_INSPECTOR_UI_URI	RTMIDI_URI"#out_ui"
//#define RTMIDI_ATOM_INSPECTOR_EO_URI	RTMIDI_URI"#out_eo"

extern const LV2_Descriptor rtmidi_out;
extern const LV2_Descriptor rtmidi_in;
//extern const LV2UI_Descriptor rtmidi_out_ui;
//extern const LV2UI_Descriptor rtmidi_out_eo;

#endif // _RTMIDI_LV2_H
