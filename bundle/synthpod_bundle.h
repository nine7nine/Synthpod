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

// bundle uri
#define SYNTHPOD_URI			"http://open-music-kontrollers.ch/lv2/synthpod"

// plugin uris
#define SYNTHPOD_SOURCE_URI			SYNTHPOD_URI"#source"
#define SYNTHPOD_SINK_URI				SYNTHPOD_URI"#sink"

#define SYNTHPOD_OSC_SOURCE_URI	SYNTHPOD_URI"#osc_source"
#define SYNTHPOD_OSC_SINK_URI		SYNTHPOD_URI"#osc_sink"

#define SYNTHPOD_CV_SOURCE_URI	SYNTHPOD_URI"#cv_source"
#define SYNTHPOD_CV_SINK_URI		SYNTHPOD_URI"#cv_sink"

#define SYNTHPOD_AUDIO_SOURCE_URI	SYNTHPOD_URI"#audio_source"
#define SYNTHPOD_AUDIO_SINK_URI		SYNTHPOD_URI"#audio_sink"

#define SYNTHPOD_MIDI_SOURCE_URI	SYNTHPOD_URI"#midi_source"
#define SYNTHPOD_MIDI_SINK_URI		SYNTHPOD_URI"#midi_sink"

#endif // _SYNTHPOD_LV2_H
