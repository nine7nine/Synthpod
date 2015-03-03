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

#include <Eina.h>

#include <ext_urid.h>
#include <varchunk.h>

#define APP_NUM_FEATURES 2

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
	} urids;

	Eina_Inlist *mods;

	LV2_Feature feature_list [APP_NUM_FEATURES];
	const LV2_Feature *features [APP_NUM_FEATURES + 1];

	double sample_rate;
	uint32_t period_size;
	uint32_t seq_size;
};

struct _mod_t {
	EINA_INLIST;

	app_t *app;

	const LilvPlugin *plug;
	LilvInstance *inst;

	uint32_t num_ports;
	void **bufs;

	LV2_Atom_Forge forge;

	varchunk_t *varchunk;
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
