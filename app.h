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

#include <lilv/lilv.h>

#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>

#include <Eina.h>

#include <uv.h>

#include <ext_urid.h>
#include <varchunk.h>

#define NUM_FEATURES 3

typedef struct _app_t app_t;
typedef struct _mod_t mod_t;

struct _app_t {
	LilvWorld *world;
	const LilvPlugins *plugs;

	ext_urid_t *ext_urid;

	struct {
		LilvNode *audio;
		LilvNode *control;
		LilvNode *cv;
		LilvNode *atom;

		LilvNode *input;
		LilvNode *output;
		//LilvNode *duplex;

		LilvNode *midi;
		LilvNode *osc;

		LilvNode *chim_event;
		LilvNode *chim_dump;

		LilvNode *work_schedule;
	} uris;

	struct {
		LV2_URID audio;
		LV2_URID control;
		LV2_URID cv;
		LV2_URID atom;

		LV2_URID input;
		LV2_URID output;
		//LV2_URID duplex;

		LV2_URID midi;
		LV2_URID osc;

		LV2_URID chim_event;
		LV2_URID chim_dump;

		LV2_URID work_schedule;
	} urids;

	Eina_Inlist *mods;

	double sample_rate;
	uint32_t period_size;
	uint32_t seq_size;

	uv_loop_t *loop;
	uv_timer_t pacemaker;
};

struct _mod_t {
	EINA_INLIST;

	// worker
	struct {
		const LV2_Worker_Interface *iface;
		LV2_Worker_Schedule schedule;

		varchunk_t *to_thread;
		varchunk_t *from_thread;
		uv_thread_t thread;
		uv_async_t async;
		uv_async_t quit;
	} worker;

	// features
	LV2_Feature feature_list [NUM_FEATURES];
	const LV2_Feature *features [NUM_FEATURES + 1];

	// parent
	app_t *app;

	// self
	const LilvPlugin *plug;
	LilvInstance *inst;
	LV2_Handle handle;

	// ports
	uint32_t num_ports;
	void **bufs;

	// atom forge
	LV2_Atom_Forge forge;
};

app_t *
app_new();

void
app_free(app_t *app);

void
app_run(app_t *app);

mod_t *
app_mod_add(app_t *app, const char *uri);

void
app_mod_del(app_t *app, mod_t *mod);

#endif // _SYNTHPOD_APP_H
