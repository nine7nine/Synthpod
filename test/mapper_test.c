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
#include <time.h>
#include <math.h>

#include "random.c"

#define MAX_URI_LEN 46
#define MAX_ITEMS 0x100000 // 1M
#define USE_STATS

#if defined(USE_STATS)
#	include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#endif

typedef struct _rtmem_slot_t rtmem_slot_t;
typedef struct _rtmem_t rtmem_t;
typedef struct _nrtmem_t nrtmem_t;
typedef struct _pool_t pool_t;

// test URIs are of fixed length
struct _rtmem_slot_t {
	char uri [MAX_URI_LEN];
};

// dummy rt memory structure
struct _rtmem_t {
	atomic_uint nalloc; // counts number of allocations
	atomic_uint nfree; // counts number of frees
	rtmem_slot_t slots []; // contains slots as multiple of MAX_ITEMS
};

// dummy non-rt memory structure
struct _nrtmem_t {
	atomic_uint nalloc; // counts number of allocations
	atomic_uint nfree; // counts number of frees
};

// per-thread properties
struct _pool_t {
	mapper_t *mapper;
	pthread_t thread;
	MT mersenne;
};

enum {
	lv2_invalid = 0,

#if defined(USE_STATS)
	lv2_atom_Atom,
	lv2_atom_AtomPort,
	lv2_atom_Blank,
	lv2_atom_Bool,
	lv2_atom_Chunk,
	lv2_atom_Double,
#endif

	nstats	
};

static const char *stats [nstats]  = {
	[lv2_invalid]         = NULL,

#if defined(USE_STATS)
	[lv2_atom_Atom]       = LV2_ATOM__Atom,
	[lv2_atom_AtomPort]   = LV2_ATOM__AtomPort,
	[lv2_atom_Blank]      = LV2_ATOM__Blank,
	[lv2_atom_Bool]       = LV2_ATOM__Bool,
	[lv2_atom_Chunk]      = LV2_ATOM__Chunk,
	[lv2_atom_Double]     = LV2_ATOM__Double
#endif
};

static rtmem_t *
rtmem_new(uint32_t rpools)
{
	// create as many slots as worst case scenario dictates it
	rtmem_t *rtmem = calloc(1, sizeof(rtmem_t) + (rpools*MAX_ITEMS)*sizeof(rtmem_slot_t));
	if(!rtmem)
	{
		return NULL;
	}

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
	(void)size;

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

static void
nrtmem_init(nrtmem_t *nrtmem)
{
	atomic_init(&nrtmem->nalloc, 0);
	atomic_init(&nrtmem->nfree, 0);
}

static char *
_nrtmem_alloc(void *data, size_t size)
{
	nrtmem_t *nrtmem = data;

	atomic_fetch_add_explicit(&nrtmem->nalloc, 1, memory_order_relaxed);
	return malloc(size);
}

static void
_nrtmem_free(void *data, char *uri)
{
	nrtmem_t *nrtmem = data;

	atomic_fetch_add_explicit(&nrtmem->nfree, 1, memory_order_relaxed);
	free(uri);
}

// threads should start (un)mapping at the same time
static atomic_bool rolling = ATOMIC_VAR_INIT(false);

static void *
_thread(void *data)
{
	pool_t *pool = data;
	mapper_t *mapper = pool->mapper;
	LV2_URID_Map *map = mapper_get_map(mapper);
	LV2_URID_Unmap *unmap = mapper_get_unmap(mapper);

	while(!atomic_load_explicit(&rolling, memory_order_relaxed))
	{} // wait for go signal

	// test static URIDs
	for(uint32_t i = 1; i < nstats; i++)
	{
		const char *uri = stats[i];

		const uint32_t urid = map->map(map->handle, uri);
		assert(urid);
		assert(urid == i);

		const char *dst = unmap->unmap(unmap->handle, i);
		assert(dst);
		assert(strcmp(dst, uri) == 0);
	}

	char uri [MAX_URI_LEN];
	for(uint32_t i = 0; i < MAX_ITEMS/2; i++)
	{
		// generate UUID version 4 URN via mersenne twister random number generator
		{
			union {
				uint8_t bytes [0x10];
				uint32_t u32s [0x4];
			} un;

			for(unsigned i=0x0; i<0x4; i++)
			{
				un.u32s[i] = genrand_int32(&pool->mersenne);
			}

			un.bytes[6] = (un.bytes[6] & 0x0f) | 0x40; // set four most significant bits of 7th byte to 0b0100
			un.bytes[8] = (un.bytes[8] & 0x3f) | 0x80; // set two most significant bits of 9th byte to 0b10

			snprintf(uri, MAX_URI_LEN,
				"urn:uuid:%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
				un.bytes[0x0], un.bytes[0x1], un.bytes[0x2], un.bytes[0x3],
				un.bytes[0x4], un.bytes[0x5],
				un.bytes[0x6], un.bytes[0x7],
				un.bytes[0x8], un.bytes[0x9],
				un.bytes[0xa], un.bytes[0xb], un.bytes[0xc], un.bytes[0xd], un.bytes[0xe], un.bytes[0xf]);
		}

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
	static nrtmem_t nrtmem;

	assert(mapper_is_lock_free());

	assert(argc > 2);
	const uint32_t n = atoi(argv[1]); // number of concurrent threads
	const bool is_rt = atoi(argv[2]); // whether to use rt-memory

	const uint64_t seed = (argc == 4) // get seed from command line or from time
		? atol(argv[3])
		: time(NULL);

	// create rt memory
	rtmem_t *rtmem = rtmem_new(n);
	assert(rtmem);

	// initialize non-rt memory
	nrtmem_init(&nrtmem);

	// create mapper
	mapper_t *mapper = is_rt
		? mapper_new(MAX_ITEMS, nstats, stats, _rtmem_alloc, _rtmem_free, rtmem)
		: mapper_new(MAX_ITEMS, nstats, stats, _nrtmem_alloc, _nrtmem_free, &nrtmem);
	assert(mapper);

	// create array of threads
	pool_t *pools = calloc(n, sizeof(pool_t));
	assert(pools);

	(void)genrand_res53; // to make pedantic compiler happy

	// init/start threads
	for(uint32_t p = 0; p < n; p++)
	{
		pool_t *pool = &pools[p];

		pool->mapper = mapper;
		init_genrand(&pool->mersenne, seed);
		pthread_create(&pool->thread, NULL, _thread, pool);
	}

	// signal rolling
	atomic_store_explicit(&rolling, true, memory_order_relaxed);

	// stop threads
	for(uint32_t p = 0; p < n; p++)
	{
		pool_t *pool = &pools[p];

		pthread_join(pool->thread, NULL);
	}

	// query usage
	const uint32_t usage = mapper_get_usage(mapper);
	assert(usage == MAX_ITEMS/2 + (nstats - 1));

	// query rt memory allocations and frees
	const uint32_t rt_nalloc = atomic_load_explicit(&rtmem->nalloc, memory_order_relaxed);
	const uint32_t rt_nfree = atomic_load_explicit(&rtmem->nfree, memory_order_relaxed);

	// query non-rt memory allocations and frees
	const uint32_t nrt_nalloc = atomic_load_explicit(&nrtmem.nalloc, memory_order_relaxed);
	const uint32_t nrt_nfree = atomic_load_explicit(&nrtmem.nfree, memory_order_relaxed);

	// check whether combined allocations and frees match usage
	const uint32_t tot_nalloc = rt_nalloc + nrt_nalloc;
	const uint32_t tot_nfree = rt_nfree + nrt_nfree;
	assert(tot_nalloc - tot_nfree == usage);

	// distribution of fills/gaps
	uint32_t fill_min = UINT32_MAX;
	uint32_t fill_max = 0;
	double fill_avg = 0;
	uint32_t fill_n = 0;
	for(uint32_t idx = 0, from = 0; idx < mapper->nitems; idx++)
	{
		mapper_item_t *item = &mapper->items[idx];

		if(atomic_load_explicit(&item->val, memory_order_relaxed) == 0) // a gap is starting
		{
			const uint32_t fill = idx - from; // length of preceding fill

			if(fill == 0)
			{
				continue; // ignore zero-length fills
			}

			if(fill < fill_min)
			{
				fill_min = fill;
			}

			if(fill > fill_max)
			{
				fill_max = fill;
			}

			fill_avg += fill;
			fill_n += 1;
			from = idx;
		}
	}

	fill_avg /= fill_n;

	double fill_dev = 0;
	for(uint32_t idx = 0, from = 0; idx < mapper->nitems; idx++)
	{
		mapper_item_t *item = &mapper->items[idx];

		if(atomic_load_explicit(&item->val, memory_order_relaxed) == 0) // a gap is starting
		{
			const uint32_t fill = idx - from; // length of preceding fill

			if(fill == 0)
			{
				continue; // ignore zero-length fills
			}

			const uint32_t fill_diff = fill - fill_avg;
			fill_dev += fill_diff*fill_diff;
			from = idx;
		}
	}

	fill_dev = sqrt(fill_dev / (fill_n - 1));

	// free mapper
	mapper_free(mapper);

	// free threads
	free(pools);

	// check if all rt memory has been cleared
	for(uint32_t i = 0; i < rt_nalloc; i++)
	{
		rtmem_slot_t *slot = &rtmem->slots[i];

		assert(memcmp(slot->uri, zeros, MAX_URI_LEN) == 0);
	}

	// free rt memory
	rtmem_free(rtmem);

	fprintf(stderr,
		"  fill-min  : %"PRIu32"\n"
		"  fill-max  : %"PRIu32"\n"
		"  fill-avg  : %.2lf\n"
		"  fill-dev  : %.2lf\n"
		"  rt-allocs : %"PRIu32"\n"
		"  rt-frees  : %"PRIu32"\n"
		"  nrt-allocs: %"PRIu32"\n"
		"  nrt-frees : %"PRIu32"\n"
		"  collisions: %"PRIu32" (%.1f%% of total allocations -> +%.1f%% allocation overhead)\n",
		fill_min, fill_max, fill_avg, fill_dev,
		rt_nalloc, rt_nfree, nrt_nalloc, nrt_nfree,
		tot_nfree, 100.f * tot_nfree / tot_nalloc, 100.f * tot_nfree / usage);

	return 0;
}
