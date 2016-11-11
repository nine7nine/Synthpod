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

#include <stdlib.h>
#include <stdatomic.h>

#include <synthpod_lv2.h>

static _Atomic xpress_uuid_t voice_uuid = ATOMIC_VAR_INIT(INT64_MAX / UINT16_MAX * 3LL);

static xpress_uuid_t
_voice_map_new_uuid(void *handle)
{
	_Atomic xpress_uuid_t *uuid = handle;
	return atomic_fetch_add_explicit(uuid, 1, memory_order_relaxed);
}

xpress_map_t voice_map_fallback = {
	.handle = &voice_uuid,
	.new_uuid = _voice_map_new_uuid
};

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch(index)
	{
		case 0:
			return &synthpod_stereo;
		case 1:
			return &synthpod_keyboard;
		case 2:
			return &synthpod_cv2control;
		case 3:
			return &synthpod_control2cv;
		case 4:
			return &synthpod_cv2atom;
		case 5:
			return &synthpod_atom2cv;
		case 6:
			return &synthpod_audioxfademono;
		case 7:
			return &synthpod_audioxfadestereo;
		case 8:
			return &synthpod_atom2control;
		case 9:
			return &synthpod_control2atom;
		case 10:
			return &synthpod_mixer;
		case 11:
			return &synthpod_anonymizer;
		case 12:
			return &synthpod_midisplitter;
		case 13:
			return &synthpod_midi2control;
		case 14:
			return &synthpod_string2control;
		case 15:
			return &synthpod_panic;
		case 16:
			return &synthpod_heavyload;
		default:
			return NULL;
	}
}
