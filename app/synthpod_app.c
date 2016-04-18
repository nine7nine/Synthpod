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
	mlock(app, sizeof(sp_app_t));

	atomic_flag_clear_explicit(&app->dirty, memory_order_relaxed);

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

	sp_app_from_ui_fill(app);

	const char *uri_str;
	mod_t *mod;

	app->uid = 1;

	// inject source mod
	uri_str = SYNTHPOD_PREFIX"source";
	mod = _sp_app_mod_add(app, uri_str, 0);
	if(mod)
	{
		app->ords[app->num_mods] = mod;
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
		app->ords[app->num_mods] = mod;
		app->mods[app->num_mods] = mod;
		app->num_mods += 1;
	}
	else
		fprintf(stderr, "failed to create system sink\n");

	app->fps.bound = driver->sample_rate / 24; //TODO make this configurable
	app->fps.counter = 0;

	app->ramp_samples = driver->sample_rate / 10; // ramp over 0.1s

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

		// handle end of work
		if(mod->zero.iface && mod->zero.iface->end)
			mod->zero.iface->end(mod->handle);
		else if(mod->worker.iface && mod->worker.iface->end_run)
			mod->worker.iface->end_run(mod->handle);
	
		for(unsigned p=0; p<mod->num_ports; p++)
		{
			port_t *port = &mod->ports[p];

			// stash control port values
			if(port->stashing)
			{
				port->stashing = false;
				_sp_app_port_control_stash(port);
			}

			if(port->direction == PORT_DIRECTION_OUTPUT)
				continue; // ignore output ports

			// clear atom sequence input buffers
			if(  (port->type == PORT_TYPE_ATOM)
				&& (port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) )
			{
				LV2_Atom_Sequence *seq = PORT_BUF_ALIGNED(port);
				seq->atom.type = app->regs.port.sequence.urid;
				seq->atom.size = sizeof(LV2_Atom_Sequence_Body); // empty sequence
			}
			else if(port->type == PORT_TYPE_EVENT)
			{
				LV2_Event_Buffer *evbuf = PORT_BUF_ALIGNED(port);
				size_t offset = lv2_atom_pad_size(sizeof(LV2_Event_Buffer));
				lv2_event_buffer_reset(evbuf, 0, (uint8_t*)evbuf + offset);
				evbuf->capacity = port->size - offset;
			}
		}
	}

	if(del_me)
		_sp_app_mod_eject(app, del_me);
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

	// iterate over all modules
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		if(mod->bypassed)
			continue; // skip this plugin, it is loading a preset
	
		// multiplex multiple sources to single sink where needed
		for(unsigned p=0; p<mod->num_ports; p++)
		{
			port_t *port = &mod->ports[p];

			if(port->direction == PORT_DIRECTION_OUTPUT)
				continue; // not a sink

			if(SINK_IS_MULTIPLEX(port))
			{
				if(port->driver->multiplex)
					port->driver->multiplex(app, port, nsamples);
			}
			else if(SINK_IS_SIMPLEX(port))
			{
				if(port->driver->simplex)
					port->driver->simplex(app, port, nsamples);
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
				LV2_Atom_Sequence *seq = PORT_BASE_ALIGNED(port);
				seq->atom.type = app->regs.port.sequence.urid;
				seq->atom.size = port->size;
			}
			else if((port->type == PORT_TYPE_EVENT)
				&& (port->direction == PORT_DIRECTION_OUTPUT) )
			{
				LV2_Event_Buffer *evbuf = PORT_BUF_ALIGNED(port);
				size_t offset = lv2_atom_pad_size(sizeof(LV2_Event_Buffer));
				lv2_event_buffer_reset(evbuf, 0, (uint8_t*)evbuf + offset);
				evbuf->capacity = port->size - offset;
			}
		}

		struct timespec mod_t1;
		struct timespec mod_t2;
		clock_gettime(CLOCK_MONOTONIC, &mod_t1);

		// run plugin
		lilv_instance_run(mod->inst, nsamples);

		clock_gettime(CLOCK_MONOTONIC, &mod_t2);

		// profiling
		const unsigned run_time = (mod_t2.tv_sec - mod_t1.tv_sec)*1000000000
			+ mod_t2.tv_nsec - mod_t1.tv_nsec;
		mod->prof.sum += run_time;

		// handle mod ui post
		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];

			// no notification/subscription and no support for patch:Message
			const bool subscribed = port->subscriptions != 0;
			if(!(subscribed || port->patchable))
				continue; // skip this port

			/*
			if(port->patchable)
				printf("patchable %i %i %i\n", mod->uid, i, subscribed);
			*/

			if(port->driver->transfer && (port->driver->sparse_update ? sparse_update_timeout : true))
				port->driver->transfer(app, port, nsamples);
		}
	}

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
		const float sum_time_1 = 100.f / app->prof.sum;
		unsigned dsp_sum = 0;

		for(unsigned m=0; m<app->num_mods; m++)
		{
			mod_t *mod = app->mods[m];

			const float mod_avg = mod->prof.sum * sum_time_1;

			dsp_sum += mod->prof.sum;

			const size_t size = sizeof(transmit_module_profiling_t);
			transmit_module_profiling_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transmit_module_profiling_fill(&app->regs, &app->forge, trans, size,
					mod->uid, mod_avg);
				_sp_app_to_ui_advance(app, size);
			}

			mod->prof.sum = 0;
		}

		{
			const unsigned tot_time = (app_t2.tv_sec - app->prof.t0.tv_sec)*1000000000
				+ app_t2.tv_nsec - app->prof.t0.tv_nsec;
			const float tot_time_1 = 100.f / tot_time;

			const float app_min = app->prof.min * app->prof.count * tot_time_1;
			const float app_avg = app->prof.sum * tot_time_1;
			const float app_max = app->prof.max * app->prof.count * tot_time_1;
			const float app_ovh = 100.f - dsp_sum * sum_time_1;

			const size_t size = sizeof(transmit_dsp_profiling_t);
			transmit_dsp_profiling_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transmit_dsp_profiling_fill(&app->regs, &app->forge, trans, size,
					app_min, app_avg, app_max, app_ovh);
				_sp_app_to_ui_advance(app, size);
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
	const bool signaled = atomic_flag_test_and_set_explicit(&app->dirty, memory_order_acquire);
	atomic_flag_clear_explicit(&app->dirty, memory_order_release);
	if(signaled)
	{
		size_t size = sizeof(transmit_module_list_t);
		transmit_module_list_t *trans = _sp_app_to_ui_request(app, size);
		if(trans)
		{
			_sp_transmit_module_list_fill(&app->regs, &app->forge, trans, size);
			_sp_app_to_ui_advance(app, size);
		}
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

	ecore_file_shutdown();
	efreet_shutdown();
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
