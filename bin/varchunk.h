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

#ifndef _SYNTHPOD_VARCHUNK_H
#define _SYNTHPOD_VARCHUNK_H

typedef struct _varchunk_t varchunk_t;

varchunk_t *
varchunk_new(size_t minimum);

void
varchunk_free(varchunk_t *varchunk);

void *
varchunk_write_request(varchunk_t *varchunk, size_t minimum);

void
varchunk_write_advance(varchunk_t *varchunk, size_t written);

const void *
varchunk_read_request(varchunk_t *varchunk, size_t *toread);

void
varchunk_read_advance(varchunk_t *varchunk);

#endif //_SYNTHPOD_ATOM_CHUNK_H
