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

static _Atomic xpress_uuid_t voice_uuid = ATOMIC_VAR_INIT(INT64_MAX / UINT16_MAX * 4LL);

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
const LV2UI_Descriptor*
lv2ui_descriptor(uint32_t index)
{
	switch(index)
	{
		case 0:
			return &synthpod_common_1_ui;
		case 1:
			return &synthpod_common_2_kx;
		case 2:
			return &synthpod_common_3_x11;
		case 3:
			return &synthpod_common_4_eo;

		case 4:
			return &synthpod_keyboard_1_ui;
		case 5:
			return &synthpod_keyboard_2_kx;
		case 6:
			return &synthpod_keyboard_3_x11;
		case 7:
			return &synthpod_keyboard_4_eo;

		default:
			return NULL;
	}
}
