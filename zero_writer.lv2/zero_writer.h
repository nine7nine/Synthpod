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

#ifndef ZERO_WRITER_H
#define ZERO_WRITER_H

#include <stdint.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define ZERO_WRITER_URI "http://open-music-kontrollers.ch/lv2/zero-writer"
#define ZERO_WRITER_PREFIX ZERO_WRITER_URI "#"

#define ZERO_WRITER__schedule  ZERO_WRITER_PREFIX "schedule"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *Zero_Writer_Handle;

// ui-thread
typedef void *(*Zero_Writer_Request_Function)(Zero_Writer_Handle handle,
	uint32_t index, uint32_t minimum, size_t *maximum, uint32_t protocol);
typedef void (*Zero_Writer_Advance_Function)(Zero_Writer_Handle handle,
	uint32_t written);

typedef struct _Zero_Writer_Schedule {
	Zero_Writer_Request_Function request;
	Zero_Writer_Advance_Function advance;
	Zero_Writer_Handle handle;
} Zero_Writer_Schedule;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* ZERO_WRITER_H */
