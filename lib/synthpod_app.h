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

#include <synthpod.h>

#define NUM_FEATURES 4

typedef struct _sp_app_t sp_app_t;
typedef struct _sp_app_driver_t sp_app_driver_t;

typedef int (*sp_to_cb_t)(LV2_Atom *atom, void *data);

struct _sp_app_driver_t {
	uint32_t sample_rate;
	uint32_t period_size;
	uint32_t seq_size;

	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	LV2_Worker_Schedule *schedule;
	LV2_Log_Log *log;

	// from app
	sp_to_cb_t to_ui_cb;
	sp_to_cb_t to_worker_cb;

	// from worker
	sp_to_cb_t to_app_cb;
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
sp_app_from_ui(sp_app_t *app, const LV2_Atom *atom, void *data);

void
sp_app_from_worker(sp_app_t *app, const LV2_Atom *atom, void *data);

void
sp_worker_from_app(sp_app_t *app, const LV2_Atom *atom, void *data);

void
sp_app_run(sp_app_t *app, uint32_t nsamples);

void
sp_app_deactivate(sp_app_t *app);

void
sp_app_free(sp_app_t *app);

#endif // _SYNTHPOD_APP_H
