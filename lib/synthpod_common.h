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

#ifndef _SYNTHPOD_COMMON_H
#define _SYNTHPOD_COMMON_H

#define SYNTHPOD_PREFIX				"http://open-music-kontrollers.ch/lv2/synthpod#"

#ifdef _WIN32
#	define SYNTHPOD_SYMBOL_EXTERN __declspec(dllexport)
#else
#	define SYNTHPOD_SYMBOL_EXTERN __attribute__((visibility("default")))
#endif

typedef int64_t xpress_uuid_t;
typedef struct _xpress_map_t xpress_map_t;
typedef xpress_uuid_t (*xpress_map_new_uuid_t)(void *handle);

struct _xpress_map_t {
	void *handle;
	xpress_map_new_uuid_t new_uuid;
};

#endif // _SYNTHPOD_COMMON_H
