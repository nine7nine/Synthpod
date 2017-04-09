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

#include <uuid.h>

#include <synthpod_app_private.h>

#define ANSI_COLOR_BOLD    "\x1b[1m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define URN_UUID_PREFIX "urn:uuid:"
#define URN_UUID_LENGTH 46

typedef char urn_uuid_t [URN_UUID_LENGTH];

static void
urn_uuid_unparse_random(char *buf)
{
	uuid_t uuid;
	uuid_generate_random(uuid);

	strncpy(buf, URN_UUID_PREFIX, strlen(URN_UUID_PREFIX));
	uuid_unparse(uuid, buf + strlen(URN_UUID_PREFIX));
}

//FIXME is actually __realtime
__non_realtime static int
_log_vprintf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, va_list args)
{
	mod_t *mod = handle;
	sp_app_t *app = mod->app;

	char prefix [32]; //TODO how big?
	char buf [1024]; //TODO how big?

	snprintf(prefix, 32, "("ANSI_COLOR_CYAN"DSP"ANSI_COLOR_RESET") {"ANSI_COLOR_BOLD"%i"ANSI_COLOR_RESET"} ", mod->uid);
	vsnprintf(buf, 1024, fmt, args);

	char *pch = strtok(buf, "\n");
	while(pch)
	{
		if(app->driver->log)
			app->driver->log->printf(app->driver->log->handle, type, "%s%s\n", prefix, pch);
		pch = strtok(NULL, "\n");
	}

	return 0;
}

//FIXME is actually __realtime
__non_realtime static int
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

	return LV2_WORKER_ERR_NO_SPACE;
}

__realtime static void *
_zero_sched_request(Zero_Worker_Handle handle, uint32_t size)
{
	mod_t *mod = handle;
	mod_worker_t *mod_worker = &mod->mod_worker;

	return varchunk_write_request(mod_worker->app_to_worker, size);
}

__realtime static Zero_Worker_Status
_zero_sched_advance(Zero_Worker_Handle handle, uint32_t written)
{
	mod_t *mod = handle;
	mod_worker_t *mod_worker = &mod->mod_worker;

	varchunk_write_advance(mod_worker->app_to_worker, written);
	sem_post(&mod_worker->sem);

	return ZERO_WORKER_SUCCESS;
}

static inline mod_t *
_mod_bsearch(u_id_t p, mod_t **a, unsigned n)
{
	unsigned start = 0;
	unsigned end = n;

	while(start < end)
	{
		const unsigned mid = start + (end - start)/2;
		mod_t *dst = a[mid];

		if(p < dst->uid)
			end = mid;
		else if(p > dst->uid)
			start = mid + 1;
		else
			return dst;
	}

	return NULL;
}

void
_sp_app_mod_qsort(mod_t **A, int n)
{
	if(n < 2)
		return;

	const mod_t *p = *A;

	int i = -1;
	int j = n;

	while(true)
	{
		do {
			i += 1;
		} while(A[i]->uid < p->uid);

		do {
			j -= 1;
		} while(A[j]->uid > p->uid);

		if(i >= j)
			break;

		mod_t *tmp = A[i];
		A[i] = A[j];
		A[j] = tmp;
	}

	_sp_app_mod_qsort(A, j + 1);
	_sp_app_mod_qsort(A + j + 1, n - j - 1);
}

__non_realtime static char *
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
		mlock(pool->buf, pool->size);
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
		munlock(pool->buf, pool->size);
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
			tar->buf = ptr;
			tar->base = tar->buf;

			// initialize control buffers to default value
			if(tar->type == PORT_TYPE_CONTROL)
			{
				float *buf_ptr = PORT_BASE_ALIGNED(tar);
				*buf_ptr = tar->dflt;
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
	mod->handle = lilv_instance_get_handle(mod->inst),

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

	mod->feature_list[nfeatures].URI = XPRESS_VOICE_MAP;
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

const LilvPlugin *
_sp_app_mod_is_supported(sp_app_t *app, const void *uri)
{
	LilvNode *uri_node = lilv_new_uri(app->world, uri);
	if(!uri_node)
		return NULL;

	const LilvPlugin *plug = lilv_plugins_get_by_uri(app->plugs, uri_node);
	lilv_node_free(uri_node);
			
	if(!plug)
		return NULL;

	const LilvNode *library_uri= lilv_plugin_get_library_uri(plug);
	if(!library_uri)
		return NULL;

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
			fprintf(stderr, "<%s> NOT supported: mixes DSP and UI code in same binary.\n", uri);
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
				fprintf(stderr, "<%s> NOT supported: required feature <%s>\n",
					uri, required_feature_uri);
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

	return LV2_WORKER_ERR_NO_SPACE;
}

__non_realtime static LV2_Worker_Status
_sp_worker_respond_sync(LV2_Worker_Respond_Handle handle, uint32_t size, const void *data)
{
	mod_t *mod = handle;

	if(mod->worker.iface && mod->worker.iface->work_response)
		return mod->worker.iface->work_response(mod->handle, size, data);

	return LV2_WORKER_ERR_NO_SPACE;
}

__non_realtime static void *
_sp_zero_request(Zero_Worker_Handle handle, uint32_t size)
{
	mod_t *mod = handle;
	mod_worker_t *mod_worker = &mod->mod_worker;

	return varchunk_write_request(mod_worker->app_from_worker, size);
}

__non_realtime static Zero_Worker_Status
_sp_zero_advance(Zero_Worker_Handle handle, uint32_t written)
{
	mod_t *mod = handle;
	mod_worker_t *mod_worker = &mod->mod_worker;

	varchunk_write_advance(mod_worker->app_from_worker, written);

	return ZERO_WORKER_SUCCESS;
}

__non_realtime LV2_Worker_Status
_sp_app_mod_worker_work_sync(mod_t *mod, size_t size, const void *payload)
{
	//TODO implement zero worker
	if(mod->worker.iface && mod->worker.iface->work)
	{
		return mod->worker.iface->work(mod->handle, _sp_worker_respond_sync, mod,
			size, payload);
		//TODO check return status
	}

	return LV2_WORKER_ERR_NO_SPACE;
}

__non_realtime static void
_sp_app_mod_worker_work_async(mod_t *mod, size_t size, const void *payload)
{
	//printf("_mod_worker_work: %u, %zu\n", mod->uid, size);

	// zero worker takes precedence over standard worker
	if(mod->zero.iface && mod->zero.iface->work)
	{
		mod->zero.iface->work(mod->handle, _sp_zero_request, _sp_zero_advance,
			mod, size, payload);
		//TODO check return status
	}
	else if(mod->worker.iface && mod->worker.iface->work)
	{
		mod->worker.iface->work(mod->handle, _sp_worker_respond_async, mod,
			size, payload);
		//TODO check return status
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
	}

	return NULL;
}

__realtime static void
_mod_queue_draw(void *data)
{
	mod_t *mod = data;

	if(mod->idisp.iface)
	{
		//FIXME signal worker thread to call :render
	}
}

mod_t *
_sp_app_mod_add(sp_app_t *app, const char *uri, u_id_t uid, LV2_URID urn)
{
	const LilvPlugin *plug;

	if(!(plug = _sp_app_mod_is_supported(app, uri)))
		return NULL;

	mod_t *mod = calloc(1, sizeof(mod_t));
	if(!mod)
		return NULL;
	mlock(mod, sizeof(mod_t));

	atomic_init(&mod->dsp_client.ref_count, 0);

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

	mod->idisp.queue_draw.handle = mod;
	mod->idisp.queue_draw.queue_draw = _mod_queue_draw;
		
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
	mod->uid = (uid != 0)
		? uid
		: app->uid++;
	if(urn == 0)
	{
		urn_uuid_t urn_uri;
		urn_uuid_unparse_random(urn_uri);
		urn = app->driver->map->map(app->driver->map->handle, urn_uri);
	}
	mod->urn = urn;
	mod->plug = plug;
	mod->plug_urid = app->driver->map->map(app->driver->map->handle, uri);
	mod->num_ports = lilv_plugin_get_num_ports(plug);
	mod->inst = lilv_plugin_instantiate(plug, app->driver->sample_rate, mod->features);
	if(!mod->inst)
	{
		free(mod);
		return NULL;
	}
	mod->uri_str = strdup(uri); //TODO check
	mod->handle = lilv_instance_get_handle(mod->inst);
	mod->worker.iface = lilv_instance_get_extension_data(mod->inst,
		LV2_WORKER__interface);
	mod->zero.iface = lilv_instance_get_extension_data(mod->inst,
		ZERO_WORKER__interface);
	mod->opts.iface = lilv_instance_get_extension_data(mod->inst,
		LV2_OPTIONS__interface);
	mod->idisp.iface = lilv_instance_get_extension_data(mod->inst,
		LV2_INLINEDISPLAY__interface);
	mod->system_ports = lilv_plugin_has_feature(plug, app->regs.synthpod.system_ports.node);
	bool load_default_state = lilv_plugin_has_feature(plug, app->regs.state.load_default_state.node);

	// clear pool sizes
	for(port_type_t pool=0; pool<PORT_TYPE_NUM; pool++)
		mod->pools[pool].size = 0;

	mod->ports = calloc(mod->num_ports, sizeof(port_t));
	if(!mod->ports)
	{
		free(mod);
		return NULL; // failed to alloc ports
	}
	mlock(mod->ports, mod->num_ports * sizeof(port_t));

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
		atomic_flag_clear_explicit(&tar->lock, memory_order_relaxed);

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

				asprintf(&short_name, "#%u_%s",
					mod->uid, lilv_node_as_string(port_symbol_node));
				asprintf(&pretty_name, "#%u - %s",
					mod->uid, lilv_node_as_string(port_name_node));
				designation = port_designation ? lilv_node_as_string(port_designation) : NULL;
				const uint32_t order = (mod->uid << 16) | tar->index;

				tar->sys.data = app->driver->system_port_add(app->data, tar->sys.type,
					short_name, pretty_name, designation,
					tar->direction == PORT_DIRECTION_OUTPUT, order);

				lilv_node_free(port_designation);
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
			tar->protocol = app->regs.port.peak_protocol.urid;
			tar->driver = &audio_port_driver;
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.cv.node))
		{
			tar->size = app->driver->max_block_size * sizeof(float);
			tar->type = PORT_TYPE_CV;
			tar->selected = 1;
			tar->monitored = 1;
			tar->protocol = app->regs.port.peak_protocol.urid;
			tar->driver = &cv_port_driver;
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.control.node))
		{
			tar->size = sizeof(float);
			tar->type = PORT_TYPE_CONTROL;
			tar->selected = 0;
			tar->monitored = 1;
			tar->protocol = app->regs.port.float_protocol.urid;
			tar->driver = &control_port_driver;
		
			LilvNode *dflt_node;
			LilvNode *min_node;
			LilvNode *max_node;
			lilv_port_get_range(mod->plug, tar->tar, &dflt_node, &min_node, &max_node);
			tar->dflt = dflt_node ? lilv_node_as_float(dflt_node) : 0.f;
			tar->min = min_node ? lilv_node_as_float(min_node) : 0.f;
			tar->max = max_node ? lilv_node_as_float(max_node) : 1.f;
			tar->range = tar->max - tar->min;
			tar->range_1 = 1.f / tar->range;
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
			tar->buffer_type = PORT_BUFFER_TYPE_SEQUENCE; //FIXME properly discover this
			tar->protocol = app->regs.port.event_transfer.urid; //FIXME handle atom_transfer
			tar->driver = &seq_port_driver; // FIXME handle atom_port_driver 

			// does this port support patch:Message?
			tar->patchable = lilv_port_supports_event(plug, port, app->regs.patch.message.node);

			// check whether this is a control port
			const LilvPort *control_port = lilv_plugin_get_port_by_designation(plug,
				tar->direction == PORT_DIRECTION_INPUT
					? app->regs.port.input.node
					: app->regs.port.output.node
					, app->regs.core.control.node);
			(void)control_port; //TODO use this?

			// only select supported event ports by default
			tar->selected = lilv_port_supports_event(plug, port, app->regs.port.midi.node)
				|| lilv_port_supports_event(plug, port, app->regs.port.time_position.node)
				|| lilv_port_supports_event(plug, port, app->regs.port.osc_event.node)
				|| lilv_port_supports_event(plug, port, app->regs.xpress.message.node);
		}
		else
		{
			fprintf(stderr, "unknown port type\n"); //FIXME plugin should fail to initialize here

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

	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *tar = &mod->ports[i];

		// set port buffer
		lilv_instance_connect_port(mod->inst, i, tar->base);

		// initialize atom sequence ports
		if(  (tar->type == PORT_TYPE_ATOM)
			&& (tar->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) )
		{
			LV2_Atom *atom = tar->base;
			atom->size = sizeof(LV2_Atom_Sequence_Body);
			atom->type = app->forge.Sequence;
		}
	}

	// load presets
	mod->presets = lilv_plugin_get_related(mod->plug, app->regs.pset.preset.node);
	
	// selection
	mod->selected = 1;
	mod->embedded = 1;

	// spawn worker thread
	if(mod->worker.iface)
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

	// load default state
	if(load_default_state && _sp_app_state_preset_load(app, mod, uri, false))
		fprintf(stderr, "default state loading failed\n");

	// activate
	lilv_instance_activate(mod->inst);

	// initialize profiling reference time
	mod->prof.sum = 0;

	return mod;
}

int
_sp_app_mod_del(sp_app_t *app, mod_t *mod)
{
	// deinit worker thread
	if(mod->worker.iface)
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
		munlock(mod->ports, mod->num_ports * sizeof(port_t));
		free(mod->ports);
	}

	if(mod->uri_str)
		free(mod->uri_str);

	munlock(mod, sizeof(mod_t));
	free(mod);

	return 0; //success
}

mod_t *
_sp_app_mod_get(sp_app_t *app, u_id_t uid)
{
	mod_t *mod = _mod_bsearch(uid, app->ords, app->num_mods);
	if(mod)
		return mod;

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
	// remove mod from ->ords
	for(unsigned m=0, offset=0; m<app->num_mods; m++)
	{
		if(app->ords[m] == mod)
			offset += 1;
		app->ords[m] = app->ords[m+offset];
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
	size_t size = sizeof(job_t);
	job_t *job = _sp_app_to_worker_request(app, size);
	if(job)
	{
		job->request = JOB_TYPE_REQUEST_MODULE_DEL;
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
