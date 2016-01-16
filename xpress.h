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
typedef struct _xpress_map_t xpress_map_t;
typedef struct _xpress_state_t xpress_state_t;
typedef struct _xpress_voice_t xpress_voice_t;
typedef struct _xpress_iface_t xpress_iface_t;
typedef struct _xpress_t xpress_t;

// function callbacks
typedef uint32_t (*xpress_map_new_id_t)(void *handle);

typedef void (*xpress_state_cb_t)(void *data,
	int64_t frames, const xpress_state_t *state,
	LV2_URID subject, void *target);

enum _xpress_event_t {
	XPRESS_EVENT_ADD					= (1 << 0),
	XPRESS_EVENT_DEL					= (1 << 1),
	XPRESS_EVENT_PUT					= (1 << 2)
};

#define XPRESS_EVENT_NONE		(0)
#define XPRESS_EVENT_ALL		(XPRESS_EVENT_ADD | XPRESS_EVENT_DEL | XPRESS_EVENT_PUT)

struct _xpress_state_t {
	int32_t zone;
	float pitch;
	float pressure;
	float timbre;
};

struct _xpress_iface_t {
	size_t size;

	xpress_state_cb_t add;
	xpress_state_cb_t put;
	xpress_state_cb_t del;
};

#define XPRESS_UNUSED				(-1)

struct _xpress_voice_t {
	LV2_URID subject;
	void *target;
};

struct _xpress_map_t {
	void *handle;
	xpress_map_new_id_t new_id;
};

struct _xpress_t {
	struct {
		LV2_URID patch_delete;
		LV2_URID patch_put;
		LV2_URID patch_subject;
		LV2_URID patch_body;

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
	xpress_map_t *voice_map;

	xpress_event_t event_mask;
	const xpress_iface_t *iface;
	void *data;

	unsigned max_nvoices;
	unsigned nvoices;
	xpress_voice_t voices [0];
};

#define XPRESS_CONCAT_IMPL(X, Y) X##Y
#define XPRESS_CONCAT(X, Y) XPRESS_CONCAT_IMPL(X, Y)

#define XPRESS_T(XPRESS, MAX_NVOICES) \
	xpress_t (XPRESS); \
	xpress_voice_t XPRESS_CONCAT(_voices, __COUNTER__) [(MAX_NVOICES)];

#define XPRESS_VOICE_FOREACH(XPRESS, VOICE) \
	for(xpress_voice_t *VOICE = &(XPRESS)->voices[(int)(XPRESS)->nvoices - 1]; \
		VOICE >= (XPRESS)->voices; \
		VOICE--)

#define XPRESS_VOICE_FREE(XPRESS, VOICE) \
	for(xpress_voice_t *VOICE = &(XPRESS)->voices[(int)(XPRESS)->nvoices - 1]; \
		VOICE >= (XPRESS)->voices; \
		VOICE->subject = 0, VOICE--, (XPRESS)->nvoices--)

// non rt-safe
static inline int
xpress_init(xpress_t *xpress, const size_t max_nvoices, LV2_URID_Map *map,
	xpress_map_t *voice_map, xpress_event_t event_mask, const xpress_iface_t *iface,
	void *target, void *data);

// rt-safe
static inline void *
xpress_get(xpress_t *xpress, LV2_URID subject);

// rt-safe
static inline int
xpress_advance(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames,
	const LV2_Atom_Object *obj, LV2_Atom_Forge_Ref *ref);

// rt-safe
static inline void * 
xpress_add(xpress_t *xpress, LV2_URID subject);

// rt-safe
static inline void *
xpress_create(xpress_t *xpress, LV2_URID *subject);

// rt-safe
static inline int
xpress_free(xpress_t *xpress, LV2_URID subject);

// rt-safe
static inline LV2_Atom_Forge_Ref
xpress_del(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames,
	LV2_URID subject);

// rt-safe
static inline LV2_Atom_Forge_Ref
xpress_put(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames,
	LV2_URID subject, const xpress_state_t *state);

// rt-safe
static inline int32_t
xpress_map(xpress_t *xpress);

/*****************************************************************************
 * API END
 *****************************************************************************/

static inline int32_t
_xpress_int_get(xpress_t *xpress, const LV2_Atom *atom)
{
	if(atom->type == xpress->urid.atom_int)
		return ((const LV2_Atom_Int *)atom)->body;
	else if(atom->type == xpress->urid.atom_long)
		return ((const LV2_Atom_Long *)atom)->body;

	else if(atom->type == xpress->urid.atom_float)
		return ((const LV2_Atom_Float *)atom)->body;
	else if(atom->type == xpress->urid.atom_double)
		return ((const LV2_Atom_Double *)atom)->body;

	else if(atom->type == xpress->urid.atom_bool)
		return ((const LV2_Atom_Bool *)atom)->body;

	return 0;
}

static inline float
_xpress_float_get(xpress_t *xpress, const LV2_Atom *atom)
{
	if(atom->type == xpress->urid.atom_float)
		return ((const LV2_Atom_Float *)atom)->body;
	else if(atom->type == xpress->urid.atom_double)
		return ((const LV2_Atom_Double *)atom)->body;

	else if(atom->type == xpress->urid.atom_int)
		return ((const LV2_Atom_Int *)atom)->body;
	else if(atom->type == xpress->urid.atom_long)
		return ((const LV2_Atom_Long *)atom)->body;

	else if(atom->type == xpress->urid.atom_bool)
		return ((const LV2_Atom_Bool *)atom)->body;

	return 0.f;
}

static inline int
_xpress_signum(LV2_URID urid1, LV2_URID urid2)
{
	if(urid1 < urid2)
		return 1;
	else if(urid1 > urid2)
		return -1;
	
	return 0;
}

static int
_xpress_voice_sort(const void *itm1, const void *itm2)
{
	const xpress_voice_t *voice1 = itm1;
	const xpress_voice_t *voice2 = itm2;

	return _xpress_signum(voice1->subject, voice2->subject);
}

static inline void
_xpress_sort(xpress_t *xpress)
{
	qsort(xpress->voices, xpress->nvoices, sizeof(xpress_voice_t), _xpress_voice_sort);
}

static inline xpress_voice_t *
_xpress_voice_get(xpress_t *xpress, LV2_URID subject)
{
	const xpress_voice_t voice = {
		.subject = subject,
		.target = NULL
	};

	return bsearch(&voice, xpress->voices, xpress->nvoices, sizeof(xpress_voice_t),
		_xpress_voice_sort);
}

static inline void *
_xpress_voice_add(xpress_t *xpress, LV2_URID subject)
{
	if(xpress->nvoices >= xpress->max_nvoices)
		return NULL; // failed

	xpress_voice_t *voice = &xpress->voices[xpress->nvoices++];
	voice->subject = subject;
	void *target = voice->target;

	_xpress_sort(xpress);

	return target;
}

static inline void
_xpress_voice_free(xpress_t *xpress, xpress_voice_t *voice)
{
	voice->subject = 0;

	_xpress_sort(xpress);

	xpress->nvoices--;
}

static inline int
xpress_init(xpress_t *xpress, const size_t max_nvoices, LV2_URID_Map *map,
	xpress_map_t *voice_map, xpress_event_t event_mask, const xpress_iface_t *iface,
	void *target, void *data)
{
	if(!map || ( (event_mask != XPRESS_EVENT_NONE) && !iface))
		return 0;

	xpress->nvoices = 0;
	xpress->max_nvoices = max_nvoices;
	xpress->map = map;
	xpress->voice_map = voice_map;
	xpress->event_mask = event_mask;
	xpress->iface = iface;
	xpress->data = data;
	
	xpress->urid.patch_delete = map->map(map->handle, LV2_PATCH__Delete);
	xpress->urid.patch_put = map->map(map->handle, LV2_PATCH__Put);
	xpress->urid.patch_subject = map->map(map->handle, LV2_PATCH__subject);
	xpress->urid.patch_body = map->map(map->handle, LV2_PATCH__body);
	
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
		voice->target = target && iface
			? target + i*iface->size
			: NULL;
	}

	return 1;
}

static inline void *
xpress_get(xpress_t *xpress, LV2_URID subject)
{
	xpress_voice_t *voice = _xpress_voice_get(xpress, subject);
	if(voice)
		return voice->target;

	return NULL;
}

static inline int
xpress_advance(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames,
	const LV2_Atom_Object *obj, LV2_Atom_Forge_Ref *ref)
{
	if(!lv2_atom_forge_is_object_type(forge, obj->atom.type))
		return 0;

	if(obj->body.otype == xpress->urid.patch_put)
	{
		const LV2_Atom_URID *subject = NULL;
		const LV2_Atom_Object *body = NULL;

		LV2_Atom_Object_Query q [] = {
			{ xpress->urid.patch_subject, (const LV2_Atom **)&subject },
			{ xpress->urid.patch_body, (const LV2_Atom **)&body },
			{ 0, NULL}
		};
		lv2_atom_object_query(obj, q);

		if(  !subject || (subject->atom.type != xpress->urid.atom_urid)
			|| !body || !lv2_atom_forge_is_object_type(forge, body->atom.type))
		{
			return 0;
		}

		bool added;
		xpress_voice_t *voice = _xpress_voice_get(xpress, subject->body);
		void *target;
		if(voice)
		{
			target = voice->target;

			added = false;
		}
		else
		{
			if(!(target = _xpress_voice_add(xpress, subject->body)))
				return 0;

			added = true;
		}

		xpress_state_t state;

		LV2_ATOM_OBJECT_FOREACH(body, prop)
		{
			const LV2_URID property = prop->key;
			const LV2_Atom *value = &prop->value;

			if(property == xpress->urid.xpress_zone)
				state.zone = _xpress_int_get(xpress, value);
			else if(property == xpress->urid.xpress_pitch)
				state.pitch = _xpress_float_get(xpress, value);
			else if(property == xpress->urid.xpress_pressure)
				state.pressure = _xpress_float_get(xpress, value);
			else if(property == xpress->urid.xpress_timbre)
				state.timbre = _xpress_float_get(xpress, value);
		}

		if(added)
		{
			if( (xpress->event_mask & XPRESS_EVENT_ADD) && xpress->iface->add)
				xpress->iface->add(xpress->data, frames, &state, subject->body, target);
		}
		else
		{
			if( (xpress->event_mask & XPRESS_EVENT_PUT) && xpress->iface->put)
				xpress->iface->put(xpress->data, frames, &state, subject->body, target);
		}

		return 1;
	}
	else if(obj->body.otype == xpress->urid.patch_delete)
	{
		const LV2_Atom_URID *subject = NULL;

		LV2_Atom_Object_Query q [] = {
			{ xpress->urid.patch_subject, (const LV2_Atom **)&subject },
			{ 0, NULL}
		};
		lv2_atom_object_query(obj, q);

		if(!subject || (subject->atom.type != xpress->urid.atom_urid))
			return 0;

		xpress_voice_t *voice = _xpress_voice_get(xpress, subject->body);
		if(!voice)
			return 0;

		const LV2_URID voice_subject = voice->subject;
		void *voice_target = voice->target;
	
		_xpress_voice_free(xpress, voice);

		if( (xpress->event_mask & XPRESS_EVENT_DEL) && xpress->iface->del)
			xpress->iface->del(xpress->data, frames, NULL, voice_subject, voice_target);

		return 1;
	}

	return 0; // did not handle a patch event
}

static inline void *
xpress_add(xpress_t *xpress, LV2_URID subject)
{
	return _xpress_voice_add(xpress, subject);
}

static inline void *
xpress_create(xpress_t *xpress, LV2_URID *subject)
{
	*subject = xpress->voice_map->new_id(xpress->voice_map->handle);

	return xpress_add(xpress, *subject);
}

static inline int
xpress_free(xpress_t *xpress, LV2_URID subject)
{
	xpress_voice_t *voice = _xpress_voice_get(xpress, subject);
	if(!voice)
		return 0; // failed

	_xpress_voice_free(xpress, voice);

	return 1;
}

static inline LV2_Atom_Forge_Ref
xpress_del(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames,
	LV2_URID subject)
{
	LV2_Atom_Forge_Frame obj_frame;

	LV2_Atom_Forge_Ref ref = lv2_atom_forge_frame_time(forge, frames);

	if(ref)
		ref = lv2_atom_forge_object(forge, &obj_frame, 0, xpress->urid.patch_delete);
	{
		if(ref)
			ref = lv2_atom_forge_key(forge, xpress->urid.patch_subject);
		if(ref)
			ref = lv2_atom_forge_urid(forge, subject);
	}
	if(ref)
		lv2_atom_forge_pop(forge, &obj_frame);

	return ref;
}

static inline LV2_Atom_Forge_Ref
xpress_put(xpress_t *xpress, LV2_Atom_Forge *forge, uint32_t frames,
	LV2_URID subject, const xpress_state_t *state)
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
			ref = lv2_atom_forge_urid(forge, subject);

		if(ref)
			ref = lv2_atom_forge_key(forge, xpress->urid.patch_body);
		if(ref)
			ref = lv2_atom_forge_object(forge, &body_frame, 0, 0);
		{
			if(ref)
				ref = lv2_atom_forge_key(forge, xpress->urid.xpress_zone);
			if(ref)
				ref = lv2_atom_forge_int(forge, state->zone);

			if(ref)
				ref = lv2_atom_forge_key(forge, xpress->urid.xpress_pitch);
			if(ref)
				ref = lv2_atom_forge_float(forge, state->pitch);

			if(ref)
				ref = lv2_atom_forge_key(forge, xpress->urid.xpress_pressure);
			if(ref)
				ref = lv2_atom_forge_float(forge, state->pressure);

			if(ref)
				ref = lv2_atom_forge_key(forge, xpress->urid.xpress_timbre);
			if(ref)
					ref = lv2_atom_forge_float(forge, state->timbre);
		}
		if(ref)
			lv2_atom_forge_pop(forge, &body_frame);
	}
	if(ref)
		lv2_atom_forge_pop(forge, &obj_frame);

	return ref;
}

static inline int32_t
xpress_map(xpress_t *xpress)
{
	return xpress->voice_map->new_id(xpress->voice_map->handle);
}

#ifdef __cplusplus
}
#endif

#endif // _LV2_XPRESS_H_
