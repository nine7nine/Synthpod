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

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <ctype.h> // isspace
#include <math.h>

#include <Eina.h>
#include <Efreet.h>
#include <Ecore_File.h>

#include <synthpod_app.h>
#include <synthpod_private.h>

#include <sratom/sratom.h>
#include <varchunk.h>

#include <lv2_extensions.h> // ardour's inline display

#define XSD_PREFIX "http://www.w3.org/2001/XMLSchema#"
#define RDF_PREFIX "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
#define RDFS_PREFIX "http://www.w3.org/2000/01/rdf-schema#"
#define SPOD_PREFIX "http://open-music-kontrollers.ch/lv2/synthpod#"

#define NUM_FEATURES 18
#define MAX_SOURCES 32 // TODO how many?
#define MAX_MODS 512 // TODO how many?
#define FROM_UI_NUM 24
#define MAX_SLAVES 7 // e.g. 8-core machines

typedef enum _job_type_request_t job_type_request_t;
typedef enum _job_type_reply_t job_type_reply_t;
typedef enum _blocking_state_t blocking_state_t;
typedef enum _silencing_state_t silencing_state_t;
typedef enum _ramp_state_t ramp_state_t;

typedef struct _dsp_slave_t dsp_slave_t;
typedef struct _dsp_client_t dsp_client_t;
typedef struct _dsp_master_t dsp_master_t;

typedef struct _mod_worker_t mod_worker_t;
typedef struct _mod_t mod_t;
typedef struct _port_t port_t;
typedef struct _job_t job_t;
typedef struct _source_t source_t;
typedef struct _pool_t pool_t;
typedef struct _port_driver_t port_driver_t;
typedef struct _app_prof_t app_prof_t;
typedef struct _mod_prof_t mod_prof_t;

typedef struct _from_ui_t from_ui_t;
typedef bool (*from_ui_cb_t)(sp_app_t *app, const LV2_Atom *atom);

typedef void (*port_simplex_cb_t) (sp_app_t *app, port_t *port, uint32_t nsamples);
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
	atomic_flag flag;
	atomic_bool done;
	atomic_uint ref_count;
	unsigned num_sinks;
	unsigned num_sources;
	dsp_client_t *sinks [64]; //FIXME
	int count;
	unsigned mark;
};

struct _dsp_master_t {
	dsp_slave_t dsp_slaves [MAX_SLAVES];
	atomic_bool kill;
	atomic_bool roll;
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
	char uri [0];
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
	varchunk_t *app_from_worker;
};

struct _mod_t {
	sp_app_t *app;
	u_id_t uid;
	bool selected;
	LV2_URID visible;
	bool disabled;
	bool embedded;

	bool delete_request;
	bool bypassed;

	// worker
	struct {
		const LV2_Worker_Interface *iface;
		LV2_Worker_Schedule schedule;
	} worker;

	// zero_worker
	struct {
		const Zero_Worker_Interface *iface;
		Zero_Worker_Schedule schedule;
	} zero;

	mod_worker_t mod_worker;

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
	} idisp;

	// opts
	struct {
		LV2_Options_Option options [6];
		const LV2_Options_Interface *iface;
	} opts;

	// features
	LV2_Feature feature_list [NUM_FEATURES];
	const LV2_Feature *features [NUM_FEATURES + 1];

	// self
	const LilvPlugin *plug;
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
};

struct _port_driver_t {
	port_simplex_cb_t simplex;
	port_multiplex_cb_t multiplex;
	port_transfer_cb_t transfer;
	bool sparse_update;
};

struct _source_t {
	port_t *port;

	// ramping
	struct {
		int can;
		int samples;
		ramp_state_t state;
		float value;
	} ramp;
};

struct _port_t {
	mod_t *mod;
	int selected;
	int monitored;
	
	const LilvPort *tar;
	uint32_t index;

	int num_sources;
	int num_feedbacks;
	bool is_ramping;
	source_t sources [MAX_SOURCES];

	size_t size;
	void *buf;
	void *base;
	int32_t i32;
	float f32;

	int integer;
	int toggled;
	port_direction_t direction; // input, output
	port_type_t type; // audio, CV, control, atom
	port_buffer_type_t buffer_type; // none, sequence
	bool patchable; // support patch:Message

	LV2_URID protocol; // floatProtocol, peakProtocol, atomTransfer, eventTransfer
	int subscriptions; // subsriptions reference counter
	const port_driver_t *driver;

	float last;

	float dflt;
	float min;
	float max;
	float range;
	float range_1;

	// system_port iface
	struct {
		system_port_t type;
		void *data;
	} sys;

	float stash;
	bool stashing;
	atomic_flag lock;
};

struct _from_ui_t {
	LV2_URID protocol;
	from_ui_cb_t cb;
};

struct _sp_app_t {
	sp_app_driver_t *driver;
	void *data;

	atomic_flag dirty;

	blocking_state_t block_state;
	silencing_state_t silence_state;
	bool load_bundle;

	struct {
		const char *home;
		const char *data;
		const char *config;
	} dir;

	int embedded;
	LilvWorld *world;
	const LilvPlugins *plugs;
	
	reg_t regs;
	LV2_Atom_Forge forge;

	unsigned num_mods;
	mod_t *mods [MAX_MODS];
	mod_t *ords [MAX_MODS];

	sp_app_system_source_t system_sources [64]; //FIXME, how many?
	sp_app_system_sink_t system_sinks [64]; //FIXME, how many?

	u_id_t uid;
	
	LV2_State_Make_Path make_path;
	LV2_State_Map_Path map_path;
	LV2_Feature state_feature_list [2];
	LV2_Feature *state_features [3];
	LV2_URI_Map_Feature uri_to_id;

	char *bundle_path;
	char *bundle_filename;

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

	from_ui_t from_uis [FROM_UI_NUM];

	dsp_master_t dsp_master;
};

extern const port_driver_t control_port_driver;
extern const port_driver_t audio_port_driver;
extern const port_driver_t cv_port_driver;
extern const port_driver_t atom_port_driver;
extern const port_driver_t seq_port_driver;
extern const port_driver_t ev_port_driver;

#define SINK_IS_NILPLEX(PORT) ((((PORT)->num_sources + (PORT)->num_feedbacks) == 0) && !(PORT)->is_ramping)
#define SINK_IS_SIMPLEX(PORT) ((((PORT)->num_sources + (PORT)->num_feedbacks) == 1) && !(PORT)->is_ramping)
#define SINK_IS_MULTIPLEX(PORT) ((((PORT)->num_sources + (PORT)->num_feedbacks) > 1) || (PORT)->is_ramping)

#define PORT_BASE_ALIGNED(PORT) ASSUME_ALIGNED((PORT)->base)
#define PORT_BUF_ALIGNED(PORT) ASSUME_ALIGNED((PORT)->buf)

/*
 * UI
 */
static inline void *
__sp_app_to_ui_request(sp_app_t *app, size_t size)
{
	if(app->driver->to_ui_request)
		return app->driver->to_ui_request(size, app->data);
	else
		return NULL;
}
#define _sp_app_to_ui_request(APP, SIZE) \
	ASSUME_ALIGNED(__sp_app_to_ui_request((APP), (SIZE)))

static inline void
_sp_app_to_ui_advance(sp_app_t *app, size_t size)
{
	if(app->driver->to_ui_advance)
		app->driver->to_ui_advance(size, app->data);
}
	
void
sp_app_from_ui_fill(sp_app_t *app);

/*
 * Worker
 */

static inline void *
__sp_app_to_worker_request(sp_app_t *app, size_t size)
{
	if(app->driver->to_worker_request)
		return app->driver->to_worker_request(size, app->data);
	else
		return NULL;
}
#define _sp_app_to_worker_request(APP, SIZE) \
	ASSUME_ALIGNED(__sp_app_to_worker_request((APP), (SIZE)))

static inline void
_sp_app_to_worker_advance(sp_app_t *app, size_t size)
{
	if(app->driver->to_worker_advance)
		app->driver->to_worker_advance(size, app->data);
}

/*
 * State
 */
int
_sp_app_state_preset_load(sp_app_t *app, mod_t *mod, const char *uri);

int
_sp_app_state_preset_save(sp_app_t *app, mod_t *mod, const char *target);

int
_sp_app_state_bundle_save(sp_app_t *app, const char *bundle_path);

int
_sp_app_state_bundle_load(sp_app_t *app, const char *bundle_path);

/*
 * Mod
 */
mod_t *
_sp_app_mod_get(sp_app_t *app, u_id_t uid);

const LilvPlugin *
_sp_app_mod_is_supported(sp_app_t *app, const void *uri);

mod_t *
_sp_app_mod_add(sp_app_t *app, const char *uri, u_id_t uid);

void
_sp_app_mod_eject(sp_app_t *app, mod_t *mod);

int
_sp_app_mod_del(sp_app_t *app, mod_t *mod);

void
_sp_app_mod_reinitialize(mod_t *mod);

void
_sp_app_mod_qsort(mod_t **a, unsigned n);

/*
 * Port
 */
port_t *
_sp_app_port_get(sp_app_t *app, u_id_t uid, uint32_t index);

void
_sp_app_port_disconnect(sp_app_t *app, port_t *src_port, port_t *snk_port);

int
_sp_app_port_disconnect_request(sp_app_t *app, port_t *src_port, port_t *snk_port,
	ramp_state_t ramp_state);

bool
_sp_app_port_connected(port_t *src_port, port_t *snk_port);

int
_sp_app_port_connect(sp_app_t *app, port_t *src_port, port_t *snk_port);

int
_sp_app_port_silence_request(sp_app_t *app, port_t *src_port, port_t *snk_port,
	ramp_state_t ramp_state);

void
_sp_app_port_control_stash(port_t *port);

int
_sp_app_port_desilence(sp_app_t *app, port_t *src_port, port_t *snk_port);

static inline void
_sp_app_port_spin_lock(port_t *port)
{
	while(atomic_flag_test_and_set_explicit(&port->lock, memory_order_acquire))
	{
		// spin
	}
}

static inline bool
_sp_app_port_try_lock(port_t *port)
{
	return atomic_flag_test_and_set_explicit(&port->lock, memory_order_acquire) == false;
}

static inline void
_sp_app_port_unlock(port_t *port)
{
	atomic_flag_clear_explicit(&port->lock, memory_order_release);
}

#endif
