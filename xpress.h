/*
 * Copyright (c) 2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the voiceied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#ifndef _LV2_XPRESS_H_
#define _LV2_XPRESS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/patch/patch.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>

/*****************************************************************************
 * API START
 *****************************************************************************/

#define XPRESS_PREFIX				"http://open-music-kontrollers.ch/lv2/xpress#"

// Features
#define XPRESS_VOICE_MAP		XPRESS_PREFIX"voiceMap"

// Properties
#define XPRESS_ZONE					XPRESS_PREFIX"zone"
#define XPRESS_PITCH				XPRESS_PREFIX"pitch"
#define XPRESS_PRESSURE			XPRESS_PREFIX"pressure"
#define XPRESS_TIMBRE				XPRESS_PREFIX"timbre"

// enumerations
typedef enum _xpress_event_t xpress_event_t;

// structures
typedef struct _xpress_voice_t xpress_voice_t;
typedef struct _xpress_t xpress_t;

// function callbacks
typedef void (*xpress_event_cb_t)(
	void *data,
	LV2_Atom_Forge *forge,
	int64_t frames,
	xpress_event_t event,
	xpress_voice_t *voice);

enum _xpress_event_t {
	XPRESS_EVENT_ALLOC			= (1 << 0),
	XPRESS_EVENT_FREE				= (1 << 1),
	XPRESS_EVENT_PUT				= (1 << 2)
};

#define XPRESS_EVENT_NONE		(0)
#define XPRESS_EVENT_ALL		(XPRESS_EVENT_ALLOC | XPRESS_EVENT_FREE | XPRESS_EVENT_PUT)

struct _xpress_voice_t {
	uint32_t pos;
	LV2_URID subject;

	int32_t zone;
	float pitch;
	float pressure;
	float timbre;
};

struct _xpress_t {
	struct {
		LV2_URID patch_get;
		LV2_URID patch_set;
		LV2_URID patch_delete;
		LV2_URID patch_put;
		LV2_URID patch_subject;
		LV2_URID patch_body;
		LV2_URID patch_property;
		LV2_URID patch_value;

		LV2_URID atom_int;
		LV2_URID atom_long;
		LV2_URID atom_float;
		LV2_URID atom_double;
		LV2_URID atom_bool;
		LV2_URID atom_urid;

		LV2_URID xpress_zone;
		LV2_URID xpress_pitch;
		LV2_URID xpress_pressure;
		LV2_URID xpress_timbre;
	} urid;

	LV2_URID_Map *map;

	xpress_event_t event_mask;
	xpress_event_cb_t event_cb;
	void *data;

	unsigned max_nvoices;
	unsigned nvoices;
	xpress_voice_t voices [0];
};

#define XPRESS_T(XPRESS, MAX_NVOICES) \
	xpress_t (XPRESS); \
	xpress_voice_t _voices [(MAX_NVOICES)];

#define XPRESS_VOICE_FOREACH(XPRESS, VOICE) \
	for(xpress_voice_t *VOICE = (XPRESS)->voices; VOICE - (XPRESS)->voices < (XPRESS)->nvoices; VOICE++)

// rt-safe
static inline int
xpress_init(xpress_t *xpress, const size_t max_nvoices, const char *subject,
	LV2_URID_Map *map, xpress_event_t event_mask, xpress_event_cb_t event_cb, void *data);

// rt-safe
static inline int
xpress_advance(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames,
	const LV2_Atom_Object *obj, LV2_Atom_Forge_Ref *ref);

// rt-safe
static inline LV2_URID
xpress_alloc(xpress_t *xpress);

// rt-safe
static inline void 
xpress_realloc(xpress_t *xpress, LV2_URID subject);

// rt-safe
static inline int
xpress_free(xpress_t *xpress, LV2_URID subject);

// rt-safe
static inline LV2_Atom_Forge_Ref
xpress_new(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames, LV2_URID subject);

// rt-safe
static inline LV2_Atom_Forge_Ref
xpress_del(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames, LV2_URID subject);

// rt-safe
static inline LV2_Atom_Forge_Ref
xpress_put(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames, LV2_URID subject);

/*****************************************************************************
 * API END
 *****************************************************************************/

static LV2_Atom_Forge_Ref
_xpress_int_get_cb(LV2_Atom_Forge *forge, const void *value)
{
	return lv2_atom_forge_int(forge, *(const int32_t *)value);
}
static void
_xpress_int_set_cb(xpress_t *xpress, void *value, LV2_URID new_type, const void *new_value)
{
	int32_t *ref = value;

	if(new_type == xpress->urid.atom_int)
		*ref = *(const int32_t *)new_value;
	else if(new_type == xpress->urid.atom_bool)
		*ref = *(const int32_t *)new_value;
	else if(new_type == xpress->urid.atom_urid)
		*ref = *(const uint32_t *)new_value;
	else if(new_type == xpress->urid.atom_long)
		*ref = *(const int64_t *)new_value;

	else if(new_type == xpress->urid.atom_float)
		*ref = *(const float *)new_value;
	else if(new_type == xpress->urid.atom_double)
		*ref = *(const double *)new_value;
}

static LV2_Atom_Forge_Ref
_xpress_float_get_cb(LV2_Atom_Forge *forge, const void *value)
{
	return lv2_atom_forge_float(forge, *(const float *)value);
}
static void
_xpress_float_set_cb(xpress_t *xpress, void *value, LV2_URID new_type, const void *new_value)
{
	float *ref = value;

	if(new_type == xpress->urid.atom_float)
		*ref = *(const float *)new_value;
	else if(new_type == xpress->urid.atom_double)
		*ref = *(const double *)new_value;

	else if(new_type == xpress->urid.atom_int)
		*ref = *(const int32_t *)new_value;
	else if(new_type == xpress->urid.atom_bool)
		*ref = *(const int32_t *)new_value;
	else if(new_type == xpress->urid.atom_urid)
		*ref = *(const uint32_t *)new_value;
	else if(new_type == xpress->urid.atom_long)
		*ref = *(const int64_t *)new_value;
}

static inline int
_signum(LV2_URID urid1, LV2_URID urid2)
{
	if(urid1 < urid2)
		return 1;
	else if(urid1 > urid2)
		return -1;
	
	return 0;
}

static int
_voice_sort(const void *itm1, const void *itm2)
{
	const xpress_voice_t *voice1 = itm1;
	const xpress_voice_t *voice2 = itm2;

	return _signum(voice1->subject, voice2->subject);
}

static int
_voice_search(const void *itm1, const void *itm2)
{
	const LV2_URID *subject = itm1;
	const xpress_voice_t *voice = itm2;

	return _signum(*subject, voice->subject);
}

static inline xpress_voice_t *
_xpress_voice_search(xpress_t *xpress, LV2_URID subject)
{
	return bsearch(&subject, xpress->voices, xpress->nvoices, sizeof(xpress_voice_t), _voice_search);
}

static inline LV2_Atom_Forge_Ref
_xpress_put(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames, xpress_voice_t *voice)
{
	LV2_Atom_Forge_Frame obj_frame;
	LV2_Atom_Forge_Frame body_frame;

	LV2_Atom_Forge_Ref ref = lv2_atom_forge_frame_time(forge, frames);

	if(ref)
		ref = lv2_atom_forge_object(forge, &obj_frame, 0, xpress->urid.patch_put);
	{
		if(ref)
			ref = lv2_atom_forge_key(forge, xpress->urid.patch_subject);
		if(ref)
			ref = lv2_atom_forge_urid(forge, voice->subject);

		if(ref)
			ref = lv2_atom_forge_key(forge, xpress->urid.patch_body);
		if(ref)
			ref = lv2_atom_forge_object(forge, &body_frame, 0, 0);
		{
			if(ref)
				ref = lv2_atom_forge_key(forge, xpress->urid.xpress_zone);
			if(ref)
				ref = lv2_atom_forge_int(forge, voice->zone);

			if(ref)
				ref = lv2_atom_forge_key(forge, xpress->urid.xpress_pitch);
			if(ref)
				ref = lv2_atom_forge_float(forge, voice->pitch);

			if(ref)
				ref = lv2_atom_forge_key(forge, xpress->urid.xpress_pressure);
			if(ref)
				ref = lv2_atom_forge_float(forge, voice->pressure);

			if(ref)
				ref = lv2_atom_forge_key(forge, xpress->urid.xpress_timbre);
			if(ref)
				ref = lv2_atom_forge_float(forge, voice->timbre);
		}
		if(ref)
			lv2_atom_forge_pop(forge, &body_frame);
	}
	if(ref)
		lv2_atom_forge_pop(forge, &obj_frame);

	return ref;
}

static inline LV2_Atom_Forge_Ref
_xpress_del(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames, xpress_voice_t *voice)
{
	LV2_Atom_Forge_Frame obj_frame;

	LV2_Atom_Forge_Ref ref = lv2_atom_forge_frame_time(forge, frames);

	if(ref)
		ref = lv2_atom_forge_object(forge, &obj_frame, 0, xpress->urid.patch_delete);
	{
		if(ref)
			ref = lv2_atom_forge_key(forge, xpress->urid.patch_subject);
		if(ref)
			ref = lv2_atom_forge_urid(forge, voice->subject);
	}
	if(ref)
		lv2_atom_forge_pop(forge, &obj_frame);

	return ref;
}

static inline void
_xpress_set_prop(xpress_t *xpress, LV2_Atom_Forge *forge, xpress_voice_t *voice,
	LV2_URID property, LV2_URID type, const void *value)
{
		if(property == xpress->urid.xpress_zone)
		{
			_xpress_int_set_cb(xpress, &voice->zone, type, value);
		}
		else if(property == xpress->urid.xpress_pitch)
		{
			_xpress_float_set_cb(xpress, &voice->pitch, type, value);
		}
		else if(property == xpress->urid.xpress_pressure)
		{
			_xpress_float_set_cb(xpress, &voice->pressure, type, value);
		}
		else if(property == xpress->urid.xpress_timbre)
		{
			_xpress_float_set_cb(xpress, &voice->timbre, type, value);
		}
}

static inline int
xpress_init(xpress_t *xpress, const size_t max_nvoices, const char *subject,
	LV2_URID_Map *map, xpress_event_t event_mask, xpress_event_cb_t event_cb, void *data)
{
	if(!map)
		return 0;

	xpress->nvoices = 0;
	xpress->max_nvoices = max_nvoices;
	xpress->map = map;
	xpress->event_mask = event_mask;
	xpress->event_cb = event_cb;
	xpress->data = data;
	
	xpress->urid.patch_get = map->map(map->handle, LV2_PATCH__Get);
	xpress->urid.patch_set = map->map(map->handle, LV2_PATCH__Set);
	xpress->urid.patch_delete = map->map(map->handle, LV2_PATCH__Delete);
	xpress->urid.patch_put = map->map(map->handle, LV2_PATCH__Put);
	xpress->urid.patch_subject = map->map(map->handle, LV2_PATCH__subject);
	xpress->urid.patch_body = map->map(map->handle, LV2_PATCH__body);
	xpress->urid.patch_property = map->map(map->handle, LV2_PATCH__property);
	xpress->urid.patch_value = map->map(map->handle, LV2_PATCH__value);
	
	xpress->urid.atom_int = map->map(map->handle, LV2_ATOM__Int);
	xpress->urid.atom_long = map->map(map->handle, LV2_ATOM__Long);
	xpress->urid.atom_float = map->map(map->handle, LV2_ATOM__Float);
	xpress->urid.atom_double = map->map(map->handle, LV2_ATOM__Double);
	xpress->urid.atom_bool = map->map(map->handle, LV2_ATOM__Bool);
	xpress->urid.atom_urid = map->map(map->handle, LV2_ATOM__URID);

	xpress->urid.xpress_zone = map->map(map->handle, XPRESS_ZONE);
	xpress->urid.xpress_pitch = map->map(map->handle, XPRESS_PITCH);
	xpress->urid.xpress_pressure = map->map(map->handle, XPRESS_PRESSURE);
	xpress->urid.xpress_timbre = map->map(map->handle, XPRESS_TIMBRE);

	for(unsigned i = 0; i < xpress->max_nvoices; i++)
	{
		xpress_voice_t *voice = &xpress->voices[i];

		voice->subject = 0;
		voice->pos = i;
	}

	return 1;
}

static inline void
xpress_sort(xpress_t *xpress)
{
	qsort(xpress->voices, xpress->nvoices, sizeof(xpress_voice_t), _voice_sort);
}

static inline int
xpress_advance(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames,
	const LV2_Atom_Object *obj, LV2_Atom_Forge_Ref *ref)
{
	if(!lv2_atom_forge_is_object_type(forge, obj->atom.type))
		return 0;

	if(obj->body.otype == xpress->urid.patch_set)
	{
		// FIXME
		const LV2_Atom_URID *subject = NULL;
		const LV2_Atom_URID *property = NULL;
		const LV2_Atom *value = NULL;

		LV2_Atom_Object_Query q [] = {
			{ xpress->urid.patch_subject, (const LV2_Atom **)&subject },
			{ xpress->urid.patch_property, (const LV2_Atom **)&property },
			{ xpress->urid.patch_value, &value },
			LV2_ATOM_OBJECT_QUERY_END
		};
		lv2_atom_object_query(obj, q);

		if(  !subject || (subject->atom.type != xpress->urid.atom_urid)
			|| !property || (property->atom.type != xpress->urid.atom_urid)
			|| !value)
		{
			return 0;
		}

		xpress_voice_t *voice = _xpress_voice_search(xpress, subject->body);
		if(!voice)
		{
			return 0;
		}

		_xpress_set_prop(xpress, forge, voice, property->body,
			value->type, LV2_ATOM_BODY_CONST(value));

		if(xpress->event_cb && (xpress->event_mask & XPRESS_EVENT_PUT) ) //FIXME EVENT_SET?
			xpress->event_cb(xpress->data, forge, frames, XPRESS_EVENT_PUT, voice);

		return 1;
	}
	else if(obj->body.otype == xpress->urid.patch_put)
	{
		const LV2_Atom_URID *subject = NULL;
		const LV2_Atom_Object *body = NULL;

		LV2_Atom_Object_Query q [] = {
			{ xpress->urid.patch_subject, (const LV2_Atom **)&subject },
			{ xpress->urid.patch_body, (const LV2_Atom **)&body },
			LV2_ATOM_OBJECT_QUERY_END
		};
		lv2_atom_object_query(obj, q);

		if(  !subject || (subject->atom.type != xpress->urid.atom_urid)
			|| !body || !lv2_atom_forge_is_object_type(forge, body->atom.type))
		{
			return 0;
		}

		bool allocated = false;
		xpress_voice_t *voice = _xpress_voice_search(xpress, subject->body);
		if(!voice)
		{
			xpress_realloc(xpress, subject->body); //FIXME check

			allocated = true;
			voice = _xpress_voice_search(xpress, subject->body);
			if(!voice)
			{
				return 0;
			}
		}

		LV2_ATOM_OBJECT_FOREACH(body, prop)
		{
			const LV2_URID property = prop->key;
			const LV2_Atom *value = &prop->value;

			_xpress_set_prop(xpress, forge, voice, property,
				value->type, LV2_ATOM_BODY_CONST(value));
		}

		if(allocated)
		{
			if(xpress->event_cb && (xpress->event_mask & XPRESS_EVENT_ALLOC) )
				xpress->event_cb(xpress->data, forge, frames, XPRESS_EVENT_ALLOC, voice);
		}
		else
		{
			if(xpress->event_cb && (xpress->event_mask & XPRESS_EVENT_PUT) )
				xpress->event_cb(xpress->data, forge, frames, XPRESS_EVENT_PUT, voice);
		}

		return 1;
	}
	else if(obj->body.otype == xpress->urid.patch_delete)
	{
		const LV2_Atom_URID *subject = NULL;

		LV2_Atom_Object_Query q [] = {
			{ xpress->urid.patch_subject, (const LV2_Atom **)&subject },
			LV2_ATOM_OBJECT_QUERY_END
		};
		lv2_atom_object_query(obj, q);

		if(!subject || (subject->atom.type != xpress->urid.atom_urid))
		{
			return 0;
		}

		xpress_voice_t *voice = _xpress_voice_search(xpress, subject->body);
		if(voice)
		{
			if(xpress->event_cb && (xpress->event_mask & XPRESS_EVENT_FREE) )
				xpress->event_cb(xpress->data, forge, frames, XPRESS_EVENT_FREE, voice);
		}

		return xpress_free(xpress, subject->body); //FIXME make more efficient
	}

	return 0; // did not handle a patch event
}

static uint32_t counter = UINT32_MAX; //FIXME

static inline LV2_URID
xpress_alloc(xpress_t *xpress)
{
	if(xpress->nvoices >= xpress->max_nvoices)
		return 0; // failed

	const LV2_URID subject = counter--;
	xpress_voice_t *voice = &xpress->voices[xpress->nvoices++];
	voice->subject = subject;
	xpress_sort(xpress);

	return subject;
}

static inline void
xpress_realloc(xpress_t *xpress, LV2_URID subject)
{
	if(xpress->nvoices >= xpress->max_nvoices)
		return; // failed

	xpress_voice_t *voice = &xpress->voices[xpress->nvoices++];
	voice->subject = subject;
	xpress_sort(xpress);
}

static inline int
xpress_free(xpress_t *xpress, LV2_URID subject)
{
	xpress_voice_t *voice = _xpress_voice_search(xpress, subject);
	if(!voice)
		return 0; // failed

	voice->subject = 0;
	xpress_sort(xpress);
	xpress->nvoices--;

	return 1; // success
}

static inline LV2_Atom_Forge_Ref
xpress_new(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames, LV2_URID subject)
{
	xpress_voice_t *voice = _xpress_voice_search(xpress, subject);
	if(voice)
		return _xpress_put(xpress, forge, frames, voice);

	return 1; // we have not written anything, ref thus is set to 'good'
}

static inline LV2_Atom_Forge_Ref
xpress_del(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames, LV2_URID subject)
{
	xpress_voice_t *voice = _xpress_voice_search(xpress, subject);
	if(voice)
		return _xpress_del(xpress, forge, frames, voice);

	return 1; // we have not written anything, ref thus is set to 'good'
}

static inline LV2_Atom_Forge_Ref
xpress_put(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames, LV2_URID subject)
{
	xpress_voice_t *voice = _xpress_voice_search(xpress, subject);
	if(voice)
		return _xpress_put(xpress, forge, frames, voice);

	return 1; // we have not written anything, ref thus is set to 'good'
}

#ifdef __cplusplus
}
#endif

#endif // _LV2_XPRESS_H_
