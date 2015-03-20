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

#include <uuid.h>

#include <ext_urid.h>
#include <varchunk.h>

#define NUM_FEATURES 4
#define NUM_UI_FEATURES 6
#define LV2_UI__EoUI          LV2_UI_PREFIX"EoUI"

typedef enum _port_type_t port_type_t;
typedef enum _port_buffer_type_t port_buffer_type_t;
typedef enum _port_direction_t port_direction_t;
typedef enum _port_widget_t port_widget_t;
typedef enum _port_protocol_t port_protocol_t;

typedef struct _app_t app_t;
typedef struct _mod_t mod_t;
typedef struct _port_t port_t;
typedef struct _conn_t conn_t;
typedef struct _reg_t reg_t;

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

enum _port_widget_t {
	PORT_WIDGET_SLIDER = 0,
	PORT_WIDGET_CHECK,
	PORT_WIDGET_DROPBOX,
	PORT_WIDGET_SEGMENT,
	PORT_WIDGET_PROGRESS,

	PORT_WIDGET_NUM
};

enum _port_protocol_t {
	PORT_PROTOCOL_FLOAT = 0,
	PORT_PROTOCOL_PEAK,
	PORT_PROTOCOL_ATOM,
	PORT_PROTOCOL_SEQUENCE,

	PORT_PROTOCOL_NUM
}; //TODO use this

struct _reg_t {
	LilvNode *node;
	LV2_URID urid;
};

struct _app_t {
	LilvWorld *world;
	const LilvPlugins *plugs;

	ext_urid_t *ext_urid;

	struct {
		struct {
			reg_t input;
			reg_t output;

			reg_t control;
			reg_t audio;
			reg_t cv;
			reg_t atom;

			// atom buffer type
			reg_t sequence;

			// atom sequence event types
			reg_t midi;
			reg_t osc;
			reg_t chim_event;
			reg_t chim_dump;

			// control port property
			reg_t integer;
			reg_t toggled;

			// port protocols
			reg_t float_protocol;
			reg_t peak_protocol;
			reg_t atom_transfer;
			reg_t event_transfer;

			reg_t notification;
		} port;

		struct {
			reg_t schedule;
		} work;

		struct {
			reg_t entry;
			reg_t error;
			reg_t note;
			reg_t trace;
			reg_t warning;
		} log;

		struct {
			reg_t eo;
		} ui;
	} regs;

	Eina_Inlist *mods;
	Eina_Inlist *conns;

	double sample_rate;
	uint32_t period_size;
	uint32_t seq_size;

	struct {
		Evas_Object *win;

		Evas_Object *plugpane;
		Evas_Object *modpane;
		Evas_Object *patchpane;

		Evas_Object *pluglist;
		Evas_Object *modlist;
		Evas_Object *modgrid;
		Evas_Object *patchbox;
		Evas_Object *matrix[PORT_TYPE_NUM];

		Elm_Genlist_Item_Class *plugitc;
		Elm_Genlist_Item_Class *moditc;
		Elm_Genlist_Item_Class *stditc;
		Elm_Gengrid_Item_Class *griditc;
	} ui;

	struct {
		Ecore_Animator *anim;
		varchunk_t *to;
		varchunk_t *from;
	} rt;
	
	// rt-thread
	uv_loop_t *loop;
	uv_timer_t pacemaker;
	uv_async_t quit;
	uv_thread_t thread;
};

struct _mod_t {
	EINA_INLIST;

	uuid_t uuid;
	int selected;

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
		int col;

		// Eo UI
		struct {
			const LilvUI *ui;
			uv_lib_t lib;

			const LV2UI_Descriptor *descriptor;
			LV2UI_Handle handle;
			Elm_Object_Item *itm;
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
	uuid_t uuid;
	int selected;

	mod_t *mod;
	const LilvPort *tar;
	uint32_t index;

	// only for sink port
	int num_sources;
	port_t *sources [32]; // TODO how many?

	port_direction_t direction; // input, output
	port_type_t type; // audio, CV, control, atom
	port_buffer_type_t buffer_type; // none, sequence

	volatile LV2_URID protocol; // floatProtocol, peakProtocol, atomTransfer, eventTransfer
	int subscriptions; // subsriptions reference counter
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

struct _conn_t {
	port_t *source;
	port_t *sink;
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

void
app_load(app_t *app, const char *path);

void
app_save(app_t *app, const char *path);

#endif // _SYNTHPOD_APP_H
