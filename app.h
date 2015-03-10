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
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#include <Elementary.h>

#include <uv.h>

#include <ext_urid.h>
#include <varchunk.h>

#define NUM_FEATURES 4
#define NUM_UI_FEATURES 6
#define LV2_UI__EoUI          LV2_UI_PREFIX"EoUI"

typedef struct _app_t app_t;
typedef struct _mod_t mod_t;
typedef struct _port_t port_t;

struct _app_t {
	LilvWorld *world;
	const LilvPlugins *plugs;

	ext_urid_t *ext_urid;

	struct {
		LilvNode *audio;
		LilvNode *control;
		LilvNode *cv;
		LilvNode *atom;
		
		LilvNode *sequence;

		LilvNode *input;
		LilvNode *output;
		//LilvNode *duplex;

		LilvNode *midi;
		LilvNode *osc;

		LilvNode *chim_event;
		LilvNode *chim_dump;

		LilvNode *work_schedule;
		
		LilvNode *float_protocol;
		LilvNode *peak_protocol;
		LilvNode *atom_transfer;
		LilvNode *event_transfer;

		struct {
			LilvNode *entry;
			LilvNode *error;
			LilvNode *note;
			LilvNode *trace;
			LilvNode *warning;
		} log;

		LilvNode *eo;

		LilvNode *integer;
		LilvNode *toggled;
	} uris;

	struct {
		LV2_URID audio;
		LV2_URID control;
		LV2_URID cv;
		LV2_URID atom;

		LV2_URID sequence;

		LV2_URID input;
		LV2_URID output;
		//LV2_URID duplex;

		LV2_URID midi;
		LV2_URID osc;

		LV2_URID chim_event;
		LV2_URID chim_dump;

		LV2_URID work_schedule;

		LV2_URID float_protocol;
		LV2_URID peak_protocol;
		LV2_URID atom_transfer;
		LV2_URID event_transfer;

		struct {
			LV2_URID entry;
			LV2_URID error;
			LV2_URID note;
			LV2_URID trace;
			LV2_URID warning;
		} log;

		LV2_URID eo;
	} urids;

	Eina_Inlist *mods;

	double sample_rate;
	uint32_t period_size;
	uint32_t seq_size;

	struct {
		Evas_Object *win;
		Evas_Object *plugpane;
		Evas_Object *modpane;
		Evas_Object *pluglist;
		Evas_Object *modlist;
		Evas_Object *modgrid;
		Elm_Genlist_Item_Class *plugitc;
		Elm_Genlist_Item_Class *moditc;
		Elm_Genlist_Item_Class *stditc;
		Elm_Gengrid_Item_Class *griditc;
	} ui;
	
	// rt-thread
	uv_loop_t *loop;
	uv_timer_t pacemaker;
	uv_async_t quit;
	uv_thread_t thread;
};

struct _mod_t {
	EINA_INLIST;

	// worker
	struct {
		const LV2_Worker_Interface *iface;
		LV2_Worker_Schedule schedule;

		varchunk_t *to;
		varchunk_t *from;
		uv_thread_t thread;
		uv_async_t async;
		uv_async_t quit;
	} worker;

	// features
	LV2_Feature feature_list [NUM_FEATURES];
	const LV2_Feature *features [NUM_FEATURES + 1];
	
	LV2_Feature ui_feature_list [NUM_UI_FEATURES];
	const LV2_Feature *ui_features [NUM_UI_FEATURES + 1];

	// parent
	app_t *app;

	// self
	const LilvPlugin *plug;
	LilvInstance *inst;
	LV2_Handle handle;

	LilvUIs *all_uis;

	// ui
	struct {
		varchunk_t *to;
		varchunk_t *from;
		Ecore_Animator *port_event_anim;

		// Eo UI
		struct {
			const LilvUI *ui;

			uv_lib_t lib;
			const LV2UI_Descriptor *descriptor;

			LV2UI_Handle handle;
			Evas_Object *widget;

			// LV2UI_Port_Map extention
			LV2UI_Port_Map port_map;
		
			// LV2UI_Port_Subscribe extension
			LV2UI_Port_Subscribe port_subscribe;
			
			// LV2UI_Idle_Interface extension
			const LV2UI_Idle_Interface *idle_interface;
			Ecore_Animator *idle_anim;
		} eo;

		// standard "automatic" UI
		struct {
			LV2UI_Descriptor descriptor;
			Elm_Object_Item *itm;
		} std;
	} ui;

	LV2_Log_Log log;

	// ports
	uint32_t num_ports;
	port_t *ports;

	// atom forge
	LV2_Atom_Forge forge;
};

struct _port_t {
	mod_t *mod;
	const LilvPort *tar;
	uint32_t index;

	LV2_URID direction; // input, output, duplex
	LV2_URID type; // audio, CV, control, atom
	LV2_URID buffer_type; // sequence

	volatile LV2_URID protocol; // floatProtocol, peakProtocol, atomTransfer, eventTransfer
	LilvScalePoints *points;

	void *buf;

	float last;
	uint32_t period_cnt;

	float dflt;
	float min;
	float max;
			
	struct {
		Evas_Object *widget;
	} std;
};

app_t *
app_new();

void
app_free(app_t *app);

void
app_run(app_t *app);

void
app_stop(app_t *app);

mod_t *
app_mod_add(app_t *app, const char *uri);

void
app_mod_del(app_t *app, mod_t *mod);

#endif // _SYNTHPOD_APP_H
