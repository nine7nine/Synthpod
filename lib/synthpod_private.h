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

#define SYNTHPOD_PREFIX				"http://open-music-kontrollers.ch/synthpod#"
#define LV2_UI__EoUI          LV2_UI_PREFIX"EoUI"

#include <lilv/lilv.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
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
		reg_item_t module_add;
		reg_item_t module_del;
		reg_item_t port_update;
		reg_item_t port_connect;
		reg_item_t port_disconnect;

		reg_item_t module_index;
		reg_item_t module_source_index;
		reg_item_t module_sink_index;

		reg_item_t port_index;
		reg_item_t port_source_index;
		reg_item_t port_sink_index;
		
		reg_item_t port_value;
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
		
	regs->synthpod.module_add.urid = map->map(map->handle, SYNTHPOD_PREFIX"moduleAdd");
	regs->synthpod.module_del.urid = map->map(map->handle, SYNTHPOD_PREFIX"moduleDel");
	regs->synthpod.port_update.urid = map->map(map->handle, SYNTHPOD_PREFIX"portUpdate");
	regs->synthpod.port_connect.urid = map->map(map->handle, SYNTHPOD_PREFIX"portConnect");
	regs->synthpod.port_disconnect.urid = map->map(map->handle, SYNTHPOD_PREFIX"portDisconnect");

	regs->synthpod.module_index.urid = map->map(map->handle, SYNTHPOD_PREFIX"moduleIndex");
	regs->synthpod.module_source_index.urid = map->map(map->handle, SYNTHPOD_PREFIX"moduleSourceIndex");
	regs->synthpod.module_sink_index.urid = map->map(map->handle, SYNTHPOD_PREFIX"moduleSinkIndex");

	regs->synthpod.port_index.urid = map->map(map->handle, SYNTHPOD_PREFIX"portIndex");
	regs->synthpod.port_source_index.urid = map->map(map->handle, SYNTHPOD_PREFIX"portSourceIndex");
	regs->synthpod.port_sink_index.urid = map->map(map->handle, SYNTHPOD_PREFIX"portSinkIndex");
	
	regs->synthpod.port_value.urid = map->map(map->handle, SYNTHPOD_PREFIX"portValue");
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
}

#define _ATOM_ALIGNED __attribute__((aligned(8)))

// app <-> ui communication for module/port manipulations
typedef struct _transmit_t transmit_t;
typedef struct _transmit_module_add_t transmit_module_add_t;
typedef struct _transmit_module_del_t transmit_module_del_t;
typedef struct _transmit_port_connect_t transmit_port_connect_t;
typedef struct _transmit_port_disconnect_t transmit_port_disconnect_t;

struct _transmit_t {
	LV2_Atom_Tuple tuple _ATOM_ALIGNED;
	LV2_Atom_URID protocol _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_add_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_String uri _ATOM_ALIGNED;
		char str [0] _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_del_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_String uuid _ATOM_ALIGNED;
		char str [37] _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_port_connect_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_String src_uuid _ATOM_ALIGNED;
		char src_str [37] _ATOM_ALIGNED;
	LV2_Atom_Int src_port _ATOM_ALIGNED;
	LV2_Atom_String snk_uuid _ATOM_ALIGNED;
		char snk_str [37] _ATOM_ALIGNED;
	LV2_Atom_Int snk_port _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_port_disconnect_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_String src_uuid _ATOM_ALIGNED;
		char src_str [37] _ATOM_ALIGNED;
	LV2_Atom_Int src_port _ATOM_ALIGNED;
	LV2_Atom_String snk_uuid _ATOM_ALIGNED;
		char snk_str [37] _ATOM_ALIGNED;
	LV2_Atom_Int snk_port _ATOM_ALIGNED;
} _ATOM_ALIGNED;

// app <-> ui communication for port notifications
typedef struct _transfer_t transfer_t;
typedef struct _transfer_float_t transfer_float_t;
typedef struct _transfer_peak_t transfer_peak_t;
typedef struct _transfer_atom_t transfer_atom_t;

struct _transfer_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_String uuid _ATOM_ALIGNED;
		char str [37] _ATOM_ALIGNED; //TODO use
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

#endif // _SYNTHPOD_PRIVATE_H
