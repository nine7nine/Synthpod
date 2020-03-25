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

#include <inttypes.h>
#include <unistd.h>

#include <synthpod_app_private.h>

#define ANSI_COLOR_BOLD    "\x1b[1m"
#define ANSI_COLOR_RESET   "\x1b[0m"

//tools.ietf.org/html/rfc4122 version 4
static void
urn_uuid_unparse_random(urn_uuid_t urn_uuid)
{
	uint8_t bytes [0x10];

	for(unsigned i=0x0; i<0x10; i++)
		bytes[i] = rand() & 0xff;

	bytes[6] = (bytes[6] & 0b00001111) | 0b01000000; // set four most significant bits of 7th byte to 0b0100
	bytes[8] = (bytes[8] & 0b00111111) | 0b10000000; // set two most significant bits of 9th byte to 0b10

	snprintf(urn_uuid, URN_UUID_LENGTH, "urn:uuid:%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		bytes[0x0], bytes[0x1], bytes[0x2], bytes[0x3],
		bytes[0x4], bytes[0x5],
		bytes[0x6], bytes[0x7],
		bytes[0x8], bytes[0x9],
		bytes[0xa], bytes[0xb], bytes[0xc], bytes[0xd], bytes[0xe], bytes[0xf]);
}

#if defined(_WIN32)
static inline char *
strsep(char **sp, char *sep)
{
	char *p, *s;
	if(sp == NULL || *sp == NULL || **sp == '\0')
		return(NULL);
	s = *sp;
	p = s + strcspn(s, sep);
	if(*p != '\0')
		*p++ = '\0';
	*sp = p;
	return(s);
}
#endif

//FIXME is actually __realtime
__non_realtime static int
_log_vprintf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, va_list args)
{
	mod_t *mod = handle;
	sp_app_t *app = mod->app;

	char prefix [128]; //TODO how big?
	char buf [1024]; //TODO how big?

	if(isatty(STDERR_FILENO))
		snprintf(prefix, sizeof(prefix), "{"ANSI_COLOR_BOLD"%s"ANSI_COLOR_RESET"} ", mod->urn_uri);
	else
		snprintf(prefix, sizeof(prefix), "{%s} ", mod->urn_uri);
	vsnprintf(buf, sizeof(buf), fmt, args);

	const char *sep = "\n";
	for(char *bufp = buf, *pch = strsep(&bufp, sep);
		pch;
		pch = strsep(&bufp, sep) )
	{
		if(strlen(pch) && app->driver->log)
			app->driver->log->printf(app->driver->log->handle, type, "%s%s\n", prefix, pch);
	}

	return 0;
}

//FIXME is actually __realtime
__non_realtime static int __attribute__((format(printf, 3, 4)))
_log_printf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(handle, type, fmt, args);
  va_end(args);

	return ret;
}

__realtime static LV2_Worker_Status
_schedule_work(LV2_Worker_Schedule_Handle handle, uint32_t size, const void *data)
{
	mod_t *mod = handle;
	mod_worker_t *mod_worker = &mod->mod_worker;

	void *target;
	if((target = varchunk_write_request(mod_worker->app_to_worker, size)))
	{
		memcpy(target, data, size);
		varchunk_write_advance(mod_worker->app_to_worker, size);
		sem_post(&mod_worker->sem);

		return LV2_WORKER_SUCCESS;
	}

	sp_app_log_trace(mod->app, "%s: failed to request buffer\n", __func__);

	return LV2_WORKER_ERR_NO_SPACE;
}

__non_realtime static char *
_mod_make_path(LV2_State_Make_Path_Handle instance, const char *abstract_path)
{
	mod_t *mod = instance;
	sp_app_t *app = mod->app;
	
	char *absolute_path = NULL;
	asprintf(&absolute_path, "%s/%s/%s", app->bundle_path, mod->urn_uri, abstract_path);

	// create leading directory tree, e.g. up to last '/'
	if(absolute_path)
	{
		const char *end = strrchr(absolute_path, '/');
		if(end)
		{
			char *path = strndup(absolute_path, end - absolute_path);
			if(path)
			{
				mkpath(path);

				free(path);
			}
		}
	}

	return absolute_path;
}

static inline int
_sp_app_mod_alloc_pool(pool_t *pool)
{
#if defined(_WIN32)
	pool->buf = _aligned_malloc(pool->size, 8);
#else
	posix_memalign(&pool->buf, 8, pool->size);
#endif
	if(pool->buf)
	{
		memset(pool->buf, 0x0, pool->size);

		return 0;
	}

	return -1;
}

static inline void
_sp_app_mod_free_pool(pool_t *pool)
{
	if(pool->buf)
	{
		free(pool->buf);
		pool->buf = NULL;
	}
}

static inline void
_sp_app_mod_slice_pool(mod_t *mod, port_type_t type)
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
			tar->base = ptr;

			// initialize control buffers to default value
			if(tar->type == PORT_TYPE_CONTROL)
			{
				control_port_t *control = &tar->control;

				float *buf_ptr = PORT_BASE_ALIGNED(tar);
				*buf_ptr = control->dflt;
				control->stash = control->dflt;
				control->last = control->dflt;
				control->auto_dirty = true;
			}

			ptr += lv2_atom_pad_size(tar->size);
		}
	}
}

void
_sp_app_mod_reinitialize(mod_t *mod)
{
	sp_app_t *app = mod->app;

	// reinitialize all modules,
	lilv_instance_deactivate(mod->inst);
	lilv_instance_free(mod->inst);
	mod->inst = NULL;
	mod->handle = NULL;

	// mod->features should be up-to-date
	mod->inst = lilv_plugin_instantiate(mod->plug, app->driver->sample_rate, mod->features);
	mod->handle = lilv_instance_get_handle(mod->inst);

	//TODO should we re-get extension_data?

	// resize sample based buffers only (e.g. AUDIO and CV)
	_sp_app_mod_free_pool(&mod->pools[PORT_TYPE_AUDIO]);
	_sp_app_mod_free_pool(&mod->pools[PORT_TYPE_CV]);
		
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
	
	_sp_app_mod_alloc_pool(&mod->pools[PORT_TYPE_AUDIO]);
	_sp_app_mod_alloc_pool(&mod->pools[PORT_TYPE_CV]);

	_sp_app_mod_slice_pool(mod, PORT_TYPE_AUDIO);
	_sp_app_mod_slice_pool(mod, PORT_TYPE_CV);
}

static inline int 
_sp_app_mod_features_populate(sp_app_t *app, mod_t *mod)
{
	// populate feature list
	int nfeatures = 0;
	mod->feature_list[nfeatures].URI = LV2_URID__map;
	mod->feature_list[nfeatures++].data = app->driver->map;

	mod->feature_list[nfeatures].URI = LV2_URID__unmap;
	mod->feature_list[nfeatures++].data = app->driver->unmap;

	mod->feature_list[nfeatures].URI = XPRESS__voiceMap;
	mod->feature_list[nfeatures++].data = app->driver->xmap;

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

	mod->feature_list[nfeatures].URI = LV2_STATE__loadDefaultState;
	mod->feature_list[nfeatures++].data = NULL;

	if(app->driver->system_port_add && app->driver->system_port_del)
	{
		mod->feature_list[nfeatures].URI = SYNTHPOD_PREFIX"systemPorts";
		mod->feature_list[nfeatures++].data = NULL;
	}

	if(app->driver->osc_sched)
	{
		mod->feature_list[nfeatures].URI = LV2_OSC__schedule;
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

	mod->feature_list[nfeatures].URI = LV2_URI_MAP_URI;
	mod->feature_list[nfeatures++].data = &app->uri_to_id;

	mod->feature_list[nfeatures].URI = LV2_CORE__inPlaceBroken;
	mod->feature_list[nfeatures++].data = NULL;

	mod->feature_list[nfeatures].URI = LV2_INLINEDISPLAY__queue_draw;
	mod->feature_list[nfeatures++].data = &mod->idisp.queue_draw;

	mod->feature_list[nfeatures].URI = LV2_STATE__threadSafeRestore;
	mod->feature_list[nfeatures++].data = NULL;

	assert(nfeatures <= NUM_FEATURES);

	for(int i=0; i<nfeatures; i++)
		mod->features[i] = &mod->feature_list[i];
	mod->features[nfeatures] = NULL; // sentinel

	return nfeatures;
}

static const LilvPlugin *
_sp_app_mod_is_supported(sp_app_t *app, const char *uri)
{
	LilvNode *uri_node = lilv_new_uri(app->world, uri);
	if(!uri_node)
	{
		sp_app_log_trace(app, "%s: failed to create URI\n", __func__);
		return NULL;
	}

	const LilvPlugin *plug = lilv_plugins_get_by_uri(app->plugs, uri_node);
	lilv_node_free(uri_node);
			
	if(!plug)
	{
		sp_app_log_trace(app, "%s: failed to get plugin\n", __func__);
		return NULL;
	}

	const LilvNode *library_uri= lilv_plugin_get_library_uri(plug);
	if(!library_uri)
	{
		sp_app_log_trace(app, "%s: failed to get library URI\n", __func__);
		return NULL;
	}

	if(!app->driver->bad_plugins)
	{
		// check whether DSP and UI code is mixed into same binary
		bool mixed_binary = false;
		LilvUIs *all_uis = lilv_plugin_get_uis(plug);
		if(all_uis)
		{
			LILV_FOREACH(uis, ptr, all_uis)
			{
				const LilvUI *ui = lilv_uis_get(all_uis, ptr);
				if(!ui)
					continue;

				const LilvNode *ui_uri_node = lilv_ui_get_uri(ui);
				if(!ui_uri_node)
					continue;
				
				// nedded if ui ttl referenced via rdfs#seeAlso
				lilv_world_load_resource(app->world, ui_uri_node);
		
				const LilvNode *ui_library_uri= lilv_ui_get_binary_uri(ui);
				if(ui_library_uri && lilv_node_equals(library_uri, ui_library_uri))
					mixed_binary = true; // this is bad, we don't support that

				lilv_world_unload_resource(app->world, ui_uri_node);
			}

			lilv_uis_free(all_uis);
		}

		if(mixed_binary)
		{
			sp_app_log_error(app, "%s: <%s> NOT supported: mixes DSP and UI code in same binary.\n", __func__, uri);
			return NULL;
		}
	}

	// populate feature list in dummy mod structure
	mod_t mod;
	const int nfeatures = _sp_app_mod_features_populate(app, &mod);

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
				if(!strcmp(mod.feature_list[f].URI, required_feature_uri))
				{
					missing_required_feature = 0;
					break;
				}
			}

			if(missing_required_feature)
			{
				sp_app_log_error(app, "%s: <%s> NOT supported: requires feature <%s>\n",
					__func__, uri, required_feature_uri);
				break;
			}
		}
		lilv_nodes_free(required_features);
	}

	if(missing_required_feature)
		return NULL;

	return plug;
}

__non_realtime static LV2_Worker_Status
_sp_worker_respond_async(LV2_Worker_Respond_Handle handle, uint32_t size, const void *data)
{
	mod_t *mod = handle;
	mod_worker_t *mod_worker = &mod->mod_worker;

	void *payload;
	if((payload = varchunk_write_request(mod_worker->app_from_worker, size)))
	{
		memcpy(payload, data, size);
		varchunk_write_advance(mod_worker->app_from_worker, size);
		return LV2_WORKER_SUCCESS;
	}

	sp_app_log_error(mod->app, "%s: failed to request buffer\n", __func__);

	return LV2_WORKER_ERR_NO_SPACE;
}

__non_realtime static LV2_Worker_Status
_sp_worker_respond_sync(LV2_Worker_Respond_Handle handle, uint32_t size, const void *data)
{
	mod_t *mod = handle;

	if(mod->worker.iface && mod->worker.iface->work_response)
		return mod->worker.iface->work_response(mod->handle, size, data);

	sp_app_log_error(mod->app, "%s: failed to call work:response\n", __func__);

	return LV2_WORKER_ERR_NO_SPACE;
}

__non_realtime LV2_Worker_Status
_sp_app_mod_worker_work_sync(mod_t *mod, size_t size, const void *payload)
{
	if(mod->worker.iface && mod->worker.iface->work)
	{
		return mod->worker.iface->work(mod->handle, _sp_worker_respond_sync, mod,
			size, payload);
	}

	sp_app_log_error(mod->app, "%s: failed to call work:work\n", __func__);

	return LV2_WORKER_ERR_NO_SPACE;
}

__non_realtime static void
_sp_app_mod_worker_work_async(mod_t *mod, size_t size, const void *payload)
{
	//printf("_mod_worker_work: %s, %zu\n", mod->urn_uri, size);

	if(mod->worker.iface && mod->worker.iface->work)
	{
		mod->worker.iface->work(mod->handle, _sp_worker_respond_async, mod,
			size, payload);
		//TODO check return status
	}
	else
	{
		sp_app_log_error(mod->app, "%s: failed to call work:work\n", __func__);
	}
}

__non_realtime static void *
_mod_worker_thread(void *data)
{
	mod_t *mod = data;
	mod_worker_t *mod_worker = &mod->mod_worker;

	// will inherit thread priority from main worker thread
	
	while(!atomic_load_explicit(&mod_worker->kill, memory_order_acquire))
	{
		sem_wait(&mod_worker->sem);

		const void *payload;
		size_t size;
		while((payload = varchunk_read_request(mod_worker->app_to_worker, &size)))
		{
			_sp_app_mod_worker_work_async(mod, size, payload);

			varchunk_read_advance(mod_worker->app_to_worker);
		}

		while((payload = varchunk_read_request(mod_worker->state_to_worker, &size)))
		{
			_sp_app_mod_worker_work_async(mod, size, payload);

			varchunk_read_advance(mod_worker->state_to_worker);
		}

		if(mod->idisp.iface && mod->idisp.iface->render)
		{
			if(atomic_exchange(&mod->idisp.draw_queued, false))
			{
				const uint32_t w = 256; //FIXME
				const uint32_t h = 256; //FIXME

				// lock surface
				while(atomic_flag_test_and_set(&mod->idisp.lock))
				{
					// spin
				}

				mod->idisp.surf = mod->idisp.iface->render(mod->handle, w, h);

				// unlock surface
				atomic_flag_clear(&mod->idisp.lock);
			}
		}
	}

	return NULL;
}

__realtime void
_sp_app_mod_queue_draw(mod_t *mod)
{
	mod_worker_t *mod_worker = &mod->mod_worker;

	if(mod->idisp.iface && mod->idisp.subscribed)
	{
		while(mod->idisp.counter >= mod->idisp.threshold)
		{
			mod->idisp.counter -= mod->idisp.threshold;

			atomic_store(&mod->idisp.draw_queued, true);
			sem_post(&mod_worker->sem);
		}
	}
}

__realtime static void
_mod_queue_draw(void *data)
{
	mod_t *mod = data;

	_sp_app_mod_queue_draw(mod);
}

mod_t *
_sp_app_mod_add(sp_app_t *app, const char *uri, LV2_URID urn, uint32_t created,
	const char *alias)
{
	const LilvPlugin *plug;

	if(!(plug = _sp_app_mod_is_supported(app, uri)))
	{
		sp_app_log_error(app, "%s: plugin is not supported\n", __func__);
		return NULL;
	}

	if(created == 0)
	{
		created = ++app->created;
	}

	mod_t *mod = calloc(1, sizeof(mod_t));
	if(!mod)
	{
		sp_app_log_error(app, "%s: allocation failed\n", __func__);
		return NULL;
	}

	mod->created = created;

	if(alias != NULL)
	{
		strncpy(mod->alias, alias, ALIAS_MAX - 1);
	}

	mod->needs_bypassing = false; // plugins with control ports only need no bypassing upon preset load
	mod->bypassed = false;
	atomic_init(&mod->dsp_client.ref_count, 0);

	// populate worker schedule
	mod->worker.schedule.handle = mod;
	mod->worker.schedule.schedule_work = _schedule_work;

	// populate log
	mod->log.handle = mod;
	mod->log.printf = _log_printf;
	mod->log.vprintf = _log_vprintf;

	mod->make_path.handle = mod;
	mod->make_path.path = _mod_make_path;

	mod->idisp.queue_draw.handle = mod;
	mod->idisp.queue_draw.queue_draw = _mod_queue_draw;
	atomic_init(&mod->idisp.draw_queued, false);
	mod->idisp.lock = (atomic_flag)ATOMIC_FLAG_INIT;
	mod->idisp.threshold = app->driver->sample_rate / app->driver->update_rate;
	mod->idisp.counter = mod->idisp.threshold; // triggers first render immediately
		
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

	mod->opts.options[4].context = LV2_OPTIONS_INSTANCE;
	mod->opts.options[4].subject = 0;
	mod->opts.options[4].key = app->regs.param.sample_rate.urid;
	mod->opts.options[4].size = sizeof(float);
	mod->opts.options[4].type = app->forge.Float;
	mod->opts.options[4].value = &app->driver->sample_rate;

	mod->opts.options[5].context = LV2_OPTIONS_INSTANCE;
	mod->opts.options[5].subject = 0;
	mod->opts.options[5].key = app->regs.ui.update_rate.urid;
	mod->opts.options[5].size = sizeof(float);
	mod->opts.options[5].type = app->forge.Float;
	mod->opts.options[5].value = &app->driver->update_rate;

	mod->opts.options[6].key = 0; // sentinel
	mod->opts.options[6].value = NULL; // sentinel

	_sp_app_mod_features_populate(app, mod);

	mod->app = app;
	if(urn == 0)
	{
		urn_uuid_unparse_random(mod->urn_uri);
		urn = app->driver->map->map(app->driver->map->handle, mod->urn_uri);
	}
	else
	{
		const char *urn_uri = app->driver->unmap->unmap(app->driver->unmap->handle, urn);
		strcpy(mod->urn_uri, urn_uri);
	}
	//printf("urn: %s\n", mod->urn_uri);
	mod->urn = urn;
	mod->plug = plug;
	mod->plug_urid = app->driver->map->map(app->driver->map->handle, uri);
	mod->num_ports = lilv_plugin_get_num_ports(plug) + 2; // + automation ports
	mod->inst = lilv_plugin_instantiate(plug, app->driver->sample_rate, mod->features);
	if(!mod->inst)
	{
		sp_app_log_error(app, "%s: instantiation failed\n", __func__);
		free(mod);
		return NULL;
	}
	mod->uri_str = strdup(uri); //TODO check
	mod->handle = lilv_instance_get_handle(mod->inst);
	mod->worker.iface = lilv_instance_get_extension_data(mod->inst,
		LV2_WORKER__interface);
	mod->opts.iface = lilv_instance_get_extension_data(mod->inst,
		LV2_OPTIONS__interface);
	const bool has_ro_canvas_graph = lilv_world_ask(app->world,
		lilv_plugin_get_uri(mod->plug), app->regs.patch.readable.node,
		app->regs.canvas.graph.node);
	const bool has_rw_canvas_graph = lilv_world_ask(app->world,
		lilv_plugin_get_uri(mod->plug), app->regs.patch.writable.node,
		app->regs.canvas.graph.node);
	if(has_ro_canvas_graph || has_rw_canvas_graph)
	{
		//sp_app_log_note(app, "%s: detected canvas:graph parameter\n", __func__);
	}
	else
	{
		mod->idisp.iface = lilv_instance_get_extension_data(mod->inst,
			LV2_INLINEDISPLAY__interface);
	}
	mod->state.iface = lilv_instance_get_extension_data(mod->inst,
		LV2_STATE__interface);
	mod->system_ports = lilv_plugin_has_feature(plug, app->regs.synthpod.system_ports.node);
	const bool load_default_state = lilv_plugin_has_feature(plug, app->regs.state.load_default_state.node);
	const bool thread_safe_restore = lilv_plugin_has_feature(plug, app->regs.state.thread_safe_restore.node);

	if(mod->state.iface) // plugins with state:interface need bypassing upon preset load
		mod->needs_bypassing = true;

	if(thread_safe_restore) // plugins with state:threadSafeRestore need no bypassing upon preset load
		mod->needs_bypassing = false;

	// clear pool sizes
	for(port_type_t pool=0; pool<PORT_TYPE_NUM; pool++)
		mod->pools[pool].size = 0;

	mod->ports = calloc(mod->num_ports, sizeof(port_t));
	if(!mod->ports)
	{
		sp_app_log_error(app, "%s: pool allocation failed\n", __func__);
		free(mod);
		return NULL; // failed to alloc ports
	}

	for(unsigned i=0; i<mod->num_ports - 2; i++) // - automation ports
	{
		port_t *tar = &mod->ports[i];
		const LilvPort *port = lilv_plugin_get_port_by_index(plug, i);

		tar->size = 0;
		tar->mod = mod;
		tar->index = i;
		tar->symbol = lilv_node_as_string(lilv_port_get_symbol(plug, port));
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
				const char *designation = NULL;
				const LilvNode *port_symbol_node = lilv_port_get_symbol(plug, port);
				LilvNode *port_name_node = lilv_port_get_name(plug, port);
				LilvNode *port_designation= lilv_port_get(plug, port, app->regs.core.designation.node);

				asprintf(&short_name, "#%"PRIu32"_%s",
					mod->urn, lilv_node_as_string(port_symbol_node));
				asprintf(&pretty_name, "#%"PRIu32" - %s",
					mod->urn, lilv_node_as_string(port_name_node));
				designation = port_designation ? lilv_node_as_string(port_designation) : NULL;
				const uint32_t order = (mod->created << 16) | tar->index;

				tar->sys.data = app->driver->system_port_add(app->data, tar->sys.type,
					short_name, pretty_name, designation,
					tar->direction == PORT_DIRECTION_OUTPUT, order);

				lilv_node_free(port_designation);
				lilv_node_free(port_name_node);
				free(short_name);
				free(pretty_name);
			}

			if(strlen(mod->alias) && app->driver->system_port_set)
			{
				app->driver->system_port_set(app->data, tar->sys.data,
					SYNTHPOD_PREFIX"#moduleAlias", mod->alias);
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
			tar->protocol = app->regs.port.peak_protocol.urid;
			tar->driver = &audio_port_driver;
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.cv.node))
		{
			tar->size = app->driver->max_block_size * sizeof(float);
			tar->type = PORT_TYPE_CV;
			tar->protocol = app->regs.port.peak_protocol.urid;
			tar->driver = &cv_port_driver;
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.control.node))
		{
			tar->size = sizeof(float);
			tar->type = PORT_TYPE_CONTROL;
			tar->protocol = app->regs.port.float_protocol.urid;
			tar->driver = &control_port_driver;

			control_port_t *control = &tar->control;
			control->is_integer = lilv_port_has_property(plug, port, app->regs.port.integer.node);
			control->is_toggled = lilv_port_has_property(plug, port, app->regs.port.toggled.node);
			control->lock = (atomic_flag)ATOMIC_FLAG_INIT;
		
			LilvNode *dflt_node;
			LilvNode *min_node;
			LilvNode *max_node;
			lilv_port_get_range(plug, port, &dflt_node, &min_node, &max_node);
			control->dflt = dflt_node ? lilv_node_as_float(dflt_node) : 0.f; //FIXME int, bool
			control->min = min_node ? lilv_node_as_float(min_node) : 0.f; //FIXME int, bool
			control->max = max_node ? lilv_node_as_float(max_node) : 1.f; //FIXME int, bool
			control->range = control->max - control->min;
			control->range_1 = 1.f / control->range;
			lilv_node_free(dflt_node);
			lilv_node_free(min_node);
			lilv_node_free(max_node);
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.atom.node)) 
		{
			tar->size = app->driver->seq_size;
			tar->type = PORT_TYPE_ATOM;
			tar->protocol = app->regs.port.event_transfer.urid; //FIXME handle atom_transfer
			tar->driver = &seq_port_driver; // FIXME handle atom_port_driver 

			tar->atom.buffer_type = PORT_BUFFER_TYPE_SEQUENCE; //FIXME properly discover this

			// does this port support patch:Message?
			tar->atom.patchable = lilv_port_supports_event(plug, port, app->regs.patch.message.node);

			// check whether this is a control port
			const LilvPort *control_port = lilv_plugin_get_port_by_designation(plug,
				tar->direction == PORT_DIRECTION_INPUT
					? app->regs.port.input.node
					: app->regs.port.output.node
					, app->regs.core.control.node);
			(void)control_port; //TODO use this?
		}
		else
		{
			sp_app_log_warning(app, "%s: unknown port type\n", __func__); //FIXME plugin should fail to initialize here

			free(mod->uri_str);
			free(mod->ports);
			free(mod);

			return NULL;
		}
		
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

	// automation input port //FIXME check
	{
		const unsigned i = mod->num_ports - 2;
		port_t *tar = &mod->ports[i];

		tar->mod = mod;
		tar->index = i;
		tar->symbol = "__automation__in__";
		tar->direction = PORT_DIRECTION_INPUT;

		tar->size = app->driver->seq_size;
		tar->type = PORT_TYPE_ATOM;
		tar->protocol = app->regs.port.event_transfer.urid;
		tar->driver = &seq_port_driver;

		tar->atom.buffer_type = PORT_BUFFER_TYPE_SEQUENCE;

		tar->sys.type = SYSTEM_PORT_NONE;
		tar->sys.data = NULL;

		// increase pool sizes
		mod->pools[tar->type].size += lv2_atom_pad_size(tar->size);
	}

	// automation output port //FIXME check
	{
		const unsigned i = mod->num_ports - 1;
		port_t *tar = &mod->ports[i];

		tar->mod = mod;
		tar->index = i;
		tar->symbol = "__automation__out__";
		tar->direction = PORT_DIRECTION_OUTPUT;

		tar->size = app->driver->seq_size;
		tar->type = PORT_TYPE_ATOM;
		tar->protocol = app->regs.port.event_transfer.urid;
		tar->driver = &seq_port_driver;

		tar->atom.buffer_type = PORT_BUFFER_TYPE_SEQUENCE;

		tar->sys.type = SYSTEM_PORT_NONE;
		tar->sys.data = NULL;

		// increase pool sizes
		mod->pools[tar->type].size += lv2_atom_pad_size(tar->size);
	}

	// allocate 8-byte aligned buffer per plugin and port type pool
	int alloc_failed = 0;
	for(port_type_t pool=0; pool<PORT_TYPE_NUM; pool++)
	{
		if(_sp_app_mod_alloc_pool(&mod->pools[pool]))
		{
			alloc_failed = 1;
			break;
		}
	}

	if(alloc_failed)
	{
		sp_app_log_error(app, "%s: pool tiling failed\n", __func__);

		for(port_type_t pool=0; pool<PORT_TYPE_NUM; pool++)
			_sp_app_mod_free_pool(&mod->pools[pool]);

		free(mod->uri_str);
		free(mod->ports);
		free(mod);

		return NULL;
	}

	// slice plugin buffer into per-port-type-and-direction regions for
	// efficient dereference in plugin instance
	for(port_type_t pool=0; pool<PORT_TYPE_NUM; pool++)
		_sp_app_mod_slice_pool(mod, pool);

	for(unsigned i=0; i<mod->num_ports - 2; i++)
	{
		port_t *tar = &mod->ports[i];

		// set port buffer
		lilv_instance_connect_port(mod->inst, i, tar->base);
	}

	// load presets
	mod->presets = lilv_plugin_get_related(plug, app->regs.pset.preset.node);
	
	// spawn worker thread
	if(mod->worker.iface || mod->idisp.iface)
	{
		mod_worker_t *mod_worker = &mod->mod_worker;

		sem_init(&mod_worker->sem, 0, 0);
		atomic_init(&mod_worker->kill, false);
		mod_worker->app_to_worker = varchunk_new(2048, true); //FIXME how big
		mod_worker->state_to_worker = varchunk_new(2048, true); //FIXME how big
		mod_worker->app_from_worker = varchunk_new(2048, true); //FIXME how big
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_create(&mod_worker->thread, &attr, _mod_worker_thread, mod);
	}

	// activate
	lilv_instance_activate(mod->inst);

	// load default state
	if(load_default_state && _sp_app_state_preset_load(app, mod, uri, false))
		sp_app_log_error(app, "%s: default state loading failed\n", __func__);

	// initialize profiling reference time
	mod->prof.sum = 0;

	return mod;
}

int
_sp_app_mod_del(sp_app_t *app, mod_t *mod)
{
	// deinit worker thread
	if(mod->worker.iface || mod->idisp.iface)
	{
		mod_worker_t *mod_worker = &mod->mod_worker;

		atomic_store_explicit(&mod_worker->kill, true, memory_order_release);
		sem_post(&mod_worker->sem);
		void *ret;
		pthread_join(mod_worker->thread, &ret);
		varchunk_free(mod_worker->app_to_worker);
		varchunk_free(mod_worker->state_to_worker);
		varchunk_free(mod_worker->app_from_worker);
		sem_destroy(&mod_worker->sem);
	}

	// deinit instance
	lilv_nodes_free(mod->presets);
	lilv_instance_deactivate(mod->inst);
	lilv_instance_free(mod->inst);

	// free memory
	for(port_type_t pool=0; pool<PORT_TYPE_NUM; pool++)
		_sp_app_mod_free_pool(&mod->pools[pool]);

	// unregister system ports
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->sys.data && app->driver->system_port_del)
			app->driver->system_port_del(app->data, port->sys.data);
	}

	// free ports
	if(mod->ports)
	{
		free(mod->ports);
	}

	if(mod->uri_str)
		free(mod->uri_str);

	free(mod);

	return 0; //success
}

mod_t *
_sp_app_mod_get_by_uid(sp_app_t *app, int32_t uid)
{
	for(unsigned m = 0; m < app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		if(mod->uid == uid)
			return mod;
	}

	return NULL;
}

void
_sp_app_mod_eject(sp_app_t *app, mod_t *mod)
{
	// eject module from graph
	app->num_mods -= 1;
	// remove mod from ->mods
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

		connectable_t *conn = _sp_app_port_connectable(port);
		if(conn)
		{
			// disconnect sources
			for(int s=0; s<conn->num_sources; s++)
				_sp_app_port_disconnect(app, conn->sources[s].port, port);
		}

		// disconnect sinks
		for(unsigned m=0; m<app->num_mods; m++)
		{
			for(unsigned p2=0; p2<app->mods[m]->num_ports; p2++)
				_sp_app_port_disconnect(app, port, &app->mods[m]->ports[p2]);
		}
	}

	// send request to worker thread
	size_t size = sizeof(job_t);
	job_t *job = _sp_app_to_worker_request(app, size);
	if(job)
	{
		job->request = JOB_TYPE_REQUEST_MODULE_DEL;
		job->mod = mod;
		_sp_app_to_worker_advance(app, size);
	}
	else
	{
		sp_app_log_error(app, "%s: failed requesting buffer\n", __func__);
	}

	_sp_app_order(app);

#if 0
	// signal to ui
	size = sizeof(transmit_module_del_t);
	transmit_module_del_t *trans = _sp_app_to_ui_request(app, size);
	if(trans)
	{
		_sp_transmit_module_del_fill(&app->regs, &app->forge, trans, size, mod->uid);
		_sp_app_to_ui_advance(app, size);
	}
#endif
}

static void
_sp_app_mod_reinitialize_soft(mod_t *mod)
{
	sp_app_t *app = mod->app;

	// reinitialize all modules,
	lilv_instance_deactivate(mod->inst);
	lilv_instance_free(mod->inst);

	mod->inst = NULL;
	mod->handle = NULL;

	// mod->features should be up-to-date
	mod->inst = lilv_plugin_instantiate(mod->plug, app->driver->sample_rate, mod->features);
	mod->handle = lilv_instance_get_handle(mod->inst);

	// refresh all connections
	for(unsigned i=0; i<mod->num_ports - 2; i++)
	{
		port_t *tar = &mod->ports[i];

		// set port buffer
		lilv_instance_connect_port(mod->inst, i, tar->base);
	}
}

void
_sp_app_mod_reinstantiate(sp_app_t *app, mod_t *mod)
{
	char *path;

	if(asprintf(&path, "file:///tmp/%s.preset.lv2", mod->urn_uri) == -1)
	{
		sp_app_log_note(app, "%s: failed to create temporary path\n", __func__);
		return;
	}

	LilvState *const state = _sp_app_state_preset_create(app, mod, path);
	free(path);

	if(state)
	{
		_sp_app_mod_reinitialize_soft(mod);

		lilv_instance_activate(mod->inst);

		_sp_app_state_preset_restore(app, mod, state, false);

		lilv_state_free(state);
	}
}
