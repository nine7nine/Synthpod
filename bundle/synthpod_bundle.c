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

#include <stdlib.h>

#include <synthpod_bundle.h>

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
			return &synthpod_source;
		case 1:
			return &synthpod_sink;

		case 2:
			return &synthpod_osc_source;
		case 3:
			return &synthpod_osc_sink;

		case 4:
			return &synthpod_cv_source;
		case 5:
			return &synthpod_cv_sink;

		case 6:
			return &synthpod_audio_source;
		case 7:
			return &synthpod_audio_sink;

		case 8:
			return &synthpod_midi_source;
		case 9:
			return &synthpod_midi_sink;

		default:
			return NULL;
	}
}
