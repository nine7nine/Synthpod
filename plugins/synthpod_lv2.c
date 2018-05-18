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
			return &synthpod_midisplitter;
		case 4:
			return &synthpod_panic;
		case 5:
			return &synthpod_heavyload;
		case 6:
			return &synthpod_placeholder;
		case 7:
			return &synthpod_stereo;
		default:
			return NULL;
	}
}
