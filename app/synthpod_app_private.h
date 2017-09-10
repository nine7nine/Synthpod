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

#ifndef _SYNTHPOD_APP_PRIVATE_H
#define _SYNTHPOD_APP_PRIVATE_H

#include <uv.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <ctype.h> // isspace
#include <math.h>

#include <synthpod_app.h>
#include <synthpod_private.h>

#include <sratom/sratom.h>
#include <varchunk.h>

#include <lv2_extensions.h> // ardour's inline display

#define XSD_PREFIX "http://www.w3.org/2001/XMLSchema#"
#define RDF_PREFIX "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
#define RDFS_PREFIX "http://www.w3.org/2000/01/rdf-schema#"
#define SPOD_PREFIX "http://open-music-kontrollers.ch/lv2/synthpod#"

#define URN_UUID_LENGTH 46

#define NUM_FEATURES 17
#define MAX_SOURCES 32 // TODO how many?
#define MAX_MODS 512 // TODO how many?
#define MAX_SLAVES 7 // e.g. 8-core machines
#define MAX_AUTOMATIONS 64

typedef enum _job_type_request_t job_type_request_t;
typedef enum _job_type_reply_t job_type_reply_t;
typedef enum _blocking_state_t blocking_state_t;
typedef enum _silencing_state_t silencing_state_t;
typedef enum _ramp_state_t ramp_state_t;
typedef enum _auto_type_t auto_type_t;

typedef char urn_uuid_t [URN_UUID_LENGTH];
typedef struct _dsp_slave_t dsp_slave_t;
typedef struct _dsp_client_t dsp_client_t;
typedef struct _dsp_master_t dsp_master_t;

typedef struct _mod_worker_t mod_worker_t;
typedef struct _midi_auto_t midi_auto_t;
typedef struct _osc_auto_t osc_auto_t;
typedef struct _auto_t auto_t;
typedef struct _mod_t mod_t;
typedef struct _port_t port_t;
typedef struct _job_t job_t;
typedef struct _source_t source_t;
typedef struct _pool_t pool_t;
typedef struct _port_driver_t port_driver_t;
typedef struct _app_prof_t app_prof_t;
typedef struct _mod_prof_t mod_prof_t;

typedef void (*port_multiplex_cb_t) (sp_app_t *app, port_t *port, uint32_t nsamples);
typedef void (*port_transfer_cb_t) (sp_app_t *app, port_t *port, uint32_t nsamples);

enum _silencing_state_t {
	SILENCING_STATE_RUN = 0,
	SILENCING_STATE_BLOCK,
	SILENCING_STATE_WAIT
};

enum _blocking_state_t {
	BLOCKING_STATE_RUN = 0,
	BLOCKING_STATE_DRAIN,
	BLOCKING_STATE_BLOCK,
	BLOCKING_STATE_WAIT,
};

static const bool advance_ui [] = {
	[BLOCKING_STATE_RUN]		= true,
	[BLOCKING_STATE_DRAIN]	= false,
	[BLOCKING_STATE_BLOCK]	= true,
	[BLOCKING_STATE_WAIT]		= false
};

static const bool advance_work [] = {
	[BLOCKING_STATE_RUN]		= true,
	[BLOCKING_STATE_DRAIN]	= true,
	[BLOCKING_STATE_BLOCK]	= true,
	[BLOCKING_STATE_WAIT]		= true
};

enum _ramp_state_t {
	RAMP_STATE_NONE = 0,
	RAMP_STATE_UP,
	RAMP_STATE_DOWN,
	RAMP_STATE_DOWN_DEL,
	RAMP_STATE_DOWN_DRAIN,
	RAMP_STATE_DOWN_DISABLE,
};

enum _job_type_request_t {
	JOB_TYPE_REQUEST_MODULE_SUPPORTED,
	JOB_TYPE_REQUEST_MODULE_ADD,
	JOB_TYPE_REQUEST_MODULE_DEL,
	JOB_TYPE_REQUEST_PRESET_LOAD,
	JOB_TYPE_REQUEST_PRESET_SAVE,
	JOB_TYPE_REQUEST_BUNDLE_LOAD,
	JOB_TYPE_REQUEST_BUNDLE_SAVE,
	JOB_TYPE_REQUEST_DRAIN
};

enum _job_type_reply_t {
	JOB_TYPE_REPLY_MODULE_SUPPORTED,
	JOB_TYPE_REPLY_MODULE_ADD,
	JOB_TYPE_REPLY_MODULE_DEL,
	JOB_TYPE_REPLY_PRESET_LOAD,
	JOB_TYPE_REPLY_PRESET_SAVE,
	JOB_TYPE_REPLY_BUNDLE_LOAD,
	JOB_TYPE_REPLY_BUNDLE_SAVE,
	JOB_TYPE_REPLY_DRAIN
};

struct _dsp_slave_t {
	dsp_master_t *dsp_master;
	sem_t sem;
	pthread_t thread;
};

struct _dsp_client_t {
	atomic_int ref_count;
	unsigned num_sinks;
	unsigned num_sources;
	dsp_client_t *sinks [64]; //FIXME

#if defined(USE_DYNAMIC_PARALLELIZER)
	unsigned weight;
#else
	int count;
	unsigned mark;
#endif
};

struct _dsp_master_t {
	dsp_slave_t dsp_slaves [MAX_SLAVES];
	atomic_bool kill;
	atomic_int ref_count;
	unsigned concurrent;
	unsigned num_slaves;
	uint32_t nsamples;
};

struct _job_t {
	union {
		job_type_request_t request;
		job_type_reply_t reply;
	};
	union {
		mod_t *mod;
		int32_t status;
	};
	LV2_URID urn;
};

struct _pool_t {
	size_t size;
	void *buf;
};

struct _app_prof_t {
	struct timespec t0;
	struct timespec t1;
	unsigned sum;
	unsigned min;
	unsigned max;
	unsigned count;
};

struct _mod_prof_t {
	unsigned sum;
	unsigned min;
	unsigned max;
};

struct _mod_worker_t {
	sem_t sem;
	pthread_t thread;
	atomic_bool kill;
	varchunk_t *app_to_worker;
	varchunk_t *state_to_worker;
	varchunk_t *app_from_worker;
};

enum _auto_type_t {
	AUTO_TYPE_NONE = 0,
	AUTO_TYPE_MIDI,
	AUTO_TYPE_OSC
};

struct _midi_auto_t {
	int8_t channel;
	int8_t controller;
};

struct _osc_auto_t {
	char path [128]; //TODO how big?
};

struct _auto_t {
	auto_type_t type;
	uint32_t index;
	LV2_URID property;
	LV2_URID range;

	double a;
	double b;
	double c;
	double d;

	double mul;
	double add;

	union {
		midi_auto_t midi;
		osc_auto_t osc;
	};
};

struct _mod_t {
	sp_app_t *app;
	int32_t uid;
	urn_uuid_t urn_uri;
	LV2_URID urn;
	LV2_URID visible;
	bool disabled;

	bool delete_request;
	bool needs_bypassing;
	bool bypassed;

	// worker
	struct {
		const LV2_Worker_Interface *iface;
		LV2_Worker_Schedule schedule;
	} worker;

	mod_worker_t mod_worker;

	LV2_Worker_Schedule state_worker;
	LV2_Feature state_feature_list [1];
	LV2_Feature *state_features [2];

	// system_port
	bool system_ports;

	// log
	LV2_Log_Log log;

	// make_path
	LV2_State_Make_Path make_path;

	struct {
		const LV2_Inline_Display_Interface *iface;
		const LV2_Inline_Display_Image_Surface *surf;
		LV2_Inline_Display queue_draw;
		atomic_bool draw_queued;
		atomic_bool rendered;
		atomic_flag lock;
	} idisp;

	// opts
	struct {
		LV2_Options_Option options [7];
		const LV2_Options_Interface *iface;
	} opts;

	// state
	struct {
		const LV2_State_Interface *iface;
	} state;

	// features
	LV2_Feature feature_list [NUM_FEATURES];
	const LV2_Feature *features [NUM_FEATURES + 1];

	// self
	const LilvPlugin *plug;
	LV2_URID plug_urid;
	LilvInstance *inst;
	LV2_Handle handle;
	LilvNodes *presets;
	char *uri_str;

	// ports
	unsigned num_ports;
	port_t *ports;

	pool_t pools [PORT_TYPE_NUM];
	mod_prof_t prof;

	dsp_client_t dsp_client;

	struct {
		float x;
		float y;
	} pos;

	auto_t automations [MAX_AUTOMATIONS];
};

struct _port_driver_t {
	port_multiplex_cb_t multiplex;
	port_transfer_cb_t transfer;
	bool sparse_update;
};

struct _source_t {
	port_t *port;
	float gain;
	struct {
		float x;
		float y;
	} pos;

	// ramping
	struct {
		int can;
		int samples;
		ramp_state_t state;
		float value;
	} ramp;
};

typedef struct _connectable_t connectable_t;
typedef struct _control_port_t control_port_t;
typedef struct _audio_port_t audio_port_t;
typedef struct _cv_port_t cv_port_t;
typedef struct _atom_port_t atom_port_t;

struct _connectable_t {
	int num_sources;
	source_t sources [MAX_SOURCES];
};

struct _control_port_t {
	bool is_integer;
	bool is_toggled;

	float dflt;
	float min;
	float max;
	float range;
	float range_1;
	float last;
	int32_t i32;
	float f32;

	float stash;
	bool stashing;
	atomic_flag lock;
};

struct _audio_port_t {
	connectable_t connectable;
	float last;
};

struct _cv_port_t {
	connectable_t connectable;
	float last;
};

struct _atom_port_t {
	connectable_t connectable;
	port_buffer_type_t buffer_type; // none, sequence
	bool patchable; // support patch:Message
};

struct _port_t {
	mod_t *mod;

	uint32_t index;
	const char *symbol;

	size_t size;
	void *base;

	port_type_t type; // audio, CV, control, atom
	port_direction_t direction; // input, output
	LV2_URID protocol; // floatProtocol, peakProtocol, atomTransfer, eventTransfer
	const port_driver_t *driver;

	int subscriptions; // subsriptions reference counter

	// system_port iface
	struct {
		system_port_t type;
		void *data;
	} sys;

	union {
		control_port_t control;
		audio_port_t audio;
		cv_port_t cv;
		atom_port_t atom;
	};
};

struct _sp_app_t {
	sp_app_driver_t *driver;
	void *data;

	atomic_bool dirty;

	blocking_state_t block_state;
	silencing_state_t silence_state;
	bool load_bundle;

	struct {
		const char *home;
	} dir;

	int embedded;
	LilvWorld *world;
	const LilvPlugins *plugs;

	reg_t regs;
	LV2_Atom_Forge forge;

	unsigned num_mods;
	mod_t *mods [MAX_MODS];

	sp_app_system_source_t system_sources [64]; //FIXME, how many?
	sp_app_system_sink_t system_sinks [64]; //FIXME, how many?

	LV2_State_Make_Path make_path;
	LV2_State_Map_Path map_path;
	LV2_Feature state_feature_list [2];
	LV2_Feature *state_features [3];
	LV2_URI_Map_Feature uri_to_id;

	char *bundle_path;
	char *bundle_filename;
	LV2_URID bundle_urn; //FIXME use this instead of bundle_path

	struct {
		unsigned period_cnt;
		unsigned bound;
		unsigned counter;
	} fps;

	int ramp_samples;

	Sratom *sratom;
	app_prof_t prof;

	int32_t ncols;
	int32_t nrows;
	float nleft;

	dsp_master_t dsp_master;

	LV2_OSC_URID osc_urid;
};

extern const port_driver_t control_port_driver;
extern const port_driver_t audio_port_driver;
extern const port_driver_t cv_port_driver;
extern const port_driver_t atom_port_driver;
extern const port_driver_t seq_port_driver;

#define PORT_BASE_ALIGNED(PORT) ASSUME_ALIGNED((PORT)->base)
#define PORT_SIZE(PORT) ((PORT)->size)

/*
 * Debug
 */
int
sp_app_log_error(sp_app_t *app, const char *fmt, ...);

int
sp_app_log_note(sp_app_t *app, const char *fmt, ...);

int
sp_app_log_warning(sp_app_t *app, const char *fmt, ...);

int
sp_app_log_trace(sp_app_t *app, const char *fmt, ...);

/*
 * UI
 */
static inline void *
__sp_app_to_ui_request(sp_app_t *app, size_t minimum, size_t *maximum)
{
	if(app->driver->to_ui_request)
		return app->driver->to_ui_request(minimum, maximum, app->data);

	sp_app_log_trace(app, "%s: failed to request buffer\n", __func__);
	return NULL;
}
#define _sp_app_to_ui_request(APP, MINIMUM) \
	ASSUME_ALIGNED(__sp_app_to_ui_request((APP), (MINIMUM), NULL))
#define _sp_app_to_ui_request_max(APP, MINIMUM, MAXIMUM) \
	ASSUME_ALIGNED(__sp_app_to_ui_request((APP), (MINIMUM), (MAXIMUM)))

static inline void
_sp_app_to_ui_advance(sp_app_t *app, size_t written)
{
	if(app->driver->to_ui_advance)
		app->driver->to_ui_advance(written, app->data);
	else
		sp_app_log_trace(app, "%s: failed to advance buffer\n", __func__);
}

static inline LV2_Atom *
_sp_app_to_ui_request_atom(sp_app_t *app)
{
	size_t maximum;
	LV2_Atom *atom = _sp_app_to_ui_request_max(app, 4096, &maximum); //FIXME what should minimum be?

	if(atom)
		lv2_atom_forge_set_buffer(&app->forge, (uint8_t *)atom, maximum);
	else
		sp_app_log_trace(app, "%s: failed to request atom\n", __func__);

	return atom;
}

static inline void
_sp_app_to_ui_advance_atom(sp_app_t *app, const LV2_Atom *atom)
{
	_sp_app_to_ui_advance(app, lv2_atom_total_size(atom));
}

static inline void
_sp_app_to_ui_overflow(sp_app_t *app)
{
	sp_app_log_trace(app, "%s: buffer overflow\n", __func__);
}

static inline LV2_Atom *
_sp_request_atom(sp_app_t *app, sp_to_request_t req, void *data)
{
	size_t maximum;
	LV2_Atom *atom = req(4096, &maximum, data); //FIXME what should minimum be?

	if(atom)
		lv2_atom_forge_set_buffer(&app->forge, (uint8_t *)atom, maximum);
	else
		sp_app_log_trace(app, "%s: failed to request atom\n", __func__);

	return atom;
}

static inline void
_sp_advance_atom(sp_app_t *app, const LV2_Atom *atom, sp_to_advance_t adv, void *data)
{
	adv(lv2_atom_total_size(atom), data);
}

/*
 * Worker
 */

static inline void *
__sp_app_to_worker_request(sp_app_t *app, size_t minimum, size_t *maximum)
{
	if(app->driver->to_worker_request)
		return app->driver->to_worker_request(minimum, maximum, app->data);

	sp_app_log_trace(app, "%s: failed to request buffer\n", __func__);
	return NULL;
}
#define _sp_app_to_worker_request(APP, MINIMUM) \
	ASSUME_ALIGNED(__sp_app_to_worker_request((APP), (MINIMUM), NULL))
#define _sp_app_to_worker_request_max(APP, MINIMUM, MAXIMUM) \
	ASSUME_ALIGNED(__sp_app_to_worker_request((APP), (MINIMUM), (MAXIMUM)))

static inline void
_sp_app_to_worker_advance(sp_app_t *app, size_t written)
{
	if(app->driver->to_worker_advance)
		app->driver->to_worker_advance(written, app->data);
	else
		sp_app_log_trace(app, "%s: failed to advance buffer\n", __func__);
}

void
_sp_app_order(sp_app_t *app);

void
_sp_app_reset(sp_app_t *app);

void
_sp_app_populate(sp_app_t *app);

/*
 * State
 */
int
_sp_app_state_preset_load(sp_app_t *app, mod_t *mod, const char *uri, bool async);

int
_sp_app_state_preset_save(sp_app_t *app, mod_t *mod, const char *target);

int
_sp_app_state_bundle_save(sp_app_t *app, const char *bundle_path);

int
_sp_app_state_bundle_load(sp_app_t *app, const char *bundle_path);

/*
 * Mod
 */
const LilvPlugin *
_sp_app_mod_is_supported(sp_app_t *app, const void *uri);

mod_t *
_sp_app_mod_add(sp_app_t *app, const char *uri, LV2_URID urn);

void
_sp_app_mod_eject(sp_app_t *app, mod_t *mod);

int
_sp_app_mod_del(sp_app_t *app, mod_t *mod);

mod_t *
_sp_app_mod_get_by_uid(sp_app_t *app, int32_t uid);

void
_sp_app_mod_reinitialize(mod_t *mod);

LV2_Worker_Status
_sp_app_mod_worker_work_sync(mod_t *mod, size_t size, const void *payload);

/*
 * Port
 */
void 
_dsp_master_reorder(sp_app_t *app);

void
_sp_app_port_disconnect(sp_app_t *app, port_t *src_port, port_t *snk_port);

int
_sp_app_port_disconnect_request(sp_app_t *app, port_t *src_port, port_t *snk_port,
	ramp_state_t ramp_state);

bool
_sp_app_port_connected(port_t *src_port, port_t *snk_port, float gain);

int
_sp_app_port_connect(sp_app_t *app, port_t *src_port, port_t *snk_port, float gain);

int
_sp_app_port_silence_request(sp_app_t *app, port_t *src_port, port_t *snk_port,
	ramp_state_t ramp_state);

void
_sp_app_port_control_stash(port_t *port);

int
_sp_app_port_desilence(sp_app_t *app, port_t *src_port, port_t *snk_port);

connectable_t *
_sp_app_port_connectable(port_t *src_port);

static inline void
_sp_app_port_spin_lock(control_port_t *control)
{
	while(atomic_flag_test_and_set_explicit(&control->lock, memory_order_acquire))
	{
		// spin
	}
}

static inline bool
_sp_app_port_try_lock(control_port_t *control)
{
	return atomic_flag_test_and_set_explicit(&control->lock, memory_order_acquire) == false;
}

static inline void
_sp_app_port_unlock(control_port_t *control)
{
	atomic_flag_clear_explicit(&control->lock, memory_order_release);
}

/*
 * Ui
 */
void
_sp_app_ui_set_modlist(sp_app_t *app, LV2_URID subj, int32_t seqn);

void
_connection_list_add(sp_app_t *app, const LV2_Atom_Object *obj);

void
_node_list_add(sp_app_t *app, const LV2_Atom_Object *obj);

void
_automation_list_add(sp_app_t *app, const LV2_Atom_Object *obj);

#endif
