/*
 * Copyright (c) 2018 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#ifndef _LFRTM_H
#define _LFRTM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifndef LFRTM_API
#	define LFRTM_API static
#endif

typedef struct _lfrtm_t lfrtm_t;

// non-rt
LFRTM_API lfrtm_t *
lfrtm_new(unsigned num, size_t minimum);

// non-rt
LFRTM_API void
lfrtm_free(lfrtm_t *lfrtm);

// non-rt
LFRTM_API int
lfrtm_inject(lfrtm_t *lfrtm);

// rt-safe
LFRTM_API void *
lfrtm_alloc(lfrtm_t *lfrtm, size_t size, bool *more);

#ifdef LFRTM_IMPLEMENTATION

struct _lfrtm_t {
	unsigned num;
	size_t size;
	size_t shift;
	size_t mask;
	atomic_size_t offset;
	atomic_uintptr_t pools [];
};

LFRTM_API lfrtm_t *
lfrtm_new(unsigned num, size_t minimum)
{
	lfrtm_t *lfrtm = calloc(1, sizeof(lfrtm_t) + num*sizeof(atomic_uintptr_t));
	if(!lfrtm)
	{
		return NULL;
	}

	lfrtm->num = num;

	for(lfrtm->shift = 0; lfrtm->shift < sizeof(size_t)*8; lfrtm->shift++)
	{
		const size_t sz = 1 << lfrtm->shift;

		if(sz < minimum)
		{
			continue;
		}

		lfrtm->size = sz;
		lfrtm->mask = sz - 1;
		break;
	}

	atomic_init(&lfrtm->offset, 0);
	atomic_init(&lfrtm->pools[0], UINTPTR_MAX);
	for(unsigned i = 1; i < lfrtm->num; i++)
	{
		atomic_init(&lfrtm->pools[i], 0);
	}

	if(lfrtm_inject(lfrtm) == 0)
	{
		return lfrtm;
	}

	free(lfrtm);
	return NULL;
}

LFRTM_API void
lfrtm_free(lfrtm_t *lfrtm)
{
	for(unsigned idx = 0; idx < lfrtm->num; idx++)
	{
		uintptr_t pool = atomic_exchange(&lfrtm->pools[idx], 0);
		if( (pool != 0) && (pool != UINTPTR_MAX) )
		{
			free((void *)pool);
		}
	}

	free(lfrtm);
}

LFRTM_API int
lfrtm_inject(lfrtm_t *lfrtm)
{
	void *pool = calloc(lfrtm->size, sizeof(uint8_t));
	if(!pool)
	{
		return -1;
	}

	for(unsigned idx = 0; idx < lfrtm->num; idx++)
	{
		const uintptr_t desired = (const uintptr_t)pool;
		uintptr_t expected = UINTPTR_MAX;
		const bool match = atomic_compare_exchange_strong(&lfrtm->pools[idx],
			&expected, desired);

		if(match) // we have successfully taken this slot first
		{
			return 0;
		}
		else if(expected == 0) // sentinel, e.g. nothing to do
		{
			break;
		}
	}

	free(pool);
	return -1; // double injection
}

LFRTM_API void *
lfrtm_alloc(lfrtm_t *lfrtm, size_t size, bool *more)
{
	if( (size > lfrtm->size) || !more)
	{
		return NULL;
	}

	*more = false;

	uintptr_t pool;
	size_t offset;
	unsigned idx;

	while(true)
	{
		offset = atomic_fetch_add(&lfrtm->offset, size);

		idx = offset >> lfrtm->shift; // number of overflows
		offset &= lfrtm->mask; // remainder

		if(idx >= lfrtm->num) // pool overflow
		{
			return NULL;
		}

		if(offset + size > lfrtm->size) // size does not fit in pool
		{
			continue; // e.g. skip to next pool
		}

		while(true)
		{
			pool = atomic_load(&lfrtm->pools[idx]);

			if(pool == UINTPTR_MAX) // injection ongoing
			{
				continue; // waiting for injection
			}
			else if(pool == 0) // missing injection request
			{
				return NULL;
			}

			break;
		}

		break;
	}

	if( (++idx < lfrtm->num) && (atomic_load(&lfrtm->pools[idx]) == 0) )
	{
		const uintptr_t desired = UINTPTR_MAX;
		uintptr_t expected = 0;
		*more = atomic_compare_exchange_strong(&lfrtm->pools[idx], &expected, desired);
	}

	return (void *)(pool + offset);
}

#endif // LFRTM_IMPLEMENTATION

#ifdef __cplusplus
}
#endif

#endif //_LFRTM_H
