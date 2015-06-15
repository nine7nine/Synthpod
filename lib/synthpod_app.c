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

#define NUM_FEATURES 8
#define MAX_SOURCES 32 // TODO how many?
#define MAX_MODS 512 // TODO how many?

typedef enum _job_type_t job_type_t;
typedef enum _bypass_state_t bypass_state_t;

typedef struct _mod_t mod_t;
typedef struct _port_t port_t;
typedef struct _work_t work_t;
typedef struct _job_t job_t;

enum _bypass_state_t {
	BYPASS_STATE_PAUSED	= 0,
	BYPASS_STATE_RUNNING,
	BYPASS_STATE_PAUSE_REQUESTED
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

struct _mod_t {
	sp_app_t *app;
	u_id_t uid;
	int selected;

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

	// log
	LV2_Log_Log log;

	// opts
	struct {
		LV2_Options_Option options [4];
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

	// ports
	uint32_t num_ports;
	port_t *ports;
};

struct _port_t {
	mod_t *mod;
	int selected;
	
	const LilvPort *tar;
	uint32_t index;

	int num_sources;
	port_t *sources [MAX_SOURCES];

	size_t size;
	void *buf;

	port_direction_t direction; // input, output
	port_type_t type; // audio, CV, control, atom
	port_buffer_type_t buffer_type; // none, sequence
	int patchable; // support patch:Message

	LV2_URID protocol; // floatProtocol, peakProtocol, atomTransfer, eventTransfer
	int subscriptions; // subsriptions reference counter

	float last;
	uint32_t period_cnt;

	float min;
	float dflt;
	float max;
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

	uint32_t num_mods;
	mod_t *mods [MAX_MODS];

	struct {
		mod_t *source;
		mod_t *sink;
	} system;

	u_id_t uid;
	
	LV2_State_Make_Path make_path;
	LV2_State_Map_Path map_path;
	LV2_Feature state_feature_list [2];
	LV2_Feature *state_features [3];

	char *bundle_path;
	char *bundle_filename;
};

static void
_state_set_value(const char *symbol, void *data,
	const void *value, uint32_t size, uint32_t type);

static const void *
_state_get_value(const char *symbol, void *data, uint32_t *size, uint32_t *type);

// rt
static inline void *
_sp_app_to_ui_request(sp_app_t *app, size_t size)
{
	if(app->driver->to_ui_request)
		return app->driver->to_ui_request(size, app->data);
	else
		return NULL;
}
static inline void
_sp_app_to_ui_advance(sp_app_t *app, size_t size)
{
	if(app->driver->to_ui_advance)
		app->driver->to_ui_advance(size, app->data);
}

// rt
static inline void *
_sp_app_to_worker_request(sp_app_t *app, size_t size)
{
	if(app->driver->to_worker_request)
		return app->driver->to_worker_request(size, app->data);
	else
		return NULL;
}
static inline void
_sp_app_to_worker_advance(sp_app_t *app, size_t size)
{
	if(app->driver->to_worker_advance)
		app->driver->to_worker_advance(size, app->data);
}

// non-rt worker-thread
static inline void *
_sp_worker_to_app_request(sp_app_t *app, size_t size)
{
	if(app->driver->to_app_request)
		return app->driver->to_app_request(size, app->data);
	else
		return NULL;
}
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
int
sp_app_set_system_source(sp_app_t *app, uint32_t index, const void *buf)
{
	if(!app->system.source)
		return -1;

	// get first mod aka system source
	mod_t *mod = app->system.source;
	port_t *port = &mod->ports[index];

	port->num_sources = 1;
	lilv_instance_connect_port(mod->inst, index, (void *)buf);

	return 0;
}

// rt
int
sp_app_set_system_sink(sp_app_t *app, uint32_t index, void *buf)
{
	if(!app->system.sink)
		return -1;

	// get last mod aka system sink
	mod_t *mod = app->system.sink;

	index += 7; //TODO make configurable
	lilv_instance_connect_port(mod->inst, index, (void *)buf);

	return 0;
}

// rt
void *
sp_app_get_system_source(sp_app_t *app, uint32_t index)
{
	if(!app->system.source)
		return NULL;

	// get last mod aka system source
	mod_t *mod = app->system.source;
	port_t *port = &mod->ports[index];

	port->num_sources = 1;
	//lilv_instance_connect_port(mod->inst, index, mod->ports[index].buf);
	return port->buf;
}

// rt
const void *
sp_app_get_system_sink(sp_app_t *app, uint32_t index)
{
	if(!app->system.sink)
		return NULL;

	// get last mod aka system sink
	mod_t *mod = app->system.sink;
	port_t *port = &mod->ports[index];

	index += 7; //TODO make configurable
	//lilv_instance_connect_port(mod->inst, index, mod->ports[index].buf);
	return port->buf;
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
_zero_sched_request(Zero_Worker_Handle handle, int64_t frames, uint32_t size)
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

// non-rt worker-thread
static inline mod_t *
_sp_app_mod_add(sp_app_t *app, const char *uri)
{
	LilvNode *uri_node = lilv_new_uri(app->world, uri);
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
	
	mod->opts.options[3].key = 0; // sentinel
	mod->opts.options[3].value = NULL; // sentinel

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
	
	mod->feature_list[nfeatures].URI = LV2_BUF_SIZE__boundedBlockLength;
	mod->feature_list[nfeatures++].data = NULL;

	/* TODO support
	mod->feature_list[nfeatures].URI = LV2_BUF_SIZE__fixedBlockLength;
	mod->feature_list[nfeatures++].data = NULL;
	
	mod->feature_list[nfeatures].URI = LV2_BUF_SIZE__powerOf2BlockLength;
	mod->feature_list[nfeatures++].data = NULL;
	*/

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

			for(int i=0; i<nfeatures; i++)
			{
				if(!strcmp(mod->feature_list[i].URI, required_feature_uri))
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
	mod->uid = app->uid++;
	mod->plug = plug;
	mod->num_ports = lilv_plugin_get_num_ports(plug);
	mod->inst = lilv_plugin_instantiate(plug, app->driver->sample_rate, mod->features);
	mod->handle = lilv_instance_get_handle(mod->inst),
	mod->worker.iface = lilv_instance_get_extension_data(mod->inst,
		LV2_WORKER__interface);
	mod->zero.iface = lilv_instance_get_extension_data(mod->inst,
		ZERO_WORKER__interface);
	mod->opts.iface = lilv_instance_get_extension_data(mod->inst,
		LV2_OPTIONS__interface); //TODO actually use this for something?
	lilv_instance_activate(mod->inst);

	int system_source = 0;
	int system_sink = 0;
	if(!strcmp(uri, SYNTHPOD_PREFIX"source"))
		system_source = 1;
	else if(!strcmp(uri, SYNTHPOD_PREFIX"sink"))
		system_sink = 1;

	mod->ports = calloc(mod->num_ports, sizeof(port_t));
	for(uint32_t i=0; i<mod->num_ports; i++)
	{
		port_t *tar = &mod->ports[i];
		const LilvPort *port = lilv_plugin_get_port_by_index(plug, i);

		tar->size = 0;
		tar->mod = mod;
		tar->tar = port;
		tar->index = i;
		tar->direction = lilv_port_is_a(plug, port, app->regs.port.input.node)
			? PORT_DIRECTION_INPUT
			: PORT_DIRECTION_OUTPUT;

		if(lilv_port_is_a(plug, port, app->regs.port.audio.node))
		{
			tar->size = app->driver->max_block_size * sizeof(float);
			tar->type =  PORT_TYPE_AUDIO;
			tar->selected = 1;
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.cv.node))
		{
			tar->size = app->driver->max_block_size * sizeof(float);
			tar->type = PORT_TYPE_CV;
			tar->selected = 1;
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.control.node))
		{
			tar->size = sizeof(float);
			tar->type = PORT_TYPE_CONTROL;
			tar->selected = 0;
		
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
			tar->buffer_type = PORT_BUFFER_TYPE_SEQUENCE;
			//tar->buffer_type = lilv_port_is_a(plug, port, app->regs.port.sequence.node)
			//	? PORT_BUFFER_TYPE_SEQUENCE
			//	: PORT_BUFFER_TYPE_NONE; //TODO discriminate properly
				
			// does this port support patch:Message?
			tar->patchable = lilv_port_supports_event(plug, port, app->regs.patch.message.node);
			if(tar->patchable)
				tar->protocol = app->regs.port.event_transfer.urid;

			// check whether this is a control port
			LilvNode *control_designation = lilv_new_uri(app->world, LV2_CORE__control);
			const LilvPort *control_port = lilv_plugin_get_port_by_designation(plug,
				tar->direction == PORT_DIRECTION_INPUT
					? app->regs.port.input.node
					: app->regs.port.output.node
					, control_designation);

			tar->selected = control_port == port; // only select control ports by default
			lilv_node_free(control_designation);
		}
		else
			; //TODO
		
		if(system_source && (tar->direction == PORT_DIRECTION_INPUT) )
			tar->selected = 0;
		if(system_sink && (tar->direction == PORT_DIRECTION_OUTPUT) )
			tar->selected = 0;

		// get minimum port size if specified
		LilvNode *minsize = lilv_port_get(plug, port, app->regs.port.minimum_size.node);
		if(minsize)
			tar->size = lilv_node_as_int(minsize);
		lilv_node_free(minsize);

		// allocate 8-byte aligned buffer
#if defined(_WIN32)
		tar->buf = _aligned_malloc(tar->size, 8); //FIXME check
#else
		posix_memalign(&tar->buf, 8, tar->size); //FIXME check
		mlock(tar->buf, tar->size);
#endif
		//TODO mlock
		memset(tar->buf, 0x0, tar->size);

		// initialize control buffers to default value
		if(tar->type == PORT_TYPE_CONTROL)
			*(float *)tar->buf = tar->dflt;

		// set port buffer
		lilv_instance_connect_port(mod->inst, i, tar->buf);
	}

	// load presets
	mod->presets = lilv_plugin_get_related(mod->plug, app->regs.pset.preset.node);
	
	// selection
	mod->selected = 0;

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

	// deinit ports
	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

#if !defined(_WIN32)
		munlock(port->buf, port->size);
#endif
		free(port->buf);
	}
	free(mod->ports);

	eina_semaphore_free(&mod->bypass_sem);

	free(mod);
}

static inline mod_t *
_sp_app_mod_get(sp_app_t *app, u_id_t uid)
{
	for(int i=0; i<app->num_mods; i++)
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

	snk_port->sources[snk_port->num_sources] = src_port;;
	snk_port->num_sources += 1;

	if(snk_port->num_sources == 1)
	{
		// directly wire source port output buffer to sink input buffer
		lilv_instance_connect_port(
			snk_port->mod->inst,
			snk_port->index,
			snk_port->sources[0]->buf);
	}
	else
	{
		// multiplex multiple source port output buffers to sink input buffer
		lilv_instance_connect_port(
			snk_port->mod->inst,
			snk_port->index,
			snk_port->buf);
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
		if(snk_port->sources[i] == src_port)
		{
			connected = 1;
			continue;
		}

		snk_port->sources[j++] = snk_port->sources[i];
	}

	if(!connected)
		return;

	snk_port->num_sources -= 1;

	if(snk_port->num_sources == 1)
	{
		// directly wire source port output buffer to sink input buffer
		lilv_instance_connect_port(
			snk_port->mod->inst,
			snk_port->index,
			snk_port->sources[0]->buf);
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
	}
	app->plugs = lilv_world_get_all_plugins(app->world);

	lv2_atom_forge_init(&app->forge, app->driver->map);
	sp_regs_init(&app->regs, app->world, app->driver->map);

	const char *uri_str;
	size_t size;
	mod_t *mod;

	app->uid = 1;

	// inject source mod
	uri_str = SYNTHPOD_PREFIX"source";
	mod = _sp_app_mod_add(app, uri_str);
	if(mod)
	{
		app->system.source = mod;
		app->mods[app->num_mods] = mod;
		app->num_mods += 1;
	}
	else
		; //TODO report

	// inject sink mod
	uri_str = SYNTHPOD_PREFIX"sink";
	mod = _sp_app_mod_add(app, uri_str);
	if(mod)
	{
		app->system.sink = mod;
		app->mods[app->num_mods] = mod;
		app->num_mods += 1;
	}
	else
		; //TODO report
	
	return app;
}

// rt
void
sp_app_from_ui(sp_app_t *app, const LV2_Atom *atom)
{
	const transmit_t *transmit = (const transmit_t *)atom;

	// check for correct atom object type
	assert( (transmit->obj.atom.type == app->forge.Object)
		&& (transmit->obj.body.id == app->regs.synthpod.event.urid) );

	LV2_URID protocol = transmit->obj.body.otype;

	if(protocol == app->regs.port.float_protocol.urid)
	{
		const transfer_float_t *trans = (const transfer_float_t *)atom;

		port_t *port = _sp_app_port_get(app, trans->transfer.uid.body, trans->transfer.port.body);
		if(!port) // port not found
			return;

		// set port value
		void *buf = port->num_sources == 1
			? port->sources[0]->buf // direct link to source output buffer
			: port->buf; // empty (n==0) or multiplexed (n>1) link
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
		void *buf = port->num_sources == 1
			? port->sources[0]->buf // direct link to source output buffer
			: port->buf; // empty (n==0) or multiplexed (n>1) link
		memcpy(buf, trans->atom, sizeof(LV2_Atom) + trans->atom->size);
	}
	else if(protocol == app->regs.port.event_transfer.urid)
	{
		const transfer_atom_t *trans = (const transfer_atom_t *)atom;

		port_t *port = _sp_app_port_get(app, trans->transfer.uid.body, trans->transfer.port.body);
		if(!port) // port not found
			return;

		void *buf = port->num_sources == 1
			? port->sources[0]->buf // direct link to source output buffer
			: port->buf; // empty (n==0) or multiplexed (n>1) link

		//inject atom at end of (existing) sequence
		LV2_Atom_Sequence *seq = buf;

		// find last event in sequence
		LV2_Atom_Event *last = NULL;
		LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
			last = ev;

		void *ptr;
		if(last)
		{
			ptr = last;
			ptr += sizeof(LV2_Atom_Event) + lv2_atom_pad_size(last->body.size);
		}
		else
			ptr = LV2_ATOM_CONTENTS(LV2_Atom_Sequence, seq);
					
		// append event at end of sequence
		// TODO check for buffer overflow
		LV2_Atom_Event *new_last = ptr;
		new_last->time.frames = last ? last->time.frames : 0;
		memcpy(&new_last->body, trans->atom, sizeof(LV2_Atom) + trans->atom->size);
		seq->atom.size += sizeof(LV2_Atom_Event) + lv2_atom_pad_size(trans->atom->size);
	}
	else if(protocol == app->regs.synthpod.module_list.urid)
	{
		// iterate over existing modules and send module_add_t
		for(int m=0; m<app->num_mods; m++)
		{
			mod_t *mod = app->mods[m];

			const LilvNode *uri_node = lilv_plugin_get_uri(mod->plug);
			const char *uri_str = lilv_node_as_string(uri_node);
			
			//signal to UI
			size_t size = sizeof(transmit_module_add_t)
				+ lv2_atom_pad_size(strlen(uri_str) + 1);
			transmit_module_add_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				const LV2_Descriptor *descriptor = lilv_instance_get_descriptor(mod->inst);
				data_access_t data_access = descriptor
					? descriptor->extension_data
					: NULL;
				_sp_transmit_module_add_fill(&app->regs, &app->forge, trans, size,
					mod->uid, uri_str, mod->handle, data_access);
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

		// eject module from graph
		app->num_mods -= 1;
		for(int m=0, offset=0; m<app->num_mods; m++)
		{
			if(app->mods[m] == mod)
				offset += 1;
			app->mods[m] = app->mods[m+offset];
		}

		// disconnect all ports
		for(int p1=0; p1<mod->num_ports; p1++)
		{
			port_t *port = &mod->ports[p1];

			// disconnect sources
			for(int s=0; s<port->num_sources; s++)
				_sp_app_port_disconnect(app, port->sources[s], port);

			// disconnect sinks
			for(int m=0; m<app->num_mods; m++)
				for(int p2=0; p2<app->mods[m]->num_ports; p2++)
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
	else if(protocol == app->regs.synthpod.module_move.urid)
	{
		const transmit_module_move_t *move = (const transmit_module_move_t *)atom;

		mod_t *mod = _sp_app_mod_get(app, move->uid.body);
		mod_t *prev = _sp_app_mod_get(app, move->prev.body);
		if(!mod || !prev)
			return;

		int mod_idx;
		for(mod_idx=0; mod_idx<app->num_mods; mod_idx++)
			if(app->mods[mod_idx] == mod)
				break;

		int prev_idx;
		for(prev_idx=0; prev_idx<app->num_mods; prev_idx++)
			if(app->mods[prev_idx] == prev)
				break;

		if(mod_idx < prev_idx)
		{
			// forward loop
			for(int i=mod_idx, j=i; i<app->num_mods; i++)
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
					if(snk_port->sources[s] == src_port)
					{
						state = 1;
						break;
					}
				}
				break;
			}
			case 0: // disconnect
			{
				_sp_app_port_disconnect(app, src_port, snk_port);
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
			indirect = 1;
		}
		else
		{
			for(int m=0; m<app->num_mods; m++)
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

		port->last = *(float *)port->buf - 0.1; // will force notification
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
	const work_t *work = data;

	if(work->target == app) // work is for self
	{
		const job_t *job = (const job_t *)work->payload;

		switch(job->type)
		{
			case JOB_TYPE_MODULE_ADD:
			{
				if(app->num_mods >= MAX_MODS)
					break; //TODO delete mod

				// inject module into module graph
				app->mods[app->num_mods] = app->mods[app->num_mods-1]; // system sink
				app->mods[app->num_mods-1] = job->mod;
				app->num_mods += 1;
			
				const LilvNode *uri_node = lilv_plugin_get_uri(job->mod->plug);
				const char *uri_str = lilv_node_as_string(uri_node);
				
				//signal to UI
				size_t size = sizeof(transmit_module_add_t)
					+ lv2_atom_pad_size(strlen(uri_str) + 1);
				transmit_module_add_t *trans = _sp_app_to_ui_request(app, size);
				if(trans)
				{
					const LV2_Descriptor *descriptor = lilv_instance_get_descriptor(job->mod->inst);
					data_access_t data_access = descriptor
						? descriptor->extension_data
						: NULL;
					_sp_transmit_module_add_fill(&app->regs, &app->forge, trans, size,
						job->mod->uid, uri_str, job->mod->handle, data_access);
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
			int64_t frames = 0; //TODO
			mod->zero.iface->response(mod->handle, frames, work->size, work->payload);
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
_sp_zero_request(Zero_Worker_Handle handle, int64_t frames, uint32_t size)
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

const LilvNode *
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
	if(absolute_path)
		ecore_file_mkpath(absolute_path);

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
	mod->bypass_state = BYPASS_STATE_PAUSE_REQUESTED; // atomic instruction
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

	char *manifest_dst = NULL;
	char *state_dst = NULL;

	asprintf(&manifest_dst, "%s/manifest.ttl", app->bundle_path);
	asprintf(&state_dst, "%s/state.ttl", app->bundle_path);

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

		asprintf(&app->bundle_filename, "%s/state.json", app->bundle_path);

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
	app->bypass_state = BYPASS_STATE_PAUSE_REQUESTED; // atomic instruction
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
	const work_t *work = data;

	if(work->target == app) // work is for self
	{
		const job_t *job = (const job_t *)work->payload;

		switch(job->type)
		{
			case JOB_TYPE_MODULE_ADD:
			{
				mod_t *mod = _sp_app_mod_add(app, job->uri);
				if(!mod)
					break; //TODO report

				// signal to ui
				size_t work_size = sizeof(work_t) + sizeof(job_t);
				work_t *work = _sp_worker_to_app_request(app, work_size);
				if(work)
				{
						work->target = app;
						work->size = sizeof(job_t);
					job_t *job = (job_t *)work->payload;
						job->type = JOB_TYPE_MODULE_ADD;
						job->mod = mod;
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
					job_t *job = (job_t *)work->payload;
						job->type = JOB_TYPE_BUNDLE_LOAD;
						job->status = status;
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
					job_t *job = (job_t *)work->payload;
						job->type = JOB_TYPE_BUNDLE_SAVE;
						job->status = status;
					_sp_worker_to_app_advance(app, work_size);
				}

				break;
			}
		}
	}
	else // work is for module
	{
		mod_t *mod = work->target;
		if(!mod)
			return;

		// zero worker takes precedence over standard worker
		if(mod->zero.iface && mod->zero.iface->work)
		{
			int64_t frames = 0; //TODO
			mod->zero.iface->work(mod->handle, _sp_zero_request, _sp_zero_advance,
				mod, frames, work->size, work->payload);
			//TODO check return status
		}
		else if(mod->worker.iface && mod->worker.iface->work)
		{
			mod->worker.iface->work(mod->handle, _sp_worker_respond, mod,
				work->size, work->payload);
			//TODO check return status
		}
	}
}

// rt
void
sp_app_run_pre(sp_app_t *app, uint32_t nsamples)
{
	// iterate over all modules
	for(int m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		// handle end of work
		if(mod->zero.iface && mod->zero.iface->end)
			mod->zero.iface->end(mod->handle);
		else if(mod->worker.iface && mod->worker.iface->end_run)
			mod->worker.iface->end_run(mod->handle);
	
		// clear atom sequence input / output buffers where needed
		for(int i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];

			if(  (port->type == PORT_TYPE_ATOM)
				&& (port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) )
			{
				if(port->num_sources == 1)
					continue; // atom already cleared/filled by source (direct link)

				LV2_Atom_Sequence *seq = port->buf;
				seq->atom.type = app->regs.port.sequence.urid;
				seq->atom.size = port->direction == PORT_DIRECTION_INPUT
					? sizeof(LV2_Atom_Sequence_Body) // empty sequence
					: app->driver->seq_size; // capacity
			}
		}
	}
}

// rt
void
sp_app_run_post(sp_app_t *app, uint32_t nsamples)
{
	// iterate over all modules
	for(int m=0; m<app->num_mods; m++)
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
			case BYPASS_STATE_PAUSED:
				continue; // skip this module
		}
	
		// multiplex multiple sources to single sink where needed
		for(int i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];

			if(port->direction == PORT_DIRECTION_OUTPUT)
				continue; // not a sink

			if(port->num_sources > 1) // needs multiplexing
			{
				if(port->type == PORT_TYPE_CONTROL)
				{
					float *val = port->buf;
					*val = 0; // init
					for(int i=0; i<port->num_sources; i++)
					{
						float *src = port->sources[i]->buf;
						*val += *src;
					}
				}
				else if( (port->type == PORT_TYPE_AUDIO)
							|| (port->type == PORT_TYPE_CV) )
				{
					float *val = port->buf;
					memset(val, 0, nsamples * sizeof(float)); // init
					for(int i=0; i<port->num_sources; i++)
					{
						float *src = port->sources[i]->buf;
						for(int j=0; j<nsamples; j++)
						{
							val[j] += src[j];
						}
					}
				}
				else if( (port->type == PORT_TYPE_ATOM)
							&& (port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) )
				{
					//FIXME append to UI messages 
					LV2_Atom_Forge *forge = &app->forge;
					lv2_atom_forge_set_buffer(forge, port->buf, app->driver->seq_size);
					LV2_Atom_Forge_Frame frame;
					lv2_atom_forge_sequence_head(forge, &frame, 0);

					LV2_Atom_Sequence *seq [32]; //TODO how big?
					LV2_Atom_Event *itr [32]; //TODO how big?
					for(int i=0; i<port->num_sources; i++)
					{
						seq[i] = port->sources[i]->buf;
						itr[i] = lv2_atom_sequence_begin(&seq[i]->body);
					}

					while(1)
					{
						int nxt = -1;
						int64_t frames = nsamples;

						// search for next event in timeline accross source ports
						for(i=0; i<port->num_sources; i++)
						{
							if(lv2_atom_sequence_is_end(&seq[i]->body, seq[i]->atom.size, itr[i]))
								continue; // reached sequence end
							
							if(itr[i]->time.frames < frames)
							{
								frames = itr[i]->time.frames;
								nxt = i;
							}
						}

						if(nxt >= 0) // next event found
						{
							// add event to forge
							size_t len = sizeof(LV2_Atom) + itr[nxt]->body.size;
							lv2_atom_forge_frame_time(forge, frames);
							lv2_atom_forge_raw(forge, &itr[nxt]->body, len);
							lv2_atom_forge_pad(forge, len);

							// advance iterator
							itr[nxt] = lv2_atom_sequence_next(itr[nxt]);
						}
						else
							break; // no more events to process
					};
					
					lv2_atom_forge_pop(forge, &frame);
				}
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
		for(int i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];

			// no notification/subscription and no support for patch:Message
			int subscribed = port->subscriptions != 0;
			int patchable = port->patchable && (port->direction == PORT_DIRECTION_OUTPUT);
			if(!(subscribed || patchable))
				continue; // skip this port

			//FIXME
			if( (mod == app->system.source) && (port->direction == PORT_DIRECTION_INPUT) )
				continue;
			if( (mod == app->system.sink) && (port->direction == PORT_DIRECTION_OUTPUT) )
				continue;
			//FIXME

			const void *buf = port->num_sources == 1
				? port->sources[0]->buf // direct link to source buffer
				: port->buf; // dummy (n==0) or multiplexed (n>1) link

			assert(buf != NULL);

			/*
			if(patchable)
				printf("patchable %i %i %i\n", mod->uid, i, subscribed);
			*/

			if(port->protocol == app->regs.port.float_protocol.urid)
			{
				const float val = *(const float *)buf;
				if(val != port->last) // has value changed since last time?
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
			else if(port->protocol == app->regs.port.peak_protocol.urid)
			{
				const float *vec = (const float *)buf;

				// find peak value in current period
				float peak = 0.f;
				for(int j=0; j<nsamples; j++)
				{
					float val = fabs(vec[j]);
					if(val > peak)
						peak = val;
				}

				port->period_cnt += 1; // increase period counter
				//printf("%u %f\n", port->period_cnt, peak);

				if(  (peak != port->last) //TODO make below two configurable
					&& ((port->period_cnt & 0x3f) == 0x00) ) // only update every 512th period
				{
					//printf("peak different: %i %i\n", port->last == 0.f, peak == 0.f);

					// update last value
					port->last = peak;

					LV2UI_Peak_Data data = {
						.period_start = port->period_cnt,
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
							|| (obj->body.otype != app->regs.patch.set.urid) ) //FIXME handle more
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
	for(int m=0; m<app->num_mods; m++)
		_sp_app_mod_del(app, app->mods[m]);
	
	sp_regs_deinit(&app->regs);

	if(!app->embedded)
		lilv_world_free(app->world);

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

		if(type == app->forge.Int)
			val = *(const int32_t *)value;
		else if(type == app->forge.Long)
			val = *(const int64_t *)value;
		else if(type == app->forge.Float)
			val = *(const float *)value;
		else if(type == app->forge.Double)
			val = *(const double *)value;
		else
			return; //TODO warning

		//printf("%u %f\n", index, val);
		*(float *)tar->buf = val;
		tar->last = val - 0.1; // triggers notification
	}
	// TODO handle non-seq atom ports, e.g. string, path, ...
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

	if(tar->direction == PORT_DIRECTION_INPUT)
	{
		if(tar->type == PORT_TYPE_CONTROL)
		{
			*size = sizeof(float);
			*type = app->forge.Float;
			return tar->buf;
		}
		// TODO handle non-seq atom ports, e.g. string, path, ...
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

	// create json object
	cJSON *root_json = cJSON_CreateObject();
	cJSON *arr_json = cJSON_CreateArray();
	cJSON_AddItemToObject(root_json, "items", arr_json);
	
	for(int m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		char uid [32];
		sprintf(uid, "%u/", mod->uid);
		char *path = make_path->path(make_path->handle, uid);
		if(!path)
			continue;

		//printf("made path: %s\n", path);

		LilvState *const state = lilv_state_new_from_instance(mod->plug, mod->inst,
			app->driver->map, NULL, NULL, NULL, path,
			_state_get_value, mod, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, features);

		lilv_state_set_label(state, "state"); //TODO use path prefix?
		lilv_state_save(app->world, app->driver->map, app->driver->unmap,
			state, NULL, path, "state.ttl");

		free(path);
		
		const LilvNode *uri_node = lilv_state_get_plugin_uri(state);
		const char *uri_str = lilv_node_as_string(uri_node);

		// fill mod json object
		cJSON *mod_json = cJSON_CreateObject();
		cJSON *mod_uid_json = cJSON_CreateNumber(mod->uid);
		cJSON *mod_uri_json = cJSON_CreateString(uri_str);
		cJSON *mod_selected_json = cJSON_CreateBool(mod->selected);
		cJSON *mod_ports_json = cJSON_CreateArray();

		for(int i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];

			cJSON *port_json = cJSON_CreateObject();
			cJSON *port_symbol_json = cJSON_CreateString(
				lilv_node_as_string(lilv_port_get_symbol(mod->plug, port->tar)));
			cJSON *port_selected_json = cJSON_CreateBool(port->selected);
			cJSON *port_sources_json = cJSON_CreateArray();
			if(mod->uid != 1) // skip system source
				for(int j=0; j<port->num_sources; j++)
				{
					port_t *source = port->sources[j];

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

	char *absolute = map_path->absolute_path(map_path->handle, "state.json");
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
		fprintf(stderr, "sp_app_save: LV2_STATE__mapPath not supported.");
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
	app->system.source = NULL; // if host should call port_connect()
	app->system.sink = NULL; // if host should call port_connect()

	for(int m=0; m<num_mods; m++)
		_sp_app_mod_del(app, app->mods[m]);

	// iterate over mods, create and apply states
	for(cJSON *mod_json = cJSON_GetObjectItem(root_json, "items")->child;
		mod_json;
		mod_json = mod_json->next)
	{
		const char *mod_uri_str = cJSON_GetObjectItem(mod_json, "uri")->valuestring;
		u_id_t mod_uid = cJSON_GetObjectItem(mod_json, "uid")->valueint;

		mod_t *mod = _sp_app_mod_add(app, mod_uri_str);
		if(!mod)
			continue;

		if(mod_uid == 1)
			app->system.source = mod;
		else if(mod_uid == 2)
			app->system.sink = mod;

		// inject module into module graph
		app->mods[app->num_mods] = mod;
		app->num_mods += 1;

		mod->selected = cJSON_GetObjectItem(mod_json, "selected")->type == cJSON_True;
		mod->uid = mod_uid;

		if(mod->uid > app->uid)
			app->uid = mod->uid;

		char uid [64];
		sprintf(uid, "%u/state.ttl", mod_uid);
		char *path = map_path->absolute_path(map_path->handle, uid);
		if(!path)
			continue;

		//printf("mapped path: %s\n", path);

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
			; //TODO

		lilv_state_free(state);
		free(path);

		// iterate over ports
		for(cJSON *port_json = cJSON_GetObjectItem(mod_json, "ports")->child;
			port_json;
			port_json = port_json->next)
		{
			const char *port_symbol_str = cJSON_GetObjectItem(port_json, "symbol")->valuestring;

			for(int i=0; i<mod->num_ports; i++)
			{
				port_t *port = &mod->ports[i];
				const LilvNode *port_symbol_node = lilv_port_get_symbol(mod->plug, port->tar);

				// search for matching port symbol
				if(strcmp(port_symbol_str, lilv_node_as_string(port_symbol_node)))
					continue;

				port->selected = cJSON_GetObjectItem(port_json, "selected")->type == cJSON_True;

				// TODO cannot handle recursive connections, needs a second loop
				for(cJSON *source_json = cJSON_GetObjectItem(port_json, "sources")->child;
					source_json;
					source_json = source_json->next)
				{
					uint32_t source_uid = cJSON_GetObjectItem(source_json, "uid")->valueint;
					const char *source_symbol_str = cJSON_GetObjectItem(source_json, "symbol")->valuestring;

					mod_t *source = _sp_app_mod_get(app, source_uid);
					if(!source)
						continue;
				
					for(int j=0; j<source->num_ports; j++)
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
		case BYPASS_STATE_PAUSED:
			return 1;
	}
}
