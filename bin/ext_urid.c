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

#include <ext_urid.h>

#include <stdatomic.h>
#include <Eina.h>

struct _ext_urid_t {
	LV2_URID cnt;

	atomic_flag lock;

	Eina_Hash *uris;
	Eina_Hash *urids;
	
	LV2_URID_Map map;
	LV2_URID_Unmap unmap;
};

static inline void
_spin_lock(ext_urid_t *ext_urid)
{
	while(atomic_flag_test_and_set_explicit(&ext_urid->lock, memory_order_acquire))
	{
		// spin
	}
	
}

static inline void
_spin_unlock(ext_urid_t *ext_urid)
{
	atomic_flag_clear_explicit(&ext_urid->lock, memory_order_release);
}

static void
_uri_hash_uri_del(void *data)
{
	LV2_URID *urid = data;
	free(urid);
}

static void
_uri_hash_urid_del(void *data)
{
	char *uri_dup = data;
	free(uri_dup);
}

static LV2_URID
_urid_map(LV2_URID_Map_Handle handle, const char *uri)
{
	ext_urid_t *uri_hash = handle;
	LV2_URID *urid = NULL;

	_spin_lock(uri_hash);

	// uri already registered?
	urid = eina_hash_find(uri_hash->uris, uri);

	_spin_unlock(uri_hash);

	if(urid)
		return *urid;

	// uri not yet registered

	// duplicate uri
	char *uri_dup = strdup(uri);
	if(!uri_dup)
	{
		fprintf(stderr, "_urid_map: out-of-memory\n");
		return 0;
	}

	// create new urid for uri
	urid = malloc(sizeof(LV2_URID));
	if(!urid)
	{
		free(uri_dup);
		fprintf(stderr, "_urid_map: out-of-memory\n");
		return 0;
	}

	_spin_lock(uri_hash);

	if(eina_hash_add(uri_hash->uris, uri_dup, urid))
	{
		*urid = uri_hash->cnt++;

		if(eina_hash_add(uri_hash->urids, urid, uri_dup))
		{
			_spin_unlock(uri_hash);
			return *urid;
		}
		else
		{
			eina_hash_del(uri_hash->uris, uri_dup, urid);
		}
	}

	_spin_unlock(uri_hash);

	fprintf(stderr, "_urid_map: map failed\n");

	free(urid);
	free(uri_dup);

	return 0;
}

static const char *
_urid_unmap(LV2_URID_Map_Handle handle, LV2_URID urid)
{
	ext_urid_t *uri_hash = handle;

	_spin_lock(uri_hash);

	const char *uri = eina_hash_find(uri_hash->urids, &urid);

	_spin_unlock(uri_hash);

	return uri;
}

ext_urid_t *
ext_urid_new()
{
	ext_urid_t *ext_urid = malloc(sizeof(ext_urid_t));
	if(!ext_urid)
		return NULL;

	atomic_flag_clear_explicit(&ext_urid->lock, memory_order_relaxed);
	ext_urid->cnt = 1;

	ext_urid->uris = eina_hash_string_superfast_new(_uri_hash_uri_del);
	if(ext_urid->uris)
	{
		ext_urid->urids = eina_hash_int32_new(_uri_hash_urid_del);
		if(ext_urid->urids)
		{
			ext_urid->map.handle = ext_urid;
			ext_urid->map.map = _urid_map;

			ext_urid->unmap.handle = ext_urid;
			ext_urid->unmap.unmap = _urid_unmap;

			return ext_urid;
		} // urids

		eina_hash_free(ext_urid->uris);
	} // uris

	free(ext_urid);
	return NULL;
}

void
ext_urid_free(ext_urid_t *ext_urid)
{
	if(!ext_urid)
		return;

	if(ext_urid->uris)
		eina_hash_free(ext_urid->uris);
	if(ext_urid->urids)
		eina_hash_free(ext_urid->urids);

	free(ext_urid);
}

LV2_URID_Map *
ext_urid_map_get(ext_urid_t *ext_urid)
{
	return &ext_urid->map;
}

LV2_URID_Unmap *
ext_urid_unmap_get(ext_urid_t *ext_urid)
{
	return &ext_urid->unmap;
}

LV2_URID
ext_urid_map(ext_urid_t *ext_urid, const char *uri)
{
	return ext_urid->map.map(ext_urid->map.handle, uri);
}

const char *
ext_urid_unmap(ext_urid_t *ext_urid, const LV2_URID urid)
{
	return ext_urid->unmap.unmap(ext_urid->unmap.handle, urid);
}
