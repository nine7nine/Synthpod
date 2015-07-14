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

#ifndef CLOCK_SYNC_H
#define CLOCK_SYNC_H

#include <stdint.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define CLOCK_SYNC_URI "http://open-music-kontrollers.ch/lv2/clock-sync"
#define CLOCK_SYNC_PREFIX CLOCK_SYNC_URI "#"

#define CLOCK_SYNC__schedule  CLOCK_SYNC_PREFIX "schedule"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *Clock_Sync_Handle;

// rt
typedef uint64_t (*Clock_Sync_Time2Frames_Function)(Clock_Sync_Handle handle,
	uint64_t time);
// rt
typedef uint64_t (*Clock_Sync_Frames2Time_Function)(Clock_Sync_Handle handle,
	uint64_t frames);
// non-rt
typedef uint64_t (*Clock_Sync_Frames_Function)(Clock_Sync_Handle handle);
// rt
typedef uint64_t (*Clock_Sync_Time_Function)(Clock_Sync_Handle handle);

typedef struct _Clock_Sync_Schedule {
	Clock_Sync_Time2Frames_Function time2frames;
	Clock_Sync_Frames2Time_Function frames2time;
	Clock_Sync_Time_Function time;
	Clock_Sync_Frames_Function frames;
	Clock_Sync_Handle handle;
} Clock_Sync_Schedule;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* CLOCK_SYNC_H */
