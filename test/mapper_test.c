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

#define MAPPER_IMPLEMENTATION
#include <mapper.lv2/mapper.h>

#include <stdio.h>
#include <stddef.h>
#include <inttypes.h>
#include <pthread.h>
#include <assert.h>

#define MAX_URI_LEN 16
#define MAX_ITEMS 0x100000 // 1M

typedef struct _rtmem_slot_t rtmem_slot_t;
typedef struct _rtmem_t rtmem_t;

// test URIs are of fixed length
struct _rtmem_slot_t {
	char uri [MAX_URI_LEN];
};

// dummy rt memory structure
struct _rtmem_t {
	atomic_uint nalloc; // counts number allocations
	atomic_uint nfree; // counts number of frees
	rtmem_slot_t slots [0]; // contains slots as multiple of MAX_ITEMS
};

static rtmem_t *
rtmem_new(uint32_t rpools)
{
	// create as many slots as worst case scenario dictates it
	rtmem_t *rtmem = calloc(1, sizeof(rtmem_t) + (rpools*MAX_ITEMS)*sizeof(rtmem_slot_t));
	if(!rtmem)
		return NULL;

	atomic_init(&rtmem->nalloc, 0);
	atomic_init(&rtmem->nfree, 0);

	return rtmem;
}

static void
rtmem_free(rtmem_t *rtmem)
{
	free(rtmem);
}

static char *
_rtmem_alloc(void *data, size_t size)
{
	rtmem_t *rtmem = data;

	// dummily just take the next slot according to allocation counter
	const uint32_t nalloc = atomic_fetch_add_explicit(&rtmem->nalloc, 1, memory_order_relaxed);
	return rtmem->slots[nalloc].uri;
}

static void
_rtmem_free(void *data, char *uri)
{
	rtmem_t *rtmem = data;

	// increase free counter (to decipher collisions later)
	atomic_fetch_add_explicit(&rtmem->nfree, 1, memory_order_relaxed);
	// clear uri buffer
	memset(uri, 0x0, MAX_URI_LEN);
}

// threads should start (un)mapping at the same time
static atomic_bool rolling = ATOMIC_VAR_INIT(false);

static void *
_thread(void *data)
{
	mapper_pool_t *mapper_pool = data;
	LV2_URID_Map *map = mapper_pool_get_map(mapper_pool);
	LV2_URID_Unmap *unmap = mapper_pool_get_unmap(mapper_pool);

	while(!atomic_load_explicit(&rolling, memory_order_relaxed))
	{} // wait for go signal

	char uri [MAX_URI_LEN];
	for(uint32_t i = 0; i < MAX_ITEMS/2; i++)
	{
		snprintf(uri, MAX_URI_LEN, "urn:hx:%08"PRIx32, i);

		const uint32_t urid1 = map->map(map->handle, uri);
		assert(urid1);
		const char *dst = unmap->unmap(unmap->handle, urid1);
		assert(dst);
		assert(strcmp(dst, uri) == 0);
		const uint32_t urid2 = map->map(map->handle, uri);
		assert(urid2);
		assert(urid1 == urid2);
	}

	return NULL;
}

int
main(int argc, char **argv)
{
	static char zeros [MAX_URI_LEN];

	assert(argc > 2);
	const uint32_t n = atoi(argv[1]); // number of concurrent threads
	const uint32_t r = atoi(argv[2]); // number of threads using rt memory
	assert(r <= n);

	// create rt memory
	rtmem_t *rtmem = rtmem_new(r);
	assert(rtmem);

	// create mapper
	mapper_t *mapper = mapper_new(n, MAX_ITEMS);
	assert(mapper);

	// create array of threads
	pthread_t *pools = calloc(n, sizeof(pthread_t));
	assert(pools);

	// init/start threads
	for(uint32_t pos = 0; pos < n; pos++)
	{
		mapper_pool_t *mapper_pool = mapper_get_pool(mapper, pos);
		assert(mapper_pool);

		if(pos < r) // let thread use real-time memory
			mapper_pool_set_alloc(mapper_pool, _rtmem_alloc, _rtmem_free, rtmem);

		pthread_create(&pools[pos], NULL, _thread, mapper_pool);
	}

	// signal rolling
	atomic_store_explicit(&rolling, true, memory_order_relaxed);

	// stop threads
	for(uint32_t pos = 0; pos < n; pos++)
		pthread_join(pools[pos], NULL);

	// free threads
	free(pools);

	// query rt memory allocations and frees
	const uint32_t nalloc = atomic_load_explicit(&rtmem->nalloc, memory_order_relaxed);
	const uint32_t nfree = atomic_load_explicit(&rtmem->nfree, memory_order_relaxed);

	// free mapper
	mapper_free(mapper);

	// check if all rt memory has been cleared
	for(uint32_t i = 0; i < nalloc; i++)
	{
		rtmem_slot_t *slot = &rtmem->slots[i];

		assert(memcmp(slot->uri, zeros, MAX_URI_LEN) == 0);
	}

	// free rt memory
	rtmem_free(rtmem);

	// report rt memory allocations and collisions
	printf("\rt-allocs: %.1f%%, rt-collisions: %.1f%%\n",
		200.f * (nalloc - nfree) / MAX_ITEMS,
		200.f * nfree / MAX_ITEMS);

	return 0;
}
