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
#include <math.h>
#include <uuid.h>

#include <synthpod_app.h>
#include <synthpod_private.h>

#define NUM_FEATURES 4

typedef enum _job_type_t job_type_t;

typedef struct _mod_t mod_t;
typedef struct _port_t port_t;
typedef struct _work_t work_t;
typedef struct _job_t job_t;

enum _job_type_t {
	JOB_TYPE_MODULE_ADD,
	JOB_TYPE_MODULE_DEL
};

struct _job_t {
	job_type_t type;
	mod_t *mod;
	char uri [0];
};

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
	mod_t *mods [512]; //TODO how many?
};

// rt
static inline void *
_sp_app_to_ui_request(sp_app_t *app, size_t size)
{
	return app->driver->to_ui_request(size, app->data);
}
static inline void
_sp_app_to_ui_advance(sp_app_t *app, size_t size)
{
	app->driver->to_ui_advance(size, app->data);
}

// rt
static inline void *
_sp_app_to_worker_request(sp_app_t *app, size_t size)
{
	return app->driver->to_worker_request(size, app->data);
}
static inline void
_sp_app_to_worker_advance(sp_app_t *app, size_t size)
{
	app->driver->to_worker_advance(size, app->data);
}

// non-rt worker-thread
static inline void *
_sp_worker_to_app_request(sp_app_t *app, size_t size)
{
	return app->driver->to_app_request(size, app->data);
}
static inline void
_sp_worker_to_app_advance(sp_app_t *app, size_t size)
{
	app->driver->to_app_advance(size, app->data);
}

//TODO move to synthpod_jack
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

	app->world = lilv_world_new();
	lilv_world_load_all(app->world);
	app->plugs = lilv_world_get_all_plugins(app->world);

	lv2_atom_forge_init(&app->forge, app->driver->map);
	sp_regs_init(&app->regs, app->world, app->driver->map);
	
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
	static uint8_t tmp [8192];
	return tmp;
}

// rt
const void *
sp_app_get_system_sink(sp_app_t *app, uint32_t index)
{
	//TODO
	static uint8_t tmp [8192];
	return tmp;
}

// rt
static LV2_Worker_Status
_schedule_work(LV2_Worker_Schedule_Handle handle, uint32_t size, const void *data)
{
	mod_t *mod = handle;
	sp_app_t *app = mod->app;

	size_t work_size = sizeof(work_t) + size;
	work_t *work = _sp_app_to_worker_request(app, work_size);
	work->target = mod;
	work->size = size;
	memcpy(work->payload, data, size);
	_sp_app_to_worker_advance(app, work_size);

	return LV2_WORKER_ERR_NO_SPACE;
}

// non-rt worker-thread
static inline mod_t *
_sp_app_mod_add(sp_app_t *app, const char *uri)
{
	LilvNode *uri_node = lilv_new_uri(app->world, uri);
	const LilvPlugin *plug = lilv_plugins_get_by_uri(app->plugs, uri_node);
	lilv_node_free(uri_node);

	const LilvNode *plugin_uri = lilv_plugin_get_uri(plug);
	const char *plugin_string = lilv_node_as_string(plugin_uri);
			
	if(!plug || !lilv_plugin_verify(plug))
		return NULL;

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
			//	: PORT_BUFFER_TYPE_NONE; //TODO discriminate properly
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

	return mod;
}

// non-rt worker-thread
static inline void
_sp_app_mod_del(sp_app_t *app, mod_t *mod)
{
	// deinit instance
	lilv_instance_deactivate(mod->inst);
	lilv_instance_free(mod->inst);

	// deinit ports
	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];
		free(port->buf);
	}
	free(mod->ports);
	free(mod);
}

static inline mod_t *
_sp_app_mod_get(sp_app_t *app, uuid_t uuid)
{
	for(int i=0; i<app->num_mods; i++)
	{
		mod_t *mod = app->mods[i];
		if(!uuid_compare(mod->uuid, uuid))
			return mod;
	}

	return NULL;
}

static inline port_t *
_sp_app_port_get(sp_app_t *app, uuid_t uuid, uint32_t index)
{
	mod_t *mod = _sp_app_mod_get(app, uuid);
	if(mod && (index < mod->num_ports) )
		return &mod->ports[index];
	
	return NULL;
}

// rt
void
sp_app_from_ui(sp_app_t *app, const LV2_Atom *atom)
{
	const transmit_t *transmit = (const transmit_t *)atom;
	LV2_URID protocol = transmit->protocol.body;

	if(protocol == app->regs.port.float_protocol.urid)
	{
		const transfer_float_t *trans = (const transfer_float_t *)atom;

		uuid_t uuid;
		uuid_parse(trans->transfer.str, uuid);
		port_t *port = _sp_app_port_get(app, uuid, trans->transfer.port.body);
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

		uuid_t uuid;
		uuid_parse(trans->transfer.str, uuid);
		port_t *port = _sp_app_port_get(app, uuid, trans->transfer.port.body);
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

		uuid_t uuid;
		uuid_parse(trans->transfer.str, uuid);
		port_t *port = _sp_app_port_get(app, uuid, trans->transfer.port.body);
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
			ptr += sizeof(LV2_Atom_Event) + last->body.size;
		}
		else
			ptr = LV2_ATOM_CONTENTS(LV2_Atom_Sequence, seq);

		// append event at end of sequence
		// TODO check for buffer overflow
		LV2_Atom_Event *new_last = ptr;
		new_last->time.frames = last ? last->time.frames : 0;
		memcpy(&new_last->body, trans->atom, sizeof(LV2_Atom) + trans->atom->size);
		seq->atom.size += sizeof(LV2_Atom_Event) + ((atom->size + 7U) & (~7U));
	}
	else if(protocol == app->regs.synthpod.module_add.urid)
	{
		const transmit_module_add_t *module_add = (const transmit_module_add_t *)atom;

		// send request to worker thread
		size_t size = sizeof(work_t) + sizeof(job_t) + module_add->uri.atom.size;
		work_t *work = _sp_app_to_worker_request(app, size);
			work->target = app;
			work->size = size - sizeof(work_t);
		job_t *job = (job_t *)work->payload;
			job->type = JOB_TYPE_MODULE_ADD;
			memcpy(job->uri, module_add->str, module_add->uri.atom.size);
		_sp_app_to_worker_advance(app, size);
	}
	else if(protocol == app->regs.synthpod.module_del.urid)
	{
		const transmit_module_del_t *module_del = (const transmit_module_del_t *)atom;

		// search mod according to its UUID
		uuid_t uuid;
		uuid_parse(module_del->str, uuid);
		mod_t *mod = _sp_app_mod_get(app, uuid);
		if(!mod) // mod not found
			return;

		// eject module from graph
		int offset = mod - app->mods[0];
		app->num_mods -= 1;
		for(int m=offset; m<app->num_mods; m++)
			app->mods[m] = app->mods[m+1];

		// send request to worker thread
		size_t size = sizeof(work_t) + sizeof(job_t);
		work_t *work = _sp_app_to_worker_request(app, size);
			work->target = app;
			work->size = size - sizeof(work_t);
		job_t *job = (job_t *)work->payload;
			job->type = JOB_TYPE_MODULE_DEL;
			job->mod = mod;
		_sp_app_to_worker_advance(app, size);
	}
	else if(protocol == app->regs.synthpod.port_connect.urid)
	{
		const transmit_port_connect_t *conn = (const transmit_port_connect_t *)atom;

		uuid_t src_uuid;
		uuid_t snk_uuid;
		uuid_parse(conn->src_str, src_uuid);
		uuid_parse(conn->snk_str, snk_uuid);
		port_t *src_port = _sp_app_port_get(app, src_uuid, conn->src_port.body);
		port_t *snk_port = _sp_app_port_get(app, snk_uuid, conn->snk_port.body);

		//TODO actually connect
	}
	else if(protocol == app->regs.synthpod.port_disconnect.urid)
	{
		const transmit_port_disconnect_t *disconn = (const transmit_port_disconnect_t *)atom;

		uuid_t src_uuid;
		uuid_t snk_uuid;
		uuid_parse(disconn->src_str, src_uuid);
		uuid_parse(disconn->snk_str, snk_uuid);
		port_t *src_port = _sp_app_port_get(app, src_uuid, disconn->src_port.body);
		port_t *snk_port = _sp_app_port_get(app, snk_uuid, disconn->snk_port.body);

		//TODO actually disconnect
	}
}

// rt
void
sp_app_from_worker(sp_app_t *app, const void *data)
{
	const work_t *work = data;

	if(work->target == app) // work is for self
	{
		const job_t *job = (const job_t *)work->payload;

		switch(job->type)
		{
			case JOB_TYPE_MODULE_ADD:
			{
				// inject module into module graph
				app->mods[app->num_mods] = job->mod;
				app->num_mods += 1;
				
				//TODO signal to UI

				break;
			}
			default:
				break; // never reached
		}
	}
	else // work is for module
	{
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

	size_t work_size = sizeof(work_t) + size;
	work_t *work = _sp_worker_to_app_request(app, work_size);
		work->target = mod;
		work->size = size;
		memcpy(work->payload, data, size);
	_sp_worker_to_app_advance(app, work_size);

	return LV2_WORKER_SUCCESS;
}

// non-rt worker thread
void
sp_worker_from_app(sp_app_t *app, const void *data)
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

				size_t work_size = sizeof(work_t) + sizeof(job_t);
				work_t *work = _sp_worker_to_app_request(app, work_size);
					work->target = app;
					work->size = sizeof(job_t);
				job_t *job = (job_t *)work->payload;
					job->type = JOB_TYPE_MODULE_ADD;
					job->mod = mod;
				_sp_worker_to_app_advance(app, work_size);

				break;
			}
			case JOB_TYPE_MODULE_DEL:
			{
				_sp_app_mod_del(app, job->mod);

				break;
			}
		}
	}
	else // work is for module
	{
		mod_t *mod = work->target;
		
		if(mod && mod->worker.iface && mod->worker.iface->work)
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

		// handle work
		if(mod->worker.iface)
		{
			// the actual work should already be done at this point in time

			if(mod->worker.iface->end_run)
				mod->worker.iface->end_run(mod->handle);
		}
	
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
					memset(val, 0, app->driver->period_size * sizeof(float)); // init
					for(int i=0; i<port->num_sources; i++)
					{
						float *src = port->sources[i]->buf;
						for(int j=0; j<app->driver->period_size; j++)
						{
							val[j] += src[j];
						}
					}
				}
				else if( (port->type == PORT_TYPE_ATOM)
							&& (port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) )
				{
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
						int64_t frames = app->driver->period_size;

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
	}
}

//rt
static inline void
_sp_transfer_fill(sp_app_t *app, transfer_t *trans, uint32_t size,
	LV2_URID protocol, port_t *port)
{
	trans->transmit.tuple.atom.size = size - sizeof(LV2_Atom);
	trans->transmit.tuple.atom.type = app->forge.Tuple;

	trans->transmit.protocol.atom.size = sizeof(LV2_URID);
	trans->transmit.protocol.atom.type = app->forge.URID;
	trans->transmit.protocol.body = protocol;

	trans->uuid.atom.size = 37;
	trans->uuid.atom.type = app->forge.String;
	uuid_unparse(port->mod->uuid, trans->str);
	
	trans->port.atom.size = sizeof(int32_t);
	trans->port.atom.type = app->forge.Int;
	trans->port.body = port->index;
}

// rt
static inline void
_sp_transfer_float_fill(sp_app_t *app, transfer_float_t *trans,
	port_t *port, float value)
{
	_sp_transfer_fill(app, &trans->transfer, sizeof(transfer_float_t),
		app->regs.port.float_protocol.urid, port);
	
	trans->value.atom.size = sizeof(float);
	trans->value.atom.type = app->forge.Float;
	trans->value.body = value;
}

// rt
static inline void
_sp_transfer_peak_fill(sp_app_t *app, transfer_peak_t *trans,
	port_t *port, uint32_t period_start, uint32_t period_size, float peak)
{
	_sp_transfer_fill(app, &trans->transfer, sizeof(transfer_peak_t),
		app->regs.port.peak_protocol.urid, port);
	
	trans->period_start.atom.size = sizeof(uint32_t);
	trans->period_start.atom.type = app->forge.Int;
	trans->period_start.body = period_start;
	
	trans->period_size.atom.size = sizeof(uint32_t);
	trans->period_size.atom.type = app->forge.Int;
	trans->period_size.body = period_size;
	
	trans->peak.atom.size = sizeof(float);
	trans->peak.atom.type = app->forge.Float;
	trans->peak.body = peak;
}

// rt
static inline void
_sp_transfer_atom_fill(sp_app_t *app, transfer_atom_t *trans,
	port_t *port, const LV2_Atom *atom)
{
	uint32_t atom_size = sizeof(LV2_Atom) + atom->size;

	_sp_transfer_fill(app, &trans->transfer, sizeof(transfer_atom_t) + atom_size,
		app->regs.port.atom_transfer.urid, port);

	memcpy(trans->atom, atom, atom_size);
}

// rt
static inline void
_sp_transfer_event_fill(sp_app_t *app, transfer_atom_t *trans,
	port_t *port, const LV2_Atom *atom)
{
	uint32_t atom_size = sizeof(LV2_Atom) + atom->size;

	_sp_transfer_fill(app, &trans->transfer, sizeof(transfer_atom_t) + atom_size,
		app->regs.port.event_transfer.urid, port);

	memcpy(trans->atom, atom, atom_size);
}

// rt
void
sp_app_run_post(sp_app_t *app, uint32_t nsamples)
{
	// iterate over all modules
	for(int m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		// run plugin
		lilv_instance_run(mod->inst, app->driver->period_size);
		
		// handle ui post
		for(int i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];

			if(port->protocol == 0) // no notification/subscription
				continue;
				
			const void *buf = port->num_sources == 1
				? port->sources[0]->buf // direct link to source buffer
				: port->buf; // dummy (n==0) or multiplexed (n>1) link

			if(port->protocol == app->regs.port.float_protocol.urid)
			{
				const float val = *(const float *)buf;
				if(val != port->last) // has value changed since last time?
				{
					// update last value
					port->last = val;

					size_t size = sizeof(transfer_float_t);
					transfer_float_t *trans = _sp_app_to_ui_request(app, size);
					_sp_transfer_float_fill(app, trans, port, val);
					_sp_app_to_ui_advance(app, size);
				}
			}
			else if(port->protocol == app->regs.port.peak_protocol.urid)
			{
				const float *vec = (const float *)buf;

				// find peak value in current period
				float peak = 0.f;
				for(int j=0; j<app->driver->period_size; j++)
				{
					float val = fabs(vec[j]);
					if(val > peak)
						peak = val;
				}

				port->period_cnt += 1; // increase period counter
				//printf("%u %f\n", port->period_cnt, peak);

				if(  (peak != port->last) //TODO make below two configurable
					&& (fabs(peak - port->last) > 0.001) // ignore smaller changes
					&& ((port->period_cnt & 0x1f) == 0x00) ) // only update every 32 samples
				{
					printf("peak different: %i %i\n", port->last == 0.f, peak == 0.f);

					// update last value
					port->last = peak;

					size_t size = sizeof(transfer_peak_t);
					transfer_peak_t *trans = _sp_app_to_ui_request(app, size);
					_sp_transfer_peak_fill(app, trans, port,
						port->period_cnt, app->driver->period_size, peak);
					_sp_app_to_ui_advance(app, size);
				}
			}
			else if(port->protocol == app->regs.port.atom_transfer.urid)
			{
				const LV2_Atom *atom = buf;
				if(atom->size == 0) // empty atom
					continue;
		
				size_t size = sizeof(transfer_atom_t) + sizeof(LV2_Atom) + atom->size;
				transfer_atom_t *trans = _sp_app_to_ui_request(app, size);
				_sp_transfer_atom_fill(app, trans, port, atom);
				_sp_app_to_ui_advance(app, size);
			}
			else if(port->protocol == app->regs.port.event_transfer.urid)
			{
				const LV2_Atom_Sequence *seq = buf;
				if(seq->atom.size <= sizeof(LV2_Atom_Sequence_Body)) // empty seq
					continue;

				// transfer each atom of sequence separately
				LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
				{
					const LV2_Atom *atom = &ev->body;

					size_t size = sizeof(transfer_atom_t) + sizeof(LV2_Atom) + atom->size;
					transfer_atom_t *trans = _sp_app_to_ui_request(app, size);
					_sp_transfer_event_fill(app, trans, port, atom);
					_sp_app_to_ui_advance(app, size);
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

	lilv_world_free(app->world);

	free(app);
}
