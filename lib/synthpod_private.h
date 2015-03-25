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

#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

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

#endif // _SYNTHPOD_PRIVATE_H
