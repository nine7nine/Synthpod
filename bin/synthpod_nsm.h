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

typedef struct _synthpod_nsm_t synthpod_nsm_t;
typedef struct _synthpod_nsm_driver_t synthpod_nsm_driver_t;
	
typedef int (*synthpod_nsm_open_t)(const char *path, const char *name,
	const char *id, void *data);
typedef int (*synthpod_nsm_save_t)(void *data);

struct _synthpod_nsm_driver_t {
	synthpod_nsm_open_t open;
	synthpod_nsm_save_t save;
};

synthpod_nsm_t *
synthpod_nsm_new(const char *exe, const synthpod_nsm_driver_t *driver, void *data);

void
synthpod_nsm_free(synthpod_nsm_t *nsm);
