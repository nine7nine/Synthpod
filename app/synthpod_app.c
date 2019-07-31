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

#include <osc.lv2/util.h>
#include <osc.lv2/forge.h>

#if defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
#	include <pthread_np.h>
typedef cpuset_t cpu_set_t;
#endif

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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
__non_realtime static uint32_t
_uri_to_id(LV2_URI_Map_Callback_Data handle, const char *_, const char *uri)
{
	sp_app_t *app = handle;

	LV2_URID_Map *map = app->driver->map;

	return map->map(map->handle, uri);
}
#pragma GCC diagnostic pop

__realtime static inline bool
_sp_app_has_source_automations(mod_t *mod)
{
	for(unsigned i = 0; i < MAX_AUTOMATIONS; i++)
	{
		auto_t *automation = &mod->automations[i];

		if(  (automation->type != AUTO_TYPE_NONE)
			&& automation->src_enabled )
		{
			return true; // has automations
		}
	}

	return false; // has no automations
}

__realtime static inline auto_t *
_sp_app_find_automation_for_port(mod_t *mod, uint32_t index)
{
	for(unsigned i = 0; i < MAX_AUTOMATIONS; i++)
	{
		auto_t *automation = &mod->automations[i];

		if(automation->type == AUTO_TYPE_NONE)
			continue; // skip empty slot

		if( (automation->property == 0) && (automation->index == index) )
			return automation; // found match
	}

	return NULL;
}

__realtime static inline auto_t *
_sp_app_find_automation_for_property(mod_t *mod, LV2_URID property)
{
	for(unsigned i = 0; i < MAX_AUTOMATIONS; i++)
	{
		auto_t *automation = &mod->automations[i];

		if(automation->type == AUTO_TYPE_NONE)
			continue; // skip empty slot

		if(automation->property == property)
			return automation; // found match
	}

	return NULL;
}

__realtime static inline LV2_Atom_Forge_Ref
_sp_app_automation_out(sp_app_t *app, LV2_Atom_Forge *forge, auto_t *automation, uint32_t frames, double value)
{
	LV2_Atom_Forge_Ref ref = 0;

	if(automation->type == AUTO_TYPE_MIDI)
	{
		midi_auto_t *mauto = &automation->midi;

		const uint8_t channel = (mauto->channel >= 0)
			? mauto->channel
			: 0;
		const uint8_t controller = (mauto->controller >= 0)
			? mauto->controller
			: 0;
		const uint8_t msg [3] = {0xb0 | channel, controller, round(value)};

		ref = lv2_atom_forge_frame_time(forge, frames);
		if(ref)
			ref = lv2_atom_forge_atom(forge, 3, app->regs.port.midi.urid);
		if(ref)
			ref = lv2_atom_forge_write(forge, msg, 3);
	}
	else if(automation->type == AUTO_TYPE_OSC)
	{
		osc_auto_t *oauto = &automation->osc;

		ref = lv2_atom_forge_frame_time(forge, frames);
		if(ref)
			ref = lv2_osc_forge_message_vararg(forge, &app->osc_urid, oauto->path, "d", value); //FIXME what type should be used?
	}

	return ref;
}

__realtime static inline void
_sp_app_process_single_run(mod_t *mod, uint32_t nsamples)
{
	sp_app_t *app = mod->app;

	struct timespec mod_t1;
	struct timespec mod_t2;
	cross_clock_gettime(&app->clk_mono, &mod_t1);

	// multiplex multiple sources to single sink where needed
	for(int p=mod->num_ports-1; p>=0; p--)
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

	mod_worker_t *mod_worker = &mod->mod_worker;
	if(mod_worker->app_from_worker)
	{
		const void *payload;
		size_t size;
		while((payload = varchunk_read_request(mod_worker->app_from_worker, &size)))
		{
			if(mod->worker.iface && mod->worker.iface->work_response)
			{
				mod->worker.iface->work_response(mod->handle, size, payload);
				//TODO check return status
			}

			varchunk_read_advance(mod_worker->app_from_worker);
		}

		// handle end of work
		if(mod->worker.iface && mod->worker.iface->end_run)
		{
			mod->worker.iface->end_run(mod->handle);
		}
	}

	// is module currently loading a preset asynchronously?
	if(!mod->bypassed)
	{
		// run plugin
		if(!mod->disabled)
		{
			lilv_instance_run(mod->inst, nsamples);
		}
	}

	//handle automation output
	{
		const unsigned ao = mod->num_ports - 1;
		port_t *auto_port = &mod->ports[ao];
		LV2_Atom_Sequence *seq = PORT_BASE_ALIGNED(auto_port);
		//const uint32_t capacity = seq->atom.size;
		const uint32_t capacity = PORT_SIZE(auto_port);
		LV2_Atom_Forge_Frame frame;

		LV2_Atom_Forge forge = app->forge; //FIXME do this only once
		lv2_atom_forge_set_buffer(&forge, (uint8_t *)seq, capacity);
		LV2_Atom_Forge_Ref ref = lv2_atom_forge_sequence_head(&forge, &frame, 0);

		if(_sp_app_has_source_automations(mod))
		{
			uint32_t t0 = 0;

			for(unsigned p=0; p<mod->num_ports; p++)
			{
				port_t *port = &mod->ports[p];

				if(port->type == PORT_TYPE_CONTROL)
				{
					const float *val = PORT_BASE_ALIGNED(port);

					if(  (*val != port->control.last)
						|| (port->control.auto_dirty) ) // has changed since last cycle
					{
						auto_t *automation = _sp_app_find_automation_for_port(mod, p);

						if(automation && automation->src_enabled)
						{
							const double value = (*val - automation->add) / automation->mul;

							if(ref)
								ref = _sp_app_automation_out(app, &forge, automation, t0, value);
						}

						port->control.auto_dirty = false;
					}
				}
				else if( (port->type == PORT_TYPE_ATOM)
					&& port->atom.patchable )
				{
					const LV2_Atom_Sequence *patch_seq = PORT_BASE_ALIGNED(port);

					LV2_ATOM_SEQUENCE_FOREACH(patch_seq, ev)
					{
						const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

						if(  lv2_atom_forge_is_object_type(&forge, obj->atom.type)
							&& (obj->body.otype == app->regs.patch.set.urid) ) //FIXME also consider patch:Put
						{
							const LV2_Atom_URID *patch_property = NULL;
							const LV2_Atom *patch_value = NULL;

							lv2_atom_object_get(obj,
								app->regs.patch.property.urid, &patch_property,
								app->regs.patch.value.urid, &patch_value,
								0);

							if(!patch_property || (patch_property->atom.type != forge.URID) || !patch_value)
								continue;

							auto_t *automation = _sp_app_find_automation_for_property(mod, patch_property->body);
							if(automation && automation->src_enabled && (patch_value->type == automation->range))
							{
								double val = 0.0;

								if(patch_value->type == forge.Bool)
									val = ((const LV2_Atom_Bool *)patch_value)->body;
								else if(patch_value->type == forge.Int)
									val = ((const LV2_Atom_Int *)patch_value)->body;
								else if(patch_value->type == forge.Long)
									val = ((const LV2_Atom_Long *)patch_value)->body;
								else if(patch_value->type == forge.Float)
									val = ((const LV2_Atom_Float *)patch_value)->body;
								else if(patch_value->type == forge.Double)
									val = ((const LV2_Atom_Double *)patch_value)->body;
								//FIXME support more types

								const double value = (val - automation->add) / automation->mul;

								if(ref)
									ref = _sp_app_automation_out(app, &forge, automation, ev->time.frames, value);

								t0 = ev->time.frames;
							}
						}
					}
				}
			}
		}

		if(ref)
			lv2_atom_forge_pop(&forge, &frame);
		else
		{
			lv2_atom_sequence_clear(seq);
			sp_app_log_trace(app, "%s: automation out buffer overflow\n", __func__);
		}
	}

	cross_clock_gettime(&app->clk_mono, &mod_t2);

	// profiling
	const unsigned run_time = (mod_t2.tv_sec - mod_t1.tv_sec)*1000000000
		+ mod_t2.tv_nsec - mod_t1.tv_nsec;
	mod->prof.sum += run_time;

	if(run_time < mod->prof.min)
		mod->prof.min = run_time;
	else if(run_time > mod->prof.max)
		mod->prof.max = run_time;
}

__realtime static void
_sync_midi_automation_to_ui(sp_app_t *app, mod_t *mod, auto_t *automation)
{
	LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
	if(answer)
	{
		const LV2_URID subj = 0; //FIXME
		const int32_t sn = 0; //FIXME
		const LV2_URID prop = app->regs.synthpod.automation_list.urid;
		port_t *port = &mod->ports[automation->index]; //FIXME handle prop

		LV2_Atom_Forge_Frame frame [3];
		LV2_Atom_Forge_Ref ref = synthpod_patcher_add_object(
			&app->regs, &app->forge, &frame[0], subj, sn, prop);

		if(ref)
			ref = _sp_app_forge_midi_automation(app, &frame[2], mod, port, automation);

		if(ref)
		{
			synthpod_patcher_pop(&app->forge, frame, 2);
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
}

__realtime static void
_sync_osc_automation_to_ui(sp_app_t *app, mod_t *mod, auto_t *automation)
{
	LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
	if(answer)
	{
		const LV2_URID subj = 0; //FIXME
		const int32_t sn = 0; //FIXME
		const LV2_URID prop = app->regs.synthpod.automation_list.urid;
		port_t *port = &mod->ports[automation->index]; //FIXME handle prop

		LV2_Atom_Forge_Frame frame [3];
		LV2_Atom_Forge_Ref ref = synthpod_patcher_add_object(
			&app->regs, &app->forge, &frame[0], subj, sn, prop);

		if(ref)
			ref = _sp_app_forge_osc_automation(app, &frame[2], mod, port, automation);

		if(ref)
		{
			synthpod_patcher_pop(&app->forge, frame, 2);
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

	// handle inline display
	if(mod->idisp.iface)
	{
		mod->idisp.counter += nsamples;

		// trylock
		if(!atomic_flag_test_and_set(&mod->idisp.lock))
		{
			const LV2_Inline_Display_Image_Surface *surf= mod->idisp.surf;
			if(surf)
			{
				// to nk
				LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
				if(answer)
				{
					LV2_Atom_Forge_Frame frame [3];

					LV2_Atom_Forge_Ref ref = synthpod_patcher_set_object(&app->regs, &app->forge, &frame[0],
						mod->urn, 0, app->regs.idisp.surface.urid); //TODO seqn
					if(ref)
						ref = lv2_atom_forge_tuple(&app->forge, &frame[1]);
					if(ref)
						ref = lv2_atom_forge_int(&app->forge, surf->width);
					if(ref)
						ref = lv2_atom_forge_int(&app->forge, surf->height);
					if(ref)
						ref = lv2_atom_forge_vector_head(&app->forge, &frame[2], sizeof(int32_t), app->forge.Int);
					if(surf->stride == surf->width * sizeof(uint32_t))
					{
						if(ref)
							ref = lv2_atom_forge_write(&app->forge, surf->data, surf->height * surf->stride);
					}
					else
					{
						for(int h = 0; h < surf->height; h++)
						{
							const uint8_t *row = &surf->data[surf->stride * h];

							if(ref)
								ref = lv2_atom_forge_raw(&app->forge, row, surf->width * sizeof(uint32_t));
						}
						if(ref)
							lv2_atom_forge_pad(&app->forge, surf->height * surf->width * sizeof(uint32_t));
					}

					if(ref)
						synthpod_patcher_pop(&app->forge, frame, 3);

					if(ref)
					{
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

				mod->idisp.surf = NULL; // invalidate
			}

			// unlock
			atomic_flag_clear(&mod->idisp.lock);
		}
	}

	// handle automation learn
	for(unsigned i = 0; i < MAX_AUTOMATIONS; i++)
	{
		auto_t *automation = &mod->automations[i];

		if(automation->sync)
		{
			if(automation->type == AUTO_TYPE_MIDI)
			{
				_sync_midi_automation_to_ui(app, mod, automation);
			}
			else if(automation->type == AUTO_TYPE_OSC)
			{
				_sync_osc_automation_to_ui(app, mod, automation);
			}

			automation->sync = false;
		}
	}
}

__realtime static inline int
_dsp_slave_fetch(dsp_master_t *dsp_master, int head)
{
	sp_app_t *app = (void *)dsp_master - offsetof(sp_app_t, dsp_master);

	const unsigned M = head;
	head = -1; // assume no more work left

	for(unsigned m=M; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];
		dsp_client_t *dsp_client = &mod->dsp_client;

		int expected = 0;
		const int desired = -1; // mark as done
		const bool match = atomic_compare_exchange_weak(&dsp_client->ref_count,
			&expected, desired);
		if(match) // needs to run now
		{
			_sp_app_process_single_run(mod, dsp_master->nsamples);

			for(unsigned j=0; j<dsp_client->num_sinks; j++)
			{
				dsp_client_t *sink = dsp_client->sinks[j];
				const int32_t ref_count = atomic_fetch_sub(&sink->ref_count, 1);
				assert(ref_count >= 0);
			}
		}
		else if(expected >= 0) // needs to run later
		{
			if(head == -1) // only set heading position once per loop
			{
				head = m;
			}
		}
	}

	return head;
}

__realtime static inline void
_dsp_slave_spin(sp_app_t *app, dsp_master_t *dsp_master, bool post)
{
	int head = 0;

	while(!atomic_load(&dsp_master->emergency_exit))
	{
		head = _dsp_slave_fetch(dsp_master, head);
		if(head == -1) // no more work left
		{
			break;
		}
	}

	if(post)
	{
		sem_post(&dsp_master->sem);
	}
}

__non_realtime static void *
_dsp_slave_thread(void *data)
{
	dsp_slave_t *dsp_slave = data;
	dsp_master_t *dsp_master = dsp_slave->dsp_master;
	sp_app_t *app = (void *)dsp_master - offsetof(sp_app_t, dsp_master);
	const int num = dsp_slave - dsp_master->dsp_slaves + 1;
	//printf("thread: %i\n", num);

	const pthread_t self = pthread_self();

	if(app->driver->audio_prio)
	{
		struct sched_param schedp;
		memset(&schedp, 0, sizeof(struct sched_param));
		schedp.sched_priority = app->driver->audio_prio - 1;

		if(pthread_setschedparam(self, SCHED_FIFO, &schedp))
			sp_app_log_error(app, "%s: pthread_setschedparam error\n", __func__);
	}

	if(app->driver->cpu_affinity)
	{
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(num, &cpuset);
		if(pthread_setaffinity_np(self, sizeof(cpu_set_t), &cpuset))
			sp_app_log_error(app, "%s: pthread_setaffinity_np error\n", __func__);
	}

	while(true)
	{
		sem_wait(&dsp_slave->sem);

		_dsp_slave_spin(app, dsp_master, true);

		if(atomic_load(&dsp_master->kill))
			break;

		//sched_yield();
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
_dsp_master_wait(sp_app_t *app, dsp_master_t *dsp_master, unsigned num)
{
	// derive timeout
	struct timespec to;
	cross_clock_gettime(&app->clk_real, &to);
	to.tv_sec += 1; // if workers have not finished in due 1s, do emergency exit!

	// wait for worker threads to have finished
	for(unsigned c = 0; c < num; )
	{
		if(sem_timedwait(&dsp_master->sem, &to) == -1)
		{
			switch(errno)
			{
				case ETIMEDOUT:
				{
					fprintf(stderr, "%s: taking emergency exit\n", __func__);
					atomic_store(&dsp_master->emergency_exit, true);
				} continue;
				case EINTR:
				{
					// nothing
				} continue;
			}
		}

		c++;
	}
}

__realtime static inline void
_dsp_master_process(sp_app_t *app, dsp_master_t *dsp_master, unsigned nsamples)
{
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];
		dsp_client_t *dsp_client = &mod->dsp_client;

		atomic_store(&dsp_client->ref_count, dsp_client->num_sources);
	}

	dsp_master->nsamples = nsamples;

	unsigned num_slaves = dsp_master->concurrent - 1;
	if(num_slaves > dsp_master->num_slaves)
		num_slaves = dsp_master->num_slaves;

	_dsp_master_post(dsp_master, num_slaves); // wake up other slaves
	_dsp_slave_spin(app, dsp_master, false); // runs jobs itself 
	_dsp_master_wait(app, dsp_master, num_slaves);
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
	{
		sp_app_log_error(app, "%s: failed to create system source\n", __func__);
	}

	// inject sink mod
	uri_str = SYNTHPOD_PREFIX"sink";
	mod = _sp_app_mod_add(app, uri_str, 0);
	if(mod)
	{
		app->mods[app->num_mods] = mod;
		app->num_mods += 1;
	}
	else
	{
		sp_app_log_error(app, "%s: failed to create system sink\n", __func__);
	}
}

sp_app_t *
sp_app_new(const LilvWorld *world, sp_app_driver_t *driver, void *data)
{
	if(!driver || !data)
		return NULL;

	srand(time(NULL)); // seed random number generator for UUID generator

	sp_app_t *app = calloc(1, sizeof(sp_app_t));
	if(!app)
		return NULL;

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
		LilvNode *synthpod_bundle = lilv_new_file_uri(app->world, NULL, SYNTHPOD_BUNDLE_DIR);
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
	cross_clock_init(&app->clk_mono, CROSS_CLOCK_MONOTONIC);
	cross_clock_init(&app->clk_real, CROSS_CLOCK_REALTIME);
	cross_clock_gettime(&app->clk_mono, &app->prof.t0);
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
	atomic_init(&dsp_master->emergency_exit, false);
	sem_init(&dsp_master->sem, 0, 0);
	dsp_master->num_slaves = driver->num_slaves;
	dsp_master->concurrent = dsp_master->num_slaves; // this is a safe fallback
	for(unsigned i=0; i<dsp_master->num_slaves; i++)
	{
		dsp_slave_t *dsp_slave = &dsp_master->dsp_slaves[i];

		dsp_slave->dsp_master = dsp_master;
		sem_init(&dsp_slave->sem, 0, 0);
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_create(&dsp_slave->thread, &attr, _dsp_slave_thread, dsp_slave);
	}

	lv2_osc_urid_init(&app->osc_urid, driver->map);

	return app;
}

void
sp_app_run_pre(sp_app_t *app, uint32_t nsamples)
{
	mod_t *del_me = NULL;

	cross_clock_gettime(&app->clk_mono, &app->prof.t1);

	// iterate over all modules
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		if(mod->delete_request && !del_me) // only delete 1 module at once
		{
			del_me = mod;
			mod->delete_request = false;
		}

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
				seq->atom.size = sizeof(LV2_Atom_Sequence_Body); // empty sequence
				seq->atom.type = app->regs.port.sequence.urid;
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

	if(atomic_exchange(&dsp_master->emergency_exit, false))
	{
		app->dsp_master.concurrent = dsp_master->num_slaves; // spin up all cores
		sp_app_log_trace(app, "%s: had to take emergency exit\n", __func__);
	}

	// profiling
	struct timespec app_t2;
	cross_clock_gettime(&app->clk_mono, &app_t2);

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

#if defined(USE_DYNAMIC_PARALLELIZER)
		// reset DAG weights
		for(unsigned m=0; m<app->num_mods; m++)
		{
			mod_t *mod = app->mods[m];
			dsp_client_t *dsp_client = &mod->dsp_client;

			dsp_client->weight = 0;
		}

		unsigned T1 = 0;
		unsigned Tinf = 0;

		// calculate DAG weights
		for(unsigned m1=0; m1<app->num_mods; m1++)
		{
			mod_t *mod1 = app->mods[m1];
			dsp_client_t *dsp_client1 = &mod1->dsp_client;

			unsigned gsw = 0; // greatest sink weight

			for(unsigned m2=0; m2<m1; m2++)
			{
				mod_t *mod2 = app->mods[m2];
				dsp_client_t *dsp_client2 = &mod2->dsp_client;

				for(unsigned s=0; s<dsp_client2->num_sinks; s++)
				{
					dsp_client_t *dsp_client3 = dsp_client2->sinks[s];

					if(dsp_client3 == dsp_client1) // mod2 is source of mod1
					{
						if(dsp_client2->weight > gsw)
							gsw = dsp_client2->weight;

						break;
					}
				}
			}

			const unsigned w1 = mod1->prof.sum;

			T1 += w1;
			dsp_client1->weight = gsw + w1;

			if(dsp_client1->weight > Tinf)
				Tinf = dsp_client1->weight;
		}

		// derive average parallelism
		const float parallelism = (float)T1 / Tinf; //TODO add some head-room?
		app->dsp_master.concurrent = ceilf(parallelism);

		// to nk
		{
			LV2_Atom *answer;
			answer = _sp_app_to_ui_request_atom(app);
			if(answer)
			{
				const int32_t cpus_used = (app->dsp_master.concurrent > app->dsp_master.num_slaves + 1)
					? app->dsp_master.num_slaves + 1
					: app->dsp_master.concurrent;

				LV2_Atom_Forge_Ref ref = synthpod_patcher_set(
					&app->regs, &app->forge, 0, 0, app->regs.synthpod.cpus_used.urid,
					sizeof(int32_t), app->forge.Int, &cpus_used); //TODO subj, seqn
				if(ref)
				{
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
		}
#endif

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
	atomic_store(&dsp_master->kill, true);
	//printf("finish\n");
	_dsp_master_post(dsp_master, dsp_master->num_slaves);
	_dsp_master_wait(app, dsp_master, dsp_master->num_slaves);

	for(unsigned i=0; i<dsp_master->num_slaves; i++)
	{
		dsp_slave_t *dsp_slave = &dsp_master->dsp_slaves[i];

		void *ret;
		pthread_join(dsp_slave->thread, &ret);
		sem_destroy(&dsp_slave->sem);
	}
	sem_destroy(&dsp_master->sem);

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

	cross_clock_deinit(&app->clk_mono);
	cross_clock_deinit(&app->clk_real);

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

		for(unsigned i=0; i<mod->num_ports - 2; i++)
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
						sp_app_log_error(app, "%s:setting of minBlockSize failed\n", __func__);
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
					sp_app_log_error(app, "%s:setting of nominalblockSize failed\n", __func__);
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

__non_realtime int
sp_app_log_error(sp_app_t *app, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = app->driver->log->vprintf(app->driver->log->handle, app->regs.log.error.urid, fmt, args);
  va_end(args);

	return ret;
}

__non_realtime int
sp_app_log_note(sp_app_t *app, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = app->driver->log->vprintf(app->driver->log->handle, app->regs.log.note.urid, fmt, args);
  va_end(args);

	return ret;
}

__non_realtime int
sp_app_log_warning(sp_app_t *app, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = app->driver->log->vprintf(app->driver->log->handle, app->regs.log.warning.urid, fmt, args);
  va_end(args);

	return ret;
}

__realtime int
sp_app_log_trace(sp_app_t *app, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = app->driver->log->vprintf(app->driver->log->handle, app->regs.log.trace.urid, fmt, args);
  va_end(args);

	return ret;
}

void
sp_app_set_bundle_path(sp_app_t *app, const char *bundle_path)
{
	if(app->bundle_path)
		free(app->bundle_path);

	app->bundle_path = strdup(bundle_path);
}

void
sp_app_bundle_reset(sp_app_t *app)
{
	_sp_app_reset(app);
}
