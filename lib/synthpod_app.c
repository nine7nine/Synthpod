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

#include <stdlib.h>
#include <uuid.h>

#include <synthpod_app.h>
#include <synthpod_private.h>

typedef struct _work_t work_t;
typedef struct _mod_t mod_t;
typedef struct _port_t port_t;

struct _work_t {
	void *target;
	uint32_t size;
	uint8_t payload [0];
};

struct _mod_t {
	sp_app_t *app;
	uuid_t uuid;
	
	// worker
	struct {
		const LV2_Worker_Interface *iface;
		LV2_Worker_Schedule schedule;
	} worker;

	// log
	LV2_Log_Log log;

	// features
	LV2_Feature feature_list [NUM_FEATURES];
	const LV2_Feature *features [NUM_FEATURES + 1];

	// self
	const LilvPlugin *plug;
	LilvInstance *inst;
	LV2_Handle handle;

	// ports
	uint32_t num_ports;
	port_t *ports;
};

struct _port_t {
	mod_t *mod;
	uuid_t uuid;
	
	const LilvPort *tar;
	uint32_t index;

	int num_sources;
	port_t *sources [32]; // TODO how many?

	void *buf;

	port_direction_t direction; // input, output
	port_type_t type; // audio, CV, control, atom
	port_buffer_type_t buffer_type; // none, sequence

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

	LilvWorld *world;
	const LilvPlugins *plugs;
	
	reg_t regs;
	LV2_Atom_Forge forge;

	uint32_t num_mods;
	mod_t *mods [512]; //TODO how big?
};

// rt
static inline int
_sp_app_to_ui(sp_app_t *app, LV2_Atom *atom)
{
	return app->driver->to_ui_cb(atom, app->data);
}

// rt
static inline int
_sp_app_to_worker(sp_app_t *app, LV2_Atom *atom)
{
	return app->driver->to_worker_cb(atom, app->data);
}

// non-rt worker-thread
static inline int
_sp_worker_to_app(sp_app_t *app, LV2_Atom *atom)
{
	return app->driver->to_app_cb(atom, app->data);
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_vprintf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, va_list args)
{
	mod_t *mod = handle;
	sp_app_t *app = mod->app;
	
	if(type == app->regs.log.trace.urid)
		return 1; //TODO support logging from rt-thread

	const char *type_str = NULL;
	if(type == app->regs.log.entry.urid)
		type_str = "Entry";
	else if(type == app->regs.log.error.urid)
		type_str = "Error";
	else if(type == app->regs.log.note.urid)
		type_str = "Note";
	else if(type == app->regs.log.trace.urid)
		type_str = "Trace";
	else if(type == app->regs.log.warning.urid)
		type_str = "Warning";

	fprintf(stderr, "[%s] ", type_str); //TODO report handle 
	vfprintf(stderr, fmt, args);
	fputc('\n', stderr);

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
sp_app_t *
sp_app_new(sp_app_driver_t *driver, void *data)
{
	if(!driver || !data)
		return NULL;

	sp_app_t *app = calloc(1, sizeof(sp_app_t));
	if(!app)
		return NULL;

	app->driver = driver;
	app->data = data;

	lv2_atom_forge_init(&app->forge, app->driver->map);

	app->world = lilv_world_new();
	lilv_world_load_all(app->world);
	app->plugs = lilv_world_get_all_plugins(app->world);

	sp_regs_init(&app->regs, app->world, driver->map);
	
	return app;
}

// non-rt
void
sp_app_activate(sp_app_t *app)
{
	//TODO
}

// rt
void
sp_app_set_system_source(sp_app_t *app, uint32_t index, const void *buf)
{
	//TODO
}

// rt
void
sp_app_set_system_sink(sp_app_t *app, uint32_t index, void *buf)
{
	//TODO
}

// rt
void *
sp_app_get_system_source(sp_app_t *app, uint32_t index)
{
	//TODO
	return NULL;
}

// rt
const void *
sp_app_get_system_sink(sp_app_t *app, uint32_t index)
{
	//TODO
	return NULL;
}

// rt
static LV2_Worker_Status
_schedule_work(LV2_Worker_Schedule_Handle handle, uint32_t size, const void *data)
{
	mod_t *mod = handle;
	sp_app_t *app = mod->app;

	LV2_Atom *atom = NULL;
	//TODO
	_sp_app_to_worker(app, atom);

	return LV2_WORKER_ERR_NO_SPACE;
}

// non-rt worker-thread
static inline void
_sp_app_mod_add(sp_app_t *app, const char *uri)
{
	LilvNode *uri_node = lilv_new_uri(app->world, uri);
	const LilvPlugin *plug = lilv_plugins_get_by_uri(app->plugs, uri_node);
	lilv_node_free(uri_node);

	const LilvNode *plugin_uri = lilv_plugin_get_uri(plug);
	const char *plugin_string = lilv_node_as_string(plugin_uri);
			
	if(!plug || !lilv_plugin_verify(plug))
		return;

	mod_t *mod = calloc(1, sizeof(mod_t));

	// populate worker schedule
	mod->worker.schedule.handle = mod;
	mod->worker.schedule.schedule_work = _schedule_work;

	// populate log
	mod->log.handle = mod;
	mod->log.printf = _log_printf;
	mod->log.vprintf = _log_vprintf;

	// populate feature list
	mod->feature_list[0].URI = LV2_URID__map;
	mod->feature_list[0].data = app->driver->map;
	mod->feature_list[1].URI = LV2_URID__unmap;
	mod->feature_list[1].data = app->driver->unmap;
	mod->feature_list[2].URI = LV2_WORKER__schedule;
	mod->feature_list[2].data = &mod->worker.schedule;
	mod->feature_list[3].URI = LV2_LOG__log;
	mod->feature_list[3].data = &mod->log;

	for(int i=0; i<NUM_FEATURES; i++)
		mod->features[i] = &mod->feature_list[i];
	mod->features[NUM_FEATURES] = NULL; // sentinel
		
	mod->app = app;
	uuid_generate_random(mod->uuid);
	mod->plug = plug;
	mod->num_ports = lilv_plugin_get_num_ports(plug);
	mod->inst = lilv_plugin_instantiate(plug, app->driver->sample_rate, mod->features);
	mod->handle = lilv_instance_get_handle(mod->inst),
	mod->worker.iface = lilv_instance_get_extension_data(mod->inst,
		LV2_WORKER__interface);
	lilv_instance_activate(mod->inst);

	mod->ports = calloc(mod->num_ports, sizeof(port_t));
	for(uint32_t i=0; i<mod->num_ports; i++)
	{
		port_t *tar = &mod->ports[i];
		size_t size = 0;
		const LilvPort *port = lilv_plugin_get_port_by_index(plug, i);

		tar->mod = mod;
		uuid_generate_random(tar->uuid);
		tar->tar = port;
		tar->index = i;
		tar->direction = lilv_port_is_a(plug, port, app->regs.port.input.node)
			? PORT_DIRECTION_INPUT
			: PORT_DIRECTION_OUTPUT;

		if(lilv_port_is_a(plug, port, app->regs.port.audio.node))
		{
			size = app->driver->period_size * sizeof(float);
			tar->type =  PORT_TYPE_AUDIO;
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.cv.node))
		{
			size = app->driver->period_size * sizeof(float);
			tar->type = PORT_TYPE_CV;
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.control.node))
		{
			size = sizeof(float);
			tar->type = PORT_TYPE_CONTROL;
			tar->protocol = app->regs.port.float_protocol.urid; //TODO remove?
		
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
			size = app->driver->seq_size;
			tar->type = PORT_TYPE_ATOM;
			tar->buffer_type = PORT_BUFFER_TYPE_SEQUENCE;
			//tar->buffer_type = lilv_port_is_a(plug, port, app->regs.port.sequence.node)
			//	? PORT_BUFFER_TYPE_SEQUENCE
			//	: PORT_BUFFER_TYPE_NONE; //FIXME
		}
		else
			; //TODO abort

		// allocate 8-byte aligned buffer
		posix_memalign(&tar->buf, 8, size); //TODO mlock
		memset(tar->buf, 0x0, size);

		// initialize control buffers to default value
		if(tar->type == PORT_TYPE_CONTROL)
			*(float *)tar->buf = tar->dflt;

		// set port buffer
		lilv_instance_connect_port(mod->inst, i, tar->buf);
	}

	struct {
		LV2_Atom_Object obj;
		LV2_Atom_Property_Body prop1;
		int32_t val1;
	} note = {
		.obj = {
			.atom = {
				.size = sizeof(LV2_Atom_Object_Body) + sizeof(LV2_Atom_Property_Body)
							+ sizeof(int32_t),
				.type = app->forge.Object
			},
			.body = {
				.id = 0,
				.otype = app->regs.synthpod.module_add.urid
			}
		},
		.prop1 = {
			.key = app->regs.synthpod.module_index.urid,
			.context = 0,
			.value = {
				.size = sizeof(int32_t),
				.type = app->forge.Int
			}
		},
		.val1 = app->num_mods
	};
	_sp_worker_to_app(app, &note.obj.atom);
	
	//TODO move to rt-thread app->mods[app->num_mods++] = mod;
}

// non-rt worker-thread
static inline void
_sp_app_mod_del(sp_app_t *app, uint32_t module_index)
{
	mod_t *mod = app->mods[module_index];
	
	// deinit instance
	lilv_instance_deactivate(mod->inst);
	lilv_instance_free(mod->inst);

	// deinit ports
	for(uint32_t i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];
		free(port->buf);
	}
	free(mod->ports);
	free(mod);

	struct {
		LV2_Atom_Object obj;
		LV2_Atom_Property_Body prop1;
		int32_t val1;
	} note = {
		.obj = {
			.atom = {
				.size = sizeof(LV2_Atom_Object_Body) + sizeof(LV2_Atom_Property_Body)
							+ sizeof(int32_t),
				.type = app->forge.Object
			},
			.body = {
				.id = 0,
				.otype = app->regs.synthpod.module_del.urid
			}
		},
		.prop1 = {
			.key = app->regs.synthpod.module_index.urid,
			.context = 0,
			.value = {
				.size = sizeof(int32_t),
				.type = app->forge.Int
			}
		},
		.val1 = module_index
	};
	_sp_worker_to_app(app, &note.obj.atom);
}

// rt
static inline void
_sp_app_port_set(sp_app_t *app,
	uint32_t module_index, uint32_t port_index, const LV2_Atom *atom)
{
	mod_t *mod = app->mods[module_index];
	port_t *port = &mod->ports[port_index];

	switch(port->protocol)
	{
		case PORT_PROTOCOL_FLOAT:
		{
			const LV2_Atom_Float *val = (const LV2_Atom_Float *)atom;
			*(float *)port->buf = val->body;
			port->last = val->body;
		}
		case PORT_PROTOCOL_ATOM:
		{
			size_t size = sizeof(LV2_Atom) + atom->size;
			memcpy(port->buf, atom, size);
		}
		case PORT_PROTOCOL_SEQUENCE:
		{
			//TODO inject at end of sequence
		}
		case PORT_PROTOCOL_PEAK:
			// is never sent from ui
			break;
	}
}

// rt
static inline void
_sp_app_port_connect(sp_app_t *app,
	uint32_t module_source_index, uint32_t port_source_index,
	uint32_t module_sink_index, uint32_t port_sink_index)
{
	//TODO
}

// rt
static inline void
_sp_app_port_disconnect(sp_app_t *app,
	uint32_t module_source_index, uint32_t port_source_index,
	uint32_t module_sink_index, uint32_t port_sink_index)
{
	//TODO
}

// rt
void
sp_app_from_ui(sp_app_t *app, const LV2_Atom *atom, void *data)
{
	const LV2_Atom_Object *ao = (const LV2_Atom_Object *)atom;
	const LV2_Atom_Object_Body *body = &ao->body;

	if(body->otype == app->regs.synthpod.module_add.urid)
	{
		// redirect to worker thread
		_sp_app_to_worker(app, (LV2_Atom *)ao);
	}
	else if(body->otype == app->regs.synthpod.module_del.urid)
	{
		// redirect to worker thread
		_sp_app_to_worker(app, (LV2_Atom *)ao);
	}
	else if(body->otype == app->regs.synthpod.port_update.urid)
	{
		const LV2_Atom_Int *module_index = NULL;
		const LV2_Atom_Int *port_index = NULL;
		const LV2_Atom *port_value = NULL;
		LV2_Atom_Object_Query q [] = {
			{ app->regs.synthpod.module_index.urid, (const LV2_Atom **)&module_index },
			{ app->regs.synthpod.port_index.urid, (const LV2_Atom **)&port_index },
			{ app->regs.synthpod.port_value.urid, &port_value },
			LV2_ATOM_OBJECT_QUERY_END
		};
		lv2_atom_object_query(ao, q);

		_sp_app_port_set(app, module_index->body, port_index->body, port_value);
	}
	else if(body->otype == app->regs.synthpod.port_connect.urid)
	{
		const LV2_Atom_Int* module_source_index = NULL;
		const LV2_Atom_Int* module_sink_index = NULL;
		const LV2_Atom_Int* port_source_index = NULL;
		const LV2_Atom_Int* port_sink_index = NULL;

		LV2_Atom_Object_Query q [] = {
			{ app->regs.synthpod.module_source_index.urid, (const LV2_Atom **)&module_source_index },
			{ app->regs.synthpod.module_sink_index.urid, (const LV2_Atom **)&module_sink_index },
			{ app->regs.synthpod.port_source_index.urid, (const LV2_Atom **)&port_source_index },
			{ app->regs.synthpod.port_sink_index.urid, (const LV2_Atom **)&port_sink_index },
			LV2_ATOM_OBJECT_QUERY_END
		};
		lv2_atom_object_query(ao, q);

		_sp_app_port_connect(app, module_source_index->body, port_source_index->body,
			module_sink_index->body, port_sink_index->body);
	}
	else if(body->otype == app->regs.synthpod.port_disconnect.urid)
	{
		const LV2_Atom_Int* module_source_index = NULL;
		const LV2_Atom_Int* module_sink_index = NULL;
		const LV2_Atom_Int* port_source_index = NULL;
		const LV2_Atom_Int* port_sink_index = NULL;

		LV2_Atom_Object_Query q [] = {
			{ app->regs.synthpod.module_source_index.urid, (const LV2_Atom **)&module_source_index },
			{ app->regs.synthpod.module_sink_index.urid, (const LV2_Atom **)&module_sink_index },
			{ app->regs.synthpod.port_source_index.urid, (const LV2_Atom **)&port_source_index },
			{ app->regs.synthpod.port_sink_index.urid, (const LV2_Atom **)&port_sink_index },
			LV2_ATOM_OBJECT_QUERY_END
		};
		lv2_atom_object_query(ao, q);

		_sp_app_port_disconnect(app, module_source_index->body, port_source_index->body,
			module_sink_index->body, port_sink_index->body);
	}
}

// rt
void
sp_app_from_worker(sp_app_t *app, const LV2_Atom *atom, void *data)
{
	if(atom->type == app->forge.Object)
	{
		const LV2_Atom_Object *ao = (const LV2_Atom_Object *)atom;
		const LV2_Atom_Object_Body *body = &ao->body;

		if(body->otype == app->regs.synthpod.module_add.urid)
		{
			// TODO actually inject into graph

			// redirect to UI
			_sp_app_to_ui(app, (LV2_Atom *)ao);
		}
		else if(body->otype == app->regs.synthpod.module_del.urid)
		{
			// TODO actually inject into graph

			// redirect to UI
			_sp_app_to_ui(app, (LV2_Atom *)ao);
		}
	}
	else
	{	
		const work_t *work = LV2_ATOM_BODY_CONST(atom);
		mod_t *mod = work->target;

		if(mod && mod->worker.iface && mod->worker.iface->work_response)
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
	uint8_t *payload = (uint8_t *)data;

	uint8_t buf [8192]; //TODO solve differently

	LV2_Atom *atom = (LV2_Atom *)buf;
	atom->type = 0; //TODO
	atom->size = size;

	work_t *work = LV2_ATOM_BODY(atom);
	work->target = mod;
	memcpy(work->payload, data, size);

	_sp_worker_to_app(app, atom);
	return LV2_WORKER_SUCCESS;
}

// non-rt worker thread
void
sp_worker_from_app(sp_app_t *app, const LV2_Atom *atom, void *data)
{
	if(atom->type == app->forge.Object)
	{
		const LV2_Atom_Object *ao = (const LV2_Atom_Object *)atom;
		const LV2_Atom_Object_Body *body = &ao->body;

		if(body->otype == app->regs.synthpod.module_add.urid)
		{
			const LV2_Atom_String* module_uri = NULL;
			LV2_Atom_Object_Query q [] = {
				{ app->forge.URI, (const LV2_Atom **)&module_uri },
				LV2_ATOM_OBJECT_QUERY_END
			};
			lv2_atom_object_query(ao, q);
			
			_sp_app_mod_add(app, LV2_ATOM_BODY_CONST(module_uri));
		}
		else if(body->otype == app->regs.synthpod.module_del.urid)
		{
			const LV2_Atom_Int* module_index = NULL;
			LV2_Atom_Object_Query q [] = {
				{ app->regs.synthpod.module_index.urid, (const LV2_Atom **)&module_index },
				LV2_ATOM_OBJECT_QUERY_END
			};
			lv2_atom_object_query(ao, q);

			_sp_app_mod_del(app, module_index->body);
		}
	}
	else
	{
		const work_t *work = LV2_ATOM_BODY_CONST(atom);
		mod_t *mod = work->target;

		if(mod && mod->worker.iface && mod->worker.iface->work)
		{
			mod->worker.iface->work(mod->handle, _sp_worker_respond, mod,
				work->size, &work->payload);
			//TODO check return status
		}
	}
}

// rt
void
sp_app_run(sp_app_t *app, uint32_t nsamples)
{
	//TODO
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
	
	sp_regs_deinit(&app->regs);

	lilv_world_free(app->world);

	free(app);
}
