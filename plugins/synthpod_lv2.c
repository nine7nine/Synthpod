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

static atomic_long voice_uuid = ATOMIC_VAR_INIT(INT64_MAX / UINT16_MAX * 3LL);

static xpress_uuid_t
_voice_map_new_uuid(void *handle)
{
	atomic_long *uuid = handle;
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
			return &synthpod_keyboard;
		case 1:
			return &synthpod_cv2control;
		case 2:
			return &synthpod_control2cv;
		case 3:
			return &synthpod_mixer;
		case 4:
			return &synthpod_anonymizer;
		case 5:
			return &synthpod_midisplitter;
		case 6:
			return &synthpod_panic;
		case 7:
			return &synthpod_heavyload;
		case 8:
			return &synthpod_placeholder;
		case 9:
			return &synthpod_stereo;
		default:
			return NULL;
	}
}
