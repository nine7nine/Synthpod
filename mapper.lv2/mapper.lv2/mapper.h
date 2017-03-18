/*
 * Copyright (c) 2017 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#ifndef _MAPPER_H
#define _MAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <lv2/lv2plug.in/ns/ext/urid/urid.h>

#ifndef MAPPER_API
#	define MAPPER_API static
#endif

typedef struct _mapper_pool_t mapper_pool_t;
typedef struct _mapper_t mapper_t;

typedef char *(*mapper_pool_alloc_t)(void *data, size_t size);
typedef void (*mapper_pool_free_t)(void *data, char *uri);

struct _mapper_pool_t {
	mapper_t *mapper;
	mapper_pool_alloc_t alloc;
	mapper_pool_free_t free;
	void *data;

	LV2_URID_Map map;
	LV2_URID_Unmap unmap;
};

MAPPER_API bool
mapper_is_lock_free(void);

MAPPER_API mapper_t *
mapper_new(uint32_t nitems);

MAPPER_API void
mapper_free(mapper_t *mapper);

MAPPER_API uint32_t
mapper_get_usage(mapper_t *mapper);

MAPPER_API void
mapper_pool_init(mapper_pool_t *mapper_pool, mapper_t *mapper,
	mapper_pool_alloc_t mapper_pool_alloc, mapper_pool_free_t mapper_pool_free,
	void *data);

MAPPER_API void
mapper_pool_deinit(mapper_pool_t *mapper_pool);

MAPPER_API LV2_URID_Map *
mapper_pool_get_map(mapper_pool_t *mapper_pool);

MAPPER_API LV2_URID_Unmap *
mapper_pool_get_unmap(mapper_pool_t *mapper_pool);

#ifdef MAPPER_IMPLEMENTATION

#if !defined(_WIN32)
#	include <sys/mman.h> // mlock
#endif

typedef struct _mapper_item_t mapper_item_t;

struct _mapper_item_t {
	atomic_uintptr_t val;
	mapper_pool_t *mapper_pool;
};

struct _mapper_t {
	uint32_t nitems;
	uint32_t nitems_mask;
	atomic_uint usage;
	mapper_item_t items [0];
};

/*
 * MurmurHash3 was created by Austin Appleby  in 2008. The initial
 * implementation was published in C++ and placed in the public.
 *   https://sites.google.com/site/murmurhash/
 * Seungyoung Kim has ported its implementation into C language
 * in 2012 and published it as a part of qLibc component.
 *
 * Copyright (c) 2010-2015 Seungyoung Kim.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/
static inline uint32_t
_mapper_murmur3_32(const void *data, size_t nbytes)
{
	const uint32_t c1 = 0xcc9e2d51;
	const uint32_t c2 = 0x1b873593;

	const int nblocks = nbytes / 4;
	const uint32_t *blocks = (const uint32_t *)(data);
	const uint8_t *tail = (const uint8_t *)(data + (nblocks * 4));

	uint32_t h = 0;

	uint32_t k;
	for(int i = 0; i < nblocks; i++)
	{
		k = blocks[i];

		k *= c1;
		k = (k << 15) | (k >> (32 - 15));
		k *= c2;

		h ^= k;
		h = (h << 13) | (h >> (32 - 13));
		h = (h * 5) + 0xe6546b64;
	}

	k = 0;
	switch(nbytes & 3)
	{
		case 3:
			k ^= tail[2] << 16;
		case 2:
			k ^= tail[1] << 8;
		case 1:
			k ^= tail[0];
			k *= c1;
			k = (k << 15) | (k >> (32 - 15));
			k *= c2;
			h ^= k;
	};

	h ^= nbytes;

	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;

	return h;
}

static uint32_t
_mapper_pool_map(void *data, const char *uri)
{
	if(!uri) // invalid URI
		return 0;

	const size_t uri_len = strlen(uri) + 1;
	mapper_pool_t *mapper_pool = data;
	mapper_t *mapper = mapper_pool->mapper;
	const uint32_t hash = _mapper_murmur3_32(uri, uri_len - 1); // ignore zero terminator

	for(uint32_t i = 0, idx = (hash + i) & mapper->nitems_mask;
		i < mapper->nitems;
		i++, idx = (hash + i) & mapper->nitems_mask)
	{
		mapper_item_t *item = &mapper->items[idx];

		// find out if URI is already mapped
		const uintptr_t val = atomic_load_explicit(&item->val, memory_order_acquire);
		if(val != 0) // slot is already taken
		{
			if(memcmp((const char *)val, uri, uri_len) == 0) // URI is already mapped, use that
				return idx + 1;

			// slot is already taken by another URI, try next slot
			continue;
		}

		// clone URI for possible injection
		char *uri_clone = mapper_pool->alloc(mapper_pool->data, uri_len);
		if(!uri_clone) // allocation failed
			return 0;
		memcpy(uri_clone, uri, uri_len);
		const uintptr_t desired = (uintptr_t)uri_clone;

		// try to populate slot with newly mapped URI
		uintptr_t expected = 0;
		const bool match = atomic_compare_exchange_strong_explicit(&item->val,
			&expected, desired, memory_order_release, memory_order_relaxed);
		if(match) // we have successfully taken this slot first
		{
			atomic_fetch_add_explicit(&mapper->usage, 1, memory_order_relaxed);
			item->mapper_pool = mapper_pool; // set owning pool
			return idx + 1;
		}

		mapper_pool->free(mapper_pool->data, uri_clone); // free superfluous URI

		if(memcmp((const char *)expected, uri, uri_len) == 0) // other thread stole it
			return idx + 1;

		// slot is already taken by another URI, try next slot
	}

	return 0; // item buffer overflow
}

static const char *
_mapper_pool_unmap(void *data, uint32_t idx)
{
	mapper_pool_t *mapper_pool = data;
	mapper_t *mapper = mapper_pool->mapper;

	if( (idx == 0) || (idx > mapper->nitems)) // invalid URID
		return NULL;

	mapper_item_t *item = &mapper->items[idx - 1];

	const uintptr_t val = atomic_load_explicit(&item->val, memory_order_relaxed);

	return (const char *)val;
}

static char *
_mapper_pool_alloc_fallback(void *data, size_t size)
{
	(void)data;
	return malloc(size);
}

static void
_mapper_pool_free_fallback(void *data, char *uri)
{
	(void)data;
	free(uri);
}

MAPPER_API bool
mapper_is_lock_free(void)
{
	atomic_uintptr_t val;

	return atomic_is_lock_free(&val);
}

MAPPER_API mapper_t *
mapper_new(uint32_t nitems)
{
	// item number needs to be a power of two
	uint32_t power_of_two = 1;
	while(power_of_two < nitems)
		power_of_two <<= 1; // assure size to be a power of 2

	// allocate mapper structure
	mapper_t *mapper = calloc(1, sizeof(mapper_t) + power_of_two*sizeof(mapper_item_t));
	if(!mapper) // allocation failed
		return NULL;

	// set mapper properties
	mapper->nitems = power_of_two;
	mapper->nitems_mask = power_of_two - 1;

	// initialize atomic usage counter
	atomic_init(&mapper->usage, 0);

	// initialize atomic variables of items
	for(uint32_t idx = 0; idx < mapper->nitems; idx++)
	{
		mapper_item_t *item = &mapper->items[idx];

		atomic_init(&item->val, 0);
	}

#if !defined(_WIN32)
	// lock memory
	mlock(mapper, sizeof(mapper_t) + mapper->nitems*sizeof(mapper_item_t));
#endif

	return mapper;
}

MAPPER_API void
mapper_free(mapper_t *mapper)
{
#if !defined(_WIN32)
	// unlock memory
	munlock(mapper, sizeof(mapper_t) + mapper->nitems*sizeof(mapper_item_t));
#endif

	free(mapper);
}

MAPPER_API uint32_t
mapper_get_usage(mapper_t *mapper)
{
	return atomic_load_explicit(&mapper->usage, memory_order_relaxed);
}

MAPPER_API void
mapper_pool_init(mapper_pool_t *mapper_pool, mapper_t *mapper,
	mapper_pool_alloc_t mapper_pool_alloc, mapper_pool_free_t mapper_pool_free,
	void *data)
{
	mapper_pool->mapper = mapper;
	mapper_pool->alloc = mapper_pool_alloc
		? mapper_pool_alloc
		: _mapper_pool_alloc_fallback;
	mapper_pool->free = mapper_pool_free
		? mapper_pool_free
		: _mapper_pool_free_fallback;
	mapper_pool->data = data;

	mapper_pool->map.map = _mapper_pool_map;
	mapper_pool->map.handle = mapper_pool;

	mapper_pool->unmap.unmap = _mapper_pool_unmap;
	mapper_pool->unmap.handle = mapper_pool;
}

MAPPER_API void
mapper_pool_deinit(mapper_pool_t *mapper_pool)
{
	mapper_t *mapper = mapper_pool->mapper;

	// free URIs in item array with owning pool's free function
	for(uint32_t idx = 0; idx < mapper->nitems; idx++)
	{
		mapper_item_t *item = &mapper->items[idx];
		if(item->mapper_pool != mapper_pool) // item not owned by this pool
			continue;

		const uintptr_t val = atomic_load_explicit(&item->val,
			memory_order_relaxed);
		if(val != 0) // slot is populated by a URI
		{
			atomic_fetch_sub_explicit(&mapper->usage, 1, memory_order_relaxed);
			mapper_pool->free(mapper_pool->data, (char *)val);
		}
	}
}

MAPPER_API LV2_URID_Map *
mapper_pool_get_map(mapper_pool_t *mapper_pool)
{
	return &mapper_pool->map;
}

MAPPER_API LV2_URID_Unmap *
mapper_pool_get_unmap(mapper_pool_t *mapper_pool)
{
	return &mapper_pool->unmap;
}

#endif // MAPPER_IMPLEMENTATION

#ifdef __cplusplus
}
#endif

#endif //_MAPPER_H
