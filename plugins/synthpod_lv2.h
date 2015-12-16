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

#ifndef _SYNTHPOD_LV2_H
#define _SYNTHPOD_LV2_H

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"

#define CHUNK_SIZE 0x10000
#define SEQ_SIZE 0x2000
#define _ATOM_ALIGNED __attribute__((aligned(8)))

// bundle uri
#define SYNTHPOD_URI						"http://open-music-kontrollers.ch/lv2/synthpod"
#define SYNTHPOD_EVENT_URI			SYNTHPOD_URI"#event"

// plugin uris
#define SYNTHPOD_STEREO_URI						SYNTHPOD_URI"#stereo"
#define SYNTHPOD_KEYBOARD_URI					SYNTHPOD_URI"#keyboard"
#define SYNTHPOD_CV2CONTROL_URI				SYNTHPOD_URI"#cv2control"
#define SYNTHPOD_CONTROL2CV_URI				SYNTHPOD_URI"#control2cv"
#define SYNTHPOD_CV2ATOM_URI					SYNTHPOD_URI"#cv2atom"
#define SYNTHPOD_ATOM2CV_URI					SYNTHPOD_URI"#atom2cv"
#define SYNTHPOD_AUDIOXFADEMONO_URI		SYNTHPOD_URI"#audioxfademono"
#define SYNTHPOD_AUDIOXFADESTEREO_URI	SYNTHPOD_URI"#audioxfadestereo"
#define SYNTHPOD_ATOM2CONTROL_URI			SYNTHPOD_URI"#atom2control"
#define SYNTHPOD_CONTROL2ATOM_URI			SYNTHPOD_URI"#control2atom"
#define SYNTHPOD_MIXER_URI						SYNTHPOD_URI"#mixer"
#define SYNTHPOD_MONOATOM_URI					SYNTHPOD_URI"#monoatom"

extern const LV2_Descriptor synthpod_stereo;
extern const LV2_Descriptor synthpod_keyboard;
extern const LV2_Descriptor synthpod_cv2control;
extern const LV2_Descriptor synthpod_control2cv;
extern const LV2_Descriptor synthpod_cv2atom;
extern const LV2_Descriptor synthpod_atom2cv;
extern const LV2_Descriptor synthpod_audioxfademono;
extern const LV2_Descriptor synthpod_audioxfadestereo;
extern const LV2_Descriptor synthpod_atom2control;
extern const LV2_Descriptor synthpod_control2atom;
extern const LV2_Descriptor synthpod_mixer;
extern const LV2_Descriptor synthpod_monoatom;

// common UI uris
#define SYNTHPOD_COMMON_EO_URI	SYNTHPOD_URI"#common_eo"
#define SYNTHPOD_COMMON_UI_URI	SYNTHPOD_URI"#common_ui"
#define SYNTHPOD_COMMON_X11_URI	SYNTHPOD_URI"#common_x11"
#define SYNTHPOD_COMMON_KX_URI	SYNTHPOD_URI"#common_kx"

extern const LV2UI_Descriptor synthpod_common_eo;
extern const LV2UI_Descriptor synthpod_common_ui;
extern const LV2UI_Descriptor synthpod_common_x11;
extern const LV2UI_Descriptor synthpod_common_kx;

// keyboard UI uris
#define SYNTHPOD_KEYBOARD_EO_URI	SYNTHPOD_URI"#keyboard_eo"
#define SYNTHPOD_KEYBOARD_UI_URI	SYNTHPOD_URI"#keyboard_ui"
#define SYNTHPOD_KEYBOARD_X11_URI	SYNTHPOD_URI"#keyboard_x11"
#define SYNTHPOD_KEYBOARD_KX_URI	SYNTHPOD_URI"#keyboard_kx"

extern const LV2UI_Descriptor synthpod_keyboard_eo;
extern const LV2UI_Descriptor synthpod_keyboard_ui;
extern const LV2UI_Descriptor synthpod_keyboard_x11;
extern const LV2UI_Descriptor synthpod_keyboard_kx;

#endif // _SYNTHPOD_LV2_H
