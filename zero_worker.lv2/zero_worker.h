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

#ifndef ZERO_WORKER_H
#define ZERO_WORKER_H

#include <stdint.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define ZERO_WORKER_URI "http://open-music-kontrollers.ch/lv2/zero-worker"
#define ZERO_WORKER_PREFIX ZERO_WORKER_URI "#"

#define ZERO_WORKER__interface ZERO_WORKER_PREFIX "interface"
#define ZERO_WORKER__schedule  ZERO_WORKER_PREFIX "schedule"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ZERO_WORKER_SUCCESS       = 0,  // Completed successfully
	ZERO_WORKER_ERR_NO_SPACE  = 1   // Failed due to lack of space
} Zero_Worker_Status;

typedef void *Zero_Worker_Handle;

// non-rt thread
typedef void *(*Zero_Worker_Request_Function)(Zero_Worker_Handle handle,
	uint32_t minimum, size_t *maximum);
typedef Zero_Worker_Status (*Zero_Worker_Advance_Function)(Zero_Worker_Handle handle,
	uint32_t written);

typedef struct _Zero_Worker_Interface {
	// non-rt thread
	Zero_Worker_Status (*work)(LV2_Handle instance,
		Zero_Worker_Request_Function request,
		Zero_Worker_Advance_Function advance,
		Zero_Worker_Handle handle,
		uint32_t size, const void *body);

	// rt thread
	Zero_Worker_Status (*response)(LV2_Handle instance,
		uint32_t size, const void* body);
	Zero_Worker_Status (*end)(LV2_Handle instance);

} Zero_Worker_Interface;

typedef struct _Zero_Worker_Schedule {
	// rt thread
	Zero_Worker_Request_Function request;
	Zero_Worker_Advance_Function advance;
	Zero_Worker_Handle handle;

} Zero_Worker_Schedule;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* ZERO_WORKER_H */
