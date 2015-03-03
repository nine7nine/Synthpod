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

#ifndef _SYNTHPOD_EXT_WORKER_H
#define _SYNTHPOD_EXT_WORKER_H

#include <lv2/lv2plug.in/ns/ext/worker/worker.h>

typedef struct _ext_worker_t ext_worker_t;

ext_worker_t *
ext_worker_new();

void
ext_worker_free(ext_worker_t *worker);

#endif // _SYNTHPOD_EXT_WORKER_H
