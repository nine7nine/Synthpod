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

#ifndef _SYNTHPOD_APP_H
#define _SYNTHPOD_APP_H

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/options/options.h>
#include <lv2/lv2plug.in/ns/ext/buf-size/buf-size.h>

#include <lilv/lilv.h>

#include <synthpod_common.h>
#include <osc.lv2/osc.h>

typedef enum _sp_app_features_t sp_app_features_t;
typedef enum _system_port_t system_port_t;

typedef struct _sp_app_t sp_app_t;
typedef struct _sp_app_system_source_t sp_app_system_source_t;
typedef struct _sp_app_system_sink_t sp_app_system_sink_t;
typedef struct _sp_app_driver_t sp_app_driver_t;

typedef void *(*sp_to_request_t)(size_t minimum, size_t *maximum, void *data);
typedef void (*sp_to_advance_t)(size_t written, void *data);

typedef void *(*sp_system_port_add)(void *data, system_port_t type,
	const char *short_name, const char *pretty_name, const char *designation,
	bool input, uint32_t order);
typedef void (*sp_system_port_del)(void *data, void *sys_port);

typedef void (*sp_close_request_t)(void *data);

typedef void (*sp_opened_t)(void *data, int status);
typedef void (*sp_saved_t)(void *data, int status);

enum _sp_app_features_t {
	SP_APP_FEATURE_FIXED_BLOCK_LENGTH				= (1 << 0),
	SP_APP_FEATURE_POWER_OF_2_BLOCK_LENGTH	= (1 << 1)
};

enum _system_port_t {
	SYSTEM_PORT_NONE = 0,
	SYSTEM_PORT_CONTROL,
	SYSTEM_PORT_AUDIO,
	SYSTEM_PORT_CV,
	SYSTEM_PORT_MIDI,
	SYSTEM_PORT_OSC,
	SYSTEM_PORT_COM
};

struct _sp_app_system_source_t {
	system_port_t type;
	void *sys_port;
	void *buf;
};

struct _sp_app_system_sink_t {
	system_port_t type;
	void *sys_port;
	const void *buf;
};

struct _sp_app_driver_t {
	float sample_rate;
	float update_rate;
	uint32_t min_block_size;
	uint32_t max_block_size;
	uint32_t seq_size;
	uint32_t num_periods;

	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	xpress_map_t *xmap;

	// from app
	sp_to_request_t to_ui_request;
	sp_to_advance_t to_ui_advance;
	
	sp_to_request_t to_worker_request;
	sp_to_advance_t to_worker_advance;

	// from worker
	sp_to_request_t to_app_request;
	sp_to_advance_t to_app_advance;

	// logging
	LV2_Log_Log *log;

	// system_port
	sp_system_port_add system_port_add;
	sp_system_port_del system_port_del;

	// clock_sync
	LV2_OSC_Schedule *osc_sched;

	sp_app_features_t features;

	unsigned num_slaves;

	int audio_prio;
	bool bad_plugins;
	bool cpu_affinity;

	sp_close_request_t close_request;
	sp_opened_t opened;
	sp_saved_t saved;
};

sp_app_t *
sp_app_new(const LilvWorld *world, sp_app_driver_t *driver, void *data);

void
sp_app_activate(sp_app_t *app);

const sp_app_system_source_t *
sp_app_get_system_sources(sp_app_t *app);

const sp_app_system_sink_t *
sp_app_get_system_sinks(sp_app_t *app);

bool
sp_app_from_ui(sp_app_t *app, const LV2_Atom *atom);

bool
sp_app_from_worker(sp_app_t *app, uint32_t len, const void *data);

void
sp_worker_from_app(sp_app_t *app, uint32_t len, const void *data);

void
sp_app_run_pre(sp_app_t *app, uint32_t nsamples);

void
sp_app_run_post(sp_app_t *app, uint32_t nsamples);

void
sp_app_deactivate(sp_app_t *app);

void
sp_app_free(sp_app_t *app);

LV2_State_Status
sp_app_save(sp_app_t *app, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags, const LV2_Feature *const *features);

LV2_Atom_Object *
sp_app_stash(sp_app_t *app, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags, const LV2_Feature *const *features);

void
sp_app_apply(sp_app_t *app, LV2_Atom_Object *obj, char *bundle_path);

const LV2_Feature *const *
sp_app_state_features(sp_app_t *app, void *prefix_path);

LV2_State_Status
sp_app_restore(sp_app_t *app, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags, const LV2_Feature *const *features);

const void *
sp_app_state_retrieve(LV2_State_Handle state, uint32_t key, size_t *size,
	uint32_t *type, uint32_t *flags);

void
sp_app_set_bundle_path(sp_app_t *app, const char *bundle_path);

bool
sp_app_bypassed(sp_app_t *app);

uint32_t
sp_app_options_set(sp_app_t *app, const LV2_Options_Option *options);

int
sp_app_nominal_block_length(sp_app_t *app, uint32_t nsamples);

int
sp_app_com_event(sp_app_t *app, LV2_URID otype); 

void
sp_app_bundle_reset(sp_app_t *app);

void
sp_app_bundle_load(sp_app_t *app, LV2_URID urn, bool via_app);

void
sp_app_bundle_save(sp_app_t *app, LV2_URID urn, bool via_app);

void
sp_app_visibility_set(sp_app_t *app, bool visibility);

bool
sp_app_visibility_get(sp_app_t *app);

void
sp_app_xrun_report(sp_app_t *app);

#endif // _SYNTHPOD_APP_H
