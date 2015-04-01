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

#ifndef _SYNTHPOD_PRIVATE_H
#define _SYNTHPOD_PRIVATE_H

#define SYNTHPOD_PREFIX				"http://open-music-kontrollers.ch/lv2/synthpod#"
#define LV2_UI__EoUI          LV2_UI_PREFIX"EoUI"

#include <lilv/lilv.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/buf-size/buf-size.h>
#include <lv2/lv2plug.in/ns/ext/presets/presets.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

typedef enum _port_type_t port_type_t;
typedef enum _port_buffer_type_t port_buffer_type_t;
typedef enum _port_direction_t port_direction_t;
typedef enum _port_protocol_t port_protocol_t;

enum _port_type_t {
	PORT_TYPE_CONTROL,
	PORT_TYPE_AUDIO,
	PORT_TYPE_CV,
	PORT_TYPE_ATOM,

	PORT_TYPE_NUM
};

enum _port_buffer_type_t {
	PORT_BUFFER_TYPE_NONE = 0,
	PORT_BUFFER_TYPE_SEQUENCE,

	PORT_BUFFER_TYPE_NUM
};

enum _port_direction_t {
	PORT_DIRECTION_INPUT = 0,
	PORT_DIRECTION_OUTPUT,

	PORT_DIRECTION_NUM
};

enum _port_protocol_t {
	PORT_PROTOCOL_FLOAT = 0,
	PORT_PROTOCOL_PEAK,
	PORT_PROTOCOL_ATOM,
	PORT_PROTOCOL_SEQUENCE,

	PORT_PROTOCOL_NUM
}; //TODO use this

typedef int32_t u_id_t; 
typedef struct _reg_item_t reg_item_t;
typedef struct _reg_t reg_t;

struct _reg_item_t {
	LilvNode *node;
	LV2_URID urid;
};
	
struct _reg_t {
	struct {
		reg_item_t input;
		reg_item_t output;

		reg_item_t control;
		reg_item_t audio;
		reg_item_t cv;
		reg_item_t atom;

		// atom buffer type
		reg_item_t sequence;

		// atom sequence event types
		reg_item_t midi;
		reg_item_t osc;
		reg_item_t chim_event;
		reg_item_t chim_dump;

		// control port property
		reg_item_t integer;
		reg_item_t toggled;

		// port protocols
		reg_item_t float_protocol;
		reg_item_t peak_protocol;
		reg_item_t atom_transfer;
		reg_item_t event_transfer;

		reg_item_t notification;
	} port;

	struct {
		reg_item_t schedule;
	} work;

	struct {
		reg_item_t entry;
		reg_item_t error;
		reg_item_t note;
		reg_item_t trace;
		reg_item_t warning;
	} log;

	struct {
		reg_item_t eo;
	} ui;

	struct {
		reg_item_t preset;
		reg_item_t rdfs_label;
	} pset;

	struct {
		reg_item_t max_block_length;
		reg_item_t min_block_length;
		reg_item_t sequence_size;
	} bufsz;

	struct {
		reg_item_t event;
		reg_item_t state;

		reg_item_t module_list;
		reg_item_t module_add;
		reg_item_t module_del;
		reg_item_t module_preset;
		reg_item_t module_selected;
		reg_item_t port_refresh;
		reg_item_t port_connected;
		reg_item_t port_subscribed;
		reg_item_t port_selected;
	} synthpod;
};

static inline void
sp_regs_init(reg_t *regs, LilvWorld *world, LV2_URID_Map *map)
{
	// init nodes
	regs->port.input.node = lilv_new_uri(world, LV2_CORE__InputPort);
	regs->port.output.node = lilv_new_uri(world, LV2_CORE__OutputPort);

	regs->port.control.node = lilv_new_uri(world, LV2_CORE__ControlPort);
	regs->port.audio.node = lilv_new_uri(world, LV2_CORE__AudioPort);
	regs->port.cv.node = lilv_new_uri(world, LV2_CORE__CVPort);
	regs->port.atom.node = lilv_new_uri(world, LV2_ATOM__AtomPort);

	regs->port.sequence.node = lilv_new_uri(world, LV2_ATOM__Sequence);
	regs->port.midi.node = lilv_new_uri(world, LV2_MIDI__MidiEvent);
	regs->port.osc.node = lilv_new_uri(world,
		"http://opensoundcontrol.org#OscEvent");
	regs->port.chim_event.node = lilv_new_uri(world,
		"http://open-music-kontrollers.ch/lv2/chimaera#Event");
	regs->port.chim_dump.node = lilv_new_uri(world,
		"http://open-music-kontrollers.ch/lv2/chimaera#Dump");

	regs->port.integer.node = lilv_new_uri(world, LV2_CORE__integer);
	regs->port.toggled.node = lilv_new_uri(world, LV2_CORE__toggled);

	regs->port.float_protocol.node = lilv_new_uri(world, LV2_UI_PREFIX"floatProtocol");
	regs->port.peak_protocol.node = lilv_new_uri(world, LV2_UI_PREFIX"peakProtocol");
	regs->port.atom_transfer.node = lilv_new_uri(world, LV2_ATOM__atomTransfer);
	regs->port.event_transfer.node = lilv_new_uri(world, LV2_ATOM__eventTransfer);
	regs->port.notification.node = lilv_new_uri(world, LV2_UI__portNotification);

	regs->work.schedule.node = lilv_new_uri(world, LV2_WORKER__schedule);

	regs->log.entry.node = lilv_new_uri(world, LV2_LOG__Entry);
	regs->log.error.node = lilv_new_uri(world, LV2_LOG__Error);
	regs->log.note.node = lilv_new_uri(world, LV2_LOG__Note);
	regs->log.trace.node = lilv_new_uri(world, LV2_LOG__Trace);
	regs->log.warning.node = lilv_new_uri(world, LV2_LOG__Warning);

	regs->ui.eo.node = lilv_new_uri(world, LV2_UI__EoUI);
	
	regs->pset.preset.node = lilv_new_uri(world, LV2_PRESETS__Preset);
	regs->pset.rdfs_label.node = lilv_new_uri(world, LILV_NS_RDFS"label");

	// init URIDs
	regs->port.input.urid = map->map(map->handle, LV2_CORE__InputPort);
	regs->port.output.urid = map->map(map->handle, LV2_CORE__OutputPort);

	regs->port.control.urid = map->map(map->handle, LV2_CORE__ControlPort);
	regs->port.audio.urid = map->map(map->handle, LV2_CORE__AudioPort);
	regs->port.cv.urid = map->map(map->handle, LV2_CORE__CVPort);
	regs->port.atom.urid = map->map(map->handle, LV2_ATOM__AtomPort);

	regs->port.sequence.urid = map->map(map->handle, LV2_ATOM__Sequence);
	regs->port.midi.urid = map->map(map->handle, LV2_MIDI__MidiEvent);
	regs->port.osc.urid = map->map(map->handle,
		"http://opensoundcontrol.org#OscEvent");
	regs->port.chim_event.urid = map->map(map->handle,
		"http://open-music-kontrollers.ch/lv2/chimaera#Event");
	regs->port.chim_dump.urid = map->map(map->handle,
		"http://open-music-kontrollers.ch/lv2/chimaera#Dump");

	regs->port.integer.urid = map->map(map->handle, LV2_CORE__integer);
	regs->port.toggled.urid= map->map(map->handle, LV2_CORE__toggled);

	regs->port.float_protocol.urid = map->map(map->handle, LV2_UI_PREFIX"floatProtocol");
	regs->port.peak_protocol.urid = map->map(map->handle, LV2_UI_PREFIX"peakProtocol");
	regs->port.atom_transfer.urid = map->map(map->handle, LV2_ATOM__atomTransfer);
	regs->port.event_transfer.urid = map->map(map->handle, LV2_ATOM__eventTransfer);
	regs->port.notification.urid = map->map(map->handle, LV2_UI__portNotification);

	regs->work.schedule.urid = map->map(map->handle, LV2_WORKER__schedule);

	regs->log.entry.urid = map->map(map->handle, LV2_LOG__Entry);
	regs->log.error.urid = map->map(map->handle, LV2_LOG__Error);
	regs->log.note.urid = map->map(map->handle, LV2_LOG__Note);
	regs->log.trace.urid = map->map(map->handle, LV2_LOG__Trace);
	regs->log.warning.urid = map->map(map->handle, LV2_LOG__Warning);
	
	regs->ui.eo.urid = map->map(map->handle, LV2_UI__EoUI);
	
	regs->pset.preset.urid = map->map(map->handle, LV2_PRESETS__Preset);
	regs->pset.rdfs_label.urid = map->map(map->handle, LILV_NS_RDFS"label");
	
	regs->bufsz.max_block_length.urid = map->map(map->handle, LV2_BUF_SIZE__maxBlockLength);
	regs->bufsz.min_block_length.urid = map->map(map->handle, LV2_BUF_SIZE__minBlockLength);
	regs->bufsz.sequence_size.urid = map->map(map->handle, LV2_BUF_SIZE__sequenceSize);
		
	regs->synthpod.event.urid = map->map(map->handle, SYNTHPOD_PREFIX"event");
	regs->synthpod.state.urid = map->map(map->handle, SYNTHPOD_PREFIX"state");
	regs->synthpod.module_list.urid = map->map(map->handle, SYNTHPOD_PREFIX"moduleList");
	regs->synthpod.module_add.urid = map->map(map->handle, SYNTHPOD_PREFIX"moduleAdd");
	regs->synthpod.module_del.urid = map->map(map->handle, SYNTHPOD_PREFIX"moduleDel");
	regs->synthpod.module_preset.urid = map->map(map->handle, SYNTHPOD_PREFIX"modulePreset");
	regs->synthpod.module_selected.urid = map->map(map->handle, SYNTHPOD_PREFIX"moduleSelect");
	regs->synthpod.port_refresh.urid = map->map(map->handle, SYNTHPOD_PREFIX"portRefresh");
	regs->synthpod.port_connected.urid = map->map(map->handle, SYNTHPOD_PREFIX"portConnect");
	regs->synthpod.port_subscribed.urid = map->map(map->handle, SYNTHPOD_PREFIX"portSubscribe");
	regs->synthpod.port_selected.urid = map->map(map->handle, SYNTHPOD_PREFIX"portSelect");
}

static inline void
sp_regs_deinit(reg_t *regs)
{
	// deinit nodes
	lilv_node_free(regs->port.input.node);
	lilv_node_free(regs->port.output.node);

	lilv_node_free(regs->port.control.node);
	lilv_node_free(regs->port.audio.node);
	lilv_node_free(regs->port.cv.node);
	lilv_node_free(regs->port.atom.node);

	lilv_node_free(regs->port.sequence.node);

	lilv_node_free(regs->port.midi.node);
	lilv_node_free(regs->port.osc.node);
	lilv_node_free(regs->port.chim_event.node);
	lilv_node_free(regs->port.chim_dump.node);

	lilv_node_free(regs->port.integer.node);
	lilv_node_free(regs->port.toggled.node);

	lilv_node_free(regs->port.float_protocol.node);
	lilv_node_free(regs->port.peak_protocol.node);
	lilv_node_free(regs->port.atom_transfer.node);
	lilv_node_free(regs->port.event_transfer.node);
	lilv_node_free(regs->port.notification.node);

	lilv_node_free(regs->work.schedule.node);

	lilv_node_free(regs->log.entry.node);
	lilv_node_free(regs->log.error.node);
	lilv_node_free(regs->log.note.node);
	lilv_node_free(regs->log.trace.node);
	lilv_node_free(regs->log.warning.node);

	lilv_node_free(regs->ui.eo.node);
	
	lilv_node_free(regs->pset.preset.node);
	lilv_node_free(regs->pset.rdfs_label.node);

}

#define _ATOM_ALIGNED __attribute__((aligned(8)))

// app <-> ui communication for module/port manipulations
typedef struct _transmit_t transmit_t;
typedef struct _transmit_module_list_t transmit_module_list_t;
typedef struct _transmit_module_add_t transmit_module_add_t;
typedef struct _transmit_module_del_t transmit_module_del_t;
typedef struct _transmit_module_preset_t transmit_module_preset_t;
typedef struct _transmit_module_selected_t transmit_module_selected_t;
typedef struct _transmit_port_connected_t transmit_port_connected_t;
typedef struct _transmit_port_subscribed_t transmit_port_subscribed_t;
typedef struct _transmit_port_refresh_t transmit_port_refresh_t;
typedef struct _transmit_port_selected_t transmit_port_selected_t;

struct _transmit_t {
	LV2_Atom_Object obj _ATOM_ALIGNED;
	LV2_Atom_Property_Body prop _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_list_t {
	transmit_t transmit _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_add_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_String uri _ATOM_ALIGNED;
		char uri_str [0] _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_del_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_preset_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_String label _ATOM_ALIGNED;
		char label_str [0] _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_selected_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_Int state _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_port_connected_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int src_uid _ATOM_ALIGNED;
	LV2_Atom_Int src_port _ATOM_ALIGNED;
	LV2_Atom_Int snk_uid _ATOM_ALIGNED;
	LV2_Atom_Int snk_port _ATOM_ALIGNED;
	LV2_Atom_Int state _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_port_subscribed_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_Int port _ATOM_ALIGNED;
	LV2_Atom_URID prot _ATOM_ALIGNED;
	LV2_Atom_Int state _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_port_refresh_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_Int port _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_port_selected_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_Int port _ATOM_ALIGNED;
	LV2_Atom_Int state _ATOM_ALIGNED;
} _ATOM_ALIGNED;

// app <-> ui communication for port notifications
typedef struct _transfer_t transfer_t;
typedef struct _transfer_float_t transfer_float_t;
typedef struct _transfer_peak_t transfer_peak_t;
typedef struct _transfer_atom_t transfer_atom_t;

struct _transfer_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_Int port _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transfer_float_t {
	transfer_t transfer _ATOM_ALIGNED;
	LV2_Atom_Float value _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transfer_peak_t {
	transfer_t transfer _ATOM_ALIGNED;
	LV2_Atom_Int period_start _ATOM_ALIGNED;
	LV2_Atom_Int period_size _ATOM_ALIGNED;
	LV2_Atom_Float peak _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transfer_atom_t {
	transfer_t transfer _ATOM_ALIGNED;
	LV2_Atom atom [0] _ATOM_ALIGNED;
};

static inline void
_sp_transmit_fill(reg_t *regs, LV2_Atom_Forge *forge, transmit_t *trans, uint32_t size,
	LV2_URID protocol)
{
	trans->obj.atom.size = size - sizeof(LV2_Atom);
	trans->obj.atom.type = forge->Object;
	trans->obj.body.id = 0;
	trans->obj.body.otype = regs->synthpod.event.urid;

	trans->prop.key = protocol;
	trans->prop.context = 0;
	trans->prop.value.size = size - sizeof(transmit_t);
	trans->prop.value.type = forge->Tuple;
}

static inline void
_sp_transmit_module_list_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_list_t *trans, uint32_t size)
{
	_sp_transmit_fill(regs, forge, &trans->transmit, size, regs->synthpod.module_list.urid);
}

static inline void
_sp_transmit_module_add_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_add_t *trans, uint32_t size,
	u_id_t module_uid, const char *module_uri)
{
	_sp_transmit_fill(regs, forge, &trans->transmit, size, regs->synthpod.module_add.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	if(module_uri)
	{
		trans->uri.atom.size = strlen(module_uri) + 1;
		strcpy(trans->uri_str, module_uri);
	}
	else
		trans->uri.atom.size = 0;
	trans->uri.atom.type = forge->String;
}

static inline void
_sp_transmit_module_del_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_del_t *trans, uint32_t size, u_id_t module_uid)
{
	_sp_transmit_fill(regs, forge, &trans->transmit, size, regs->synthpod.module_del.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;
}

static inline void
_sp_transmit_module_preset_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_preset_t *trans, uint32_t size, u_id_t module_uid, const char *label)
{
	_sp_transmit_fill(regs, forge, &trans->transmit, size, regs->synthpod.module_preset.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->label.atom.size = strlen(label) + 1;
	trans->label.atom.type = forge->String;
	strcpy(trans->label_str, label);
}

static inline void
_sp_transmit_module_selected_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_selected_t *trans, uint32_t size, u_id_t module_uid, int state)
{
	_sp_transmit_fill(regs, forge, &trans->transmit, size, regs->synthpod.module_selected.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->state.atom.size = sizeof(int32_t);
	trans->state.atom.type = forge->Int;
	trans->state.body = state;
}

static inline void
_sp_transmit_port_connected_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_port_connected_t *trans, uint32_t size, u_id_t src_uid,
	uint32_t src_port, u_id_t snk_uid, uint32_t snk_port, int32_t state)
{
	_sp_transmit_fill(regs, forge, &trans->transmit, size, regs->synthpod.port_connected.urid);

	trans->src_uid.atom.size = sizeof(int32_t);
	trans->src_uid.atom.type = forge->Int;
	trans->src_uid.body = src_uid;

	trans->src_port.atom.size = sizeof(int32_t);
	trans->src_port.atom.type = forge->Int;
	trans->src_port.body = src_port;
	
	trans->snk_uid.atom.size = sizeof(int32_t);
	trans->snk_uid.atom.type = forge->Int;
	trans->snk_uid.body = snk_uid;

	trans->snk_port.atom.size = sizeof(int32_t);
	trans->snk_port.atom.type = forge->Int;
	trans->snk_port.body = snk_port;
	
	trans->state.atom.size = sizeof(int32_t);
	trans->state.atom.type = forge->Int;
	trans->state.body = state; // -1 (query), 0 (disconnected), 1 (connected)
}

static inline void
_sp_transmit_port_subscribed_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_port_subscribed_t *trans, uint32_t size,
	u_id_t module_uid, uint32_t port_index, LV2_URID prot, int32_t state)
{
	_sp_transmit_fill(regs, forge, &trans->transmit, size, regs->synthpod.port_subscribed.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->port.atom.size = sizeof(int32_t);
	trans->port.atom.type = forge->Int;
	trans->port.body = port_index;
	
	trans->prot.atom.size = sizeof(LV2_URID);
	trans->prot.atom.type = forge->URID;
	trans->prot.body = prot;
	
	trans->state.atom.size = sizeof(int32_t);
	trans->state.atom.type = forge->Int;
	trans->state.body = state; // -1 (query), 0 (disconnected), 1 (connected)
}

static inline void
_sp_transmit_port_refresh_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_port_refresh_t *trans, uint32_t size,
	u_id_t module_uid, uint32_t port_index)
{
	_sp_transmit_fill(regs, forge, &trans->transmit, size, regs->synthpod.port_refresh.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->port.atom.size = sizeof(int32_t);
	trans->port.atom.type = forge->Int;
	trans->port.body = port_index;
}

static inline void
_sp_transmit_port_selected_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_port_selected_t *trans, uint32_t size,
	u_id_t module_uid, uint32_t port_index, int32_t state)
{
	_sp_transmit_fill(regs, forge, &trans->transmit, size, regs->synthpod.port_selected.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->port.atom.size = sizeof(int32_t);
	trans->port.atom.type = forge->Int;
	trans->port.body = port_index;
	
	trans->state.atom.size = sizeof(int32_t);
	trans->state.atom.type = forge->Int;
	trans->state.body = state;
}

static inline void
_sp_transfer_fill(reg_t *regs, LV2_Atom_Forge *forge, transfer_t *trans, uint32_t size,
	LV2_URID protocol, u_id_t module_uid, uint32_t port_index)
{
	_sp_transmit_fill(regs, forge, &trans->transmit, size, protocol);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;
	
	trans->port.atom.size = sizeof(int32_t);
	trans->port.atom.type = forge->Int;
	trans->port.body = port_index;
}

static inline void
_sp_transfer_float_fill(reg_t *regs, LV2_Atom_Forge *forge, transfer_float_t *trans,
	u_id_t module_uid, uint32_t port_index, const float *value)
{
	_sp_transfer_fill(regs, forge, &trans->transfer, sizeof(transfer_float_t),
		regs->port.float_protocol.urid, module_uid, port_index);
	
	trans->value.atom.size = sizeof(float);
	trans->value.atom.type = forge->Float;
	trans->value.body = *value;
}

static inline void
_sp_transfer_peak_fill(reg_t *regs, LV2_Atom_Forge *forge, transfer_peak_t *trans,
	u_id_t module_uid, uint32_t port_index, const LV2UI_Peak_Data *data)
{
	_sp_transfer_fill(regs, forge, &trans->transfer, sizeof(transfer_peak_t),
		regs->port.peak_protocol.urid, module_uid, port_index);
	
	trans->period_start.atom.size = sizeof(uint32_t);
	trans->period_start.atom.type = forge->Int;
	trans->period_start.body = data->period_start;
	
	trans->period_size.atom.size = sizeof(uint32_t);
	trans->period_size.atom.type = forge->Int;
	trans->period_size.body = data->period_size;
	
	trans->peak.atom.size = sizeof(float);
	trans->peak.atom.type = forge->Float;
	trans->peak.body = data->peak;
}

static inline void
_sp_transfer_atom_fill(reg_t *regs, LV2_Atom_Forge *forge, transfer_atom_t *trans,
	u_id_t module_uid, uint32_t port_index, const LV2_Atom *atom)
{
	uint32_t atom_size = sizeof(LV2_Atom) + atom->size;

	_sp_transfer_fill(regs, forge, &trans->transfer, sizeof(transfer_atom_t) + lv2_atom_pad_size(atom_size),
		regs->port.atom_transfer.urid, module_uid, port_index);

	memcpy(trans->atom, atom, atom_size);
}

static inline void
_sp_transfer_event_fill(reg_t *regs, LV2_Atom_Forge *forge, transfer_atom_t *trans,
	u_id_t module_uid, uint32_t port_index, const LV2_Atom *atom)
{
	uint32_t atom_size = sizeof(LV2_Atom) + atom->size;

	_sp_transfer_fill(regs, forge, &trans->transfer, sizeof(transfer_atom_t) + lv2_atom_pad_size(atom_size),
		regs->port.event_transfer.urid, module_uid, port_index);

	memcpy(trans->atom, atom, atom_size);
}

static const char *
_preset_label_get(LilvWorld *world, reg_t *regs, const LilvNode *preset)
{
	lilv_world_load_resource(world, preset);
	LilvNodes* labels = lilv_world_find_nodes(world, preset,
		regs->pset.rdfs_label.node, NULL);
	if(labels)
	{
		const LilvNode *label = lilv_nodes_get_first(labels);
		const char *lbl = lilv_node_as_string(label);
		lilv_nodes_free(labels);

		return lbl;
	}

	return NULL;
}

#endif // _SYNTHPOD_PRIVATE_H
