/*
 * Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#ifndef _SYNTHPOD_PATCHER_H
#define _SYNTHPOD_PATCHER_H

#include <synthpod_private.h>

static inline LV2_Atom_Forge_Ref
_synthpod_patcher_internal_object(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_Atom_Forge_Frame *frame, LV2_URID otype, LV2_URID subject, int32_t seqn)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_object(forge, frame, 0, otype);

	if(subject)
	{
		if(ref)
			ref = lv2_atom_forge_key(forge, regs->patch.subject.urid);
		if(ref)
			ref = lv2_atom_forge_urid(forge, subject);
	}

	if(seqn)
	{
		if(ref)
			ref = lv2_atom_forge_key(forge, regs->patch.sequence_number.urid);
		if(ref)
			ref = lv2_atom_forge_int(forge, seqn);
	}

	return ref;
}

static inline LV2_Atom_Forge_Ref
_synthpod_patcher_internal_property(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID property)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(forge, regs->patch.property.urid);
	if(ref)
		ref = lv2_atom_forge_urid(forge, property);

	return ref;
}

static inline LV2_Atom_Forge_Ref
_synthpod_patcher_internal_destination(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID destination)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(forge, regs->patch.destination.urid);
	if(ref)
		ref = lv2_atom_forge_urid(forge, destination);

	return ref;
}

static inline LV2_Atom_Forge_Ref
_synthpod_patcher_internal_body(reg_t *regs, LV2_Atom_Forge *forge)
{
	return lv2_atom_forge_key(forge, regs->patch.body.urid);
}

static inline LV2_Atom_Forge_Ref
_synthpod_patcher_internal_add(reg_t *regs, LV2_Atom_Forge *forge)
{
	return lv2_atom_forge_key(forge, regs->patch.add.urid);
}

static inline LV2_Atom_Forge_Ref
_synthpod_patcher_internal_remove(reg_t *regs, LV2_Atom_Forge *forge)
{
	return lv2_atom_forge_key(forge, regs->patch.remove.urid);
}

static inline LV2_Atom_Forge_Ref
_synthpod_patcher_internal_value(reg_t *regs, LV2_Atom_Forge *forge)
{
	return lv2_atom_forge_key(forge, regs->patch.value.urid);
}

static inline LV2_Atom_Forge_Ref
_synthpod_patcher_internal_raw(reg_t *regs, LV2_Atom_Forge *forge,
	uint32_t size, LV2_URID type, const void *body)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_atom(forge, size, type);
	if(ref)
		ref = lv2_atom_forge_write(forge, body, size);

	return ref;
}

static inline LV2_Atom_Forge_Ref
_synthpod_patcher_internal_atom(reg_t *regs, LV2_Atom_Forge *forge,
	const LV2_Atom *atom)
{
	return lv2_atom_forge_write(forge, atom, lv2_atom_total_size(atom));
}

static inline void
synthpod_patcher_pop(LV2_Atom_Forge *forge,
	LV2_Atom_Forge_Frame *frame, int nframes)
{
	for(int i = nframes-1; i >= 0; i--)
		lv2_atom_forge_pop(forge, &frame[i]);
}

/******************************************************************************/

// patch:Copy
static LV2_Atom_Forge_Ref
synthpod_patcher_copy(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn, LV2_URID destination)
{
	LV2_Atom_Forge_Frame frame [1];

	LV2_Atom_Forge_Ref ref = _synthpod_patcher_internal_object(regs, forge, frame,
		regs->patch.copy.urid, subject, seqn);
	if(ref)
		ref = _synthpod_patcher_internal_destination(regs, forge, destination);
	if(ref)
		synthpod_patcher_pop(forge, frame, 1);

	return ref;
}

// patch:Delete
static LV2_Atom_Forge_Ref
synthpod_patcher_delete(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn)
{
	LV2_Atom_Forge_Frame frame [1];

	LV2_Atom_Forge_Ref ref = _synthpod_patcher_internal_object(regs, forge, frame,
		regs->patch.delete.urid, subject, seqn);
	if(ref)
		synthpod_patcher_pop(forge, frame, 1);

	return ref;
}

// patch:Get
static LV2_Atom_Forge_Ref
synthpod_patcher_get(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn, LV2_URID property)
{
	LV2_Atom_Forge_Frame frame [1];

	LV2_Atom_Forge_Ref ref = _synthpod_patcher_internal_object(regs, forge, frame,
		regs->patch.get.urid, subject, seqn);
	if(property && ref)
		ref = _synthpod_patcher_internal_property(regs, forge, property);
	if(ref)
		synthpod_patcher_pop(forge, frame, 1);

	return ref;
}

// patch:Insert
static LV2_Atom_Forge_Ref
synthpod_patcher_insert_object(reg_t *regs, LV2_Atom_Forge *forge, LV2_Atom_Forge_Frame *frame,
	LV2_URID subject, int32_t seqn)
{
	LV2_Atom_Forge_Ref ref = _synthpod_patcher_internal_object(regs, forge, frame,
		regs->patch.insert.urid, subject, seqn);
	if(ref)
		ref = _synthpod_patcher_internal_body(regs, forge);

	return ref;
}

static LV2_Atom_Forge_Ref
synthpod_patcher_insert(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn,
	uint32_t size, LV2_URID type, const void *body)
{
	LV2_Atom_Forge_Frame frame [1];

	LV2_Atom_Forge_Ref ref = synthpod_patcher_insert_object(regs, forge, frame,
		subject, seqn);
	if(ref)
		ref = _synthpod_patcher_internal_raw(regs, forge, size, type, body);
	if(ref)
		synthpod_patcher_pop(forge, frame, 1);

	return ref;
}

static LV2_Atom_Forge_Ref
synthpod_patcher_insert_atom(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn,
	const LV2_Atom *atom)
{
	LV2_Atom_Forge_Frame frame [1];

	LV2_Atom_Forge_Ref ref = synthpod_patcher_insert_object(regs, forge, frame,
		subject, seqn);
	if(ref)
		ref = _synthpod_patcher_internal_atom(regs, forge, atom);
	if(ref)
		synthpod_patcher_pop(forge, frame, 1);

	return ref;
}

// patch:remove aka patch:Patch
static LV2_Atom_Forge_Ref
synthpod_patcher_remove_object(reg_t *regs, LV2_Atom_Forge *forge, LV2_Atom_Forge_Frame *frame,
	LV2_URID subject, int32_t seqn, LV2_URID property)
{
	LV2_Atom_Forge_Ref ref = _synthpod_patcher_internal_object(regs, forge, frame,
		regs->patch.patch.urid, subject, seqn);
	if(ref)
		ref = _synthpod_patcher_internal_add(regs, forge);
	if(ref)
		ref = lv2_atom_forge_object(forge, &frame[1], 0, 0);
	// add nothing
	if(ref)
		lv2_atom_forge_pop(forge, &frame[1]);
	if(ref)
		ref = _synthpod_patcher_internal_remove(regs, forge);
	if(ref)
		ref = lv2_atom_forge_object(forge, &frame[1], 0, 0);
	if(ref)
		ref = lv2_atom_forge_key(forge, property);

	return ref;
}

static LV2_Atom_Forge_Ref
synthpod_patcher_remove(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn, LV2_URID property,
	uint32_t size, LV2_URID type, const void *body)
{
	LV2_Atom_Forge_Frame frame [2];

	LV2_Atom_Forge_Ref ref = synthpod_patcher_remove_object(regs, forge, frame,
		subject, seqn, property);
	if(ref)
		ref = _synthpod_patcher_internal_raw(regs, forge, size, type, body);
	if(ref)
		synthpod_patcher_pop(forge, frame, 2);

	return ref;
}

static LV2_Atom_Forge_Ref
synthpod_patcher_remove_atom(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn, LV2_URID property,
	const LV2_Atom *atom)
{
	LV2_Atom_Forge_Frame frame [2];

	LV2_Atom_Forge_Ref ref = synthpod_patcher_remove_object(regs, forge, frame,
		subject, seqn, property);
	if(ref)
		ref = _synthpod_patcher_internal_atom(regs, forge, atom);
	if(ref)
		synthpod_patcher_pop(forge, frame, 2);

	return ref;
}

// patch:add aka patch:Patch
static LV2_Atom_Forge_Ref
synthpod_patcher_add_object(reg_t *regs, LV2_Atom_Forge *forge, LV2_Atom_Forge_Frame *frame,
	LV2_URID subject, int32_t seqn, LV2_URID property)
{
	LV2_Atom_Forge_Ref ref = _synthpod_patcher_internal_object(regs, forge, frame,
		regs->patch.patch.urid, subject, seqn);
	if(ref)
		ref = _synthpod_patcher_internal_remove(regs, forge);
	if(ref)
		ref = lv2_atom_forge_object(forge, &frame[1], 0, 0);
	// add nothing
	if(ref)
		lv2_atom_forge_pop(forge, &frame[1]);
	if(ref)
		ref = _synthpod_patcher_internal_add(regs, forge);
	if(ref)
		ref = lv2_atom_forge_object(forge, &frame[1], 0, 0);
	if(ref)
		ref = lv2_atom_forge_key(forge, property);

	return ref;
}

static LV2_Atom_Forge_Ref
synthpod_patcher_add(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn, LV2_URID property,
	uint32_t size, LV2_URID type, const void *body)
{
	LV2_Atom_Forge_Frame frame [2];

	LV2_Atom_Forge_Ref ref = synthpod_patcher_add_object(regs, forge, frame,
		subject, seqn, property);
	if(ref)
		ref = _synthpod_patcher_internal_raw(regs, forge, size, type, body);
	if(ref)
		synthpod_patcher_pop(forge, frame, 2);

	return ref;
}

static LV2_Atom_Forge_Ref
synthpod_patcher_add_atom(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn, LV2_URID property,
	const LV2_Atom *atom)
{
	LV2_Atom_Forge_Frame frame [2];

	LV2_Atom_Forge_Ref ref = synthpod_patcher_add_object(regs, forge, frame,
		subject, seqn, property);
	if(ref)
		ref = _synthpod_patcher_internal_atom(regs, forge, atom);
	if(ref)
		synthpod_patcher_pop(forge, frame, 2);

	return ref;
}

// patch:Move
static LV2_Atom_Forge_Ref
synthpod_patcher_move(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn,
	LV2_URID destination)
{
	LV2_Atom_Forge_Frame frame [1];

	LV2_Atom_Forge_Ref ref = _synthpod_patcher_internal_object(regs, forge, frame,
		regs->patch.move.urid, subject, seqn);
	if(ref)
		ref = _synthpod_patcher_internal_destination(regs, forge, destination);
	if(ref)
		synthpod_patcher_pop(forge, frame, 1);

	return ref;
}

// patch:Patch
// TODO


// patch:Put
static LV2_Atom_Forge_Ref
synthpod_patcher_put_object(reg_t *regs, LV2_Atom_Forge *forge, LV2_Atom_Forge_Frame *frame,
	LV2_URID subject, int32_t seqn)
{
	LV2_Atom_Forge_Ref ref = _synthpod_patcher_internal_object(regs, forge, frame,
		regs->patch.put.urid, subject, seqn);
	if(ref)
		ref = _synthpod_patcher_internal_body(regs, forge);

	return ref;
}

static LV2_Atom_Forge_Ref
synthpod_patcher_put(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn,
	uint32_t size, LV2_URID type, const void *body)
{
	LV2_Atom_Forge_Frame frame [1];

	LV2_Atom_Forge_Ref ref = synthpod_patcher_put_object(regs, forge, frame,
		subject, seqn);
	if(ref)
		ref = _synthpod_patcher_internal_raw(regs, forge, size, type, body);
	if(ref)
		synthpod_patcher_pop(forge, frame, 1);

	return ref;
}

static LV2_Atom_Forge_Ref
synthpod_patcher_put_atom(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn,
	const LV2_Atom *atom)
{
	LV2_Atom_Forge_Frame frame [1];

	LV2_Atom_Forge_Ref ref = synthpod_patcher_put_object(regs, forge, frame,
		subject, seqn);
	if(ref)
		ref = _synthpod_patcher_internal_atom(regs, forge, atom);
	if(ref)
		synthpod_patcher_pop(forge, frame, 1);

	return ref;
}

// patch:Set
static LV2_Atom_Forge_Ref
synthpod_patcher_set_object(reg_t *regs, LV2_Atom_Forge *forge, LV2_Atom_Forge_Frame *frame,
	LV2_URID subject, int32_t seqn, LV2_URID property)
{
	LV2_Atom_Forge_Ref ref = _synthpod_patcher_internal_object(regs, forge, frame,
		regs->patch.set.urid, subject, seqn);
	if(ref)
		ref = _synthpod_patcher_internal_property(regs, forge, property);
	if(ref)
		ref = _synthpod_patcher_internal_value(regs, forge);

	return ref;
}
static LV2_Atom_Forge_Ref
synthpod_patcher_set(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn, LV2_URID property,
	uint32_t size, LV2_URID type, const void *body)
{
	LV2_Atom_Forge_Frame frame [1];

	LV2_Atom_Forge_Ref ref = synthpod_patcher_set_object(regs, forge, frame,
		subject, seqn, property);
	if(ref)
		ref = _synthpod_patcher_internal_raw(regs, forge, size, type, body);
	if(ref)
		synthpod_patcher_pop(forge, frame, 1);

	return ref;
}

static LV2_Atom_Forge_Ref
synthpod_patcher_set_atom(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn, LV2_URID property,
	const LV2_Atom *atom)
{
	LV2_Atom_Forge_Frame frame [1];

	LV2_Atom_Forge_Ref ref = synthpod_patcher_set_object(regs, forge, frame,
		subject, seqn, property);
	if(ref)
		ref = _synthpod_patcher_internal_atom(regs, forge, atom);
	if(ref)
		synthpod_patcher_pop(forge, frame, 1);

	return ref;
}

// patch:Ack
static LV2_Atom_Forge_Ref
synthpod_patcher_ack(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn)
{
	LV2_Atom_Forge_Frame frame [1];

	LV2_Atom_Forge_Ref ref = _synthpod_patcher_internal_object(regs, forge, frame,
		regs->patch.ack.urid, subject, seqn);
	if(ref)
		synthpod_patcher_pop(forge, frame, 1);

	return ref;
}

// patch:Error
static LV2_Atom_Forge_Ref
synthpod_patcher_error(reg_t *regs, LV2_Atom_Forge *forge,
	LV2_URID subject, int32_t seqn)
{
	LV2_Atom_Forge_Frame frame [1];

	LV2_Atom_Forge_Ref ref = _synthpod_patcher_internal_object(regs, forge, frame,
		regs->patch.error.urid, subject, seqn);
	if(ref)
		synthpod_patcher_pop(forge, frame, 1);

	return ref;
}

#endif // _SYNTHPOD_PATCHER_H
