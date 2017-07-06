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

#ifndef _SYNTHPOD_PRIVATE_H
#define _SYNTHPOD_PRIVATE_H

#define SYNTHPOD_WORLD				SYNTHPOD_PREFIX"world"

#if defined(HAS_BUILTIN_ASSUME_ALIGNED)
#	define ASSUME_ALIGNED(PTR) __builtin_assume_aligned((PTR), 8)
#else
#	define ASSUME_ALIGNED(PTR) (PTR)
#endif

#include <lilv/lilv.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/uri-map/uri-map.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/buf-size/buf-size.h>
#include <lv2/lv2plug.in/ns/ext/resize-port/resize-port.h>
#include <lv2/lv2plug.in/ns/ext/presets/presets.h>
#include <lv2/lv2plug.in/ns/ext/patch/patch.h>
#include <lv2/lv2plug.in/ns/ext/port-props/port-props.h>
#include <lv2/lv2plug.in/ns/ext/port-groups/port-groups.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/time/time.h>
#include <lv2/lv2plug.in/ns/ext/parameters/parameters.h>
#include <lv2/lv2plug.in/ns/ext/instance-access/instance-access.h>
#include <lv2/lv2plug.in/ns/ext/data-access/data-access.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>
#include <lv2/lv2plug.in/ns/extensions/units/units.h>
#include <zero_worker.h>
#include <lv2_external_ui.h> // kxstudio kx-ui extension

#include <osc.lv2/osc.h>

typedef enum _port_type_t port_type_t;
typedef enum _port_atom_type_t port_atom_type_t;
typedef enum _port_buffer_type_t port_buffer_type_t;
typedef enum _port_direction_t port_direction_t;
typedef enum _port_protocol_t port_protocol_t;

enum _port_type_t {
	PORT_TYPE_AUDIO,
	PORT_TYPE_CONTROL,
	PORT_TYPE_CV,
	PORT_TYPE_ATOM,

	PORT_TYPE_NUM
};

enum _port_atom_type_t {
	PORT_ATOM_TYPE_ALL		= 0,

	PORT_ATOM_TYPE_MIDI		= (1 << 0),
	PORT_ATOM_TYPE_OSC		= (1 << 1),
	PORT_ATOM_TYPE_TIME		= (1 << 2),
	PORT_ATOM_TYPE_PATCH	= (1 << 3),
	PORT_ATOM_TYPE_XPRESS	= (1 << 4)
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
		reg_item_t osc_event;
		reg_item_t time_position;

		// control port property
		reg_item_t integer;
		reg_item_t enumeration;
		reg_item_t toggled;
		reg_item_t is_bitmask;

		// port protocols
		reg_item_t float_protocol;
		reg_item_t peak_protocol;
		reg_item_t atom_transfer;
		reg_item_t event_transfer;

		reg_item_t notification;
		reg_item_t minimum_size;

		// properties
		reg_item_t logarithmic;
	} port;

	struct {
		reg_item_t message;
		reg_item_t path;
	} osc;

	struct {
		reg_item_t property;
	} parameter;

	struct {
		reg_item_t schedule;
	} work;

	struct {
		reg_item_t schedule;
	} zero;

	struct {
		reg_item_t entry;
		reg_item_t error;
		reg_item_t note;
		reg_item_t trace;
		reg_item_t warning;
	} log;

	struct {
		reg_item_t window_title;
		reg_item_t show_interface;
		reg_item_t idle_interface;
		reg_item_t kx_widget;
		reg_item_t external;
		reg_item_t x11;
		reg_item_t gtk2;
		reg_item_t gtk3;
		reg_item_t qt4;
		reg_item_t qt5;
		reg_item_t plugin;
		reg_item_t protocol;
		reg_item_t period_start;
		reg_item_t period_size;
		reg_item_t peak;
		reg_item_t port_subscribe;
		reg_item_t port_index;
		reg_item_t update_rate;
		reg_item_t instance_access;
		reg_item_t data_access;
	} ui;

	struct {
		reg_item_t preset;
		reg_item_t preset_bank;
		reg_item_t bank;
	} pset;

	struct {
		reg_item_t value;
		reg_item_t type;
		reg_item_t subject;
	} rdf;

	struct {
		reg_item_t label;
		reg_item_t range;
		reg_item_t comment;
		reg_item_t see_also;
	} rdfs;

	struct {
		reg_item_t license;
		reg_item_t name;
	} doap;

	struct {
		reg_item_t optional_feature;
		reg_item_t required_feature;
		reg_item_t name;
		reg_item_t minor_version;
		reg_item_t micro_version;
		reg_item_t extension_data;
		reg_item_t control;
		reg_item_t symbol;
		reg_item_t index;
		reg_item_t minimum;
		reg_item_t maximum;
		reg_item_t scale_point;
		reg_item_t port;
		reg_item_t Port;
		reg_item_t plugin;
		reg_item_t applies_to;
		reg_item_t designation;
	} core;

	struct {
		reg_item_t sample_rate;
		reg_item_t gain;
	} param;

	struct {
		reg_item_t nominal_block_length;
		reg_item_t max_block_length;
		reg_item_t min_block_length;
		reg_item_t sequence_size;
	} bufsz;

	struct {
		reg_item_t writable;
		reg_item_t readable;
		reg_item_t message;
		reg_item_t set;
		reg_item_t get;
		reg_item_t subject;
		reg_item_t body;
		reg_item_t property;
		reg_item_t value;
		reg_item_t wildcard;
		reg_item_t patch;
		reg_item_t add;
		reg_item_t remove;
		reg_item_t put;
		reg_item_t insert;
		reg_item_t delete;
		reg_item_t destination;
		reg_item_t copy;
		reg_item_t move;
		reg_item_t sequence_number;
		reg_item_t error;
		reg_item_t ack;
	} patch;

	struct {
		reg_item_t message;
	} xpress;

	struct {
		reg_item_t group;
		reg_item_t left;
		reg_item_t right;
		reg_item_t center;
		reg_item_t side;
		reg_item_t center_left;
		reg_item_t center_right;
		reg_item_t side_left;
		reg_item_t side_right;
		reg_item_t rear_left;
		reg_item_t rear_right;
		reg_item_t rear_center;
		reg_item_t low_frequency_effects;
	} group;

	struct {
		// properties
		reg_item_t conversion;
		reg_item_t prefixConversion;
		reg_item_t render;
		reg_item_t symbol;
		reg_item_t unit;

		// instances;
		reg_item_t bar;
		reg_item_t beat;
		reg_item_t bpm;
		reg_item_t cent;
		reg_item_t cm;
		reg_item_t coef;
		reg_item_t db;
		reg_item_t degree;
		reg_item_t frame;
		reg_item_t hz;
		reg_item_t inch;
		reg_item_t khz;
		reg_item_t km;
		reg_item_t m;
		reg_item_t mhz;
		reg_item_t midiNote;
		reg_item_t midiController;
		reg_item_t mile;
		reg_item_t min;
		reg_item_t mm;
		reg_item_t ms;
		reg_item_t oct;
		reg_item_t pc;
		reg_item_t s;
		reg_item_t semitone12TET;
	} units;

	struct {
		reg_item_t state;
		reg_item_t load_default_state;
		reg_item_t thread_safe_restore;
	} state;

	struct {
		reg_item_t payload;
		reg_item_t state;
		reg_item_t graph;
		reg_item_t null;
		reg_item_t stereo;
		reg_item_t monoatom;

		reg_item_t module_list;
		reg_item_t module_supported;
		reg_item_t module_add;
		reg_item_t module_del;
		reg_item_t module_move;
		reg_item_t module_preset_load;
		reg_item_t module_preset_save;
		reg_item_t module_visible;
		reg_item_t module_disabled;
		reg_item_t module_profiling;
		reg_item_t module_position_x;
		reg_item_t module_position_y;
		reg_item_t node_position_x;
		reg_item_t node_position_y;
		reg_item_t port_refresh;
		reg_item_t bundle_load;
		reg_item_t bundle_save;
		reg_item_t path_get;
		reg_item_t dsp_profiling;
		reg_item_t cpus_available;
		reg_item_t cpus_used;
		reg_item_t grid_cols;
		reg_item_t grid_rows;
		reg_item_t pane_left;
		reg_item_t quit;

		reg_item_t system_ports;
		reg_item_t control_port;
		reg_item_t audio_port;
		reg_item_t cv_port;
		reg_item_t midi_port;
		reg_item_t osc_port;
		reg_item_t com_port;

		reg_item_t subscription_list;
		reg_item_t notification_list;
		reg_item_t automation_list;
		reg_item_t connection_list;
		reg_item_t node_list;

		reg_item_t source_module;
		reg_item_t source_symbol;
		reg_item_t sink_module;
		reg_item_t sink_symbol;

		reg_item_t source_min;
		reg_item_t source_max;
		reg_item_t sink_min;
		reg_item_t sink_max;
	} synthpod;

	struct {
		reg_item_t Controller;
		reg_item_t channel;
		reg_item_t controller_number;
	} midi;
};

static inline void
_register(reg_item_t *itm, LilvWorld *world, LV2_URID_Map *map, const char *uri)
{
	itm->node = lilv_new_uri(world, uri);
	itm->urid = map->map(map->handle, uri);
}

static inline void
_register_string(reg_item_t *itm, LilvWorld *world, LV2_URID_Map *map, const char *uri)
{
	itm->node = lilv_new_string(world, uri);
	itm->urid = map->map(map->handle, uri);
}

static inline void
_unregister(reg_item_t *itm)
{
	if(itm->node)
		lilv_node_free(itm->node);
}

static inline void
sp_regs_init(reg_t *regs, LilvWorld *world, LV2_URID_Map *map)
{
	_register(&regs->port.input, world, map, LV2_CORE__InputPort);
	_register(&regs->port.output, world, map, LV2_CORE__OutputPort);

	_register(&regs->port.control, world, map, LV2_CORE__ControlPort);
	_register(&regs->port.audio, world, map, LV2_CORE__AudioPort);
	_register(&regs->port.cv, world, map, LV2_CORE__CVPort);
	_register(&regs->port.atom, world, map, LV2_ATOM__AtomPort);

	_register(&regs->port.sequence, world, map, LV2_ATOM__Sequence);
	_register(&regs->port.midi, world, map, LV2_MIDI__MidiEvent);
	_register(&regs->port.osc_event, world, map, LV2_OSC__Event);
	_register(&regs->port.time_position, world, map, LV2_TIME__Position);

	_register(&regs->port.integer, world, map, LV2_CORE__integer);
	_register(&regs->port.enumeration, world, map, LV2_CORE__enumeration);
	_register(&regs->port.toggled, world, map, LV2_CORE__toggled);
	_register(&regs->port.is_bitmask, world, map, LV2_CORE_PREFIX"isBitmask");

	_register(&regs->port.float_protocol, world, map, LV2_UI_PREFIX"floatProtocol");
	_register(&regs->port.peak_protocol, world, map, LV2_UI_PREFIX"peakProtocol");
	_register(&regs->port.atom_transfer, world, map, LV2_ATOM__atomTransfer);
	_register(&regs->port.event_transfer, world, map, LV2_ATOM__eventTransfer);
	_register(&regs->port.notification, world, map, LV2_UI__portNotification);

	_register(&regs->port.minimum_size, world, map, LV2_RESIZE_PORT__minimumSize);

	_register(&regs->port.logarithmic, world, map, LV2_PORT_PROPS__logarithmic);

	_register(&regs->parameter.property, world, map, LV2_CORE_PREFIX"parameterProperty");

	_register(&regs->osc.message, world, map, LV2_OSC__Message);
	_register(&regs->osc.path, world, map, LV2_OSC__messagePath);

	_register(&regs->work.schedule, world, map, LV2_WORKER__schedule);
	_register(&regs->zero.schedule, world, map, ZERO_WORKER__schedule);

	_register(&regs->log.entry, world, map, LV2_LOG__Entry);
	_register(&regs->log.error, world, map, LV2_LOG__Error);
	_register(&regs->log.note, world, map, LV2_LOG__Note);
	_register(&regs->log.trace, world, map, LV2_LOG__Trace);
	_register(&regs->log.warning, world, map, LV2_LOG__Warning);

	_register(&regs->ui.window_title, world, map, LV2_UI__windowTitle);
	_register(&regs->ui.show_interface, world, map, LV2_UI__showInterface);
	_register(&regs->ui.idle_interface, world, map, LV2_UI__idleInterface);
	_register(&regs->ui.kx_widget, world, map, LV2_EXTERNAL_UI__Widget);
	_register(&regs->ui.external, world, map, LV2_EXTERNAL_UI_DEPRECATED_URI);
	_register(&regs->ui.x11, world, map, LV2_UI__X11UI);
	_register(&regs->ui.gtk2, world, map, LV2_UI__GtkUI);
	_register(&regs->ui.gtk3, world, map, LV2_UI__Gtk3UI);
	_register(&regs->ui.qt4, world, map, LV2_UI__Qt4UI);
	_register(&regs->ui.qt5, world, map, LV2_UI_PREFIX"Qt5UI");
	_register(&regs->ui.plugin, world, map, LV2_UI__plugin);
	_register(&regs->ui.protocol, world, map, LV2_UI_PREFIX"protocol");
	_register(&regs->ui.period_start, world, map, LV2_UI_PREFIX"periodStart");
	_register(&regs->ui.period_size, world, map, LV2_UI_PREFIX"periodSize");
	_register(&regs->ui.peak, world, map, LV2_UI_PREFIX"peak");
	_register(&regs->ui.port_subscribe, world, map, LV2_UI__portSubscribe);
	_register(&regs->ui.port_index, world, map, LV2_UI__portIndex);
	_register(&regs->ui.update_rate, world, map, LV2_UI__updateRate);
	_register(&regs->ui.instance_access, world, map, LV2_INSTANCE_ACCESS_URI);
	_register(&regs->ui.data_access, world, map, LV2_DATA_ACCESS_URI);

#ifndef LV2_PRESETS__bank
#	define LV2_PRESETS__bank LV2_PRESETS_PREFIX "bank"
#endif
#ifndef LV2_PRESETS__Bank
#	define LV2_PRESETS__Bank LV2_PRESETS_PREFIX "Bank"
#endif
	_register(&regs->pset.preset, world, map, LV2_PRESETS__Preset);
	_register(&regs->pset.preset_bank, world, map, LV2_PRESETS__bank);
	_register(&regs->pset.bank, world, map, LV2_PRESETS__Bank);
	
	_register(&regs->rdf.value, world, map, LILV_NS_RDF"value");
	_register(&regs->rdf.type, world, map, LILV_NS_RDF"type");
	_register(&regs->rdf.subject, world, map, LILV_NS_RDF"subject");

	_register(&regs->rdfs.label, world, map, LILV_NS_RDFS"label");
	_register(&regs->rdfs.range, world, map, LILV_NS_RDFS"range");
	_register(&regs->rdfs.comment, world, map, LILV_NS_RDFS"comment");
	_register(&regs->rdfs.see_also, world, map, LILV_NS_RDFS"seeAlso");

	_register(&regs->doap.license, world, map, LILV_NS_DOAP"license");
	_register(&regs->doap.name, world, map, LILV_NS_DOAP"name");

	_register(&regs->core.optional_feature, world, map, LV2_CORE__optionalFeature);
	_register(&regs->core.required_feature, world, map, LV2_CORE__requiredFeature);
	_register(&regs->core.name, world, map, LV2_CORE__name);
	_register(&regs->core.minor_version, world, map, LV2_CORE__minorVersion);
	_register(&regs->core.micro_version, world, map, LV2_CORE__microVersion);
	_register(&regs->core.extension_data, world, map, LV2_CORE__extensionData);
	_register(&regs->core.control, world, map, LV2_CORE__control);
	_register(&regs->core.symbol, world, map, LV2_CORE__symbol);
	_register(&regs->core.index, world, map, LV2_CORE__index);
	_register(&regs->core.minimum, world, map, LV2_CORE__minimum);
	_register(&regs->core.maximum, world, map, LV2_CORE__maximum);
	_register(&regs->core.scale_point, world, map, LV2_CORE__scalePoint);
	_register(&regs->core.port, world, map, LV2_CORE__port);
	_register(&regs->core.Port, world, map, LV2_CORE__Port);
	_register(&regs->core.plugin, world, map, LV2_CORE__Plugin);
	_register(&regs->core.applies_to, world, map, LV2_CORE__appliesTo);
	_register(&regs->core.designation, world, map, LV2_CORE__designation);

	_register(&regs->param.sample_rate, world, map, LV2_PARAMETERS__sampleRate);
	_register(&regs->param.gain, world, map, LV2_PARAMETERS__gain);

	_register(&regs->bufsz.nominal_block_length, world, map, LV2_BUF_SIZE_PREFIX "nominalBlockLength");
	_register(&regs->bufsz.max_block_length, world, map, LV2_BUF_SIZE__maxBlockLength);
	_register(&regs->bufsz.min_block_length, world, map, LV2_BUF_SIZE__minBlockLength);
	_register(&regs->bufsz.sequence_size, world, map, LV2_BUF_SIZE__sequenceSize);

	_register(&regs->patch.writable, world, map, LV2_PATCH__writable);
	_register(&regs->patch.readable, world, map, LV2_PATCH__readable);
	_register(&regs->patch.message, world, map, LV2_PATCH__Message);
	_register(&regs->patch.set, world, map, LV2_PATCH__Set);
	_register(&regs->patch.get, world, map, LV2_PATCH__Get);
	_register(&regs->patch.subject, world, map, LV2_PATCH__subject);
	_register(&regs->patch.body, world, map, LV2_PATCH__body);
	_register(&regs->patch.property, world, map, LV2_PATCH__property);
	_register(&regs->patch.value, world, map, LV2_PATCH__value);
	_register(&regs->patch.wildcard, world, map, LV2_PATCH__wildcard);
	_register(&regs->patch.patch, world, map, LV2_PATCH__Patch);
	_register(&regs->patch.add, world, map, LV2_PATCH__add);
	_register(&regs->patch.remove, world, map, LV2_PATCH__remove);
	_register(&regs->patch.put, world, map, LV2_PATCH__Put);
	_register(&regs->patch.insert, world, map, LV2_PATCH_PREFIX "Insert");
	_register(&regs->patch.delete, world, map, LV2_PATCH_PREFIX "Delete");
	_register(&regs->patch.destination, world, map, LV2_PATCH__destination);
	_register(&regs->patch.copy, world, map, LV2_PATCH__Copy);
	_register(&regs->patch.move, world, map, LV2_PATCH__Move);
	_register(&regs->patch.sequence_number, world, map, LV2_PATCH__sequenceNumber);
	_register(&regs->patch.error, world, map, LV2_PATCH__Error);
	_register(&regs->patch.ack, world, map, LV2_PATCH__Ack);

	_register(&regs->xpress.message, world, map, "http://open-music-kontrollers.ch/lv2/xpress#Message");

	_register(&regs->group.group, world, map, LV2_PORT_GROUPS__group);
	_register(&regs->group.left, world, map, LV2_PORT_GROUPS__left);
	_register(&regs->group.right, world, map, LV2_PORT_GROUPS__right);
	_register(&regs->group.center, world, map, LV2_PORT_GROUPS__center);
	_register(&regs->group.side, world, map, LV2_PORT_GROUPS__side);
	_register(&regs->group.center_left, world, map, LV2_PORT_GROUPS__centerLeft);
	_register(&regs->group.center_right, world, map, LV2_PORT_GROUPS__centerRight);
	_register(&regs->group.side_left, world, map, LV2_PORT_GROUPS__sideLeft);
	_register(&regs->group.side_right, world, map, LV2_PORT_GROUPS__sideRight);
	_register(&regs->group.rear_left, world, map, LV2_PORT_GROUPS__rearLeft);
	_register(&regs->group.rear_right, world, map, LV2_PORT_GROUPS__rearRight);
	_register(&regs->group.rear_center, world, map, LV2_PORT_GROUPS__rearCenter);
	_register(&regs->group.low_frequency_effects, world, map, LV2_PORT_GROUPS__lowFrequencyEffects);

	_register(&regs->units.conversion, world, map, LV2_UNITS__conversion);
	_register(&regs->units.prefixConversion, world, map, LV2_UNITS__prefixConversion);
	_register(&regs->units.render, world, map, LV2_UNITS__render);
	_register(&regs->units.symbol, world, map, LV2_UNITS__symbol);
	_register(&regs->units.unit, world, map, LV2_UNITS__unit);
	_register(&regs->units.bar, world, map, LV2_UNITS__bar);
	_register(&regs->units.beat, world, map, LV2_UNITS__beat);
	_register(&regs->units.bpm, world, map, LV2_UNITS__bpm);
	_register(&regs->units.cent, world, map, LV2_UNITS__cent);
	_register(&regs->units.cm, world, map, LV2_UNITS__cm);
	_register(&regs->units.coef, world, map, LV2_UNITS__coef);
	_register(&regs->units.db, world, map, LV2_UNITS__db);
	_register(&regs->units.degree, world, map, LV2_UNITS__degree);
	_register(&regs->units.frame, world, map, LV2_UNITS__frame);
	_register(&regs->units.hz, world, map, LV2_UNITS__hz);
	_register(&regs->units.inch, world, map, LV2_UNITS__inch);
	_register(&regs->units.khz, world, map, LV2_UNITS__khz);
	_register(&regs->units.km, world, map, LV2_UNITS__km);
	_register(&regs->units.m, world, map, LV2_UNITS__m);
	_register(&regs->units.mhz, world, map, LV2_UNITS__mhz);
	_register(&regs->units.midiNote, world, map, LV2_UNITS__midiNote);
	_register(&regs->units.midiController, world, map, LV2_UNITS_PREFIX"midiController");
	_register(&regs->units.mile, world, map, LV2_UNITS__mile);
	_register(&regs->units.min, world, map, LV2_UNITS__min);
	_register(&regs->units.mm, world, map, LV2_UNITS__mm);
	_register(&regs->units.ms, world, map, LV2_UNITS__ms);
	_register(&regs->units.oct, world, map, LV2_UNITS__oct);
	_register(&regs->units.pc, world, map, LV2_UNITS__pc);
	_register(&regs->units.s, world, map, LV2_UNITS__s);
	_register(&regs->units.semitone12TET, world, map, LV2_UNITS__semitone12TET);

	_register(&regs->state.state, world, map, LV2_STATE__state);
	_register(&regs->state.load_default_state, world, map, LV2_STATE__loadDefaultState);
#ifndef LV2_STATE__threadSafeRestore
#	define LV2_STATE__threadSafeRestore LV2_STATE_PREFIX "threadSafeRestore"
#endif
	_register(&regs->state.thread_safe_restore, world, map, LV2_STATE__threadSafeRestore);

	_register(&regs->synthpod.payload, world, map, SYNTHPOD_PREFIX"payload");
	_register_string(&regs->synthpod.state, world, map, "state.ttl");
	_register(&regs->synthpod.graph, world, map, SYNTHPOD_PREFIX"graph");
	_register_string(&regs->synthpod.null, world, map, "");
	_register(&regs->synthpod.stereo, world, map, SYNTHPOD_PREFIX"stereo");
	_register(&regs->synthpod.monoatom, world, map, SYNTHPOD_PREFIX"monoatom");
	_register(&regs->synthpod.module_list, world, map, SYNTHPOD_PREFIX"moduleList");
	_register(&regs->synthpod.module_supported, world, map, SYNTHPOD_PREFIX"moduleSupported");
	_register(&regs->synthpod.module_add, world, map, SYNTHPOD_PREFIX"moduleAdd");
	_register(&regs->synthpod.module_del, world, map, SYNTHPOD_PREFIX"moduleDel");
	_register(&regs->synthpod.module_move, world, map, SYNTHPOD_PREFIX"moduleMove");
	_register(&regs->synthpod.module_preset_load, world, map, SYNTHPOD_PREFIX"modulePresetLoad");
	_register(&regs->synthpod.module_preset_save, world, map, SYNTHPOD_PREFIX"modulePresetSave");
	_register(&regs->synthpod.module_visible, world, map, SYNTHPOD_PREFIX"moduleVisible");
	_register(&regs->synthpod.module_disabled, world, map, SYNTHPOD_PREFIX"moduleDisabled");
	_register(&regs->synthpod.module_profiling, world, map, SYNTHPOD_PREFIX"moduleProfiling");
	_register(&regs->synthpod.module_position_x, world, map, SYNTHPOD_PREFIX"modulePositionX");
	_register(&regs->synthpod.module_position_y, world, map, SYNTHPOD_PREFIX"modulePositionY");
	_register(&regs->synthpod.node_position_x, world, map, SYNTHPOD_PREFIX"nodePositionX");
	_register(&regs->synthpod.node_position_y, world, map, SYNTHPOD_PREFIX"nodePositionY");
	_register(&regs->synthpod.port_refresh, world, map, SYNTHPOD_PREFIX"portRefresh");
	_register(&regs->synthpod.bundle_load, world, map, SYNTHPOD_PREFIX"bundleLoad");
	_register(&regs->synthpod.bundle_save, world, map, SYNTHPOD_PREFIX"bundleSave");
	_register(&regs->synthpod.path_get, world, map, SYNTHPOD_PREFIX"pathGet");
	_register(&regs->synthpod.dsp_profiling, world, map, SYNTHPOD_PREFIX"DSPProfiling");
	_register(&regs->synthpod.cpus_available, world, map, SYNTHPOD_PREFIX"CPUsAvailable");
	_register(&regs->synthpod.cpus_used, world, map, SYNTHPOD_PREFIX"CPUsUsed");
	_register(&regs->synthpod.grid_cols, world, map, SYNTHPOD_PREFIX"gridCols");
	_register(&regs->synthpod.grid_rows, world, map, SYNTHPOD_PREFIX"gridRows");
	_register(&regs->synthpod.pane_left, world, map, SYNTHPOD_PREFIX"paneLeft");
	_register(&regs->synthpod.quit, world, map, SYNTHPOD_PREFIX"quit");
	
	_register(&regs->synthpod.system_ports, world, map, SYNTHPOD_PREFIX"systemPorts");
	_register(&regs->synthpod.control_port, world, map, SYNTHPOD_PREFIX"ControlPort");
	_register(&regs->synthpod.audio_port, world, map, SYNTHPOD_PREFIX"AudioPort");
	_register(&regs->synthpod.cv_port, world, map, SYNTHPOD_PREFIX"CVPort");
	_register(&regs->synthpod.midi_port, world, map, SYNTHPOD_PREFIX"MIDIPort");
	_register(&regs->synthpod.osc_port, world, map, SYNTHPOD_PREFIX"OSCPort");
	_register(&regs->synthpod.com_port, world, map, SYNTHPOD_PREFIX"ComPort");

	_register(&regs->synthpod.connection_list, world, map, SYNTHPOD_PREFIX"connectionList");
	_register(&regs->synthpod.node_list, world, map, SYNTHPOD_PREFIX"nodeList");
	_register(&regs->synthpod.subscription_list, world, map, SYNTHPOD_PREFIX"subscriptionList");
	_register(&regs->synthpod.notification_list, world, map, SYNTHPOD_PREFIX"notificationList");
	_register(&regs->synthpod.automation_list, world, map, SYNTHPOD_PREFIX"automationList");

	_register(&regs->synthpod.source_module, world, map, SYNTHPOD_PREFIX"sourceModule");
	_register(&regs->synthpod.source_symbol, world, map, SYNTHPOD_PREFIX"sourceSymbol");
	_register(&regs->synthpod.sink_module, world, map, SYNTHPOD_PREFIX"sinkModule");
	_register(&regs->synthpod.sink_symbol, world, map, SYNTHPOD_PREFIX"sinkSymbol");

	_register(&regs->synthpod.source_min, world, map, SYNTHPOD_PREFIX"sourceMinimum");
	_register(&regs->synthpod.source_max, world, map, SYNTHPOD_PREFIX"sourceMaximum");
	_register(&regs->synthpod.sink_min, world, map, SYNTHPOD_PREFIX"sinkMinimum");
	_register(&regs->synthpod.sink_max, world, map, SYNTHPOD_PREFIX"sinkMaximum");

	_register(&regs->midi.Controller, world, map, LV2_MIDI__Controller);
	_register(&regs->midi.channel, world, map, LV2_MIDI__channel);
	_register(&regs->midi.controller_number, world, map, LV2_MIDI__controllerNumber);
}

static inline void
sp_regs_deinit(reg_t *regs)
{
	_unregister(&regs->port.input);
	_unregister(&regs->port.output);

	_unregister(&regs->port.control);
	_unregister(&regs->port.audio);
	_unregister(&regs->port.cv);
	_unregister(&regs->port.atom);

	_unregister(&regs->port.sequence);
	_unregister(&regs->port.midi);
	_unregister(&regs->port.osc_event);
	_unregister(&regs->port.time_position);

	_unregister(&regs->port.integer);
	_unregister(&regs->port.enumeration);
	_unregister(&regs->port.toggled);
	_unregister(&regs->port.is_bitmask);

	_unregister(&regs->port.float_protocol);
	_unregister(&regs->port.peak_protocol);
	_unregister(&regs->port.atom_transfer);
	_unregister(&regs->port.event_transfer);
	_unregister(&regs->port.notification);

	_unregister(&regs->port.minimum_size);

	_unregister(&regs->port.logarithmic);

	_unregister(&regs->parameter.property);

	_unregister(&regs->osc.message);
	_unregister(&regs->osc.path);

	_unregister(&regs->work.schedule);
	_unregister(&regs->zero.schedule);

	_unregister(&regs->log.entry);
	_unregister(&regs->log.error);
	_unregister(&regs->log.note);
	_unregister(&regs->log.trace);
	_unregister(&regs->log.warning);

	_unregister(&regs->ui.window_title);
	_unregister(&regs->ui.show_interface);
	_unregister(&regs->ui.idle_interface);
	_unregister(&regs->ui.kx_widget);
	_unregister(&regs->ui.external);
	_unregister(&regs->ui.x11);
	_unregister(&regs->ui.gtk2);
	_unregister(&regs->ui.gtk3);
	_unregister(&regs->ui.qt4);
	_unregister(&regs->ui.qt5);
	_unregister(&regs->ui.plugin);
	_unregister(&regs->ui.protocol);
	_unregister(&regs->ui.period_start);
	_unregister(&regs->ui.period_size);
	_unregister(&regs->ui.peak);
	_unregister(&regs->ui.port_subscribe);
	_unregister(&regs->ui.port_index);
	_unregister(&regs->ui.update_rate);
	_unregister(&regs->ui.instance_access);
	_unregister(&regs->ui.data_access);

	_unregister(&regs->pset.preset);
	_unregister(&regs->pset.preset_bank);
	_unregister(&regs->pset.bank);
	
	_unregister(&regs->rdf.value);
	_unregister(&regs->rdf.type);
	_unregister(&regs->rdf.subject);

	_unregister(&regs->rdfs.label);
	_unregister(&regs->rdfs.range);
	_unregister(&regs->rdfs.comment);
	_unregister(&regs->rdfs.see_also);

	_unregister(&regs->doap.license);
	_unregister(&regs->doap.name);

	_unregister(&regs->core.optional_feature);
	_unregister(&regs->core.required_feature);
	_unregister(&regs->core.name);
	_unregister(&regs->core.minor_version);
	_unregister(&regs->core.micro_version);
	_unregister(&regs->core.extension_data);
	_unregister(&regs->core.control);
	_unregister(&regs->core.symbol);
	_unregister(&regs->core.index);
	_unregister(&regs->core.minimum);
	_unregister(&regs->core.maximum);
	_unregister(&regs->core.scale_point);
	_unregister(&regs->core.port);
	_unregister(&regs->core.Port);
	_unregister(&regs->core.plugin);
	_unregister(&regs->core.applies_to);
	_unregister(&regs->core.designation);

	_unregister(&regs->param.sample_rate);
	_unregister(&regs->param.gain);

	_unregister(&regs->bufsz.nominal_block_length);
	_unregister(&regs->bufsz.max_block_length);
	_unregister(&regs->bufsz.min_block_length);
	_unregister(&regs->bufsz.sequence_size);

	_unregister(&regs->patch.writable);
	_unregister(&regs->patch.readable);
	_unregister(&regs->patch.message);
	_unregister(&regs->patch.set);
	_unregister(&regs->patch.get);
	_unregister(&regs->patch.subject);
	_unregister(&regs->patch.body);
	_unregister(&regs->patch.property);
	_unregister(&regs->patch.value);
	_unregister(&regs->patch.wildcard);
	_unregister(&regs->patch.patch);
	_unregister(&regs->patch.add);
	_unregister(&regs->patch.remove);
	_unregister(&regs->patch.put);
	_unregister(&regs->patch.insert);
	_unregister(&regs->patch.delete);
	_unregister(&regs->patch.destination);
	_unregister(&regs->patch.copy);
	_unregister(&regs->patch.move);
	_unregister(&regs->patch.sequence_number);
	_unregister(&regs->patch.error);
	_unregister(&regs->patch.ack);

	_unregister(&regs->xpress.message);

	_unregister(&regs->group.group);
	_unregister(&regs->group.left);
	_unregister(&regs->group.right);
	_unregister(&regs->group.center);
	_unregister(&regs->group.side);
	_unregister(&regs->group.center_left);
	_unregister(&regs->group.center_right);
	_unregister(&regs->group.side_left);
	_unregister(&regs->group.side_right);
	_unregister(&regs->group.rear_left);
	_unregister(&regs->group.rear_right);
	_unregister(&regs->group.rear_center);
	_unregister(&regs->group.low_frequency_effects);

	_unregister(&regs->units.conversion);
	_unregister(&regs->units.prefixConversion);
	_unregister(&regs->units.render);
	_unregister(&regs->units.symbol);
	_unregister(&regs->units.unit);
	_unregister(&regs->units.bar);
	_unregister(&regs->units.beat);
	_unregister(&regs->units.bpm);
	_unregister(&regs->units.cent);
	_unregister(&regs->units.cm);
	_unregister(&regs->units.coef);
	_unregister(&regs->units.db);
	_unregister(&regs->units.degree);
	_unregister(&regs->units.frame);
	_unregister(&regs->units.hz);
	_unregister(&regs->units.inch);
	_unregister(&regs->units.khz);
	_unregister(&regs->units.km);
	_unregister(&regs->units.m);
	_unregister(&regs->units.mhz);
	_unregister(&regs->units.midiNote);
	_unregister(&regs->units.midiController);
	_unregister(&regs->units.mile);
	_unregister(&regs->units.min);
	_unregister(&regs->units.mm);
	_unregister(&regs->units.ms);
	_unregister(&regs->units.oct);
	_unregister(&regs->units.pc);
	_unregister(&regs->units.s);
	_unregister(&regs->units.semitone12TET);

	_unregister(&regs->state.state);
	_unregister(&regs->state.load_default_state);
	_unregister(&regs->state.thread_safe_restore);

	_unregister(&regs->synthpod.payload);
	_unregister(&regs->synthpod.state);
	_unregister(&regs->synthpod.graph);
	_unregister(&regs->synthpod.null);
	_unregister(&regs->synthpod.stereo);
	_unregister(&regs->synthpod.monoatom);
	_unregister(&regs->synthpod.module_list);
	_unregister(&regs->synthpod.module_supported);
	_unregister(&regs->synthpod.module_add);
	_unregister(&regs->synthpod.module_del);
	_unregister(&regs->synthpod.module_move);
	_unregister(&regs->synthpod.module_preset_load);
	_unregister(&regs->synthpod.module_preset_save);
	_unregister(&regs->synthpod.module_visible);
	_unregister(&regs->synthpod.module_disabled);
	_unregister(&regs->synthpod.module_profiling);
	_unregister(&regs->synthpod.module_position_x);
	_unregister(&regs->synthpod.module_position_y);
	_unregister(&regs->synthpod.node_position_x);
	_unregister(&regs->synthpod.node_position_y);
	_unregister(&regs->synthpod.port_refresh);
	_unregister(&regs->synthpod.bundle_load);
	_unregister(&regs->synthpod.bundle_save);
	_unregister(&regs->synthpod.dsp_profiling);
	_unregister(&regs->synthpod.cpus_available);
	_unregister(&regs->synthpod.cpus_used);
	_unregister(&regs->synthpod.grid_cols);
	_unregister(&regs->synthpod.grid_rows);
	_unregister(&regs->synthpod.pane_left);
	_unregister(&regs->synthpod.quit);
	
	_unregister(&regs->synthpod.system_ports);
	_unregister(&regs->synthpod.control_port);
	_unregister(&regs->synthpod.audio_port);
	_unregister(&regs->synthpod.cv_port);
	_unregister(&regs->synthpod.midi_port);
	_unregister(&regs->synthpod.osc_port);
	_unregister(&regs->synthpod.com_port);

	_unregister(&regs->synthpod.connection_list);
	_unregister(&regs->synthpod.node_list);
	_unregister(&regs->synthpod.subscription_list);
	_unregister(&regs->synthpod.notification_list);
	_unregister(&regs->synthpod.automation_list);

	_unregister(&regs->synthpod.source_module);
	_unregister(&regs->synthpod.source_symbol);
	_unregister(&regs->synthpod.sink_module);
	_unregister(&regs->synthpod.sink_symbol);

	_unregister(&regs->synthpod.source_min);
	_unregister(&regs->synthpod.source_max);
	_unregister(&regs->synthpod.sink_min);
	_unregister(&regs->synthpod.sink_max);

	_unregister(&regs->midi.Controller);
	_unregister(&regs->midi.channel);
	_unregister(&regs->midi.controller_number);
}

#if 0
#define _ATOM_ALIGNED __attribute__((aligned(8)))

// app <-> ui communication for module/port manipulations
typedef struct _transmit_t transmit_t;
typedef struct _transmit_module_list_t transmit_module_list_t;
typedef struct _transmit_module_supported_t transmit_module_supported_t;
typedef struct _transmit_module_add_t transmit_module_add_t;
typedef struct _transmit_module_del_t transmit_module_del_t;
typedef struct _transmit_module_move_t transmit_module_move_t;
typedef struct _transmit_module_preset_load_t transmit_module_preset_load_t;
typedef struct _transmit_module_preset_save_t transmit_module_preset_save_t;
typedef struct _transmit_module_selected_t transmit_module_selected_t;
typedef struct _transmit_module_embedded_t transmit_module_embedded_t;
typedef struct _transmit_module_visible_t transmit_module_visible_t;
typedef struct _transmit_module_disabled_t transmit_module_disabled_t;
typedef struct _transmit_module_profiling_t transmit_module_profiling_t;
typedef struct _transmit_port_connected_t transmit_port_connected_t;
typedef struct _transmit_port_subscribed_t transmit_port_subscribed_t;
typedef struct _transmit_port_monitored_t transmit_port_monitored_t;
typedef struct _transmit_port_refresh_t transmit_port_refresh_t;
typedef struct _transmit_port_selected_t transmit_port_selected_t;
typedef struct _transmit_bundle_load_t transmit_bundle_load_t;
typedef struct _transmit_bundle_save_t transmit_bundle_save_t;
typedef struct _transmit_path_get_t transmit_path_get_t;
typedef struct _transmit_dsp_profiling_t transmit_dsp_profiling_t;
typedef struct _transmit_grid_cols_t transmit_grid_cols_t;
typedef struct _transmit_grid_rows_t transmit_grid_rows_t;
typedef struct _transmit_pane_left_t transmit_pane_left_t;
typedef struct _transmit_quit_t transmit_quit_t;

struct _transmit_t {
	LV2_Atom_Object obj _ATOM_ALIGNED;
	LV2_Atom_Property_Body prop _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_list_t {
	transmit_t transmit _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_supported_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int state _ATOM_ALIGNED;
	LV2_Atom_String uri _ATOM_ALIGNED;
		char uri_str [0] _ATOM_ALIGNED;
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

struct _transmit_module_move_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_Int prev _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_preset_load_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_String uri _ATOM_ALIGNED;
		char uri_str [0] _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_preset_save_t {
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

struct _transmit_module_embedded_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_Int state _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_visible_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_Int state _ATOM_ALIGNED;
	LV2_Atom_URID urid _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_disabled_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_Int state _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_module_profiling_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_Float min _ATOM_ALIGNED;
	LV2_Atom_Float avg _ATOM_ALIGNED;
	LV2_Atom_Float max _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_port_connected_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int src_uid _ATOM_ALIGNED;
	LV2_Atom_Int src_port _ATOM_ALIGNED;
	LV2_Atom_Int snk_uid _ATOM_ALIGNED;
	LV2_Atom_Int snk_port _ATOM_ALIGNED;
	LV2_Atom_Int state _ATOM_ALIGNED;
	LV2_Atom_Int indirect _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_port_subscribed_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_Int port _ATOM_ALIGNED;
	LV2_Atom_URID prot _ATOM_ALIGNED;
	LV2_Atom_Int state _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_port_monitored_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int uid _ATOM_ALIGNED;
	LV2_Atom_Int port _ATOM_ALIGNED;
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

struct _transmit_bundle_load_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int status _ATOM_ALIGNED;
	LV2_Atom_String path _ATOM_ALIGNED;
		char path_str [0] _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_bundle_save_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int status _ATOM_ALIGNED;
	LV2_Atom_String path _ATOM_ALIGNED;
		char path_str [0] _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_path_get_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_String path _ATOM_ALIGNED;
		char path_str [0] _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_dsp_profiling_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Float min _ATOM_ALIGNED;
	LV2_Atom_Float avg _ATOM_ALIGNED;
	LV2_Atom_Float max _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_grid_cols_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int cols _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_grid_rows_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Int rows _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_pane_left_t {
	transmit_t transmit _ATOM_ALIGNED;
	LV2_Atom_Float left _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transmit_quit_t {
	transmit_t transmit _ATOM_ALIGNED;
} _ATOM_ALIGNED;

// app <-> ui communication for port notifications
typedef struct _transfer_t transfer_t;
typedef struct _transfer_float_t transfer_float_t;
typedef struct _transfer_peak_t transfer_peak_t;
typedef struct _transfer_atom_t transfer_atom_t;
typedef struct _transfer_patch_set_obj_t transfer_patch_set_obj_t;
typedef struct _transfer_patch_get_t transfer_patch_get_t;
typedef struct _transfer_patch_get_all_t transfer_patch_get_all_t;

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
} _ATOM_ALIGNED;

struct _transfer_patch_set_obj_t {
	LV2_Atom_Object obj _ATOM_ALIGNED;
	LV2_Atom_Property_Body subj _ATOM_ALIGNED;
	LV2_URID subj_val _ATOM_ALIGNED;
	LV2_Atom_Property_Body seq _ATOM_ALIGNED;
	int32_t seq_val _ATOM_ALIGNED;
	LV2_Atom_Property_Body prop _ATOM_ALIGNED;
	LV2_URID prop_val _ATOM_ALIGNED;
	LV2_Atom_Property_Body dest _ATOM_ALIGNED;
	LV2_URID dest_val _ATOM_ALIGNED;
	LV2_Atom_Property_Body val _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transfer_patch_get_t {
	transfer_t transfer _ATOM_ALIGNED;
	LV2_Atom_Object obj _ATOM_ALIGNED;
	LV2_Atom_Property_Body subj _ATOM_ALIGNED;
	LV2_URID subj_val _ATOM_ALIGNED;
	LV2_Atom_Property_Body seq _ATOM_ALIGNED;
	int32_t seq_val _ATOM_ALIGNED;
	LV2_Atom_Property_Body prop _ATOM_ALIGNED;
	LV2_URID prop_val _ATOM_ALIGNED;
	LV2_Atom_Property_Body dest _ATOM_ALIGNED;
	LV2_URID dest_val _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _transfer_patch_get_all_t {
	transfer_t transfer _ATOM_ALIGNED;
	LV2_Atom_Object obj _ATOM_ALIGNED;
	LV2_Atom_Property_Body subj _ATOM_ALIGNED;
	LV2_URID subj_val _ATOM_ALIGNED;
	LV2_Atom_Property_Body seq _ATOM_ALIGNED;
	int32_t seq_val _ATOM_ALIGNED;
	LV2_Atom_Property_Body dest _ATOM_ALIGNED;
	LV2_URID dest_val _ATOM_ALIGNED;
} _ATOM_ALIGNED;

static inline void
_sp_transmit_fill(reg_t *regs, LV2_Atom_Forge *forge, transmit_t *trans, uint32_t size,
	LV2_URID protocol)
{
	trans = ASSUME_ALIGNED(trans);

	trans->obj.atom.size = size - sizeof(LV2_Atom);
	trans->obj.atom.type = forge->Object;
	trans->obj.body.id = 0;
	trans->obj.body.otype = protocol;

	trans->prop.key = regs->synthpod.payload.urid;
	trans->prop.context = 0;
	trans->prop.value.size = size - sizeof(transmit_t);
	trans->prop.value.type = forge->Tuple;
}

static inline void
_sp_transmit_module_list_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_list_t *trans, uint32_t size)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size, 
		regs->synthpod.module_list.urid);
}

static inline void
_sp_transmit_module_supported_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_supported_t *trans, uint32_t size,
	int32_t state, const char *module_uri)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.module_supported.urid);

	trans->state.atom.size = sizeof(int32_t);
	trans->state.atom.type = forge->Int;
	trans->state.body = state;

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
_sp_transmit_module_add_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_add_t *trans, uint32_t size,
	u_id_t module_uid, const char *module_uri)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.module_add.urid);

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
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.module_del.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;
}

static inline void
_sp_transmit_module_move_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_move_t *trans, uint32_t size, u_id_t module_uid, u_id_t prev_uid)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.module_move.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->prev.atom.size = sizeof(int32_t);
	trans->prev.atom.type = forge->Int;
	trans->prev.body = prev_uid;
}

static inline void
_sp_transmit_module_preset_load_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_preset_load_t *trans, uint32_t size, u_id_t module_uid, const char *uri)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.module_preset_load.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->uri.atom.size = uri
		? strlen(uri) + 1
		: 0;
	trans->uri.atom.type = forge->String;

	if(uri)
		strcpy(trans->uri_str, uri);
}

static inline void
_sp_transmit_module_preset_save_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_preset_save_t *trans, uint32_t size, u_id_t module_uid, const char *label)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.module_preset_save.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->label.atom.size = label
		? strlen(label) + 1
		: 0;
	trans->label.atom.type = forge->String;

	if(label)
		strcpy(trans->label_str, label);
}

static inline void
_sp_transmit_module_selected_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_selected_t *trans, uint32_t size, u_id_t module_uid, int state)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.module_selected.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->state.atom.size = sizeof(int32_t);
	trans->state.atom.type = forge->Int;
	trans->state.body = state;
}

static inline void
_sp_transmit_module_embedded_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_embedded_t *trans, uint32_t size, u_id_t module_uid, int state)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.module_embedded.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->state.atom.size = sizeof(int32_t);
	trans->state.atom.type = forge->Int;
	trans->state.body = state;
}

static inline void
_sp_transmit_module_visible_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_visible_t *trans, uint32_t size, u_id_t module_uid,
	int state, LV2_URID urid)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.module_visible.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->state.atom.size = sizeof(int32_t);
	trans->state.atom.type = forge->Int;
	trans->state.body = state;

	trans->urid.atom.size = sizeof(uint32_t);
	trans->urid.atom.type = forge->URID;
	trans->urid.body = urid;
}

static inline void
_sp_transmit_module_disabled_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_disabled_t *trans, uint32_t size, u_id_t module_uid, int state)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.module_disabled.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->state.atom.size = sizeof(int32_t);
	trans->state.atom.type = forge->Int;
	trans->state.body = state;
}

static inline void
_sp_transmit_module_profiling_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_module_profiling_t *trans, uint32_t size, u_id_t module_uid,
	float min, float avg, float max)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.module_profiling.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->min.atom.size = sizeof(float);
	trans->min.atom.type = forge->Float;
	trans->min.body = min;

	trans->avg.atom.size = sizeof(float);
	trans->avg.atom.type = forge->Float;
	trans->avg.body = avg;

	trans->max.atom.size = sizeof(float);
	trans->max.atom.type = forge->Float;
	trans->max.body = max;
}

static inline void
_sp_transmit_port_connected_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_port_connected_t *trans, uint32_t size, u_id_t src_uid,
	uint32_t src_port, u_id_t snk_uid, uint32_t snk_port, int32_t state, int32_t indirect)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.port_connected.urid);

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

	trans->indirect.atom.size = sizeof(int32_t);
	trans->indirect.atom.type = forge->Int;
	trans->indirect.body = indirect; // -1 (feedback), 0 (direct), 1 (indirect)
}

static inline void
_sp_transmit_port_subscribed_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_port_subscribed_t *trans, uint32_t size,
	u_id_t module_uid, uint32_t port_index, LV2_URID prot, int32_t state)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.port_subscribed.urid);

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
_sp_transmit_port_monitored_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_port_monitored_t *trans, uint32_t size,
	u_id_t module_uid, uint32_t port_index, int32_t state)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.port_monitored.urid);

	trans->uid.atom.size = sizeof(int32_t);
	trans->uid.atom.type = forge->Int;
	trans->uid.body = module_uid;

	trans->port.atom.size = sizeof(int32_t);
	trans->port.atom.type = forge->Int;
	trans->port.body = port_index;

	trans->state.atom.size = sizeof(int32_t);
	trans->state.atom.type = forge->Int;
	trans->state.body = state; // -1 (query), 0 (not monitored), 1 (monitored)
}

static inline void
_sp_transmit_port_refresh_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_port_refresh_t *trans, uint32_t size,
	u_id_t module_uid, uint32_t port_index)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.port_refresh.urid);

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
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.port_selected.urid);

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
_sp_transmit_bundle_load_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_bundle_load_t *trans, uint32_t size,
	int32_t status, const char *bundle_path)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.bundle_load.urid);

	trans->status.atom.size = sizeof(int32_t);
	trans->status.atom.type = forge->Int;
	trans->status.body = status;

	trans->path.atom.size = bundle_path
		? strlen(bundle_path) + 1
		: 0;
	trans->path.atom.type = forge->String;

	if(bundle_path)
		strcpy(trans->path_str, bundle_path);
}

static inline void
_sp_transmit_bundle_save_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_bundle_save_t *trans, uint32_t size,
	int32_t status, const char *bundle_path)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.bundle_save.urid);

	trans->status.atom.size = sizeof(int32_t);
	trans->status.atom.type = forge->Int;
	trans->status.body = status;

	trans->path.atom.size = bundle_path
		? strlen(bundle_path) + 1
		: 0;
	trans->path.atom.type = forge->String;

	if(bundle_path)
		strcpy(trans->path_str, bundle_path);
}

static inline void
_sp_transmit_path_get_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_path_get_t *trans, uint32_t size,
	const char *bundle_path)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.path_get.urid);

	trans->path.atom.size = bundle_path
		? strlen(bundle_path) + 1
		: 0;
	trans->path.atom.type = forge->String;

	if(bundle_path)
		strcpy(trans->path_str, bundle_path);
}

static inline void
_sp_transmit_dsp_profiling_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_dsp_profiling_t *trans, uint32_t size,
	float min, float avg, float max)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.dsp_profiling.urid);

	trans->min.atom.size = sizeof(float);
	trans->min.atom.type = forge->Float;
	trans->min.body = min;

	trans->avg.atom.size = sizeof(float);
	trans->avg.atom.type = forge->Float;
	trans->avg.body = avg;

	trans->max.atom.size = sizeof(float);
	trans->max.atom.type = forge->Float;
	trans->max.body = max;
}

static inline void
_sp_transmit_grid_cols_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_grid_cols_t *trans, uint32_t size, int32_t cols)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.grid_cols.urid);

	trans->cols.atom.size = sizeof(int32_t);
	trans->cols.atom.type = forge->Int;
	trans->cols.body = cols;
}

static inline void
_sp_transmit_grid_rows_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_grid_rows_t *trans, uint32_t size, int32_t rows)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.grid_rows.urid);

	trans->rows.atom.size = sizeof(int32_t);
	trans->rows.atom.type = forge->Int;
	trans->rows.body = rows;
}

static inline void
_sp_transmit_pane_left_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_pane_left_t *trans, uint32_t size, float left)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.pane_left.urid);

	trans->left.atom.size = sizeof(float);
	trans->left.atom.type = forge->Float;
	trans->left.body = left;
}

static inline void
_sp_transmit_quit_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transmit_quit_t *trans, uint32_t size)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		regs->synthpod.quit.urid);
}

static inline void
_sp_transfer_fill(reg_t *regs, LV2_Atom_Forge *forge, transfer_t *trans, uint32_t size,
	LV2_URID protocol, u_id_t module_uid, uint32_t port_index)
{
	trans = ASSUME_ALIGNED(trans);

	_sp_transmit_fill(regs, forge, &trans->transmit, size,
		protocol);

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
	trans = ASSUME_ALIGNED(trans);

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
	trans = ASSUME_ALIGNED(trans);

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

static inline LV2_Atom *
_sp_transfer_atom_fill(reg_t *regs, LV2_Atom_Forge *forge, transfer_atom_t *trans,
	u_id_t module_uid, uint32_t port_index, uint32_t atom_size, const LV2_Atom *atom)
{
	trans = ASSUME_ALIGNED(trans);
	//TODO atom aligned, too?

	_sp_transfer_fill(regs, forge, &trans->transfer, sizeof(transfer_atom_t)
		+ lv2_atom_pad_size(atom_size),
		regs->port.atom_transfer.urid, module_uid, port_index);

	if(atom)
		memcpy(trans->atom, atom, atom_size);

	return trans->atom;
}

static inline LV2_Atom *
_sp_transfer_event_fill(reg_t *regs, LV2_Atom_Forge *forge, transfer_atom_t *trans,
	u_id_t module_uid, uint32_t port_index, uint32_t atom_size, const LV2_Atom *atom)
{
	trans = ASSUME_ALIGNED(trans);
	//TODO atom aligned, too?

	_sp_transfer_fill(regs, forge, &trans->transfer, sizeof(transfer_atom_t)
		+ lv2_atom_pad_size(atom_size),
		regs->port.event_transfer.urid, module_uid, port_index);

	if(atom)
		memcpy(trans->atom, atom, atom_size);

	return trans->atom;
}

static inline LV2_Atom *
_sp_transfer_patch_set_obj_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transfer_patch_set_obj_t *trans,
	uint32_t body_size, LV2_URID subject,
	LV2_URID property, LV2_URID type,
	int32_t sequence_number)
{
	trans = ASSUME_ALIGNED(trans);

	uint32_t obj_size = sizeof(transfer_patch_set_obj_t) + body_size - sizeof(LV2_Atom);

	trans->obj.atom.size = obj_size;
	trans->obj.atom.type = forge->Object;
	trans->obj.body.id =  0;
	trans->obj.body.otype = regs->patch.set.urid;

	trans->subj.key = regs->patch.subject.urid;
	trans->subj.context = 0;
	trans->subj.value.size = sizeof(LV2_URID);
	trans->subj.value.type = forge->URID;

	trans->subj_val = subject;

	trans->seq.key = regs->patch.sequence_number.urid;
	trans->seq.context = 0;
	trans->seq.value.size = sizeof(int32_t);
	trans->seq.value.type = forge->Int;

	trans->seq_val = sequence_number;

	trans->prop.key = regs->patch.property.urid;
	trans->prop.context = 0;
	trans->prop.value.size = sizeof(LV2_URID);
	trans->prop.value.type = forge->URID;

	trans->prop_val = property;

	trans->dest.key = regs->patch.destination.urid;
	trans->dest.context = 0;
	trans->dest.value.size = sizeof(LV2_URID);
	trans->dest.value.type = forge->URID;

	trans->dest_val = regs->core.plugin.urid;

	trans->val.key = regs->patch.value.urid;
	trans->val.context = 0;
	trans->val.value.size = body_size;
	trans->val.value.type = type;

	//return LV2_ATOM_BODY(&trans->val.value);
	return &trans->val.value;
}

static inline void
_sp_transfer_patch_get_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transfer_patch_get_t *trans, u_id_t module_uid, uint32_t port_index,
	LV2_URID subject, LV2_URID property, int32_t sequence_number)
{
	trans = ASSUME_ALIGNED(trans);

	uint32_t trans_size = sizeof(transfer_patch_get_t);
	uint32_t obj_size = trans_size
		- offsetof(transfer_patch_get_t, obj.body);

	_sp_transfer_fill(regs, forge, &trans->transfer, trans_size,
		regs->port.event_transfer.urid, module_uid, port_index);

	trans->obj.atom.size = obj_size;
	trans->obj.atom.type = forge->Object;
	trans->obj.body.id =  0;
	trans->obj.body.otype = regs->patch.get.urid;

	trans->subj.key = regs->patch.subject.urid;
	trans->subj.context = 0;
	trans->subj.value.size = sizeof(LV2_URID);
	trans->subj.value.type = forge->URID;

	trans->subj_val = subject;

	trans->seq.key = regs->patch.sequence_number.urid;
	trans->seq.context = 0;
	trans->seq.value.size = sizeof(int32_t);
	trans->seq.value.type = forge->Int;

	trans->seq_val = sequence_number;

	trans->prop.key = regs->patch.property.urid;
	trans->prop.context = 0;
	trans->prop.value.size = sizeof(LV2_URID);
	trans->prop.value.type = forge->URID;

	trans->prop_val = property;

	trans->dest.key = regs->patch.destination.urid;
	trans->dest.context = 0;
	trans->dest.value.size = sizeof(LV2_URID);
	trans->dest.value.type = forge->URID;

	trans->dest_val = regs->core.plugin.urid;
}

static inline void
_sp_transfer_patch_get_all_fill(reg_t *regs, LV2_Atom_Forge *forge,
	transfer_patch_get_all_t *trans, u_id_t module_uid, uint32_t port_index,
	LV2_URID subject, int32_t sequence_number)
{
	trans = ASSUME_ALIGNED(trans);

	uint32_t trans_size = sizeof(transfer_patch_get_all_t);
	uint32_t obj_size = trans_size
		- offsetof(transfer_patch_get_all_t, obj.body);

	_sp_transfer_fill(regs, forge, &trans->transfer, trans_size,
		regs->port.event_transfer.urid, module_uid, port_index);

	trans->obj.atom.size = obj_size;
	trans->obj.atom.type = forge->Object;
	trans->obj.body.id = 0;
	trans->obj.body.otype = regs->patch.get.urid;

	trans->subj.key = regs->patch.subject.urid;
	trans->subj.context = 0;
	trans->subj.value.size = sizeof(LV2_URID);
	trans->subj.value.type = forge->URID;

	trans->subj_val = subject;

	trans->seq.key = regs->patch.sequence_number.urid;
	trans->seq.context = 0;
	trans->seq.value.size = sizeof(int32_t);
	trans->seq.value.type = forge->Int;

	trans->seq_val = sequence_number;

	trans->dest.key = regs->patch.destination.urid;
	trans->dest.context = 0;
	trans->dest.value.size = sizeof(LV2_URID);
	trans->dest.value.type = forge->URID;

	trans->dest_val = regs->core.plugin.urid;
}
#endif

// non-rt
static LilvNodes *
_preset_reload(LilvWorld *world, reg_t *regs, const LilvPlugin *plugin,
	LilvNodes *presets, const char *bndl)
{
	if(presets)
	{
		// unload resources for this module's presets
		LILV_FOREACH(nodes, itr, presets)
		{
			const LilvNode *preset = lilv_nodes_get(presets, itr);

			lilv_world_unload_resource(world, preset);
		}

		// free presets for this module
		lilv_nodes_free(presets);
	}

	LilvNode *bndl_node = lilv_new_file_uri(world, NULL, bndl);
	if(bndl_node)
	{
		// unload bundle (if it should already be loaded)
		lilv_world_unload_bundle(world, bndl_node);

		// load bundle
		lilv_world_load_bundle(world, bndl_node);
		lilv_node_free(bndl_node);
	}

	// discover presets for this module
	presets = lilv_plugin_get_related(plugin, regs->pset.preset.node);

	// load resources for this module's presets
	LILV_FOREACH(nodes, itr, presets)
	{
		const LilvNode *preset = lilv_nodes_get(presets, itr);

		lilv_world_load_resource(world, preset);
	}

	return presets;
}

static inline int
_signum(LV2_URID urid1, LV2_URID urid2)
{
	if(urid1 < urid2)
		return -1;
	else if(urid1 > urid2)
		return 1;

	return 0;
}

#endif // _SYNTHPOD_PRIVATE_H
