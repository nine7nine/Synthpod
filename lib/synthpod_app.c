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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h> // isspace
#include <math.h>

#if !defined(_WIN32)
#	include <sys/mman.h> // mlock
#else
#	include <Evil.h>
#endif

#include <cJSON.h>
#include <Eina.h>
#include <Efreet.h>
#include <Ecore_File.h>

#include <synthpod_app.h>
#include <synthpod_private.h>

#define NUM_FEATURES 13
#define MAX_SOURCES 32 // TODO how many?
#define MAX_MODS 512 // TODO how many?

typedef enum _job_type_t job_type_t;
typedef enum _bypass_state_t bypass_state_t;
typedef enum _ramp_state_t ramp_state_t;

typedef struct _mod_t mod_t;
typedef struct _port_t port_t;
typedef struct _work_t work_t;
typedef struct _job_t job_t;
typedef struct _source_t source_t;
typedef struct _pool_t pool_t;

enum _bypass_state_t {
	BYPASS_STATE_PAUSED	= 0,
	BYPASS_STATE_LOCKED,
	BYPASS_STATE_RUNNING,
	BYPASS_STATE_PAUSE_REQUESTED,
	BYPASS_STATE_LOCK_REQUESTED
};

enum _ramp_state_t {
	RAMP_STATE_NONE = 0,
	RAMP_STATE_UP,
	RAMP_STATE_DOWN,
	RAMP_STATE_DOWN_DEL
};

enum _job_type_t {
	JOB_TYPE_MODULE_ADD,
	JOB_TYPE_MODULE_DEL,
	JOB_TYPE_PRESET_LOAD,
	JOB_TYPE_PRESET_SAVE,
	JOB_TYPE_BUNDLE_LOAD,
	JOB_TYPE_BUNDLE_SAVE
};

struct _job_t {
	job_type_t type;
	union {
		mod_t *mod;
		int32_t status;
	};
	char uri [0];
};

struct _work_t {
	void *target;
	uint32_t size;
	uint8_t payload [0];
};

struct _pool_t {
	size_t size;
	void *buf;
};

struct _mod_t {
	sp_app_t *app;
	u_id_t uid;
	int selected;

	int delete_request;

	volatile bypass_state_t bypass_state;
	Eina_Semaphore bypass_sem;
	
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

	// system_port
	bool system_ports;	

	// log
	LV2_Log_Log log;

	// make_path
	LV2_State_Make_Path make_path;

	// opts
	struct {
		LV2_Options_Option options [5];
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
	source_t sources [MAX_SOURCES];

	size_t size;
	void *buf;
	int32_t i32;

	int integer;
	int toggled;
	port_direction_t direction; // input, output
	port_type_t type; // audio, CV, control, atom
	port_buffer_type_t buffer_type; // none, sequence
	int patchable; // support patch:Message

	LV2_URID protocol; // floatProtocol, peakProtocol, atomTransfer, eventTransfer
	int subscriptions; // subsriptions reference counter

	float last;

	float min;
	float dflt;
	float max;

	// system_port iface
	struct {
		system_port_t type;
		void *data;
	} sys;
};

struct _sp_app_t {
	sp_app_driver_t *driver;
	void *data;

	volatile bypass_state_t bypass_state;
	Eina_Semaphore bypass_sem;
	volatile int dirty;

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

	sp_app_system_source_t system_sources [128]; //FIXME, how many?
	sp_app_system_sink_t system_sinks [128]; //FIXME, how many?

	u_id_t uid;
	
	LV2_State_Make_Path make_path;
	LV2_State_Map_Path map_path;
	LV2_Feature state_feature_list [2];
	LV2_Feature *state_features [3];

	char *bundle_path;
	char *bundle_filename;

	struct {
		unsigned period_cnt;
		unsigned bound;
		unsigned counter;
	} fps;

	int ramp_samples;
};

static inline void *
_port_sink_get(port_t *port)
{
	return (port->num_sources + port->num_feedbacks) == 1
		? port->sources[0].port->buf
		: port->buf;
}
#define PORT_SINK_ALIGNED(PORT) ASSUME_ALIGNED(_port_sink_get((PORT)))
#define PORT_BUF_ALIGNED(PORT) ASSUME_ALIGNED((PORT)->buf)

static void
_state_set_value(const char *symbol, void *data,
	const void *value, uint32_t size, uint32_t type);

static const void *
_state_get_value(const char *symbol, void *data, uint32_t *size, uint32_t *type);

// rt
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

// rt
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

// non-rt worker-thread
static inline void *
__sp_worker_to_app_request(sp_app_t *app, size_t size)
{
	if(app->driver->to_app_request)
		return app->driver->to_app_request(size, app->data);
	else
		return NULL;
}
#define _sp_worker_to_app_request(APP, SIZE) \
	ASSUME_ALIGNED(__sp_worker_to_app_request((APP), (SIZE)))
static inline void
_sp_worker_to_app_advance(sp_app_t *app, size_t size)
{
	if(app->driver->to_app_advance)
		app->driver->to_app_advance(size, app->data);
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_vprintf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, va_list args)
{
	mod_t *mod = handle;
	sp_app_t *app = mod->app;

	char buf [1024]; //TODO how big?
	sprintf(buf, "{%i} ", mod->uid);
	vsprintf(buf + strlen(buf), fmt, args);
	
	app->driver->log_printf(app->data, type, "%s", buf);

	return 0;
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_printf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(handle, type, fmt, args);
  va_end(args);

	return ret;
}

// non-rt
void
sp_app_activate(sp_app_t *app)
{
	//TODO
}

// rt
static inline void
_sp_app_update_system_sources(sp_app_t *app)
{
	int num_system_sources = 0;

	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		if(!mod->system_ports) // has system ports?
			continue; // skip

		for(unsigned p=0; p<mod->num_ports; p++)
		{
			port_t *port = &mod->ports[p];

			if(port->sys.type == SYSTEM_PORT_NONE)
				continue; // skip

			if(port->direction == PORT_DIRECTION_OUTPUT)
			{
				app->system_sources[num_system_sources].type = port->sys.type;
				app->system_sources[num_system_sources].buf = port->buf;
				app->system_sources[num_system_sources].sys_port = port->sys.data;
				num_system_sources += 1;
			}
		}
	}

	// sentinel
	app->system_sources[num_system_sources].type = SYSTEM_PORT_NONE;
	app->system_sources[num_system_sources].buf = NULL;
	app->system_sources[num_system_sources].sys_port = NULL;
}

// rt
static inline void
_sp_app_update_system_sinks(sp_app_t *app)
{
	int num_system_sinks = 0;

	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		if(!mod->system_ports) // has system ports?
			continue;

		for(unsigned p=0; p<mod->num_ports; p++)
		{
			port_t *port = &mod->ports[p];

			if(port->sys.type == SYSTEM_PORT_NONE)
				continue; // skip

			if(port->direction == PORT_DIRECTION_INPUT)
			{
				app->system_sinks[num_system_sinks].type = port->sys.type;
				app->system_sinks[num_system_sinks].buf = PORT_SINK_ALIGNED(port);
				app->system_sinks[num_system_sinks].sys_port = port->sys.data;
				num_system_sinks += 1;
			}
		}
	}

	// sentinel
	app->system_sinks[num_system_sinks].type = SYSTEM_PORT_NONE;
	app->system_sinks[num_system_sinks].buf = NULL;
	app->system_sinks[num_system_sinks].sys_port = NULL;
}

// rt
const sp_app_system_source_t *
sp_app_get_system_sources(sp_app_t *app)
{
	_sp_app_update_system_sources(app);

	return app->system_sources;
}

// rt
const sp_app_system_sink_t *
sp_app_get_system_sinks(sp_app_t *app)
{
	_sp_app_update_system_sinks(app);

	return app->system_sinks;
}

// rt
static LV2_Worker_Status
_schedule_work(LV2_Worker_Schedule_Handle handle, uint32_t size, const void *data)
{
	mod_t *mod = handle;
	sp_app_t *app = mod->app;

	size_t work_size = sizeof(work_t) + size;
	work_t *work = _sp_app_to_worker_request(app, work_size);
	if(work)
	{
		work->target = mod;
		work->size = size;
		memcpy(work->payload, data, size);
		_sp_app_to_worker_advance(app, work_size);
		
		return LV2_WORKER_SUCCESS;
	}

	return LV2_WORKER_ERR_NO_SPACE;
}

// rt
static void *
_zero_sched_request(Zero_Worker_Handle handle, uint32_t size)
{
	mod_t *mod = handle;
	sp_app_t *app = mod->app;

	size_t work_size = sizeof(work_t) + size;
	work_t *work = _sp_app_to_worker_request(app, work_size);
	if(work)
	{
		work->target = mod;
		work->size = size; //TODO overwrite in _zero_advance if size != written

		return work->payload;
	}

	return NULL;
}

// rt
static Zero_Worker_Status
_zero_sched_advance(Zero_Worker_Handle handle, uint32_t written)
{
	mod_t *mod = handle;
	sp_app_t *app = mod->app;

	size_t work_written = sizeof(work_t) + written;
	_sp_app_to_worker_advance(app, work_written);

	return ZERO_WORKER_SUCCESS;
}

// non-rt
static char *
_mod_make_path(LV2_State_Make_Path_Handle instance, const char *abstract_path)
{
	mod_t *mod = instance;
	sp_app_t *app = mod->app;
	
	char *absolute_path = NULL;
	asprintf(&absolute_path, "%s/%u/%s", app->bundle_path, mod->uid, abstract_path);

	// create leading directory tree, e.g. up to last '/'
	if(absolute_path)
	{
		const char *end = strrchr(absolute_path, '/');
		if(end)
		{
			char *path = strndup(absolute_path, end - absolute_path);
			if(path)
			{
				ecore_file_mkpath(path);
				free(path);
			}
		}
	}

	return absolute_path;
}

static inline int
_mod_alloc_pool(pool_t *pool)
{
#if defined(_WIN32)
	pool->buf = _aligned_malloc(pool->size, 8);
	if(pool->buf)
	{
#else
	posix_memalign(&pool->buf, 8, pool->size);
	if(pool->buf)
	{
		mlock(pool->buf, pool->size);
#endif
		memset(pool->buf, 0x0, pool->size);

		return 0;
	}

	return -1;
}

static inline void
_mod_free_pool(pool_t *pool)
{
	if(pool->buf)
	{
#if !defined(_WIN32)
		munlock(pool->buf, pool->size);
#endif
		free(pool->buf);
		pool->buf = NULL;
	}
}

static void
_mod_slice_pool(mod_t *mod, port_type_t type)
{
	// set ptr to pool buffer
	void *ptr = mod->pools[type].buf;

	for(port_direction_t dir=0; dir<PORT_DIRECTION_NUM; dir++)
	{
		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *tar = &mod->ports[i];

			if( (tar->type != type) || (tar->direction != dir) )
				continue; //skip

			// define buffer slice
			tar->buf = ptr;

			// initialize control buffers to default value
			if(tar->type == PORT_TYPE_CONTROL)
			{
				float *buf_ptr = PORT_BUF_ALIGNED(tar);
				*buf_ptr = tar->dflt;
			}

			ptr += lv2_atom_pad_size(tar->size);
		}
	}
}


// non-rt worker-thread
static inline mod_t *
_sp_app_mod_add(sp_app_t *app, const char *uri, u_id_t uid)
{
	LilvNode *uri_node = lilv_new_uri(app->world, uri);
	if(!uri_node)
		return NULL;

	const LilvPlugin *plug = lilv_plugins_get_by_uri(app->plugs, uri_node);
	lilv_node_free(uri_node);
			
	if(!plug)
		return NULL;

	mod_t *mod = calloc(1, sizeof(mod_t));
	if(!mod)
		return NULL;

	mod->bypass_state = BYPASS_STATE_RUNNING;
	eina_semaphore_new(&mod->bypass_sem, 0);

	// populate worker schedule
	mod->worker.schedule.handle = mod;
	mod->worker.schedule.schedule_work = _schedule_work;

	// populate zero_worker schedule
	mod->zero.schedule.handle = mod;
	mod->zero.schedule.request = _zero_sched_request;
	mod->zero.schedule.advance = _zero_sched_advance;

	// populate log
	mod->log.handle = mod;
	mod->log.printf = _log_printf;
	mod->log.vprintf = _log_vprintf;

	mod->make_path.handle = mod;
	mod->make_path.path = _mod_make_path;
		
	// populate options
	mod->opts.options[0].context = LV2_OPTIONS_INSTANCE;
	mod->opts.options[0].subject = 0;
	mod->opts.options[0].key = app->regs.bufsz.max_block_length.urid;
	mod->opts.options[0].size = sizeof(int32_t);
	mod->opts.options[0].type = app->forge.Int;
	mod->opts.options[0].value = &app->driver->max_block_size;
	
	mod->opts.options[1].context = LV2_OPTIONS_INSTANCE;
	mod->opts.options[1].subject = 0;
	mod->opts.options[1].key = app->regs.bufsz.min_block_length.urid;
	mod->opts.options[1].size = sizeof(int32_t);
	mod->opts.options[1].type = app->forge.Int;
	mod->opts.options[1].value = &app->driver->min_block_size;
	
	mod->opts.options[2].context = LV2_OPTIONS_INSTANCE;
	mod->opts.options[2].subject = 0;
	mod->opts.options[2].key = app->regs.bufsz.sequence_size.urid;
	mod->opts.options[2].size = sizeof(int32_t);
	mod->opts.options[2].type = app->forge.Int;
	mod->opts.options[2].value = &app->driver->seq_size;
	
	mod->opts.options[3].context = LV2_OPTIONS_INSTANCE;
	mod->opts.options[3].subject = 0;
	mod->opts.options[3].key = app->regs.bufsz.nominal_block_length.urid;
	mod->opts.options[3].size = sizeof(int32_t);
	mod->opts.options[3].type = app->forge.Int;
	mod->opts.options[3].value = &app->driver->max_block_size; // set to max by default

	mod->opts.options[4].key = 0; // sentinel
	mod->opts.options[4].value = NULL; // sentinel

	// populate feature list
	int nfeatures = 0;
	mod->feature_list[nfeatures].URI = LV2_URID__map;
	mod->feature_list[nfeatures++].data = app->driver->map;

	mod->feature_list[nfeatures].URI = LV2_URID__unmap;
	mod->feature_list[nfeatures++].data = app->driver->unmap;

	mod->feature_list[nfeatures].URI = LV2_WORKER__schedule;
	mod->feature_list[nfeatures++].data = &mod->worker.schedule;

	mod->feature_list[nfeatures].URI = LV2_LOG__log;
	mod->feature_list[nfeatures++].data = &mod->log;

	mod->feature_list[nfeatures].URI = LV2_STATE__makePath;
	mod->feature_list[nfeatures++].data = &mod->make_path;
	
	mod->feature_list[nfeatures].URI = LV2_BUF_SIZE__boundedBlockLength;
	mod->feature_list[nfeatures++].data = NULL;

	mod->feature_list[nfeatures].URI = LV2_OPTIONS__options;
	mod->feature_list[nfeatures++].data = mod->opts.options;

	/* TODO support
	mod->feature_list[nfeatures].URI = LV2_PORT_PROPS__supportsStrictBounds;
	mod->feature_list[nfeatures++].data = NULL;
	*/
	
	/* TODO support
	mod->feature_list[nfeatures].URI = LV2_RESIZE_PORT__resize;
	mod->feature_list[nfeatures++].data = NULL;
	*/

	/* TODO support
	mod->feature_list[nfeatures].URI = LV2_STATE__loadDefaultState;
	mod->feature_list[nfeatures++].data = NULL;
	*/

	mod->feature_list[nfeatures].URI = SYNTHPOD_WORLD;
	mod->feature_list[nfeatures++].data = app->world;

	mod->feature_list[nfeatures].URI = ZERO_WORKER__schedule;
	mod->feature_list[nfeatures++].data = &mod->zero.schedule;

	if(app->driver->system_port_add && app->driver->system_port_del)
	{
		mod->feature_list[nfeatures].URI = SYNTHPOD_PREFIX"systemPorts";
		mod->feature_list[nfeatures++].data = NULL;
	}

	if(app->driver->osc_sched)
	{
		mod->feature_list[nfeatures].URI = OSC__schedule;
		mod->feature_list[nfeatures++].data = app->driver->osc_sched;
	}

	if(app->driver->features & SP_APP_FEATURE_FIXED_BLOCK_LENGTH)
	{
		mod->feature_list[nfeatures].URI = LV2_BUF_SIZE__fixedBlockLength;
		mod->feature_list[nfeatures++].data = NULL;
	}

	if(app->driver->features & SP_APP_FEATURE_POWER_OF_2_BLOCK_LENGTH)
	{
		mod->feature_list[nfeatures].URI = LV2_BUF_SIZE__powerOf2BlockLength;
		mod->feature_list[nfeatures++].data = NULL;
	}

	assert(nfeatures <= NUM_FEATURES);

	for(int i=0; i<nfeatures; i++)
		mod->features[i] = &mod->feature_list[i];
	mod->features[nfeatures] = NULL; // sentinel

	// check for missing features
	int missing_required_feature = 0;
	LilvNodes *required_features = lilv_plugin_get_required_features(plug);
	if(required_features)
	{
		LILV_FOREACH(nodes, i, required_features)
		{
			const LilvNode* required_feature = lilv_nodes_get(required_features, i);
			const char *required_feature_uri = lilv_node_as_uri(required_feature);
			missing_required_feature = 1;

			for(int f=0; f<nfeatures; f++)
			{
				if(!strcmp(mod->feature_list[f].URI, required_feature_uri))
				{
					missing_required_feature = 0;
					break;
				}
			}

			if(missing_required_feature)
			{
				fprintf(stderr, "plugin '%s' requires non-supported feature: %s\n",
					uri, required_feature_uri);
				break;
			}
		}
		lilv_nodes_free(required_features);
	}
	if(missing_required_feature)
	{
		free(mod);
		return NULL; // plugin requires a feature we do not support
	}
		
	mod->app = app;
	mod->uid = uid != 0 ? uid : app->uid++;
	mod->plug = plug;
	mod->uri_str = strdup(uri); //TODO check
	mod->num_ports = lilv_plugin_get_num_ports(plug);
	mod->inst = lilv_plugin_instantiate(plug, app->driver->sample_rate, mod->features);
	mod->handle = lilv_instance_get_handle(mod->inst),
	mod->worker.iface = lilv_instance_get_extension_data(mod->inst,
		LV2_WORKER__interface);
	mod->zero.iface = lilv_instance_get_extension_data(mod->inst,
		ZERO_WORKER__interface);
	mod->opts.iface = lilv_instance_get_extension_data(mod->inst,
		LV2_OPTIONS__interface);
	mod->system_ports = lilv_plugin_has_feature(plug, app->regs.synthpod.system_ports.node);

	// clear pool sizes
	for(port_type_t pool=0; pool<PORT_TYPE_NUM; pool++)
		mod->pools[pool].size = 0;

	mod->ports = calloc(mod->num_ports, sizeof(port_t));
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *tar = &mod->ports[i];
		const LilvPort *port = lilv_plugin_get_port_by_index(plug, i);

		tar->size = 0;
		tar->mod = mod;
		tar->tar = port;
		tar->index = i;
		tar->integer = lilv_port_has_property(plug, port, app->regs.port.integer.node);
		tar->toggled = lilv_port_has_property(plug, port, app->regs.port.toggled.node);
		tar->direction = lilv_port_is_a(plug, port, app->regs.port.input.node)
			? PORT_DIRECTION_INPUT
			: PORT_DIRECTION_OUTPUT;

		// register system ports
		if(mod->system_ports)
		{
			if(lilv_port_is_a(plug, port, app->regs.synthpod.control_port.node))
				tar->sys.type = SYSTEM_PORT_CONTROL;
			else if(lilv_port_is_a(plug, port, app->regs.synthpod.audio_port.node))
				tar->sys.type = SYSTEM_PORT_AUDIO;
			else if(lilv_port_is_a(plug, port, app->regs.synthpod.cv_port.node))
				tar->sys.type = SYSTEM_PORT_CV;
			else if(lilv_port_is_a(plug, port, app->regs.synthpod.midi_port.node))
				tar->sys.type = SYSTEM_PORT_MIDI;
			else if(lilv_port_is_a(plug, port, app->regs.synthpod.osc_port.node))
				tar->sys.type = SYSTEM_PORT_OSC;
			else if(lilv_port_is_a(plug, port, app->regs.synthpod.com_port.node))
				tar->sys.type = SYSTEM_PORT_COM;
			else
				tar->sys.type = SYSTEM_PORT_NONE;

			if(app->driver->system_port_add)
			{
				//FIXME check lilv returns
				char *short_name = NULL;
				char *pretty_name = NULL;
				const LilvNode *port_symbol_node = lilv_port_get_symbol(plug, port);
				LilvNode *port_name_node = lilv_port_get_name(plug, port);

				asprintf(&short_name, "plugin_%u_%s",
					mod->uid, lilv_node_as_string(port_symbol_node));
				asprintf(&pretty_name, "Plugin %u - %s",
					mod->uid, lilv_node_as_string(port_name_node));
				tar->sys.data = app->driver->system_port_add(app->data, tar->sys.type,
					short_name, pretty_name, tar->direction == PORT_DIRECTION_OUTPUT);

				lilv_node_free(port_name_node);
				free(short_name);
				free(pretty_name);
			}
		}
		else
		{
			tar->sys.type = SYSTEM_PORT_NONE;
			tar->sys.data = NULL;
		}

		if(lilv_port_is_a(plug, port, app->regs.port.audio.node))
		{
			tar->size = app->driver->max_block_size * sizeof(float);
			tar->type =  PORT_TYPE_AUDIO;
			tar->selected = 1;
			tar->monitored = 1;
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.cv.node))
		{
			tar->size = app->driver->max_block_size * sizeof(float);
			tar->type = PORT_TYPE_CV;
			tar->selected = 1;
			tar->monitored = 1;
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.control.node))
		{
			tar->size = sizeof(float);
			tar->type = PORT_TYPE_CONTROL;
			tar->selected = 0;
			tar->monitored = 1;
		
			LilvNode *dflt_node;
			LilvNode *min_node;
			LilvNode *max_node;
			lilv_port_get_range(mod->plug, tar->tar, &dflt_node, &min_node, &max_node);
			tar->dflt = dflt_node ? lilv_node_as_float(dflt_node) : 0.f;
			tar->min = min_node ? lilv_node_as_float(min_node) : 0.f;
			tar->max = max_node ? lilv_node_as_float(max_node) : 1.f;
			lilv_node_free(dflt_node);
			lilv_node_free(min_node);
			lilv_node_free(max_node);
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.atom.node)) 
		{
			tar->size = app->driver->seq_size;
			tar->type = PORT_TYPE_ATOM;
			tar->selected = 0;
			tar->monitored = 0;
			tar->buffer_type = PORT_BUFFER_TYPE_SEQUENCE;
			//tar->buffer_type = lilv_port_is_a(plug, port, app->regs.port.sequence.node)
			//	? PORT_BUFFER_TYPE_SEQUENCE
			//	: PORT_BUFFER_TYPE_NONE; //TODO discriminate properly
				
			// does this port support patch:Message?
			tar->patchable = lilv_port_supports_event(plug, port, app->regs.patch.message.node);
			if(tar->patchable)
				tar->protocol = app->regs.port.event_transfer.urid;

			// check whether this is a control port
			const LilvPort *control_port = lilv_plugin_get_port_by_designation(plug,
				tar->direction == PORT_DIRECTION_INPUT
					? app->regs.port.input.node
					: app->regs.port.output.node
					, app->regs.core.control.node);

			tar->selected = control_port == port; // only select control ports by default
		}
		else
			fprintf(stderr, "unknown port type\n");
		
		// get minimum port size if specified
		LilvNode *minsize = lilv_port_get(plug, port, app->regs.port.minimum_size.node);
		if(minsize)
		{
			tar->size = lilv_node_as_int(minsize);
			lilv_node_free(minsize);
		}

		// increase pool sizes
		mod->pools[tar->type].size += lv2_atom_pad_size(tar->size);
	}

	// allocate 8-byte aligned buffer per plugin and port type pool
	int alloc_failed = 0;
	for(port_type_t pool=0; pool<PORT_TYPE_NUM; pool++)
	{
		if(_mod_alloc_pool(&mod->pools[pool]))
		{
			alloc_failed = 1;
			break;
		}
	}

	if(alloc_failed)
	{
		for(port_type_t pool=0; pool<PORT_TYPE_NUM; pool++)
			_mod_free_pool(&mod->pools[pool]);

		free(mod->uri_str);
		free(mod->ports);
		free(mod);

		return NULL;
	}

	// slice plugin buffer into per-port-type-and-direction regions for
	// efficient dereference in plugin instance
	for(port_type_t pool=0; pool<PORT_TYPE_NUM; pool++)
		_mod_slice_pool(mod, pool);

	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *tar = &mod->ports[i];

		// set port buffer
		lilv_instance_connect_port(mod->inst, i, tar->buf);
	}

	// load presets
	mod->presets = lilv_plugin_get_related(mod->plug, app->regs.pset.preset.node);
	
	// selection
	mod->selected = 0;

	lilv_instance_activate(mod->inst);

	return mod;
}

// non-rt worker-thread
static inline void
_sp_app_mod_del(sp_app_t *app, mod_t *mod)
{
	// deinit instance
	lilv_nodes_free(mod->presets);
	lilv_instance_deactivate(mod->inst);
	lilv_instance_free(mod->inst);

	// free memory
	for(port_type_t pool=0; pool<PORT_TYPE_NUM; pool++)
		_mod_free_pool(&mod->pools[pool]);

	// unregister system ports
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->sys.data && app->driver->system_port_del)
			app->driver->system_port_del(app->data, port->sys.data);
	}

	// free ports
	if(mod->ports)
		free(mod->ports);

	if(mod->uri_str)
		free(mod->uri_str);

	eina_semaphore_free(&mod->bypass_sem);

	free(mod);
}

static inline mod_t *
_sp_app_mod_get(sp_app_t *app, u_id_t uid)
{
	for(unsigned i=0; i<app->num_mods; i++)
	{
		mod_t *mod = app->mods[i];
		if(mod->uid == uid)
			return mod;
	}

	return NULL;
}

static inline port_t *
_sp_app_port_get(sp_app_t *app, u_id_t uid, uint32_t index)
{
	mod_t *mod = _sp_app_mod_get(app, uid);
	if(mod && (index < mod->num_ports) )
		return &mod->ports[index];
	
	return NULL;
}

static inline int
_sp_app_port_connect(sp_app_t *app, port_t *src_port, port_t *snk_port)
{
	if(snk_port->num_sources >= MAX_SOURCES)
		return 0;

	source_t *conn = &snk_port->sources[snk_port->num_sources];
	conn->port = src_port;;
	snk_port->num_sources += 1;
	snk_port->num_feedbacks += src_port->mod == snk_port->mod ? 1 : 0;

	if( (snk_port->num_sources + snk_port->num_feedbacks) == 1)
	{
		// directly wire source port output buffer to sink input buffer
		lilv_instance_connect_port(
			snk_port->mod->inst,
			snk_port->index,
			snk_port->sources[0].port->buf);
	}
	else
	{
		// multiplex multiple source port output buffers to sink input buffer
		lilv_instance_connect_port(
			snk_port->mod->inst,
			snk_port->index,
			snk_port->buf);
	}

	// only audio output ports need to be ramped to be clickless
	if(  (src_port->type == PORT_TYPE_AUDIO)
		&& (src_port->direction == PORT_DIRECTION_OUTPUT) )
	{
		conn->ramp.samples = app->ramp_samples;
		conn->ramp.state = RAMP_STATE_UP;
		conn->ramp.value = 0.f;
	}

	return 1;
}

static inline void
_sp_app_port_disconnect(sp_app_t *app, port_t *src_port, port_t *snk_port)
{
	// update sources list 
	int connected = 0;
	for(int i=0, j=0; i<snk_port->num_sources; i++)
	{
		if(snk_port->sources[i].port == src_port)
		{
			connected = 1;
			continue;
		}

		snk_port->sources[j++].port = snk_port->sources[i].port;
	}

	if(!connected)
		return;

	snk_port->num_sources -= 1;
	snk_port->num_feedbacks -= src_port->mod == snk_port->mod ? 1 : 0;

	if( (snk_port->num_sources + snk_port->num_feedbacks) == 1)
	{
		// directly wire source port output buffer to sink input buffer
		lilv_instance_connect_port(
			snk_port->mod->inst,
			snk_port->index,
			snk_port->sources[0].port->buf);
	}
	else
	{
		// multiplex multiple source port output buffers to sink input buffer
		lilv_instance_connect_port(
			snk_port->mod->inst,
			snk_port->index,
			snk_port->buf);
	}
}

static inline int
_sp_app_port_disconnect_request(sp_app_t *app, port_t *src_port, port_t *snk_port,
	ramp_state_t ramp_state)
{
	if(  (src_port->direction == PORT_DIRECTION_OUTPUT)
		&& (snk_port->direction == PORT_DIRECTION_INPUT) )
	{
		source_t *conn = NULL;
	
		// find connection
		for(int i=0; i<snk_port->num_sources; i++)
		{
			if(snk_port->sources[i].port == src_port)
			{
				conn = &snk_port->sources[i];
				break;
			}
		}

		if(conn)
		{
			if(src_port->type == PORT_TYPE_AUDIO)
			{
				// only audio output ports need to be ramped to be clickless
				conn->ramp.samples = app->ramp_samples;
				conn->ramp.state = ramp_state;
				conn->ramp.value = 1.f;

				return 1; // needs ramping
			}
			else // !AUDIO
			{
				// disconnect immediately
				_sp_app_port_disconnect(app, src_port, snk_port);
			}
		}
	}

	return 0; // not connected
}

// non-rt
sp_app_t *
sp_app_new(const LilvWorld *world, sp_app_driver_t *driver, void *data)
{
	efreet_init();
	ecore_file_init();

	if(!driver || !data)
		return NULL;

	sp_app_t *app = calloc(1, sizeof(sp_app_t));
	if(!app)
		return NULL;
	
	app->bypass_state = BYPASS_STATE_RUNNING;
	eina_semaphore_new(&app->bypass_sem, 0);

#if !defined(_WIN32)
	app->dir.home = getenv("HOME");
#else
	app->dir.home = evil_homedir_get();
#endif
	app->dir.config = efreet_config_home_get();
	app->dir.data = efreet_data_home_get();

	//printf("%s %s %s\n", app->dir.home, app->dir.config, app->dir.data);

	app->driver = driver;
	app->data = data;

	if(world)
	{
		app->world = (LilvWorld *)world;
		app->embedded = 1;
	}
	else
	{
		app->world = lilv_world_new();
		lilv_world_load_all(app->world);
		LilvNode *synthpod_bundle = lilv_new_uri(app->world, "file://"SYNTHPOD_BUNDLE_DIR"/");
		if(synthpod_bundle)
		{
			lilv_world_load_bundle(app->world, synthpod_bundle);
			lilv_node_free(synthpod_bundle);
		}
	}
	app->plugs = lilv_world_get_all_plugins(app->world);

	lv2_atom_forge_init(&app->forge, app->driver->map);
	sp_regs_init(&app->regs, app->world, app->driver->map);

	const char *uri_str;
	mod_t *mod;

	app->uid = 1;

	// inject source mod
	uri_str = SYNTHPOD_PREFIX"source";
	mod = _sp_app_mod_add(app, uri_str, 0);
	if(mod)
	{
		app->mods[app->num_mods] = mod;
		app->num_mods += 1;
	}
	else
		fprintf(stderr, "failed to create system source\n");

	// inject sink mod
	uri_str = SYNTHPOD_PREFIX"sink";
	mod = _sp_app_mod_add(app, uri_str, 0);
	if(mod)
	{
		app->mods[app->num_mods] = mod;
		app->num_mods += 1;
	}
	else
		fprintf(stderr, "failed to create system sink\n");

	app->fps.bound = driver->sample_rate / 24; //TODO make this configurable
	app->fps.counter = 0;

	app->ramp_samples = driver->sample_rate / 10; // ramp over 0.1s
	
	return app;
}

// rt
static void
_eject_module(sp_app_t *app, mod_t *mod)
{
	// eject module from graph
	app->num_mods -= 1;
	for(unsigned m=0, offset=0; m<app->num_mods; m++)
	{
		if(app->mods[m] == mod)
			offset += 1;
		app->mods[m] = app->mods[m+offset];
	}

	// disconnect all ports
	for(unsigned p1=0; p1<mod->num_ports; p1++)
	{
		port_t *port = &mod->ports[p1];

		// disconnect sources
		for(int s=0; s<port->num_sources; s++)
			_sp_app_port_disconnect(app, port->sources[s].port, port);

		// disconnect sinks
		for(unsigned m=0; m<app->num_mods; m++)
			for(unsigned p2=0; p2<app->mods[m]->num_ports; p2++)
				_sp_app_port_disconnect(app, port, &app->mods[m]->ports[p2]);
	}

	// send request to worker thread
	size_t size = sizeof(work_t) + sizeof(job_t);
	work_t *work = _sp_app_to_worker_request(app, size);
	if(work)
	{
		work->target = app;
		work->size = size - sizeof(work_t);
		job_t *job = (job_t *)work->payload;
		job->type = JOB_TYPE_MODULE_DEL;
		job->mod = mod;
		_sp_app_to_worker_advance(app, size);
	}

	// signal to ui
	size = sizeof(transmit_module_del_t);
	transmit_module_del_t *trans = _sp_app_to_ui_request(app, size);
	if(trans)
	{
		_sp_transmit_module_del_fill(&app->regs, &app->forge, trans, size, mod->uid);
		_sp_app_to_ui_advance(app, size);
	}
}

// rt
void
sp_app_from_ui(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);
	const transmit_t *transmit = (const transmit_t *)atom;

	// check for correct atom object type
	assert( (transmit->obj.atom.type == app->forge.Object) );

	LV2_URID protocol = transmit->obj.body.otype;

	if(protocol == app->regs.port.float_protocol.urid)
	{
		const transfer_float_t *trans = (const transfer_float_t *)atom;

		port_t *port = _sp_app_port_get(app, trans->transfer.uid.body, trans->transfer.port.body);
		if(!port) // port not found
			return;

		// set port value
		void *buf = PORT_SINK_ALIGNED(port);
		*(float *)buf = trans->value.body;
		port->last = trans->value.body;
	}
	else if(protocol == app->regs.port.atom_transfer.urid)
	{
		const transfer_atom_t *trans = (const transfer_atom_t *)atom;

		port_t *port = _sp_app_port_get(app, trans->transfer.uid.body, trans->transfer.port.body);
		if(!port) // port not found
			return;

		// set port value
		void *buf = PORT_SINK_ALIGNED(port);
		memcpy(buf, trans->atom, sizeof(LV2_Atom) + trans->atom->size);
	}
	else if(protocol == app->regs.port.event_transfer.urid)
	{
		const transfer_atom_t *trans = (const transfer_atom_t *)atom;

		port_t *port = _sp_app_port_get(app, trans->transfer.uid.body, trans->transfer.port.body);
		if(!port) // port not found
			return;

		// messages from UI are always appended to default port buffer, no matter
		// how many sources the port may have
		void *buf = PORT_BUF_ALIGNED(port);

		// find last event in sequence
		LV2_Atom_Sequence *seq = buf;
		LV2_Atom_Event *last = NULL;
		LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
			last = ev;

		// create forge to append to sequence
		LV2_Atom_Forge *forge = &app->forge;
		LV2_Atom_Forge_Frame frame;
		LV2_Atom_Forge_Ref ref;
		ref = _lv2_atom_forge_sequence_append(forge, &frame, buf, port->size);

		//inject atom at end of (existing) sequence
		if(ref && (forge->offset + sizeof(LV2_Atom_Sequence_Body)
			+ sizeof(LV2_Atom) + lv2_atom_pad_size(trans->atom->size) < forge->size) )
		{
			lv2_atom_forge_frame_time(forge, last ? last->time.frames : 0);
			lv2_atom_forge_raw(forge, trans->atom, sizeof(LV2_Atom) + trans->atom->size);
			lv2_atom_forge_pad(forge, trans->atom->size);
			lv2_atom_forge_pop(forge, &frame);
		}
	}
	else if(protocol == app->regs.synthpod.module_list.urid)
	{
		// iterate over existing modules and send module_add_t
		for(unsigned m=0; m<app->num_mods; m++)
		{
			mod_t *mod = app->mods[m];

			//signal to UI
			size_t size = sizeof(transmit_module_add_t)
				+ lv2_atom_pad_size(strlen(mod->uri_str) + 1);
			transmit_module_add_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				const LV2_Descriptor *descriptor = lilv_instance_get_descriptor(mod->inst);
				const data_access_t data_access = descriptor
					? descriptor->extension_data
					: NULL;
				_sp_transmit_module_add_fill(&app->regs, &app->forge, trans, size,
					mod->uid, mod->uri_str, mod->handle, data_access);
				_sp_app_to_ui_advance(app, size);
			}
		}
	}
	else if(protocol == app->regs.synthpod.module_add.urid)
	{
		const transmit_module_add_t *module_add = (const transmit_module_add_t *)atom;

		// send request to worker thread
		size_t size = sizeof(work_t) + sizeof(job_t) + module_add->uri.atom.size;
		work_t *work = _sp_app_to_worker_request(app, size);
		if(work)
		{
			work->target = app;
			work->size = size - sizeof(work_t);
			job_t *job = (job_t *)work->payload;
			job->type = JOB_TYPE_MODULE_ADD;
			memcpy(job->uri, module_add->uri_str, module_add->uri.atom.size);
			_sp_app_to_worker_advance(app, size);
		}
	}
	else if(protocol == app->regs.synthpod.module_del.urid)
	{
		const transmit_module_del_t *module_del = (const transmit_module_del_t *)atom;

		// search mod according to its UUID
		mod_t *mod = _sp_app_mod_get(app, module_del->uid.body);
		if(!mod) // mod not found
			return;

		int needs_ramping = 0;

		for(unsigned p1=0; p1<mod->num_ports; p1++)
		{
			port_t *port = &mod->ports[p1];

			// disconnect sources
			for(int s=0; s<port->num_sources; s++)
			{
				_sp_app_port_disconnect_request(app,
					port->sources[s].port, port, RAMP_STATE_DOWN);
			}

			// disconnect sinks
			for(unsigned m=0; m<app->num_mods; m++)
				for(unsigned p2=0; p2<app->mods[m]->num_ports; p2++)
				{
					 needs_ramping = needs_ramping || _sp_app_port_disconnect_request(app,
						port, &app->mods[m]->ports[p2], RAMP_STATE_DOWN_DEL);
				}
		}

		if(!needs_ramping)
			_eject_module(app, mod);
	}
	else if(protocol == app->regs.synthpod.module_move.urid)
	{
		const transmit_module_move_t *move = (const transmit_module_move_t *)atom;

		mod_t *mod = _sp_app_mod_get(app, move->uid.body);
		mod_t *prev = _sp_app_mod_get(app, move->prev.body);
		if(!mod || !prev)
			return;

		uint32_t mod_idx;
		for(mod_idx=0; mod_idx<app->num_mods; mod_idx++)
			if(app->mods[mod_idx] == mod)
				break;

		uint32_t prev_idx;
		for(prev_idx=0; prev_idx<app->num_mods; prev_idx++)
			if(app->mods[prev_idx] == prev)
				break;

		if(mod_idx < prev_idx)
		{
			// forward loop
			for(unsigned i=mod_idx, j=i; i<app->num_mods; i++)
			{
				if(app->mods[i] == mod)
					continue;

				app->mods[j++] = app->mods[i];

				if(app->mods[i] == prev)
					app->mods[j++] = mod;
			}
		}
		else // mod_idx > prev_idx
		{
			// reverse loop
			for(int i=app->num_mods-1, j=i; i>=0; i--)
			{
				if(app->mods[i] == mod)
					continue;
				else if(app->mods[i] == prev)
					app->mods[j--] = mod;

				app->mods[j--] = app->mods[i];
			}
		}

		//TODO signal to ui
	}
	else if(protocol == app->regs.synthpod.module_preset_load.urid)
	{
		const transmit_module_preset_load_t *pset = (const transmit_module_preset_load_t *)atom;

		mod_t *mod = _sp_app_mod_get(app, pset->uid.body);
		if(!mod)
			return;

		// send request to worker thread
		size_t size = sizeof(work_t) + sizeof(job_t) + pset->label.atom.size;
		work_t *work = _sp_app_to_worker_request(app, size);
		if(work)
		{
			work->target = app;
			work->size = size - sizeof(work_t);
			job_t *job = (job_t *)work->payload;
			job->type = JOB_TYPE_PRESET_LOAD;
			job->mod = mod;
			memcpy(job->uri, pset->label_str, pset->label.atom.size);
			_sp_app_to_worker_advance(app, size);
		}
	}
	else if(protocol == app->regs.synthpod.module_preset_save.urid)
	{
		const transmit_module_preset_save_t *pset = (const transmit_module_preset_save_t *)atom;

		mod_t *mod = _sp_app_mod_get(app, pset->uid.body);
		if(!mod)
			return;

		// send request to worker thread
		size_t size = sizeof(work_t) + sizeof(job_t) + pset->label.atom.size;
		work_t *work = _sp_app_to_worker_request(app, size);
		if(work)
		{
			work->target = app;
			work->size = size - sizeof(work_t);
			job_t *job = (job_t *)work->payload;
			job->type = JOB_TYPE_PRESET_SAVE;
			job->mod = mod;
			memcpy(job->uri, pset->label_str, pset->label.atom.size);
			_sp_app_to_worker_advance(app, size);
		}
	}
	else if(protocol == app->regs.synthpod.module_selected.urid)
	{
		const transmit_module_selected_t *select = (const transmit_module_selected_t *)atom;

		mod_t *mod = _sp_app_mod_get(app, select->uid.body);
		if(!mod)
			return;

		switch(select->state.body)
		{
			case -1: // query
			{
				// signal ui
				size_t size = sizeof(transmit_module_selected_t);
				transmit_module_selected_t *trans = _sp_app_to_ui_request(app, size);
				if(trans)
				{
					_sp_transmit_module_selected_fill(&app->regs, &app->forge, trans, size,
						mod->uid, mod->selected);
					_sp_app_to_ui_advance(app, size);
				}
				break;
			}
			case 0: // deselect
				mod->selected = 0;
				break;
			case 1: // select
				mod->selected = 1;
				break;
		}
	}
	else if(protocol == app->regs.synthpod.port_connected.urid)
	{
		const transmit_port_connected_t *conn = (const transmit_port_connected_t *)atom;

		port_t *src_port = _sp_app_port_get(app, conn->src_uid.body, conn->src_port.body);
		port_t *snk_port = _sp_app_port_get(app, conn->snk_uid.body, conn->snk_port.body);
		if(!src_port || !snk_port)
			return;

		int32_t state = 0;
		switch(conn->state.body)
		{
			case -1: // query
			{
				for(int s=0; s<snk_port->num_sources; s++)
				{
					if(snk_port->sources[s].port == src_port)
					{
						state = 1;
						break;
					}
				}
				break;
			}
			case 0: // disconnect
			{
				_sp_app_port_disconnect_request(app, src_port, snk_port, RAMP_STATE_DOWN);
				state = 0;
				break;
			}
			case 1: // connect
			{
				state = _sp_app_port_connect(app, src_port, snk_port);
				break;
			}
		}

		int32_t indirect = 0; // aka direct
		if(src_port->mod == snk_port->mod)
		{
			indirect = -1; // feedback
		}
		else
		{
			for(unsigned m=0; m<app->num_mods; m++)
			{
				if(app->mods[m] == src_port->mod)
				{
					indirect = 0;
					break;
				}
				else if(app->mods[m] == snk_port->mod)
				{
					indirect = 1;
					break;
				}
			}
		}

		// signal to ui
		size_t size = sizeof(transmit_port_connected_t);
		transmit_port_connected_t *trans = _sp_app_to_ui_request(app, size);
		if(trans)
		{
			_sp_transmit_port_connected_fill(&app->regs, &app->forge, trans, size,
				src_port->mod->uid, src_port->index,
				snk_port->mod->uid, snk_port->index, state, indirect);
			_sp_app_to_ui_advance(app, size);
		}
	}
	else if(protocol == app->regs.synthpod.port_subscribed.urid)
	{
		const transmit_port_subscribed_t *subscribe = (const transmit_port_subscribed_t *)atom;

		port_t *port = _sp_app_port_get(app, subscribe->uid.body, subscribe->port.body);
		if(!port)
			return;

		if(subscribe->state.body) // subscribe
		{
			port->protocol = subscribe->prot.body;
			port->subscriptions += 1;
		}
		else // unsubscribe
		{
			if(port->subscriptions > 0)
				port->subscriptions -= 1;
		}
	}
	else if(protocol == app->regs.synthpod.port_refresh.urid)
	{
		const transmit_port_refresh_t *refresh = (const transmit_port_refresh_t *)atom;

		port_t *port = _sp_app_port_get(app, refresh->uid.body, refresh->port.body);
		if(!port)
			return;
		
		float *buf_ptr = PORT_BUF_ALIGNED(port);
		port->last = *buf_ptr - 0.1; // will force notification
	}
	else if(protocol == app->regs.synthpod.port_selected.urid)
	{
		const transmit_port_selected_t *select = (const transmit_port_selected_t *)atom;

		port_t *port = _sp_app_port_get(app, select->uid.body, select->port.body);
		if(!port)
			return;

		switch(select->state.body)
		{
			case -1: // query
			{
				// signal ui
				size_t size = sizeof(transmit_port_selected_t);
				transmit_port_selected_t *trans = _sp_app_to_ui_request(app, size);
				if(trans)
				{
					_sp_transmit_port_selected_fill(&app->regs, &app->forge, trans, size,
						port->mod->uid, port->index, port->selected);
					_sp_app_to_ui_advance(app, size);
				}
				break;
			}
			case 0: // deselect
				port->selected = 0;
				break;
			case 1: // select
				port->selected = 1;
				break;
		}
	}
	else if(protocol == app->regs.synthpod.port_monitored.urid)
	{
		const transmit_port_monitored_t *monitor = (const transmit_port_monitored_t *)atom;

		port_t *port = _sp_app_port_get(app, monitor->uid.body, monitor->port.body);
		if(!port)
			return;

		switch(monitor->state.body)
		{
			case -1: // query
			{
				// signal ui
				size_t size = sizeof(transmit_port_monitored_t);
				transmit_port_monitored_t *trans = _sp_app_to_ui_request(app, size);
				if(trans)
				{
					_sp_transmit_port_monitored_fill(&app->regs, &app->forge, trans, size,
						port->mod->uid, port->index, port->monitored);
					_sp_app_to_ui_advance(app, size);
				}
				break;
			}
			case 0: // unmonitor
				port->monitored = 0;
				break;
			case 1: // monitor
				port->monitored = 1;
				break;
		}
	}
	else if(protocol == app->regs.synthpod.bundle_load.urid)
	{
		const transmit_bundle_load_t *load = (const transmit_bundle_load_t *)atom;
		if(!load->path.atom.size)
			return;

		// send request to worker thread
		size_t size = sizeof(work_t) + sizeof(job_t) + load->path.atom.size;
		work_t *work = _sp_app_to_worker_request(app, size);
		if(work)
		{
			work->target = app;
			work->size = size - sizeof(work_t);
			job_t *job = (job_t *)work->payload;
			job->type = JOB_TYPE_BUNDLE_LOAD;
			job->status = load->status.body;
			memcpy(job->uri, load->path_str, load->path.atom.size);
			_sp_app_to_worker_advance(app, size);
		}
	}
	else if(protocol == app->regs.synthpod.bundle_save.urid)
	{
		const transmit_bundle_save_t *save = (const transmit_bundle_save_t *)atom;
		if(!save->path.atom.size)
			return;
		
		// send request to worker thread
		size_t size = sizeof(work_t) + sizeof(job_t) + save->path.atom.size;
		work_t *work = _sp_app_to_worker_request(app, size);
		if(work)
		{
			work->target = app;
			work->size = size - sizeof(work_t);
			job_t *job = (job_t *)work->payload;
			job->type = JOB_TYPE_BUNDLE_SAVE;
			job->status = save->status.body;
			memcpy(job->uri, save->path_str, save->path.atom.size);
			_sp_app_to_worker_advance(app, size);
		}
	}
}

// rt
void
sp_app_from_worker(sp_app_t *app, uint32_t len, const void *data)
{
	const work_t *work = ASSUME_ALIGNED(data);

	if(work->target == app) // work is for self
	{
		const job_t *job = (const job_t *)work->payload;

		switch(job->type)
		{
			case JOB_TYPE_MODULE_ADD:
			{
				mod_t *mod = job->mod;

				if(app->num_mods >= MAX_MODS)
					break; //TODO delete mod

				// inject module into module graph
				app->mods[app->num_mods] = app->mods[app->num_mods-1]; // system sink
				app->mods[app->num_mods-1] = mod;
				app->num_mods += 1;

				//signal to UI
				size_t size = sizeof(transmit_module_add_t)
					+ lv2_atom_pad_size(strlen(mod->uri_str) + 1);
				transmit_module_add_t *trans = _sp_app_to_ui_request(app, size);
				if(trans)
				{
					const LV2_Descriptor *descriptor = lilv_instance_get_descriptor(mod->inst);
					const data_access_t data_access = descriptor
						? descriptor->extension_data
						: NULL;
					_sp_transmit_module_add_fill(&app->regs, &app->forge, trans, size,
						mod->uid, mod->uri_str, mod->handle, data_access);
					_sp_app_to_ui_advance(app, size);
				}

				break;
			}
			case JOB_TYPE_BUNDLE_LOAD:
			{
				//printf("app: bundle loaded\n");

				// signal to app
				size_t size = sizeof(transmit_bundle_load_t);
				transmit_bundle_load_t *trans = _sp_app_to_ui_request(app, size);
				if(trans)
				{
					_sp_transmit_bundle_load_fill(&app->regs, &app->forge, trans, size,
						job->status, NULL);
					_sp_app_to_ui_advance(app, size);
				}

				break;
			}
			case JOB_TYPE_BUNDLE_SAVE:
			{
				//printf("app: bundle saved\n");

				// signal to app
				size_t size = sizeof(transmit_bundle_save_t);
				transmit_bundle_save_t *trans = _sp_app_to_ui_request(app, size);
				if(trans)
				{
					_sp_transmit_bundle_save_fill(&app->regs, &app->forge, trans, size,
						job->status, NULL);
					_sp_app_to_ui_advance(app, size);
				}

				break;
			}
			default:
				break; // never reached
		}
	}
	else // work is for module
	{
		mod_t *mod = work->target;
		if(!mod)
			return;

		// zero worker takes precedence over standard worker
		if(mod->zero.iface && mod->zero.iface->response)
		{
			mod->zero.iface->response(mod->handle, work->size, work->payload);
			//TODO check return status
		}
		else if(mod->worker.iface && mod->worker.iface->work_response)
		{
			mod->worker.iface->work_response(mod->handle, work->size, work->payload);
			//TODO check return status
		}
	}
}

// non-rt worker-thread
static LV2_Worker_Status
_sp_worker_respond(LV2_Worker_Respond_Handle handle, uint32_t size, const void *data)
{
	mod_t *mod = handle;
	sp_app_t *app = mod->app;

	size_t work_size = sizeof(work_t) + size;
	work_t *work = _sp_worker_to_app_request(app, work_size);
	if(work)
	{
		work->target = mod;
		work->size = size;
		memcpy(work->payload, data, size);
		_sp_worker_to_app_advance(app, work_size);
		
		return LV2_WORKER_SUCCESS;
	}

	return LV2_WORKER_ERR_NO_SPACE;
}

// non-rt
static void *
_sp_zero_request(Zero_Worker_Handle handle, uint32_t size)
{
	mod_t *mod = handle;
	sp_app_t *app = mod->app;

	size_t work_size = sizeof(work_t) + size;
	work_t *work = _sp_worker_to_app_request(app, work_size);
	if(work)
	{
		work->target = mod;
		work->size = size; //TODO overwrite in _sp_zero_advance if size != written

		return work->payload;
	}

	return NULL;
}

// non-rt
static Zero_Worker_Status
_sp_zero_advance(Zero_Worker_Handle handle, uint32_t written)
{
	mod_t *mod = handle;
	sp_app_t *app = mod->app;

	size_t work_written = sizeof(work_t) + written;
	_sp_worker_to_app_advance(app, work_written);

	return ZERO_WORKER_SUCCESS;
}

// non-rt
static const LilvNode *
_mod_preset_get(mod_t *mod, const char *label)
{
	sp_app_t *app = mod->app;

	LILV_FOREACH(nodes, i, mod->presets)
	{
		const LilvNode* preset = lilv_nodes_get(mod->presets, i);
		const char *lab = _preset_label_get(app->world, &app->regs, preset);

		// test for matching preset label
		if(!lab || strcmp(lab, label))
			continue;

		return preset;
	}

	return NULL;
}

static char *
_abstract_path(LV2_State_Map_Path_Handle instance, const char *absolute_path)
{
	const char *prefix_path = instance;

	const char *offset = absolute_path + strlen(prefix_path) + 1; // + 'file://' '/'

	return strdup(offset);
}

static char *
_absolute_path(LV2_State_Map_Path_Handle instance, const char *abstract_path)
{
	const char *prefix_path = instance;
	
	char *absolute_path = NULL;
	asprintf(&absolute_path, "%s/%s", prefix_path, abstract_path);

	return absolute_path;
}

static char *
_make_path(LV2_State_Make_Path_Handle instance, const char *abstract_path)
{
	char *absolute_path = _absolute_path(instance, abstract_path);

	// create leading directory tree, e.g. up to last '/'
	if(absolute_path)
	{
		const char *end = strrchr(absolute_path, '/');
		if(end)
		{
			char *path = strndup(absolute_path, end - absolute_path);
			if(path)
			{
				ecore_file_mkpath(path);
				free(path);
			}
		}
	}

	return absolute_path;
}

static const LV2_Feature *const *
_state_features(sp_app_t *app, void *data)
{
	// construct LV2 state features
	app->make_path.handle = data;
	app->make_path.path = _make_path;

	app->map_path.handle = data;
	app->map_path.abstract_path = _abstract_path;
	app->map_path.absolute_path = _absolute_path;

	app->state_feature_list[0].URI = LV2_STATE__makePath;
	app->state_feature_list[0].data = &app->make_path;
	
	app->state_feature_list[1].URI = LV2_STATE__mapPath;
	app->state_feature_list[1].data = &app->map_path;

	app->state_features[0] = &app->state_feature_list[0];
	app->state_features[1] = &app->state_feature_list[1];
	app->state_features[2] = NULL;

	return (const LV2_Feature *const *)app->state_features;
}

// non-rt
static inline void
_preset_load(sp_app_t *app, mod_t *mod, const char *target)
{
	const LilvNode *preset = _mod_preset_get(mod, target);

	if(!preset) // preset not existing
		return;

	// load preset
	LilvState *state = lilv_state_new_from_world(app->world, app->driver->map,
		preset);
	if(!state)
		return;

	//enable bypass
	mod->bypass_state = BYPASS_STATE_PAUSE_REQUESTED; // atomic instruction
	eina_semaphore_lock(&mod->bypass_sem);

	lilv_state_restore(state, mod->inst, _state_set_value, mod,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, NULL);

	// disable bypass
	mod->bypass_state = BYPASS_STATE_RUNNING; // atomic instruction

	lilv_state_free(state);
}

// non-rt
static inline void
_preset_save(sp_app_t *app, mod_t *mod, const char *target)
{
	const LilvNode *preset = _mod_preset_get(mod, target);

	if(preset) // exists already TODO overwrite?
		return;

	const LilvNode *name_node = lilv_plugin_get_name(mod->plug);
	if(!name_node)
		return;

	const char *name = lilv_node_as_string(name_node);
	char *dir = NULL;
	char *filename = NULL;
	char *bndl = NULL;

	// create bundle path
	asprintf(&dir, "%s/.lv2/%s_%s.lv2", app->dir.home, name, target);
	if(!dir)
		return;

	// replace spaces with underscore
	for(char *c = strstr(dir, ".lv2"); *c; c++)
		if(isspace(*c))
			*c = '_';
	ecore_file_mkpath(dir); // create path if not existent already

	// create plugin state file name
	asprintf(&filename, "%s.ttl", target);
	if(!filename)
	{
		free(dir);
		return;
	}
	
	// create bundle path URI
	asprintf(&bndl, "%s/", dir);
	if(!bndl)
	{
		free(dir);
		free(filename);
		return;
	}
	
	//printf("preset save: %s, %s, %s\n", dir, filename, bndl);

	// enable bypass
	mod->bypass_state = BYPASS_STATE_LOCK_REQUESTED; // atomic instruction
	eina_semaphore_lock(&mod->bypass_sem);
	
	LilvState *const state = lilv_state_new_from_instance(mod->plug, mod->inst,
		app->driver->map, NULL, NULL, NULL, dir,
		_state_get_value, mod, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE,
		_state_features(app, dir));

	// disable bypass
	mod->bypass_state = BYPASS_STATE_RUNNING; // atomic instruction

	if(state)
	{
		// actually save the state to disk
		lilv_state_set_label(state, target);
		lilv_state_save(app->world, app->driver->map, app->driver->unmap,
			state, NULL, dir, filename);
		lilv_state_free(state);

		// reload presets for this module
		mod->presets = _preset_reload(app->world, &app->regs, mod->plug,
			mod->presets, bndl);

		// signal ui to reload its presets, too
		size_t size = sizeof(transmit_module_preset_save_t)
								+ lv2_atom_pad_size(strlen(bndl) + 1);
		transmit_module_preset_save_t *trans = _sp_app_to_ui_request(app, size);
		if(trans)
		{
			_sp_transmit_module_preset_save_fill(&app->regs, &app->forge, trans,
				size, mod->uid, bndl);
			_sp_app_to_ui_advance(app, size);
		}
	}
	
	// cleanup
	free(dir);
	free(filename);
	free(bndl);
}

// non-rt / rt
static LV2_State_Status
_state_store(LV2_State_Handle state, uint32_t key, const void *value,
	size_t size, uint32_t type, uint32_t flags)
{
	sp_app_t *app = state;
	
	if(key != app->regs.synthpod.json.urid)
		return LV2_STATE_ERR_NO_PROPERTY;

	if(type != app->forge.Path)
		return LV2_STATE_ERR_BAD_TYPE;
	
	if(!(flags & (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE)))
		return LV2_STATE_ERR_BAD_FLAGS;
	
	if(strcmp(value, "state.json"))
		return LV2_STATE_ERR_UNKNOWN;

	char *manifest_dst = _make_path(app->bundle_path, "manifest.ttl");
	char *state_dst = _make_path(app->bundle_path, "state.ttl");

	if(manifest_dst && state_dst)
	{
		if(ecore_file_cp(SYNTHPOD_DATA_DIR"/manifest.ttl", manifest_dst) == EINA_FALSE)
			fprintf(stderr, "_state_store: could not save manifest.ttl\n");
		if(ecore_file_cp(SYNTHPOD_DATA_DIR"/state.ttl", state_dst) == EINA_FALSE)
			fprintf(stderr, "_state_store: could not save state.ttl\n");

		free(manifest_dst);
		free(state_dst);

		return LV2_STATE_SUCCESS;
	}
	
	if(manifest_dst)
		free(manifest_dst);
	if(state_dst)
		free(state_dst);
		
	return LV2_STATE_ERR_UNKNOWN;
}

static const void*
_state_retrieve(LV2_State_Handle state, uint32_t key, size_t *size,
	uint32_t *type, uint32_t *flags)
{
	sp_app_t *app = state;

	if(key == app->regs.synthpod.json.urid)
	{
		*type = app->forge.Path;
		*flags = LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE;

		if(app->bundle_filename)
			free(app->bundle_filename);

		app->bundle_filename = _make_path(app->bundle_path, "state.json");

		*size = strlen(app->bundle_filename) + 1;
		return app->bundle_filename;
	}

	*size = 0;
	return NULL;
}

// non-rt
static inline int
_bundle_load(sp_app_t *app, const char *bundle_path)
{
	//printf("_bundle_load: %s\n", bundle_path);

	if(app->bundle_path)
		free(app->bundle_path);

	app->bundle_path = strdup(bundle_path);
	if(!app->bundle_path)
		return -1;

	// pause rt-thread
	app->bypass_state = BYPASS_STATE_PAUSE_REQUESTED; // atomic instruction
	eina_semaphore_lock(&app->bypass_sem);

	// restore state
	sp_app_restore(app, _state_retrieve, app,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, 
		_state_features(app, app->bundle_path));

	// resume rt-thread
	app->bypass_state = BYPASS_STATE_RUNNING; // atomic instruction

	return 0; // success
}

static inline int
_bundle_save(sp_app_t *app, const char *bundle_path)
{
	//printf("_bundle_save: %s\n", bundle_path);

	if(app->bundle_path)
		free(app->bundle_path);

	app->bundle_path = strdup(bundle_path);
	if(!app->bundle_path)
		return -1;

	// pause rt-thread
	app->bypass_state = BYPASS_STATE_LOCK_REQUESTED; // atomic instruction
	eina_semaphore_lock(&app->bypass_sem);

	// store state
	sp_app_save(app, _state_store, app,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE,
		_state_features(app, app->bundle_path));

	// resume rt-thread
	app->bypass_state = BYPASS_STATE_RUNNING; // atomic instruction

	return 0; // success
}

// non-rt
void
sp_worker_from_app(sp_app_t *app, uint32_t len, const void *data)
{
	const work_t *work0 = ASSUME_ALIGNED(data);

	if(work0->target == app) // work is for self
	{
		const job_t *job = (const job_t *)work0->payload;

		switch(job->type)
		{
			case JOB_TYPE_MODULE_ADD:
			{
				mod_t *mod = _sp_app_mod_add(app, job->uri, 0);
				if(!mod)
					break; //TODO report

				// signal to ui
				size_t work_size = sizeof(work_t) + sizeof(job_t);
				work_t *work = _sp_worker_to_app_request(app, work_size);
				if(work)
				{
						work->target = app;
						work->size = sizeof(job_t);
					job_t *job1 = (job_t *)work->payload;
						job1->type = JOB_TYPE_MODULE_ADD;
						job1->mod = mod;
					_sp_worker_to_app_advance(app, work_size);
				}

				break;
			}
			case JOB_TYPE_MODULE_DEL:
			{
				_sp_app_mod_del(app, job->mod);

				//TODO signal to ui?

				break;
			}
			case JOB_TYPE_PRESET_LOAD:
			{
				_preset_load(app, job->mod, job->uri);

				break;
			}
			case JOB_TYPE_PRESET_SAVE:
			{
				_preset_save(app, job->mod, job->uri);

				break;
			}
			case JOB_TYPE_BUNDLE_LOAD:
			{
				int status = _bundle_load(app, job->uri);

				// signal to ui
				size_t work_size = sizeof(work_t) + sizeof(job_t);
				work_t *work = _sp_worker_to_app_request(app, work_size);
				if(work)
				{
						work->target = app;
						work->size = sizeof(job_t);
					job_t *job1 = (job_t *)work->payload;
						job1->type = JOB_TYPE_BUNDLE_LOAD;
						job1->status = status;
					_sp_worker_to_app_advance(app, work_size);
				}

				break;
			}
			case JOB_TYPE_BUNDLE_SAVE:
			{
				int status = _bundle_save(app, job->uri);

				// signal to ui
				size_t work_size = sizeof(work_t) + sizeof(job_t);
				work_t *work = _sp_worker_to_app_request(app, work_size);
				if(work)
				{
						work->target = app;
						work->size = sizeof(job_t);
					job_t *job1 = (job_t *)work->payload;
						job1->type = JOB_TYPE_BUNDLE_SAVE;
						job1->status = status;
					_sp_worker_to_app_advance(app, work_size);
				}

				break;
			}
		}
	}
	else // work is for module
	{
		mod_t *mod = work0->target;
		if(!mod)
			return;

		// zero worker takes precedence over standard worker
		if(mod->zero.iface && mod->zero.iface->work)
		{
			mod->zero.iface->work(mod->handle, _sp_zero_request, _sp_zero_advance,
				mod, work0->size, work0->payload);
			//TODO check return status
		}
		else if(mod->worker.iface && mod->worker.iface->work)
		{
			mod->worker.iface->work(mod->handle, _sp_worker_respond, mod,
				work0->size, work0->payload);
			//TODO check return status
		}
	}
}

// rt
void
sp_app_run_pre(sp_app_t *app, uint32_t nsamples)
{
	mod_t *del_me = NULL;

	// iterate over all modules
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		if(mod->delete_request)
		{
			del_me = mod;
			mod->delete_request = 0;
		}

		// handle end of work
		if(mod->zero.iface && mod->zero.iface->end)
			mod->zero.iface->end(mod->handle);
		else if(mod->worker.iface && mod->worker.iface->end_run)
			mod->worker.iface->end_run(mod->handle);
	
		// clear atom sequence input buffers
		for(unsigned p=0; p<mod->num_ports; p++)
		{
			port_t *port = &mod->ports[p];

			if(port->direction == PORT_DIRECTION_INPUT) // ignore output ports
			{
				if(  (port->type == PORT_TYPE_ATOM)
					&& (port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) )
				{
					LV2_Atom_Sequence *seq = PORT_BUF_ALIGNED(port);
					seq->atom.type = app->regs.port.sequence.urid;
					seq->atom.size = sizeof(LV2_Atom_Sequence_Body); // empty sequence
				}
				else if( (port->num_sources + port->num_feedbacks) == 0) // clear audio/cv ports without connections
				{
					if(  (port->type == PORT_TYPE_AUDIO)
						|| (port->type == PORT_TYPE_CV) )
					{
						memset(PORT_BUF_ALIGNED(port), 0x0, port->size);
					}
				}
			}
		}
	}

	if(del_me)
		_eject_module(app, del_me);
}

// rt
static inline void
_update_ramp(sp_app_t *app, source_t *source, port_t *port, uint32_t nsamples)
{
	// update ramp properties
	source->ramp.samples -= nsamples; // update remaining samples to ramp over
	if(source->ramp.samples <= 0)
	{
		if(source->ramp.state == RAMP_STATE_DOWN)
			_sp_app_port_disconnect(app, source->port, port);
		else if(source->ramp.state == RAMP_STATE_DOWN_DEL)
		{
			_sp_app_port_disconnect(app, source->port, port);
			source->port->mod->delete_request = 1; // mark module for removal
		}
		source->ramp.state = RAMP_STATE_NONE; // ramp is complete
	}
	else
	{
		source->ramp.value = (float)source->ramp.samples / (float)app->ramp_samples;
		if(source->ramp.state == RAMP_STATE_UP)
			source->ramp.value = 1.f - source->ramp.value;
	}
}

// rt
void
sp_app_run_post(sp_app_t *app, uint32_t nsamples)
{
	int send_port_updates = 0;

	app->fps.counter += nsamples; // increase sample counter
	app->fps.period_cnt += 1; // increase period counter
	if(app->fps.counter >= app->fps.bound) // check whether we reached boundary
	{
		send_port_updates = 1;
		app->fps.counter -= app->fps.bound; // reset sample counter
	}

	// iterate over all modules
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		switch(mod->bypass_state)
		{
			case BYPASS_STATE_RUNNING:
				// do nothing
				break;

			case BYPASS_STATE_PAUSE_REQUESTED:
				mod->bypass_state = BYPASS_STATE_PAUSED;
				eina_semaphore_release(&mod->bypass_sem, 1);
				// fall-through
			case BYPASS_STATE_PAUSED: // aka state loading
				continue; // skip this module FIXME clear audio output buffers or xfade

			case BYPASS_STATE_LOCK_REQUESTED:
				mod->bypass_state = BYPASS_STATE_LOCKED;
				eina_semaphore_release(&mod->bypass_sem, 1);
				// fall-through
			case BYPASS_STATE_LOCKED: // aka state saving
				// do nothing, plugins MUST be able to save state while running
				break;
		}
	
		// multiplex multiple sources to single sink where needed
		for(unsigned p=0; p<mod->num_ports; p++)
		{
			port_t *port = &mod->ports[p];

			if(port->direction == PORT_DIRECTION_OUTPUT)
				continue; // not a sink

			if( (port->num_sources + port->num_feedbacks) > 1) // needs multiplexing
			{
				if(port->type == PORT_TYPE_CONTROL)
				{
					float *val = PORT_BUF_ALIGNED(port);
					*val = 0; // init
					for(int s=0; s<port->num_sources; s++)
					{
						float *src = PORT_BUF_ALIGNED(port->sources[s].port);
						*val += *src;
					}
				}
				else if( (port->type == PORT_TYPE_AUDIO)
							|| (port->type == PORT_TYPE_CV) )
				{
					float *val = PORT_BUF_ALIGNED(port);
					memset(val, 0, nsamples * sizeof(float)); // init
					for(int s=0; s<port->num_sources; s++)
					{
						source_t *source = &port->sources[s];

						// ramp audio output ports
						if(source->ramp.state != RAMP_STATE_NONE)
						{
							float *src = PORT_BUF_ALIGNED(source->port);
							for(uint32_t j=0; j<nsamples; j++)
								val[j] += src[j] * source->ramp.value;

							_update_ramp(app, source, port, nsamples);
						}
						else // RAMP_STATE_NONE
						{
							float *src = PORT_BUF_ALIGNED(source->port);
							for(uint32_t j=0; j<nsamples; j++)
								val[j] += src[j];
						}
					}
				}
				else if( (port->type == PORT_TYPE_ATOM)
							&& (port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) )
				{
					// create forge to append to sequence (may contain events from UI)
					LV2_Atom_Forge *forge = &app->forge;
					LV2_Atom_Forge_Frame frame;
					LV2_Atom_Forge_Ref ref;
					ref = _lv2_atom_forge_sequence_append(forge, &frame, port->buf, port->size);

					LV2_Atom_Sequence *seq [32]; //TODO how big?
					LV2_Atom_Event *itr [32]; //TODO how big?
					for(int s=0; s<port->num_sources; s++)
					{
						seq[s] = PORT_BUF_ALIGNED(port->sources[s].port);
						itr[s] = lv2_atom_sequence_begin(&seq[s]->body);
					}

					while(1)
					{
						int nxt = -1;
						int64_t frames = nsamples;

						// search for next event in timeline accross source ports
						for(int s=0; s<port->num_sources; s++)
						{
							if(lv2_atom_sequence_is_end(&seq[s]->body, seq[s]->atom.size, itr[s]))
								continue; // reached sequence end
							
							if(itr[s]->time.frames < frames)
							{
								frames = itr[s]->time.frames;
								nxt = s;
							}
						}

						if(nxt >= 0) // next event found
						{
							// add event to forge
							size_t len = sizeof(LV2_Atom) + itr[nxt]->body.size;
							if(ref && (forge->offset + sizeof(LV2_Atom_Sequence_Body)
								+ lv2_atom_pad_size(len) < forge->size) )
							{
								lv2_atom_forge_frame_time(forge, frames);
								lv2_atom_forge_raw(forge, &itr[nxt]->body, len);
								lv2_atom_forge_pad(forge, len);
							}

							// advance iterator
							itr[nxt] = lv2_atom_sequence_next(itr[nxt]);
						}
						else
							break; // no more events to process
					};

					if(ref)
						lv2_atom_forge_pop(forge, &frame);
				}
			}
			else if( (port->num_sources + port->num_feedbacks) == 1) // move messages from UI on default buffer
			{
				if(  (port->type == PORT_TYPE_ATOM)
					&& (port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) )
				{
					const LV2_Atom_Sequence *seq = PORT_BUF_ALIGNED(port);
					if(seq->atom.size <= sizeof(LV2_Atom_Sequence_Body)) // no messages from UI
						continue; // skip

					//printf("adding UI event\n");

					// create forge to append to sequence (may contain events from UI)
					LV2_Atom_Forge *forge = &app->forge;
					LV2_Atom_Forge_Frame frame;
					LV2_Atom_Forge_Ref ref;
					ref = _lv2_atom_forge_sequence_append(forge, &frame, port->sources[0].port->buf,
						port->sources[0].port->size);

					LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
					{
						const LV2_Atom *atom = &ev->body;

						if(ref && (forge->offset + sizeof(LV2_Atom_Sequence_Body)
							+ sizeof(LV2_Atom) + lv2_atom_pad_size(atom->size) < forge->size) )
						{
							lv2_atom_forge_frame_time(forge, nsamples-1);
							lv2_atom_forge_raw(forge, atom, sizeof(LV2_Atom) + atom->size);
							lv2_atom_forge_pad(forge, atom->size);
						}
					}

					if(ref)
						lv2_atom_forge_pop(forge, &frame);
				}
				else if(port->type == PORT_TYPE_AUDIO)
				{
					source_t *source = &port->sources[0];

					if(source->ramp.state != RAMP_STATE_NONE)
					{
						float *src = PORT_BUF_ALIGNED(source->port);
						for(uint32_t j=0; j<nsamples; j++)
							src[j] *= source->ramp.value;

						_update_ramp(app, source, port, nsamples);
					}
				}
			}
		}

		// clear atom sequence output buffers
		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];

			if(  (port->type == PORT_TYPE_ATOM)
				&& (port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE)
				&& (port->direction == PORT_DIRECTION_OUTPUT)
				&& (!mod->system_ports) ) // don't overwrite source buffer events
			{
				LV2_Atom_Sequence *seq = PORT_BUF_ALIGNED(port);
				seq->atom.type = app->regs.port.sequence.urid;
				seq->atom.size = port->size;
			}
		}

		// run plugin
		lilv_instance_run(mod->inst, nsamples);
		
		// handle app ui post
		if(app->dirty)
		{
			// XXX is safe to call, as sp_app_restore is not called during sp_app_run_post
			app->dirty = 0; // reset

			size_t size = sizeof(transmit_module_list_t);
			transmit_module_list_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transmit_module_list_fill(&app->regs, &app->forge, trans, size);
				_sp_app_to_ui_advance(app, size);
			}
		}

		// handle mod ui post
		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];

			// no notification/subscription and no support for patch:Message
			int subscribed = port->subscriptions != 0;
			int patchable = port->patchable && (port->direction == PORT_DIRECTION_OUTPUT);
			if(!(subscribed || patchable))
				continue; // skip this port

			const void *buf = PORT_SINK_ALIGNED(port);
			assert(buf != NULL);

			/*
			if(patchable)
				printf("patchable %i %i %i\n", mod->uid, i, subscribed);
			*/

			if(port->protocol == app->regs.port.float_protocol.urid)
			{
				if(send_port_updates)
				{
					const float val = *(const float *)buf;

					if(val != port->last)
					{
						// update last value
						port->last = val;

						size_t size = sizeof(transfer_float_t);
						transfer_float_t *trans = _sp_app_to_ui_request(app, size);
						if(trans)
						{
							_sp_transfer_float_fill(&app->regs, &app->forge, trans, port->mod->uid, port->index, &val);
							_sp_app_to_ui_advance(app, size);
						}
					}
				}
			}
			else if(port->protocol == app->regs.port.peak_protocol.urid)
			{
				if(send_port_updates)
				{
					const float *vec = (const float *)buf;

					// find peak value in current period
					float peak = 0.f;
					for(uint32_t j=0; j<nsamples; j++)
					{
						float val = fabs(vec[j]);
						if(val > peak)
							peak = val;
					}

					if(fabs(peak - port->last) >= 1e-3) //TODO make this configurable
					{
						// update last value
						port->last = peak;

						LV2UI_Peak_Data data = {
							.period_start = app->fps.period_cnt,
							.period_size = nsamples,
							.peak = peak
						};

						size_t size = sizeof(transfer_peak_t);
						transfer_peak_t *trans = _sp_app_to_ui_request(app, size);
						if(trans)
						{
							_sp_transfer_peak_fill(&app->regs, &app->forge, trans,
								port->mod->uid, port->index, &data);
							_sp_app_to_ui_advance(app, size);
						}
					}
				}
			}
			else if(port->protocol == app->regs.port.atom_transfer.urid)
			{
				const LV2_Atom *atom = buf;
				if(atom->size == 0) // empty atom
					continue;
				else if( (port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) && (atom->size == sizeof(LV2_Atom_Sequence_Body)) ) // empty atom sequence
					continue;

				uint32_t atom_size = sizeof(LV2_Atom) + atom->size;
				size_t size = sizeof(transfer_atom_t) + lv2_atom_pad_size(atom_size);
				transfer_atom_t *trans = _sp_app_to_ui_request(app, size);
				if(trans)
				{
					_sp_transfer_atom_fill(&app->regs, &app->forge, trans,
						port->mod->uid, port->index, atom_size, atom);
					_sp_app_to_ui_advance(app, size);
				}
			}
			else if(port->protocol == app->regs.port.event_transfer.urid)
			{
				const LV2_Atom_Sequence *seq = buf;
				if(seq->atom.size == sizeof(LV2_Atom_Sequence_Body)) // empty seq
					continue;

				// transfer each atom of sequence separately
				LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
				{
					const LV2_Atom *atom = &ev->body;

					if(!subscribed) // patched
					{
						const LV2_Atom_Object *obj = (const LV2_Atom_Object *)atom;

						if(  (obj->atom.type != app->forge.Object)
							|| ( (obj->body.otype != app->regs.patch.set.urid)
								&& (obj->body.otype != app->regs.patch.patch.urid) ) ) //FIXME handle more
						{
							continue; // skip this event
						}

						//printf("routing response\n");
					}

					uint32_t atom_size = sizeof(LV2_Atom) + atom->size;
					size_t size = sizeof(transfer_atom_t) + lv2_atom_pad_size(atom_size);
					transfer_atom_t *trans = _sp_app_to_ui_request(app, size);
					if(trans)
					{
						_sp_transfer_event_fill(&app->regs, &app->forge, trans,
							port->mod->uid, port->index, atom_size, atom);
						_sp_app_to_ui_advance(app, size);
					}
				}
			}
		}
	}
}

// non-rt
void
sp_app_deactivate(sp_app_t *app)
{
	//TODO
}

// non-rt
void
sp_app_free(sp_app_t *app)
{
	if(!app)
		return;

	// free mods
	for(unsigned m=0; m<app->num_mods; m++)
		_sp_app_mod_del(app, app->mods[m]);

	sp_regs_deinit(&app->regs);

	if(!app->embedded)
		lilv_world_free(app->world);

	if(app->bundle_path)
		free(app->bundle_path);
	if(app->bundle_filename)
		free(app->bundle_filename);

	eina_semaphore_free(&app->bypass_sem);

	free(app);

	ecore_file_shutdown();
	efreet_shutdown();
}

// non-rt / rt
static void
_state_set_value(const char *symbol, void *data,
	const void *value, uint32_t size, uint32_t type)
{
	mod_t *mod = data;
	sp_app_t *app = mod->app;

	LilvNode *symbol_uri = lilv_new_string(app->world, symbol);
	if(!symbol_uri)
		return;

	const LilvPort *port = lilv_plugin_get_port_by_symbol(mod->plug, symbol_uri);
	lilv_node_free(symbol_uri);
	if(!port)
		return;

	uint32_t index = lilv_port_get_index(mod->plug, port);
	port_t *tar = &mod->ports[index];

	if(tar->type == PORT_TYPE_CONTROL)
	{
		float val = 0.f;

		if( (type == app->forge.Int) && (size == sizeof(int32_t)) )
			val = *(const int32_t *)value;
		else if( (type == app->forge.Long) && (size == sizeof(int64_t)) )
			val = *(const int64_t *)value;
		else if( (type == app->forge.Float) && (size == sizeof(float)) )
			val = *(const float *)value;
		else if( (type == app->forge.Double) && (size == sizeof(double)) )
			val = *(const double *)value;
		else if( (type == app->forge.Bool) && (size == sizeof(int32_t)) )
			val = *(const int32_t *)value;
		else
			return; //TODO warning

		//printf("%u %f\n", index, val);
		float *buf_ptr = PORT_BUF_ALIGNED(tar);
		*buf_ptr = val;
		tar->last = val - 0.1; // triggers notification
	}
}

// non-rt / rt
static const void *
_state_get_value(const char *symbol, void *data, uint32_t *size, uint32_t *type)
{
	mod_t *mod = data;
	sp_app_t *app = mod->app;
	
	LilvNode *symbol_uri = lilv_new_string(app->world, symbol);
	if(!symbol_uri)
		goto fail;

	const LilvPort *port = lilv_plugin_get_port_by_symbol(mod->plug, symbol_uri);
	lilv_node_free(symbol_uri);
	if(!port)
		goto fail;

	uint32_t index = lilv_port_get_index(mod->plug, port);
	port_t *tar = &mod->ports[index];

	if(  (tar->direction == PORT_DIRECTION_INPUT)
		&& (tar->type == PORT_TYPE_CONTROL)
		&& (tar->num_sources == 0) )
	{
		const float *val = PORT_BUF_ALIGNED(tar);

		if(tar->toggled)
		{
			*size = sizeof(int32_t);
			*type = app->forge.Bool;
			tar->i32 = floor(*val);
			return &tar->i32;
		}
		else if(tar->integer)
		{
			*size = sizeof(int32_t);
			*type = app->forge.Int;
			tar->i32 = floor(*val);
			return &tar->i32;
		}
		else // float
		{
			*size = sizeof(float);
			*type = app->forge.Float;
			return val;
		}
	}

fail:
	*size = 0;
	*type = 0;
	return NULL;
}

// non-rt
LV2_State_Status
sp_app_save(sp_app_t *app, LV2_State_Store_Function store,
	LV2_State_Handle hndl, uint32_t flags, const LV2_Feature *const *features)
{
	const LV2_State_Make_Path *make_path = NULL;
	const LV2_State_Map_Path *map_path = NULL;

	for(int i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_STATE__makePath))
			make_path = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_STATE__mapPath))
			map_path = features[i]->data;

	if(!make_path)
	{
		fprintf(stderr, "sp_app_save: LV2_STATE__makePath not supported.");
		return LV2_STATE_ERR_UNKNOWN;
	}
	if(!map_path)
	{
		fprintf(stderr, "sp_app_save: LV2_STATE__mapPath not supported.");
		return LV2_STATE_ERR_UNKNOWN;
	}

	// cleanup state module trees
	for(int uid=0; uid<app->uid; uid++)
	{
		int exists = 0;

		for(unsigned m=0; m<app->num_mods; m++)
		{
			mod_t *mod = app->mods[m];

			if(mod->uid == uid)
			{
				exists = 1;
				break;
			}
		}

		if(!exists)
		{
			char uid_str [64];
			sprintf(uid_str, "%u", uid);

			char *root_path = map_path->absolute_path(map_path->handle, uid_str);
			if(root_path)
			{
				ecore_file_recursive_rm(root_path); // remove whole bundle tree
				free(root_path);
			}
		}
		else
		{
			char uid_str [64];
			sprintf(uid_str, "%u/manifest.ttl", uid);

			char *manifest_path = map_path->absolute_path(map_path->handle, uid_str);
			if(manifest_path)
			{
				ecore_file_remove(manifest_path); // remove manifest.ttl
				free(manifest_path);
			}
		}
	}

	// create json object
	cJSON *root_json = cJSON_CreateObject();
	cJSON *arr_json = cJSON_CreateArray();
	cJSON_AddItemToObject(root_json, "items", arr_json);
	
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		char uid [64];
		sprintf(uid, "%u/", mod->uid);
		char *path = make_path->path(make_path->handle, uid);
		if(!path)
			continue;

		LilvState *const state = lilv_state_new_from_instance(mod->plug, mod->inst,
			app->driver->map, NULL, NULL, NULL, path,
			_state_get_value, mod, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, features);

		lilv_state_set_label(state, "state"); //TODO use path prefix?
		lilv_state_save(app->world, app->driver->map, app->driver->unmap,
			state, NULL, path, "state.ttl");

		free(path);
		
		// fill mod json object
		cJSON *mod_json = cJSON_CreateObject();
		cJSON *mod_uid_json = cJSON_CreateNumber(mod->uid);
		cJSON *mod_uri_json = cJSON_CreateString(mod->uri_str);
		cJSON *mod_selected_json = cJSON_CreateBool(mod->selected);
		cJSON *mod_ports_json = cJSON_CreateArray();

		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];

			cJSON *port_json = cJSON_CreateObject();
			cJSON *port_symbol_json = cJSON_CreateString(
				lilv_node_as_string(lilv_port_get_symbol(mod->plug, port->tar)));
			cJSON *port_selected_json = cJSON_CreateBool(port->selected);
			cJSON *port_monitored_json = cJSON_CreateBool(port->monitored);
			cJSON *port_sources_json = cJSON_CreateArray();
				for(int j=0; j<port->num_sources; j++)
				{
					port_t *source = port->sources[j].port;

					cJSON *conn_json = cJSON_CreateObject();

					cJSON *source_uid_json = cJSON_CreateNumber(source->mod->uid);
					cJSON *source_symbol_json = cJSON_CreateString(
						lilv_node_as_string(lilv_port_get_symbol(source->mod->plug, source->tar)));
					cJSON_AddItemToObject(conn_json, "uid", source_uid_json);
					cJSON_AddItemToObject(conn_json, "symbol", source_symbol_json);

					cJSON_AddItemToArray(port_sources_json, conn_json);
				}
			cJSON_AddItemToObject(port_json, "symbol", port_symbol_json);
			cJSON_AddItemToObject(port_json, "selected", port_selected_json);
			cJSON_AddItemToObject(port_json, "monitored", port_monitored_json);
			cJSON_AddItemToObject(port_json, "sources", port_sources_json);
			cJSON_AddItemToArray(mod_ports_json, port_json);
		}
		cJSON_AddItemToObject(mod_json, "uid", mod_uid_json);
		cJSON_AddItemToObject(mod_json, "uri", mod_uri_json);
		cJSON_AddItemToObject(mod_json, "selected", mod_selected_json);
		cJSON_AddItemToObject(mod_json, "ports", mod_ports_json);
		cJSON_AddItemToArray(arr_json, mod_json);

		lilv_state_free(state);
	}

	// serialize json object
	char *root_str = cJSON_Print(root_json);	
	cJSON_Delete(root_json);
	
	if(!root_str)
		return LV2_STATE_ERR_UNKNOWN;

	char *absolute = make_path->path(make_path->handle, "state.json");
	if(!absolute)
		return LV2_STATE_ERR_UNKNOWN;

	char *abstract = map_path->abstract_path(map_path->handle, absolute);
	if(!abstract)
		return LV2_STATE_ERR_UNKNOWN;

	FILE *f = fopen(absolute, "wb");
	if(f)
	{
		size_t written = fwrite(root_str, strlen(root_str), 1, f);
		fflush(f);
		fclose(f);

		if(written != 1)
			fprintf(stderr, "error writing to JSON file\n");
	}
	else
		fprintf(stderr, "error opening JSON file\n");

	free(root_str);

	//printf("abstract: %s (%zu)\n", abstract, strlen(abstract));
	store(hndl, app->regs.synthpod.json.urid,
		abstract, strlen(abstract) + 1, app->forge.Path,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

	free(abstract);
	free(absolute);

	return LV2_STATE_SUCCESS;
}

// non-rt
LV2_State_Status
sp_app_restore(sp_app_t *app, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle hndl, uint32_t flags, const LV2_Feature *const *features)
{
	const LV2_State_Map_Path *map_path = NULL;

	for(int i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_STATE__mapPath))
			map_path = features[i]->data;

	if(!map_path)
	{
		fprintf(stderr, "sp_app_restore: LV2_STATE__mapPath not supported.");
		return LV2_STATE_ERR_UNKNOWN;
	}

	size_t size;
	uint32_t _flags;
	uint32_t type;
	
	const char *absolute = retrieve(hndl, app->regs.synthpod.json.urid,
		&size, &type, &_flags);

	if(!absolute)
		return LV2_STATE_ERR_UNKNOWN;

	if(type != app->forge.Path)
		return LV2_STATE_ERR_BAD_TYPE;

	if(!(_flags & (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE)))
		return LV2_STATE_ERR_BAD_FLAGS;

	//printf("absolute: %s\n", absolute);

	if(!ecore_file_exists(absolute)) // new project?
	{
		// XXX is safe to call, as sp_app_restore is not called during sp_app_run_post
		app->dirty = 1;

		return LV2_STATE_SUCCESS;
	}

	char *root_str = NULL;
	FILE *f = fopen(absolute, "rb");
	if(f)
	{
		fseek(f, 0, SEEK_END);
		const size_t fsize = ftell(f);
		fseek(f, 0, SEEK_SET);
				
		root_str = malloc(fsize + 1);
		if(root_str)
		{
			if(fread(root_str, fsize, 1, f) == 1)
			{
				root_str[fsize] = '\0';
				size = fsize + 1;
			}
			else // read failed
			{
				free(root_str);
				root_str = NULL;
			}
		}
		fclose(f);
	}

	if(!root_str)
		return LV2_STATE_ERR_UNKNOWN;

	cJSON *root_json = cJSON_Parse(root_str);
	free(root_str);

	if(!root_json)
		return LV2_STATE_ERR_UNKNOWN;

	// remove existing modules
	int num_mods = app->num_mods;

	app->num_mods = 0;

	for(int m=0; m<num_mods; m++)
		_sp_app_mod_del(app, app->mods[m]);

	// iterate over mods, create and apply states
	for(cJSON *mod_json = cJSON_GetObjectItem(root_json, "items")->child;
		mod_json;
		mod_json = mod_json->next)
	{
		const cJSON *mod_uri_json = cJSON_GetObjectItem(mod_json, "uri");
		const cJSON *mod_uid_json = cJSON_GetObjectItem(mod_json, "uid");
		if(!mod_uri_json || !mod_uid_json)
			continue;

		const char *mod_uri_str = mod_uri_json->valuestring;
		u_id_t mod_uid = mod_uid_json->valueint;

		mod_t *mod = _sp_app_mod_add(app, mod_uri_str, mod_uid);
		if(!mod)
			continue;

		// inject module into module graph
		app->mods[app->num_mods] = mod;
		app->num_mods += 1;

		const cJSON *mod_selected_json = cJSON_GetObjectItem(mod_json, "selected");
		mod->selected = mod_selected_json
			? mod_selected_json->type == cJSON_True
			: 0;

		if(mod->uid > app->uid - 1)
			app->uid = mod->uid + 1;

		char uid [64];
		sprintf(uid, "%u/state.ttl", mod_uid);
		char *path = map_path->absolute_path(map_path->handle, uid);
		if(!path)
			continue;

		// strip 'file://'
		const char *tmp = !strncmp(path, "file://", 7)
			? path + 7
			: path;

		LilvState *state = lilv_state_new_from_file(app->world,
			app->driver->map, NULL, tmp);

		if(state)
		{
			lilv_state_restore(state, mod->inst, _state_set_value, mod,
				LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, features);
		}
		else
			fprintf(stderr, "failed to load state from file\n");

		lilv_state_free(state);
		free(path);
	}

	// iterate over mods and their ports
	for(cJSON *mod_json = cJSON_GetObjectItem(root_json, "items")->child;
		mod_json;
		mod_json = mod_json->next)
	{
		const cJSON *mod_uid_json = cJSON_GetObjectItem(mod_json, "uid");
		if(!mod_uid_json)
			continue;

		u_id_t mod_uid = mod_uid_json->valueint;

		mod_t *mod = _sp_app_mod_get(app, mod_uid);
		if(!mod)
			continue;

		// iterate over ports
		for(cJSON *port_json = cJSON_GetObjectItem(mod_json, "ports")->child;
			port_json;
			port_json = port_json->next)
		{
			const cJSON *port_symbol_json = cJSON_GetObjectItem(port_json, "symbol");
			if(!port_symbol_json)
				continue;

			const char *port_symbol_str = port_symbol_json->valuestring;

			for(unsigned i=0; i<mod->num_ports; i++)
			{
				port_t *port = &mod->ports[i];
				const LilvNode *port_symbol_node = lilv_port_get_symbol(mod->plug, port->tar);
				if(!port_symbol_node)
					continue;

				// search for matching port symbol
				if(strcmp(port_symbol_str, lilv_node_as_string(port_symbol_node)))
					continue;

				const cJSON *port_selected_json = cJSON_GetObjectItem(port_json, "selected");
				port->selected = port_selected_json
					? port_selected_json->type == cJSON_True
					: 0;
				const cJSON *port_monitored_json = cJSON_GetObjectItem(port_json, "monitored");
				port->monitored = port_monitored_json
					? port_monitored_json->type == cJSON_True
					: 1;

				for(cJSON *source_json = cJSON_GetObjectItem(port_json, "sources")->child;
					source_json;
					source_json = source_json->next)
				{
					const cJSON *source_uid_json = cJSON_GetObjectItem(source_json, "uid");
					const cJSON *source_symbol_json = cJSON_GetObjectItem(source_json, "symbol");
					if(!source_uid_json || !source_symbol_json)
						continue;

					uint32_t source_uid = source_uid_json->valueint;
					const char *source_symbol_str = source_symbol_json->valuestring;

					mod_t *source = _sp_app_mod_get(app, source_uid);
					if(!source)
						continue;
				
					for(unsigned j=0; j<source->num_ports; j++)
					{
						port_t *tar = &source->ports[j];
						const LilvNode *source_symbol_node = lilv_port_get_symbol(source->plug, tar->tar);

						if(strcmp(source_symbol_str, lilv_node_as_string(source_symbol_node)))
							continue;

						_sp_app_port_connect(app, tar, port);

						break;
					}
				}

				break;
			}
		}
	}
	cJSON_Delete(root_json);

	// XXX is safe to call, as sp_app_restore is not called during sp_app_run_post
	app->dirty = 1;

	return LV2_STATE_SUCCESS;
}

int
sp_app_paused(sp_app_t *app)
{
	switch(app->bypass_state)
	{
		case BYPASS_STATE_RUNNING:
			return 0;

		case BYPASS_STATE_PAUSE_REQUESTED:
			app->bypass_state = BYPASS_STATE_PAUSED;
			eina_semaphore_release(&app->bypass_sem, 1);
			// fall-through
		case BYPASS_STATE_PAUSED: // aka loading state
			return 1;

		case BYPASS_STATE_LOCK_REQUESTED:
			app->bypass_state = BYPASS_STATE_LOCKED;
			eina_semaphore_release(&app->bypass_sem, 1);
			// fall-through
		case BYPASS_STATE_LOCKED: // aka saving state
			return 2;
	}
}

uint32_t
sp_app_options_set(sp_app_t *app, const LV2_Options_Option *options)
{
	LV2_Options_Status status = LV2_OPTIONS_SUCCESS;

	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		if(mod->opts.iface && mod->opts.iface->set)
			status |= mod->opts.iface->set(mod->handle, options);
	}
	
	return status;
}

// non-rt
static void
_sp_app_reinitialize(sp_app_t *app)
{
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];
	
		// reinitialize all modules,
		lilv_instance_deactivate(mod->inst);
		lilv_instance_free(mod->inst);
		mod->inst = NULL;
		mod->handle = NULL;

		// mod->features should be up-to-date
		mod->inst = lilv_plugin_instantiate(mod->plug, app->driver->sample_rate, mod->features);
		mod->handle = lilv_instance_get_handle(mod->inst),

		//TODO should we re-get extension_data?

		// resize sample based buffers only (e.g. AUDIO and CV)
		_mod_free_pool(&mod->pools[PORT_TYPE_AUDIO]);
		_mod_free_pool(&mod->pools[PORT_TYPE_CV]);
			
		mod->pools[PORT_TYPE_AUDIO].size = 0;
		mod->pools[PORT_TYPE_CV].size = 0;
	
		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *tar = &mod->ports[i];

			if(  (tar->type == PORT_TYPE_AUDIO)
				|| (tar->type == PORT_TYPE_CV) )
			{
				tar->size = app->driver->max_block_size * sizeof(float);
				mod->pools[tar->type].size += lv2_atom_pad_size(tar->size);
			}
		}
		
		_mod_alloc_pool(&mod->pools[PORT_TYPE_AUDIO]);
		_mod_alloc_pool(&mod->pools[PORT_TYPE_CV]);

		_mod_slice_pool(mod, PORT_TYPE_AUDIO);
		_mod_slice_pool(mod, PORT_TYPE_CV);
	}

	// refresh all connections
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *tar = &mod->ports[i];

			// set port buffer
			lilv_instance_connect_port(mod->inst, i,
				tar->direction == PORT_DIRECTION_INPUT
					? PORT_SINK_ALIGNED(tar)
					: PORT_BUF_ALIGNED(tar));
		}
	
		lilv_instance_activate(mod->inst);
	}
}

// non-rt
int
sp_app_nominal_block_length(sp_app_t *app, uint32_t nsamples)
{
	if(nsamples <= app->driver->max_block_size)
	{
		for(unsigned m=0; m<app->num_mods; m++)
		{
			mod_t *mod = app->mods[m];

			if(mod->opts.iface && mod->opts.iface->set)
			{
				if(nsamples < app->driver->min_block_size)
				{
					// update driver struct
					app->driver->min_block_size = nsamples;

					const LV2_Options_Option options [2] = {{
						.context = LV2_OPTIONS_INSTANCE,
						.subject = 0, // is ignored
						.key = app->regs.bufsz.min_block_length.urid,
						.size = sizeof(int32_t),
						.type = app->forge.Int,
						.value = &app->driver->min_block_size
					}, {
						.key = 0, // sentinel
						.value =NULL // sentinel 
					}};

					// notify new minimalBlockLength
					if(mod->opts.iface->set(mod->handle, options) != LV2_OPTIONS_SUCCESS)
						fprintf(stderr, "option setting of min_block_size failed\n");
				}

				const int32_t nominal_block_length = nsamples;

				const LV2_Options_Option options [2] = {{
					.context = LV2_OPTIONS_INSTANCE,
					.subject = 0, // is ignored
					.key = app->regs.bufsz.nominal_block_length.urid,
					.size = sizeof(int32_t),
					.type = app->forge.Int,
					.value = &nominal_block_length
				}, {
					.key = 0, // sentinel
					.value =NULL // sentinel 
				}};

				// notify new nominalBlockLength
				if(mod->opts.iface->set(mod->handle, options) != LV2_OPTIONS_SUCCESS)
					fprintf(stderr, "option setting of min_block_size failed\n");
			}
		}
	}
	else // nsamples > max_block_size
	{
		// update driver struct
		app->driver->max_block_size = nsamples;

		_sp_app_reinitialize(app);
	}

	return 0;
}

// rt
int
sp_app_com_event(sp_app_t *app, LV2_URID id)
{
	return id == app->regs.synthpod.com_event.urid ? 1 : 0;
}

int
sp_app_transfer_event(sp_app_t *app, LV2_URID id)
{
	return id == app->regs.synthpod.transfer_event.urid ? 1 : 0;
}
