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

#ifdef _WIN32
#	define mlock(...)
#	define munlock(...)
#else
#	include <sys/mman.h> // mlock
#endif

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"

#include <xpress.lv2/xpress.h>

#include <synthpod_common.h>

#define __realtime __attribute__((annotate("realtime")))
#define __non_realtime __attribute__((annotate("non-realtime")))

#define SEQ_SIZE 0x2000
#define _ATOM_ALIGNED __attribute__((aligned(8)))

// bundle uri
#define SYNTHPOD_EVENT_URI				SYNTHPOD_PREFIX"event"

extern xpress_map_t voice_map_fallback;

// plugin uris
#define SYNTHPOD_KEYBOARD_URI					SYNTHPOD_PREFIX"keyboard"
#define SYNTHPOD_CV2CONTROL_URI				SYNTHPOD_PREFIX"cv2control"
#define SYNTHPOD_CONTROL2CV_URI				SYNTHPOD_PREFIX"control2cv"
#define SYNTHPOD_CV2ATOM_URI					SYNTHPOD_PREFIX"cv2atom"
#define SYNTHPOD_ATOM2CV_URI					SYNTHPOD_PREFIX"atom2cv"
#define SYNTHPOD_MIDISPLITTER_URI			SYNTHPOD_PREFIX"midisplitter"
#define SYNTHPOD_HEAVYLOAD_URI				SYNTHPOD_PREFIX"heavyload"
#define SYNTHPOD_AUDIOXFADEMONO_URI		SYNTHPOD_PREFIX"audioxfademono"
#define SYNTHPOD_AUDIOXFADESTEREO_URI	SYNTHPOD_PREFIX"audioxfadestereo"
#define SYNTHPOD_ATOM2CONTROL_URI			SYNTHPOD_PREFIX"atom2control"
#define SYNTHPOD_MIDI2CONTROL_URI			SYNTHPOD_PREFIX"midi2control"
#define SYNTHPOD_PANIC_URI						SYNTHPOD_PREFIX"panic"
#define SYNTHPOD_STRING2CONTROL_URI		SYNTHPOD_PREFIX"string2control"
#define SYNTHPOD_CONTROL2ATOM_URI			SYNTHPOD_PREFIX"control2atom"
#define SYNTHPOD_MIXER_URI						SYNTHPOD_PREFIX"mixer"
#define SYNTHPOD_ANONYMIZER_URI				SYNTHPOD_PREFIX"anonymizer"
#define SYNTHPOD_PLACEHOLDER_URI			SYNTHPOD_PREFIX"placeholder"

extern const LV2_Descriptor synthpod_stereo;
extern const LV2_Descriptor synthpod_keyboard;
extern const LV2_Descriptor synthpod_cv2control;
extern const LV2_Descriptor synthpod_control2cv;
extern const LV2_Descriptor synthpod_cv2atom;
extern const LV2_Descriptor synthpod_atom2cv;
extern const LV2_Descriptor synthpod_midisplitter;
extern const LV2_Descriptor synthpod_heavyload;
extern const LV2_Descriptor synthpod_audioxfademono;
extern const LV2_Descriptor synthpod_audioxfadestereo;
extern const LV2_Descriptor synthpod_atom2control;
extern const LV2_Descriptor synthpod_midi2control;
extern const LV2_Descriptor synthpod_panic;
extern const LV2_Descriptor synthpod_string2control;
extern const LV2_Descriptor synthpod_control2atom;
extern const LV2_Descriptor synthpod_mixer;
extern const LV2_Descriptor synthpod_anonymizer;
extern const LV2_Descriptor synthpod_placeholder;

extern const LV2UI_Descriptor synthpod_common_1_ui;
extern const LV2UI_Descriptor synthpod_common_2_kx;
extern const LV2UI_Descriptor synthpod_common_3_eo;
extern const LV2UI_Descriptor synthpod_common_4_nk;

extern const LV2UI_Descriptor synthpod_root_3_eo;
extern const LV2UI_Descriptor synthpod_root_4_nk;

// keyboard UI uris
#define SYNTHPOD_KEYBOARD_NK_URI	SYNTHPOD_PREFIX"keyboard_4_nk"

extern const LV2UI_Descriptor synthpod_keyboard_4_nk;

#endif // _SYNTHPOD_LV2_H
