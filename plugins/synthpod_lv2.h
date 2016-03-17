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

#ifndef _SYNTHPOD_LV2_H
#define _SYNTHPOD_LV2_H

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"

#include <xpress.h>

#define CHUNK_SIZE 0x10000
#define SEQ_SIZE 0x2000
#define _ATOM_ALIGNED __attribute__((aligned(8)))

// bundle uri
#define SYNTHPOD_URI						"http://open-music-kontrollers.ch/lv2/synthpod"
#define SYNTHPOD_EVENT_URI			SYNTHPOD_URI"#event"

extern xpress_map_t voice_map_fallback;

// plugin uris
#define SYNTHPOD_STEREO_URI						SYNTHPOD_URI"#stereo"
#define SYNTHPOD_KEYBOARD_URI					SYNTHPOD_URI"#keyboard"
#define SYNTHPOD_CV2CONTROL_URI				SYNTHPOD_URI"#cv2control"
#define SYNTHPOD_CONTROL2CV_URI				SYNTHPOD_URI"#control2cv"
#define SYNTHPOD_CV2ATOM_URI					SYNTHPOD_URI"#cv2atom"
#define SYNTHPOD_ATOM2CV_URI					SYNTHPOD_URI"#atom2cv"
#define SYNTHPOD_MIDISPLITTER_URI			SYNTHPOD_URI"#midisplitter"
#define SYNTHPOD_ATOM2EVENT_URI				SYNTHPOD_URI"#atom2event"
#define SYNTHPOD_EVENT2ATOM_URI				SYNTHPOD_URI"#event2atom"
#define SYNTHPOD_AUDIOXFADEMONO_URI		SYNTHPOD_URI"#audioxfademono"
#define SYNTHPOD_AUDIOXFADESTEREO_URI	SYNTHPOD_URI"#audioxfadestereo"
#define SYNTHPOD_ATOM2CONTROL_URI			SYNTHPOD_URI"#atom2control"
#define SYNTHPOD_CONTROL2ATOM_URI			SYNTHPOD_URI"#control2atom"
#define SYNTHPOD_MIXER_URI						SYNTHPOD_URI"#mixer"
#define SYNTHPOD_MONOATOM_URI					SYNTHPOD_URI"#monoatom"
#define SYNTHPOD_ANONYMIZER_URI				SYNTHPOD_URI"#anonymizer"

extern const LV2_Descriptor synthpod_stereo;
extern const LV2_Descriptor synthpod_keyboard;
extern const LV2_Descriptor synthpod_cv2control;
extern const LV2_Descriptor synthpod_control2cv;
extern const LV2_Descriptor synthpod_cv2atom;
extern const LV2_Descriptor synthpod_atom2cv;
extern const LV2_Descriptor synthpod_midisplitter;
extern const LV2_Descriptor synthpod_atom2event;
extern const LV2_Descriptor synthpod_event2atom;
extern const LV2_Descriptor synthpod_audioxfademono;
extern const LV2_Descriptor synthpod_audioxfadestereo;
extern const LV2_Descriptor synthpod_atom2control;
extern const LV2_Descriptor synthpod_control2atom;
extern const LV2_Descriptor synthpod_mixer;
extern const LV2_Descriptor synthpod_monoatom;
extern const LV2_Descriptor synthpod_anonymizer;

// common UI uris
#define SYNTHPOD_COMMON_UI_URI	SYNTHPOD_URI"#common_1_ui"
#define SYNTHPOD_COMMON_KX_URI	SYNTHPOD_URI"#common_2_kx"
#define SYNTHPOD_COMMON_X11_URI	SYNTHPOD_URI"#common_3_x11"
#define SYNTHPOD_COMMON_EO_URI	SYNTHPOD_URI"#common_4_eo"

extern const LV2UI_Descriptor synthpod_common_1_ui;
extern const LV2UI_Descriptor synthpod_common_2_kx;
extern const LV2UI_Descriptor synthpod_common_3_x11;
extern const LV2UI_Descriptor synthpod_common_4_eo;

// keyboard UI uris
#define SYNTHPOD_KEYBOARD_UI_URI	SYNTHPOD_URI"#keyboard_1_ui"
#define SYNTHPOD_KEYBOARD_KX_URI	SYNTHPOD_URI"#keyboard_2_kx"
#define SYNTHPOD_KEYBOARD_X11_URI	SYNTHPOD_URI"#keyboard_3_x11"
#define SYNTHPOD_KEYBOARD_EO_URI	SYNTHPOD_URI"#keyboard_4_eo"

extern const LV2UI_Descriptor synthpod_keyboard_1_ui;
extern const LV2UI_Descriptor synthpod_keyboard_2_kx;
extern const LV2UI_Descriptor synthpod_keyboard_3_x11;
extern const LV2UI_Descriptor synthpod_keyboard_4_eo;

#endif // _SYNTHPOD_LV2_H
