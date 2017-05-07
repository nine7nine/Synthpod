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

#include <synthpod_app_private.h>
#include <synthpod_patcher.h>

// non-rt
void
sp_app_activate(sp_app_t *app)
{
	//TODO
}

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
				app->system_sources[num_system_sources].buf = PORT_BASE_ALIGNED(port);
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
				app->system_sinks[num_system_sinks].buf = PORT_BASE_ALIGNED(port);
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

const sp_app_system_source_t *
sp_app_get_system_sources(sp_app_t *app)
{
	_sp_app_update_system_sources(app);

	return app->system_sources;
}

const sp_app_system_sink_t *
sp_app_get_system_sinks(sp_app_t *app)
{
	_sp_app_update_system_sinks(app);

	return app->system_sinks;
}

__non_realtime static uint32_t
_uri_to_id(LV2_URI_Map_Callback_Data handle, const char *_, const char *uri)
{
	sp_app_t *app = handle;

	LV2_URID_Map *map = app->driver->map;

	return map->map(map->handle, uri);
}

__realtime static inline void
_sp_app_process_single_run(mod_t *mod, uint32_t nsamples)
{
	sp_app_t *app = mod->app;

	// multiplex multiple sources to single sink where needed
	for(unsigned p=0; p<mod->num_ports; p++)
	{
		port_t *port = &mod->ports[p];

		if(port->direction == PORT_DIRECTION_OUTPUT)
		{
			if(  (port->type == PORT_TYPE_ATOM)
				&& (port->atom.buffer_type == PORT_BUFFER_TYPE_SEQUENCE)
				&& (!mod->system_ports) ) // don't overwrite source buffer events
			{
				LV2_Atom_Sequence *seq = PORT_BASE_ALIGNED(port);
				seq->atom.size = port->size;
				seq->atom.type = app->forge.Sequence;
				seq->body.unit = 0;
				seq->body.pad = 0;
			}
		}
		else // PORT_DIRECTION_INPUT
		{
			if(port->driver->multiplex)
				port->driver->multiplex(app, port, nsamples);
		}
	}

	struct timespec mod_t1;
	struct timespec mod_t2;
	clock_gettime(CLOCK_MONOTONIC, &mod_t1);

	// is module currently loading a preset asynchronously?
	if(!mod->bypassed)
	{
		//FIXME for para/dynameters needs to be multiplexed with patchabel events
		// apply automation if any
		{
			const unsigned p = mod->num_ports - 1;
			port_t *auto_port = &mod->ports[p];
			const LV2_Atom_Sequence *seq = PORT_BASE_ALIGNED(auto_port);

			LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
			{
				const int64_t frames = ev->time.frames;
				const LV2_Atom *atom = &ev->body;

				//printf("got automation\n");
				if(  (atom->type == app->regs.port.midi.urid)
					&& (atom->size == 3) ) // we're only interested in controller events
				{
					const uint8_t *msg = LV2_ATOM_BODY_CONST(atom);
					const uint8_t cmd = msg[0] & 0xf0;

					if(cmd == 0xb0) // Controller
					{
						const uint8_t channel = msg[0] & 0x0f;
						const uint8_t controller = msg[1];

						// iterate over automations
						for(unsigned i = 0; i < MAX_AUTOMATIONS; i++)
						{
							auto_t *automation = &mod->automations[i];

							if(automation->type != AUTO_TYPE_MIDI)
								continue;

							midi_auto_t *mauto = &automation->midi;

							if(  ( (mauto->channel == -1) || (mauto->channel == channel) )
								&& ( (mauto->controller == -1) || (mauto->controller == controller) ) )
							{
								float rel = (msg[2] - mauto->min) * mauto->range_1;
								if(rel < 0.f)
									rel = 0.f;
								else if(rel > 1.f)
									rel = 1.f;

								port_t *port = &mod->ports[automation->index];
								if(port->type == PORT_TYPE_CONTROL)
								{
									control_port_t *control = &port->control;

									float f32 = rel * control->range + control->min;
									if(control->is_integer)
										f32 = floorf(f32);

									if(f32 < control->min)
										f32 = control->min;
									else if(f32 > control->max)
										f32 = control->max;

									float *buf = PORT_BASE_ALIGNED(port);
									*buf = f32;

									//printf("control automation match: %f %f\n", rel, f32);
								}
								else if( (port->type == PORT_TYPE_ATOM) && automation->property )
								{
									const int32_t i32 = floorf(rel * 127.f); //FIXME use correct scaling

									LV2_Atom_Sequence *control = PORT_BASE_ALIGNED(port);
									LV2_Atom_Event *dst = lv2_atom_sequence_end(&control->body, control->atom.size);
									LV2_Atom_Forge_Frame obj_frame;

									lv2_atom_forge_set_buffer(&app->forge, (uint8_t *)dst, PORT_SIZE(port) - control->atom.size - sizeof(LV2_Atom));

									if(  lv2_atom_forge_frame_time(&app->forge, nsamples - 1)
										&& lv2_atom_forge_object(&app->forge, &obj_frame, 0, app->regs.patch.set.urid)
										&& lv2_atom_forge_key(&app->forge, app->regs.patch.property.urid)
										&& lv2_atom_forge_urid(&app->forge, automation->property)
										&& lv2_atom_forge_key(&app->forge, app->regs.patch.value.urid)
										&& lv2_atom_forge_int(&app->forge, i32) ) //FIXME use correct type
									{
										lv2_atom_forge_pop(&app->forge, &obj_frame);

										control->atom.size += sizeof(LV2_Atom_Event) + dst->body.size;
									}

									//printf("parameter automation match: %f %f\n", rel, f32);
								}
							}
						}
					}
				}
				//FIXME handle other events
			}
		}

		// run plugin
		if(!mod->disabled)
		{
			lilv_instance_run(mod->inst, nsamples);
		}
	}

	clock_gettime(CLOCK_MONOTONIC, &mod_t2);

	// profiling
	const unsigned run_time = (mod_t2.tv_sec - mod_t1.tv_sec)*1000000000
		+ mod_t2.tv_nsec - mod_t1.tv_nsec;
	mod->prof.sum += run_time;

	if(run_time < mod->prof.min)
		mod->prof.min = run_time;
	else if(run_time > mod->prof.max)
		mod->prof.max = run_time;
}

__realtime static inline void
_sp_app_process_single_post(mod_t *mod, uint32_t nsamples, bool sparse_update_timeout)
{
	sp_app_t *app = mod->app;

	// handle mod ui post
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		// no notification/subscription and no support for patch:Message
		const bool subscribed = port->subscriptions != 0;
		if(!subscribed)
			continue; // skip this port
		if( (port->type == PORT_TYPE_ATOM) && !port->atom.patchable)
			continue; // skip this port

		if(port->driver->transfer && (port->driver->sparse_update ? sparse_update_timeout : true))
			port->driver->transfer(app, port, nsamples);
	}
}

__realtime static inline bool 
_dsp_slave_fetch(dsp_master_t *dsp_master)
{
	sp_app_t *app = (void *)dsp_master - offsetof(sp_app_t, dsp_master);

	bool done = true;

	for(unsigned m=0; m<app->num_mods; m++) // TODO remember starting position
	{
		mod_t *mod = app->mods[m];
		dsp_client_t *dsp_client = &mod->dsp_client;

		int expected = 0;
		const int desired = -1; // mark as done
		const bool match = atomic_compare_exchange_weak(&dsp_client->ref_count,
			&expected, desired); //TODO can we make this explicit?
		if(match) // needs to run now
		{
			_sp_app_process_single_run(mod, dsp_master->nsamples);

			for(unsigned j=0; j<dsp_client->num_sinks; j++)
			{
				dsp_client_t *sink = dsp_client->sinks[j];
				atomic_fetch_sub_explicit(&sink->ref_count, 1, memory_order_release);
			}
		}
		else if(expected >= 0) // needs to run later
		{
			done = false;
		}
	}

	return done;
}

__realtime static inline void
_dsp_slave_spin(dsp_master_t *dsp_master, bool is_master)
{
	while(atomic_load_explicit(&dsp_master->ref_count, memory_order_acquire))
	{
		const bool done = _dsp_slave_fetch(dsp_master);
		if(done)
		{
			atomic_fetch_sub_explicit(&dsp_master->ref_count, 1, memory_order_release);
			break;
		}
	}
}

__realtime static void *
_dsp_slave_thread(void *data)
{
	dsp_slave_t *dsp_slave = data;
	dsp_master_t *dsp_master = dsp_slave->dsp_master;
	sp_app_t *app = (void *)dsp_master - offsetof(sp_app_t, dsp_master);
	int num = dsp_slave - dsp_master->dsp_slaves + 1;
	//printf("thread: %i\n", num);

	struct sched_param schedp;
	memset(&schedp, 0, sizeof(struct sched_param));
	schedp.sched_priority = app->driver->audio_prio;

	const pthread_t self = pthread_self();
	if(pthread_setschedparam(self, SCHED_FIFO, &schedp))
		fprintf(stderr, "pthread_setschedparam error\n");

	while(!atomic_load_explicit(&dsp_master->kill, memory_order_acquire))
	{
		sem_wait(&dsp_slave->sem);

		_dsp_slave_spin(dsp_master, false);
	}

	return NULL;
}

__realtime static inline void
_dsp_master_post(dsp_master_t *dsp_master, unsigned num)
{
	for(unsigned i=0; i<num; i++)
	{
		dsp_slave_t *dsp_slave = &dsp_master->dsp_slaves[i];

		sem_post(&dsp_slave->sem);
	}
}

__realtime static inline void
_dsp_master_process(sp_app_t *app, dsp_master_t *dsp_master, unsigned nsamples)
{
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];
		dsp_client_t *dsp_client = &mod->dsp_client;

		atomic_store_explicit(&dsp_client->ref_count, dsp_client->num_sources, memory_order_release);
	}

	unsigned num_slaves = dsp_master->concurrent - 1;
	if(num_slaves > dsp_master->num_slaves)
		num_slaves = dsp_master->num_slaves;
	dsp_master->nsamples = nsamples;
	const unsigned ref_count = num_slaves + 1; // plus master
	atomic_store_explicit(&dsp_master->ref_count, ref_count, memory_order_release);
	_dsp_master_post(dsp_master, num_slaves); // wake up other slaves
	_dsp_slave_spin(dsp_master, true); // runs jobs itself 

	while(atomic_load_explicit(&dsp_master->ref_count, memory_order_acquire))
	{
		// spin
	}
}

void
_sp_app_reset(sp_app_t *app)
{
	// remove existing modules
	int num_mods = app->num_mods;

	app->num_mods = 0;

	for(int m=0; m<num_mods; m++)
		_sp_app_mod_del(app, app->mods[m]);
}

void
_sp_app_populate(sp_app_t *app)
{
	const char *uri_str;
	mod_t *mod;

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
}

sp_app_t *
sp_app_new(const LilvWorld *world, sp_app_driver_t *driver, void *data)
{
	if(!driver || !data)
		return NULL;

	sp_app_t *app = calloc(1, sizeof(sp_app_t));
	if(!app)
		return NULL;
	mlock(app, sizeof(sp_app_t));

	atomic_init(&app->dirty, false);

	app->dir.home = getenv("HOME");

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
		if(!app->world)
		{
			free(app);
			return NULL;
		}
		LilvNode *node_false = lilv_new_bool(app->world, false);
		if(node_false)
		{
			lilv_world_set_option(app->world, LILV_OPTION_DYN_MANIFEST, node_false);
			lilv_node_free(node_false);
		}
		lilv_world_load_all(app->world);
		LilvNode *synthpod_bundle = lilv_new_file_uri(app->world, NULL, SYNTHPOD_BUNDLE_DIR"/");
		if(synthpod_bundle)
		{
			lilv_world_load_bundle(app->world, synthpod_bundle);
			lilv_node_free(synthpod_bundle);
		}
	}
	app->plugs = lilv_world_get_all_plugins(app->world);

	lv2_atom_forge_init(&app->forge, app->driver->map);
	sp_regs_init(&app->regs, app->world, app->driver->map);

	_sp_app_populate(app);

	app->fps.bound = driver->sample_rate / driver->update_rate;
	app->fps.counter = 0;

	app->ramp_samples = driver->sample_rate / 10; // ramp over 0.1s FIXME make this configurable

	// populate uri_to_id
	app->uri_to_id.callback_data = app;
	app->uri_to_id.uri_to_id = _uri_to_id;

	app->sratom = sratom_new(app->driver->map);
	if(app->sratom)
		sratom_set_pretty_numbers(app->sratom, false);

	// initialize DSP load profiler
	clock_gettime(CLOCK_MONOTONIC, &app->prof.t0);
	app->prof.min = UINT_MAX;
	app->prof.max = 0;
	app->prof.sum = 0;
	app->prof.count = 0;

	// initialize grid dimensions
	app->ncols = 3;
	app->nrows = 2;
	app->nleft = 0.2;

	// initialize parallel processing
	dsp_master_t *dsp_master = &app->dsp_master;
	atomic_init(&dsp_master->kill, false);
	atomic_init(&dsp_master->ref_count, 0);
	dsp_master->num_slaves = driver->num_slaves;
	dsp_master->concurrent = 1; // this is a safe fallback
	for(unsigned i=0; i<dsp_master->num_slaves; i++)
	{
		dsp_slave_t *dsp_slave = &dsp_master->dsp_slaves[i];

		dsp_slave->dsp_master = dsp_master;
		sem_init(&dsp_slave->sem, 0, 0);
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_create(&dsp_slave->thread, &attr, _dsp_slave_thread, dsp_slave);
	}

	return app;
}

void
sp_app_run_pre(sp_app_t *app, uint32_t nsamples)
{
	mod_t *del_me = NULL;

	clock_gettime(CLOCK_MONOTONIC, &app->prof.t1);

	// iterate over all modules
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		if(mod->delete_request && !del_me) // only delete 1 module at once
		{
			del_me = mod;
			mod->delete_request = false;
		}

		mod_worker_t *mod_worker = &mod->mod_worker;
		if(mod_worker->app_from_worker)
		{
			const void *payload;
			size_t size;
			while((payload = varchunk_read_request(mod_worker->app_from_worker, &size)))
			{
				// zero worker takes precedence over standard worker
				if(mod->zero.iface && mod->zero.iface->response)
				{
					mod->zero.iface->response(mod->handle, size, payload);
					//TODO check return status
				}
				else if(mod->worker.iface && mod->worker.iface->work_response)
				{
					mod->worker.iface->work_response(mod->handle, size, payload);
					//TODO check return status
				}

				varchunk_read_advance(mod_worker->app_from_worker);
			}
		}

		// handle end of work
		if(mod->zero.iface && mod->zero.iface->end)
			mod->zero.iface->end(mod->handle);
		else if(mod->worker.iface && mod->worker.iface->end_run)
			mod->worker.iface->end_run(mod->handle);

		for(unsigned p=0; p<mod->num_ports; p++)
		{
			port_t *port = &mod->ports[p];

			// stash control port values
			if( (port->type == PORT_TYPE_CONTROL) && port->control.stashing)
			{
				port->control.stashing = false;
				_sp_app_port_control_stash(port);
			}

			if(port->direction == PORT_DIRECTION_OUTPUT)
				continue; // ignore output ports

			// clear atom sequence input buffers
			if(  (port->type == PORT_TYPE_ATOM)
				&& (port->atom.buffer_type == PORT_BUFFER_TYPE_SEQUENCE) )
			{
				LV2_Atom_Sequence *seq = PORT_BASE_ALIGNED(port);
				seq->atom.type = app->regs.port.sequence.urid;
				seq->atom.size = sizeof(LV2_Atom_Sequence_Body); // empty sequence
				seq->body.unit = 0;
				seq->body.pad = 0;
			}
		}
	}

	if(del_me)
		_sp_app_mod_eject(app, del_me);
}

static inline void
_sp_app_process_serial(sp_app_t *app, uint32_t nsamples, bool sparse_update_timeout)
{
	// iterate over all modules
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		_sp_app_process_single_run(mod, nsamples);
		_sp_app_process_single_post(mod, nsamples, sparse_update_timeout);
	}
}

static inline void
_sp_app_process_parallel(sp_app_t *app, uint32_t nsamples, bool sparse_update_timeout)
{
	_dsp_master_process(app, &app->dsp_master, nsamples);

	// iterate over all modules
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		_sp_app_process_single_post(mod, nsamples, sparse_update_timeout);
	}
}

void
sp_app_run_post(sp_app_t *app, uint32_t nsamples)
{
	bool sparse_update_timeout = false;

	app->fps.counter += nsamples; // increase sample counter
	app->fps.period_cnt += 1; // increase period counter
	if(app->fps.counter >= app->fps.bound) // check whether we reached boundary
	{
		sparse_update_timeout = true;
		app->fps.counter -= app->fps.bound; // reset sample counter
	}

	dsp_master_t *dsp_master = &app->dsp_master;
	if( (dsp_master->num_slaves > 0) && (dsp_master->concurrent > 1) ) // parallel processing makes sense here
		_sp_app_process_parallel(app, nsamples, sparse_update_timeout);
	else
		_sp_app_process_serial(app, nsamples, sparse_update_timeout);

	// profiling
	struct timespec app_t2;
	clock_gettime(CLOCK_MONOTONIC, &app_t2);

	const unsigned run_time = (app_t2.tv_sec - app->prof.t1.tv_sec)*1000000000
		+ app_t2.tv_nsec - app->prof.t1.tv_nsec;
	app->prof.sum += run_time;
	app->prof.count += 1;

	if(run_time < app->prof.min)
		app->prof.min = run_time;
	else if(run_time > app->prof.max)
		app->prof.max = run_time;

	if(app_t2.tv_sec > app->prof.t0.tv_sec) // a second has passed
	{
		const unsigned tot_time = (app_t2.tv_sec - app->prof.t0.tv_sec)*1000000000
			+ app_t2.tv_nsec - app->prof.t0.tv_nsec;
		const float tot_time_1 = 100.f / tot_time;

		for(unsigned m=0; m<app->num_mods; m++)
		{
			mod_t *mod = app->mods[m];

			const float mod_min = mod->prof.min * app->prof.count * tot_time_1;
			const float mod_avg = mod->prof.sum * tot_time_1;
			const float mod_max = mod->prof.max * app->prof.count * tot_time_1;

			// to nk
			LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
			if(answer)
			{
				const float vec [] = {
					mod_min, mod_avg, mod_max
				};

				LV2_Atom_Forge_Frame frame [1];
				LV2_Atom_Forge_Ref ref = synthpod_patcher_set_object(
					&app->regs, &app->forge, &frame[0], mod->urn, 0, app->regs.synthpod.module_profiling.urid); //TODO seqn
				if(ref)
					ref = lv2_atom_forge_vector(&app->forge, sizeof(float), app->forge.Float, 3, vec);
				if(ref)
				{
					synthpod_patcher_pop(&app->forge, frame, 1);
					_sp_app_to_ui_advance_atom(app, answer);
				}
				else
				{
					_sp_app_to_ui_overflow(app);
				}
			}
			else
			{
				_sp_app_to_ui_overflow(app);
			}

			mod->prof.min = UINT_MAX;
			mod->prof.max = 0;
			mod->prof.sum = 0;
		}

		{
			const float app_min = app->prof.min * app->prof.count * tot_time_1;
			const float app_avg = app->prof.sum * tot_time_1;
			const float app_max = app->prof.max * app->prof.count * tot_time_1;

			// to nk
			LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
			if(answer)
			{
				const float vec [] = {
					app_min, app_avg, app_max
				};

				LV2_Atom_Forge_Frame frame [1];
				LV2_Atom_Forge_Ref ref = synthpod_patcher_set_object(
					&app->regs, &app->forge, &frame[0], 0, 0, app->regs.synthpod.dsp_profiling.urid); //TODO subj, seqn
				if(ref)
					ref = lv2_atom_forge_vector(&app->forge, sizeof(float), app->forge.Float, 3, vec);
				if(ref)
				{
					synthpod_patcher_pop(&app->forge, frame, 1);
					_sp_app_to_ui_advance_atom(app, answer);
				}
				else
				{
					_sp_app_to_ui_overflow(app);
				}
			}
			else
			{
				_sp_app_to_ui_overflow(app);
			}

			app->prof.t0.tv_sec = app_t2.tv_sec;
			app->prof.t0.tv_nsec = app_t2.tv_nsec;
			app->prof.min = UINT_MAX;
			app->prof.max = 0;
			app->prof.sum = 0;
			app->prof.count = 0;
		}
	}

	// handle app ui post
	bool expected = true;
	const bool desired = false;
	if(atomic_compare_exchange_weak(&app->dirty, &expected, desired))
	{
		// to nk
		_sp_app_ui_set_modlist(app, 0, 0); //FIXME subj, seqn

		// recalculate concurrency
		_dsp_master_reorder(app);
		//printf("concurrency: %i\n", app->dsp_master.concurrent);
	}
}

void
sp_app_deactivate(sp_app_t *app)
{
	//TODO
}

void
sp_app_free(sp_app_t *app)
{
	if(!app)
		return;

	// deinit parallel processing
	dsp_master_t *dsp_master = &app->dsp_master;
	atomic_store_explicit(&dsp_master->kill, true, memory_order_release);
	//printf("finish\n");
	_dsp_master_post(dsp_master, dsp_master->num_slaves);
	for(unsigned i=0; i<dsp_master->num_slaves; i++)
	{
		dsp_slave_t *dsp_slave = &dsp_master->dsp_slaves[i];

		void *ret;
		pthread_join(dsp_slave->thread, &ret);
		sem_destroy(&dsp_slave->sem);
	}

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

	if(app->sratom)
		sratom_free(app->sratom);

	munlock(app, sizeof(sp_app_t));
	free(app);
}

bool
sp_app_bypassed(sp_app_t *app)
{
	return app->load_bundle && (app->block_state == BLOCKING_STATE_WAIT);
}

__realtime uint32_t
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

static void
_sp_app_reinitialize(sp_app_t *app)
{
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		_sp_app_mod_reinitialize(mod);
	}

	// refresh all connections
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *tar = &mod->ports[i];

			// set port buffer
			lilv_instance_connect_port(mod->inst, i, tar->base);
		}

		lilv_instance_activate(mod->inst);
	}
}

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

int
sp_app_com_event(sp_app_t *app, LV2_URID otype)
{
	// it is a com event, if it is not an official port protocol
	if(  (otype == app->regs.port.float_protocol.urid)
		|| (otype == app->regs.port.peak_protocol.urid)
		|| (otype == app->regs.port.atom_transfer.urid)
		|| (otype == app->regs.port.event_transfer.urid) )
		return 0;

	return 1;
}

//TODO keep in-line with sp_ui_bundle_load
void
sp_app_bundle_load(sp_app_t *app, const char *bundle_path,
	sp_to_request_t req, sp_to_advance_t adv, void *data)
{
	if(!bundle_path)
		return;

	// nk
	LV2_Atom *answer = _sp_request_atom(app, req, data);
	if(answer)
	{
		const LV2_URID bundle_urid = app->driver->map->map(app->driver->map->handle, bundle_path);

		const LV2_Atom_Forge_Ref ref = synthpod_patcher_copy(
			&app->regs, &app->forge, bundle_urid, 0, 0);
		if(ref)
		{
			_sp_advance_atom(app, answer, adv, data);
		}
		else
		{
			_sp_app_to_ui_overflow(app);
		}
	}
	else
	{
		_sp_app_to_ui_overflow(app);
	}
}

//TODO keep in-line with sp_ui_bundle_save
void
sp_app_bundle_save(sp_app_t *app, const char *bundle_path,
	sp_to_request_t req, sp_to_advance_t adv, void *data)
{
	if(!bundle_path)
		return;

	// nk
	LV2_Atom *answer = _sp_request_atom(app, req, data);
	if(answer)
	{
		const LV2_URID bundle_urid = app->driver->map->map(app->driver->map->handle, bundle_path);

		const LV2_Atom_Forge_Ref ref = synthpod_patcher_copy(
			&app->regs, &app->forge, 0, 0, bundle_urid);
		if(ref)
		{
			_sp_advance_atom(app, answer, adv, data);
		}
		else
		{
			_sp_app_to_ui_overflow(app);
		}
	}
	else
	{
		_sp_app_to_ui_overflow(app);
	}
}

// sort according to position
__realtime static void
_sp_app_mod_qsort(mod_t **A, int n)
{
	if(n < 2)
		return;

	const mod_t *p = A[0];

	int i = -1;
	int j = n;

	while(true)
	{
		do {
			i += 1;
		} while( (A[i]->pos.x < p->pos.x) || ( (A[i]->pos.x == p->pos.x) && (A[i]->pos.y < p->pos.y) ) );

		do {
			j -= 1;
		} while( (A[j]->pos.x > p->pos.x) || ( (A[j]->pos.x == p->pos.x) && (A[j]->pos.y > p->pos.y) ) );

		if(i >= j)
			break;

		mod_t *tmp = A[i];
		A[i] = A[j];
		A[j] = tmp;
	}

	_sp_app_mod_qsort(A, j + 1);
	_sp_app_mod_qsort(A + j + 1, n - j - 1);
}

/*
__non_realtime static void
_sp_app_order_dump(sp_app_t *app)
{
	for(unsigned m = 0; m < app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		printf("%u: %u\n", m, mod->uid);
	}
	printf("\n");
}
*/

__realtime void
_sp_app_order(sp_app_t *app)
{
	//_sp_app_order_dump(app);
	_sp_app_mod_qsort(app->mods, app->num_mods);
	//_sp_app_order_dump(app);

	_dsp_master_reorder(app);
}
