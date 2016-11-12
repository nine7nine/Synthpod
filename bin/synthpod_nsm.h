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

#include <uv.h>

typedef struct _synthpod_nsm_t synthpod_nsm_t;
typedef struct _synthpod_nsm_driver_t synthpod_nsm_driver_t;
	
typedef int (*synthpod_nsm_open_t)(const char *path, const char *name,
	const char *id, void *data);
typedef int (*synthpod_nsm_save_t)(void *data);
typedef int (*synthpod_nsm_show_t)(void *data);
typedef int (*synthpod_nsm_hide_t)(void *data);

struct _synthpod_nsm_driver_t {
	synthpod_nsm_open_t open;
	synthpod_nsm_save_t save;
	synthpod_nsm_show_t show;
	synthpod_nsm_hide_t hide;
};

synthpod_nsm_t *
synthpod_nsm_new(const char *exe, const char *path, uv_loop_t *loop,
	const synthpod_nsm_driver_t *driver, void *data);

void
synthpod_nsm_free(synthpod_nsm_t *nsm);

void
synthpod_nsm_opened(synthpod_nsm_t *nsm, int status);

void
synthpod_nsm_saved(synthpod_nsm_t *nsm, int status);

int
synthpod_nsm_managed(void);
