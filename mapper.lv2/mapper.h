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

#if !defined(MAPPER_SEED)
#	define MAPPER_SEED 12345
#endif

typedef struct _mapper_t mapper_t;

typedef char *(*mapper_alloc_t)(void *data, size_t size);
typedef void (*mapper_free_t)(void *data, char *uri);

MAPPER_API bool
mapper_is_lock_free(void);

MAPPER_API mapper_t *
mapper_new(uint32_t nitems, uint32_t nstats, const char **stats,
	mapper_alloc_t mapper_alloc_cb, mapper_free_t mapper_free_cb, void *data);

MAPPER_API void
mapper_free(mapper_t *mapper);

MAPPER_API uint32_t
mapper_get_usage(mapper_t *mapper);

MAPPER_API LV2_URID_Map *
mapper_get_map(mapper_t *mapper);

MAPPER_API LV2_URID_Unmap *
mapper_get_unmap(mapper_t *mapper);

#ifdef MAPPER_IMPLEMENTATION

#include <mapper.lv2/mum.h>

#if !defined(_WIN32)
#	include <sys/mman.h> // mlock
#endif

typedef struct _mapper_item_t mapper_item_t;

struct _mapper_item_t {
	atomic_uintptr_t val;
	uint32_t stat;
};

struct _mapper_t {
	uint32_t nitems;
	uint32_t nitems_mask;
	atomic_uint usage;

	mapper_alloc_t alloc;
	mapper_free_t free;
	void *data;

	LV2_URID_Map map;
	LV2_URID_Unmap unmap;

	uint32_t nstats;
	const char **stats;

	mapper_item_t items [];
};

static uint32_t
_mapper_map(void *data, const char *uri)
{
	if(!uri) // invalid URI
	{
		return 0;
	}

	char *uri_clone = NULL;
	const size_t uri_len = strlen(uri) + 1;
	mapper_t *mapper = data;
	const uint32_t hash = mum_hash(uri, uri_len - 1, MAPPER_SEED); // ignore zero terminator

	for(uint32_t i = 0, idx = (hash + i*i) & mapper->nitems_mask;
		i < mapper->nitems;
		i++, idx = (hash + i*i) & mapper->nitems_mask)
	{
		mapper_item_t *item = &mapper->items[idx];

		// find out if URI is already mapped
		const uintptr_t val = atomic_load_explicit(&item->val, memory_order_acquire);
		if(val != 0) // slot is already taken
		{
			if(memcmp((const char *)val, uri, uri_len) == 0) // URI is already mapped, use that
			{
				if(uri_clone)
				{
					mapper->free(mapper->data, uri_clone); // free superfluous URI
				}

				return item->stat ? item->stat : idx + mapper->nstats;
			}

			// slot is already taken by another URI, try next slot
			continue;
		}

		// clone URI for possible injection
		if(!uri_clone)
		{
			uri_clone = mapper->alloc(mapper->data, uri_len);

			if(!uri_clone) // out-of-memory
			{
				return 0;
			}

			memcpy(uri_clone, uri, uri_len);
		}

		// try to populate slot with newly mapped URI
		uintptr_t expected = 0;
		const uintptr_t desired = (uintptr_t)uri_clone;
		const bool match = atomic_compare_exchange_strong_explicit(&item->val,
			&expected, desired, memory_order_release, memory_order_relaxed);
		if(match) // we have successfully taken this slot first
		{
			atomic_fetch_add_explicit(&mapper->usage, 1, memory_order_relaxed);

			return item->stat ? item->stat : idx + mapper->nstats;
		}
		else if(memcmp((const char *)expected, uri, uri_len) == 0) // other thread stole it
		{
			mapper->free(mapper->data, uri_clone); // free superfluous URI

			return item->stat ? item->stat : idx + mapper->nstats;
		}

		// slot is already taken by another URI, try next slot
	}

	// item buffer overflow

	if(uri_clone)
	{
		mapper->free(mapper->data, uri_clone); // free superfluous URI
	}

	return 0;
}

static const char *
_mapper_unmap(void *data, uint32_t urid)
{
	mapper_t *mapper = data;

	if(urid == 0) // invalid URID
	{
		return NULL;
	}

	if(urid < mapper->nstats)
	{
		return mapper->stats[urid];
	}

	urid -= mapper->nstats;

	if(urid > mapper->nitems) // invalid URID
	{
		return NULL;
	}

	mapper_item_t *item = &mapper->items[urid];

	const uintptr_t val = atomic_load_explicit(&item->val, memory_order_relaxed);

	return (const char *)val;
}

static char *
_mapper_alloc_fallback(void *data, size_t size)
{
	(void)data;

	return malloc(size);
}

static void
_mapper_free_fallback(void *data, char *uri)
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
mapper_new(uint32_t nitems, uint32_t nstats, const char **stats,
	mapper_alloc_t mapper_alloc_cb, mapper_free_t mapper_free_cb, void *data)
{
	// item number needs to be a power of two
	uint32_t power_of_two = 1;
	while(power_of_two < nitems)
	{
		power_of_two <<= 1; // assure size to be a power of 2
	}

	// allocate mapper structure
	mapper_t *mapper = calloc(1, sizeof(mapper_t) + power_of_two*sizeof(mapper_item_t));
	if(!mapper) // allocation failed
	{
		return NULL;
	}

	// set mapper properties
	mapper->nitems = power_of_two;
	mapper->nitems_mask = power_of_two - 1;

	mapper->nstats = nstats;
	mapper->stats = stats;

	mapper->alloc = mapper_alloc_cb
		? mapper_alloc_cb
		: _mapper_alloc_fallback;
	mapper->free = mapper_free_cb
		? mapper_free_cb
		: _mapper_free_fallback;
	mapper->data = data;

	mapper->map.map = _mapper_map;
	mapper->map.handle = mapper;

	mapper->unmap.unmap = _mapper_unmap;
	mapper->unmap.handle = mapper;

	// initialize atomic usage counter
	atomic_init(&mapper->usage, 0);

	// initialize atomic variables of items
	for(uint32_t idx = 0; idx < mapper->nitems; idx++)
	{
		mapper_item_t *item = &mapper->items[idx];

		atomic_init(&item->val, 0);
		item->stat = 0;
	}

#if !defined(_WIN32)
	// lock memory
	mlock(mapper, sizeof(mapper_t) + mapper->nitems*sizeof(mapper_item_t));
#endif

	// populate static URIDs
	for(uint32_t i = 1; i < mapper->nstats; i++)
	{
		const char *uri = mapper->stats[i];

		const uint32_t urid = _mapper_map(mapper, uri);
		mapper_item_t *item = &mapper->items[urid - mapper->nstats];

		item->stat = i;
	}

	return mapper;
}

MAPPER_API void
mapper_free(mapper_t *mapper)
{
	// free URIs in item array with free function
	for(uint32_t idx = 0; idx < mapper->nitems; idx++)
	{
		mapper_item_t *item = &mapper->items[idx];

		// try to depopulate slot
		uintptr_t expected = 0;
		const uintptr_t desired = 0;
		const bool match = atomic_compare_exchange_strong_explicit(&item->val,
			&expected, desired, memory_order_release, memory_order_relaxed);
		if(!match) // we have successfully depopulated this slot first
		{
			atomic_fetch_sub_explicit(&mapper->usage, 1, memory_order_relaxed);
			mapper->free(mapper->data, (char *)expected);
		}
	}

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

MAPPER_API LV2_URID_Map *
mapper_get_map(mapper_t *mapper)
{
	return &mapper->map;
}

MAPPER_API LV2_URID_Unmap *
mapper_get_unmap(mapper_t *mapper)
{
	return &mapper->unmap;
}

#endif // MAPPER_IMPLEMENTATION

#ifdef __cplusplus
}
#endif

#endif //_MAPPER_H
