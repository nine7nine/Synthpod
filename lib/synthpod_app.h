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

typedef struct _sp_app_t sp_app_t;
typedef struct _sp_app_driver_t sp_app_driver_t;

typedef void *(*sp_to_request_t)(size_t size, void *data);
typedef void (*sp_to_advance_t)(size_t size, void *data);

typedef int (*sp_printf)(void *data, LV2_URID type, const char *fmt, ...);
typedef int (*sp_vprintf)(void *data, LV2_URID type, const char *fmt, va_list args);

struct _sp_app_driver_t {
	uint32_t sample_rate;
	uint32_t period_size;
	uint32_t seq_size;

	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;

	// from app
	sp_to_request_t to_ui_request;
	sp_to_advance_t to_ui_advance;
	
	sp_to_request_t to_worker_request;
	sp_to_advance_t to_worker_advance;

	// from worker
	sp_to_request_t to_app_request;
	sp_to_advance_t to_app_advance;

	// logging
	sp_printf log_printf;
	sp_vprintf log_vprintf;
};

sp_app_t *
sp_app_new(sp_app_driver_t *driver, void *data);

void
sp_app_activate(sp_app_t *app);

void
sp_app_set_system_source(sp_app_t *app, uint32_t index, const void *buf);

void
sp_app_set_system_sink(sp_app_t *app, uint32_t index, void *buf);

void *
sp_app_get_system_source(sp_app_t *app, uint32_t index);

const void *
sp_app_get_system_sink(sp_app_t *app, uint32_t index);

void
sp_app_from_ui(sp_app_t *app, const LV2_Atom *atom);

void
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

LV2_State_Status
sp_app_restore(sp_app_t *app, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags, const LV2_Feature *const *features);

#endif // _SYNTHPOD_APP_H
