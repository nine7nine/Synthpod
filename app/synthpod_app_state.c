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

#include <synthpod_app_private.h>

typedef struct _atom_ser_t atom_ser_t;

struct _atom_ser_t {
	uint32_t size;
	uint8_t *buf;
	uint32_t offset;
};

__non_realtime static char *
_abstract_path(LV2_State_Map_Path_Handle instance, const char *absolute_path)
{
	const char *prefix_path = instance;

	const char *offset = absolute_path + strlen(prefix_path) + 1; // + 'file://' '/'

	return strdup(offset);
}

__non_realtime static char *
_absolute_path(LV2_State_Map_Path_Handle instance, const char *abstract_path)
{
	const char *prefix_path = instance;
	
	char *absolute_path = NULL;
	asprintf(&absolute_path, "%s/%s", prefix_path, abstract_path);

	return absolute_path;
}

__non_realtime static char *
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
				mkpath(path);

				free(path);
			}
		}
	}

	return absolute_path;
}

static LV2_Worker_Status
_preset_schedule_work_async(LV2_Worker_Schedule_Handle instance, uint32_t size, const void *data)
{
	mod_t *mod = instance;
	mod_worker_t *mod_worker = &mod->mod_worker;

	void *target;
	if((target = varchunk_write_request(mod_worker->state_to_worker, size)))
	{
		memcpy(target, data, size);
		varchunk_write_advance(mod_worker->state_to_worker, size);
		sem_post(&mod_worker->sem);

		return LV2_WORKER_SUCCESS;
	}
	else
	{
		sp_app_log_error(mod->app, "%s: failed to request buffer\n", __func__);
	}

	return LV2_WORKER_ERR_NO_SPACE;
}

static LV2_Worker_Status
_preset_schedule_work_sync(LV2_Worker_Schedule_Handle instance, uint32_t size, const void *data)
{
	mod_t *mod = instance;

	// call work:work synchronously
	return _sp_app_mod_worker_work_sync(mod, size, data);
}

static const LV2_Feature *const *
_preset_features(mod_t *mod, bool async)
{
	mod->state_worker.handle = mod;
	mod->state_worker.schedule_work = async
		? _preset_schedule_work_async
		: _preset_schedule_work_sync; // for state:loadDefaultState

	mod->state_feature_list[0].URI = LV2_WORKER__schedule;
	mod->state_feature_list[0].data = &mod->state_worker;

	mod->state_features[0] = &mod->state_feature_list[0];
	mod->state_features[1] = NULL;

	return (const LV2_Feature *const *)mod->state_features;
}

const LV2_Feature *const *
sp_app_state_features(sp_app_t *app, void *prefix_path)
{
	// construct LV2 state features
	app->make_path.handle = prefix_path;
	app->make_path.path = _make_path;

	app->map_path.handle = prefix_path;
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

__non_realtime static void
_state_set_value(const char *symbol, void *data,
	const void *value, uint32_t size, uint32_t type)
{
	mod_t *mod = data;
	sp_app_t *app = mod->app;

	LilvNode *symbol_uri = lilv_new_string(app->world, symbol);
	if(!symbol_uri)
	{
		sp_app_log_error(app, "%s: invalid symbol\n", __func__);
		return;
	}

	const LilvPort *port = lilv_plugin_get_port_by_symbol(mod->plug, symbol_uri);
	lilv_node_free(symbol_uri);
	if(!port)
	{
		sp_app_log_error(app, "%s: failed to get port by symbol\n", __func__);
		return;
	}

	uint32_t index = lilv_port_get_index(mod->plug, port);
	port_t *tar = &mod->ports[index];

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
	{
		sp_app_log_error(app, "%s: value of unknown type\n", __func__);
		return;
	}

	if(tar->type == PORT_TYPE_CONTROL)
	{
		control_port_t *control = &tar->control;

		// FIXME not rt-safe
		float *buf_ptr = PORT_BASE_ALIGNED(tar);
		*buf_ptr = val;
		control->last = tar->subscriptions
			? val - 0.1 // trigger notification
			: val; // don't trigger any notifications
		control->auto_dirty = true; // trigger output automation
		// FIXME not rt-safe

		_sp_app_port_spin_lock(control);
		control->stash = val;
		_sp_app_port_unlock(control);
	}
	else if(tar->type == PORT_TYPE_CV)
	{
		cv_port_t *cv = &tar->cv;

		// FIXME not rt-safe
		float *buf_ptr = PORT_BASE_ALIGNED(tar);
		for(unsigned i=0; i<app->driver->max_block_size; i++)
		{
			buf_ptr[i] = val;
		}
		// FIXME not rt-safe
	}
}

__non_realtime static const void *
_state_get_value(const char *symbol, void *data, uint32_t *size, uint32_t *type)
{
	mod_t *mod = data;
	sp_app_t *app = mod->app;
	
	LilvNode *symbol_uri = lilv_new_string(app->world, symbol);
	if(!symbol_uri)
	{
		sp_app_log_error(app, "%s: failed to create symbol URI\n", __func__);
		goto fail;
	}

	const LilvPort *port = lilv_plugin_get_port_by_symbol(mod->plug, symbol_uri);
	lilv_node_free(symbol_uri);
	if(!port)
	{
		sp_app_log_error(app, "%s: failed to get port by symbol\n", __func__);
		goto fail;
	}

	const uint32_t index = lilv_port_get_index(mod->plug, port);
	port_t *tar = &mod->ports[index];

	if(  (tar->direction == PORT_DIRECTION_INPUT)
		&& (tar->type == PORT_TYPE_CONTROL) )
	{
		control_port_t *control = &tar->control;

		_sp_app_port_spin_lock(control); // concurrent acess from worker and rt thread
		const float stash = control->stash;
		_sp_app_port_unlock(control);

		const void *ptr = NULL;
		if(control->is_toggled)
		{
			*size = sizeof(int32_t);
			*type = app->forge.Bool;
			control->i32 = floor(stash);
			ptr = &control->i32;
		}
		else if(control->is_integer)
		{
			*size = sizeof(int32_t);
			*type = app->forge.Int;
			control->i32 = floor(stash);
			ptr = &control->i32;
		}
		else // float
		{
			*size = sizeof(float);
			*type = app->forge.Float;
			control->f32 = stash;
			ptr = &control->f32;
		}

		return ptr;
	}

fail:
	*size = 0;
	*type = 0;
	return NULL;
}

void
_sp_app_state_preset_restore(sp_app_t *app, mod_t *mod, LilvState *const state,
	bool async)
{
	lilv_state_restore(state, mod->inst, _state_set_value, mod,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, _preset_features(mod, async));
}

int
_sp_app_state_preset_load(sp_app_t *app, mod_t *mod, const char *uri, bool async)
{
	LilvNode *preset = lilv_new_uri(app->world, uri);

	if(!preset) // preset not existing
	{
		sp_app_log_error(app, "%s: failed to create preset URI\n", __func__);
		return -1;
	}

	// load preset resource
	lilv_world_load_resource(app->world, preset);

	// load preset
	LilvState *state = lilv_state_new_from_world(app->world, app->driver->map,
		preset);

	// unload preset resource
	lilv_world_unload_resource(app->world, preset);

	// free preset node
	lilv_node_free(preset);

	if(!state)
	{
		sp_app_log_error(app, "%s: failed to get state from world\n", __func__);
		return -1;
	}

	_sp_app_state_preset_restore(app, mod, state, async);

	lilv_state_free(state);

	return 0; // success
}

LilvState *
_sp_app_state_preset_create(sp_app_t *app, mod_t *mod, const char *bndl)
{
	LilvState *const state = lilv_state_new_from_instance(mod->plug, mod->inst,
		app->driver->map, NULL, NULL, NULL, bndl,
		_state_get_value, mod, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE,
		NULL);

	return state;
}

int
_sp_app_state_preset_save(sp_app_t *app, mod_t *mod, const char *uri)
{
	const LilvNode *name_node = lilv_plugin_get_name(mod->plug);
	if(!name_node)
	{
		sp_app_log_error(app, "%s: failed to create preset URI\n", __func__);
		return -1;
	}

	const char *mod_label = lilv_node_as_string(name_node);
	char *prefix_path;
	if(asprintf(&prefix_path, "file:///home/hp/.lv2/%s_", mod_label) == -1) //FIXME
		prefix_path = NULL;

	if(prefix_path)
	{
		// replace white space with underline
		const char *whitespace = " \t\r\n";
		for(char *c = strpbrk(prefix_path, whitespace); c; c = strpbrk(c, whitespace))
			*c = '_';
	}

	const char *bndl = !strncmp(uri, "file://", 7)
		? uri + 7
		: uri;

	const char *target = prefix_path && !strncmp(uri, prefix_path, strlen(prefix_path))
		? uri + strlen(prefix_path)
		: uri;

	char *dest = strdup(target);
	if(dest)
	{
		char *term = strstr(dest, ".preset.lv2");
		if(term)
			*term = '\0';

		const char underline = '_';
		for(char *c = strchr(dest, underline); c; c = strchr(c, underline))
				*c = ' ';
	}

	mkpath((char *)uri);

	sp_app_log_note(app, "%s: preset save: <%s> as %s\n",
		__func__, uri, dest ? dest : target);

	LilvState *const state = _sp_app_state_preset_create(app, mod, bndl);

	if(state)
	{
		// set preset label
		lilv_state_set_label(state, dest ? dest : target);

		/*FIXME for lilv 0.24
		const char *comment = "this is a comment";
		lilv_state_set_metadata(state, app->regs.rdfs.comment.urid,
			comment, strlen(comment)+1, app->forge.String,
			LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

		const char *license = "http://opensource.org/licenses/Artistic-2.0";
		lilv_state_set_metadata(state, app->regs.doap.license.urid,
			license, strlen(license)+1, app->forge.URI,
			LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

		const LV2_URID bank = app->driver->map->map(app->driver->map->handle,
			"http://open-music-kontrollers.ch/banks#Bank1");
		lilv_state_set_metadata(state, app->regs.pset.preset_bank.urid,
			&bank, sizeof(LV2_URID), app->forge.URID,
			LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
		*/

		// actually save the state to disk
		lilv_state_save(app->world, app->driver->map, app->driver->unmap,
			state, NULL, bndl, "state.ttl");
		lilv_state_free(state);

		// reload presets for this module
		mod->presets = _preset_reload(app->world, &app->regs, mod->plug,
			mod->presets, bndl);
	}
	else
	{
		sp_app_log_error(app, "%s: failed to create state from instance\n", __func__);
	}

	// cleanup
	if(prefix_path)
		free(prefix_path);
	if(dest)
		free(dest);
	
	return 0; // success
}

#define CUINT8(str) ((const uint8_t *)(str))

static char *
synthpod_to_turtle(Sratom* sratom, LV2_URID_Unmap* unmap,
	uint32_t type, uint32_t size, const void *body)
{
	const char* base_uri = "file:///tmp/base/";
	SerdURI buri = SERD_URI_NULL;
	SerdNode base = serd_node_new_uri_from_string(CUINT8(base_uri), NULL, &buri);
	SerdEnv *env = serd_env_new(&base);
	if(env)
	{
		SerdChunk str = { .buf = NULL, .len = 0 };

		serd_env_set_prefix_from_strings(env, CUINT8("midi"), CUINT8(LV2_MIDI_PREFIX));
		serd_env_set_prefix_from_strings(env, CUINT8("atom"), CUINT8(LV2_ATOM_PREFIX));
		serd_env_set_prefix_from_strings(env, CUINT8("rdf"), CUINT8(RDF_PREFIX));
		serd_env_set_prefix_from_strings(env, CUINT8("xsd"), CUINT8(XSD_PREFIX));
		serd_env_set_prefix_from_strings(env, CUINT8("rdfs"), CUINT8(RDFS_PREFIX));
		serd_env_set_prefix_from_strings(env, CUINT8("lv2"), CUINT8(LV2_CORE_PREFIX));
		serd_env_set_prefix_from_strings(env, CUINT8("pset"), CUINT8(LV2_PRESETS_PREFIX));
		serd_env_set_prefix_from_strings(env, CUINT8("param"), CUINT8(LV2_PARAMETERS_PREFIX));
		serd_env_set_prefix_from_strings(env, CUINT8("state"), CUINT8(LV2_STATE_PREFIX));
		serd_env_set_prefix_from_strings(env, CUINT8("ui"), CUINT8(LV2_UI_PREFIX));
		serd_env_set_prefix_from_strings(env, CUINT8("spod"), CUINT8(SPOD_PREFIX));

		SerdWriter *writer = serd_writer_new(SERD_TURTLE,
			SERD_STYLE_ABBREVIATED | SERD_STYLE_RESOLVED | SERD_STYLE_CURIED,
			env, &buri, serd_chunk_sink, &str);

		if(writer)
		{
			// Write @prefix directives
			serd_env_foreach(env, (SerdPrefixSink)serd_writer_set_prefix, writer);

			sratom_set_sink(sratom, NULL,
				(SerdStatementSink)serd_writer_write_statement,
				(SerdEndSink)serd_writer_end_anon,
				writer);
			sratom_write(sratom, unmap, SERD_EMPTY_S, NULL, NULL, type, size, body);
			serd_writer_finish(writer);

			serd_writer_free(writer);
			serd_env_free(env);
			serd_node_free(&base);

			return (char *)serd_chunk_sink_finish(&str);
		}
		serd_env_free(env);
	}
	serd_node_free(&base);

	return NULL;
}

static inline void
_serialize_to_turtle(Sratom *sratom, LV2_URID_Unmap *unmap, const LV2_Atom *atom, const char *path)
{
	FILE *f = fopen(path, "wb");
	if(f)
	{
		char *ttl = synthpod_to_turtle(sratom, unmap,
			atom->type, atom->size, LV2_ATOM_BODY_CONST(atom));

		if(ttl)
		{
			fprintf(f, "%s", ttl);
			free(ttl);
		}

		fclose(f);
	}
}

static inline LV2_Atom_Object *
_deserialize_from_turtle(Sratom *sratom, LV2_URID_Unmap *unmap, const char *path)
{
	LV2_Atom_Object *obj = NULL;

	FILE *f = fopen(path, "rb");
	if(f)
	{
		fseek(f, 0, SEEK_END);
		long fsize = ftell(f);
		fseek(f, 0, SEEK_SET);

		char *ttl = malloc(fsize + 1);
		if(ttl)
		{
			if(fread(ttl, fsize, 1, f) == 1)
			{
				ttl[fsize] = 0;

				const char* base_uri = "file:///tmp/base/";

				SerdNode s = serd_node_from_string(SERD_URI, CUINT8(""));
				SerdNode p = serd_node_from_string(SERD_URI, CUINT8(LV2_STATE__state));
				obj = (LV2_Atom_Object *)sratom_from_turtle(sratom, base_uri, &s, &p, ttl);
			}

			free(ttl);
		}
	}

	return obj;
}

#undef CUINT8

// non-rt / rt
__non_realtime static LV2_State_Status
_state_store(LV2_State_Handle state, uint32_t key, const void *value,
	size_t size, uint32_t type, uint32_t flags)
{
	LV2_Atom_Forge *forge = state;

	if(!(flags & (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE)))
		return LV2_STATE_ERR_BAD_FLAGS;

	if(  lv2_atom_forge_key(forge, key)
		&& lv2_atom_forge_atom(forge, size, type)
		&& lv2_atom_forge_raw(forge, value, size) )
	{
		lv2_atom_forge_pad(forge, size);
		return LV2_STATE_SUCCESS;
	}

	return LV2_STATE_ERR_UNKNOWN;
}

__non_realtime const void *
sp_app_state_retrieve(LV2_State_Handle state, uint32_t key, size_t *size,
	uint32_t *type, uint32_t *flags)
{
	const LV2_Atom_Object *obj = state;

	const LV2_Atom *atom = NULL;
	lv2_atom_object_get(obj,
		key, &atom,
		0);

	if(atom)
	{
		*size = atom->size;
		*type = atom->type;
		*flags = LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE;
		return LV2_ATOM_BODY_CONST(atom);
	}

	*size = 0;
	*type = 0;
	*flags = 0;
	return NULL;
}

static void
_toggle_dirty(sp_app_t *app)
{
	while(true)
	{
		bool expected = false;
		const bool desired = true;
		if(atomic_compare_exchange_weak(&app->dirty, &expected, desired))
			break;
	}
}

int
_sp_app_state_bundle_load(sp_app_t *app, const char *bundle_path)
{
	//printf("_bundle_load: %s\n", bundle_path);

	if(!app->sratom)
	{
		sp_app_log_error(app, "%s: invalid sratom\n", __func__);
		return -1;
	}

	if(app->bundle_path)
		free(app->bundle_path);

	app->bundle_path = strdup(bundle_path);
	if(!app->bundle_path)
	{
		sp_app_log_error(app, "%s: path duplication failed\n", __func__);
		return -1;
	}

	char *state_dst = _make_path(app->bundle_path, "state.ttl");
	if(!state_dst)
	{
		sp_app_log_error(app, "%s: _make_path failed\n", __func__);
		return -1;
	}

	LV2_Atom_Object *obj = _deserialize_from_turtle(app->sratom, app->driver->unmap, state_dst);
	if(obj) // existing project
	{
		// restore state
		sp_app_restore(app, sp_app_state_retrieve, obj,
			LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, 
			sp_app_state_features(app, app->bundle_path));

		free(obj); // allocated by _deserialize_from_turtle
	}
	else if(!strcmp(bundle_path, SYNTHPOD_PREFIX"stereo")) // new project from UI
	{
		_sp_app_reset(app);
		_sp_app_populate(app);
		_toggle_dirty(app);
	}
	else // new project from CLI
	{
		_toggle_dirty(app);
	}

	free(state_dst);

	return 0; // success
}

__non_realtime static inline LV2_Atom_Forge_Ref
_sink(LV2_Atom_Forge_Sink_Handle handle, const void *buf, uint32_t size)
{
	atom_ser_t *ser = handle;

	const LV2_Atom_Forge_Ref ref = ser->offset + 1;

	const uint32_t new_offset = ser->offset + size;
	if(new_offset > ser->size)
	{
		uint32_t new_size = ser->size << 1;
		while(new_offset > new_size)
			new_size <<= 1;

		if(!(ser->buf = realloc(ser->buf, new_size)))
			return 0; // realloc failed

		ser->size = new_size;
	}

	memcpy(ser->buf + ser->offset, buf, size);
	ser->offset = new_offset;

	return ref;
}

__non_realtime static inline LV2_Atom *
_deref(LV2_Atom_Forge_Sink_Handle handle, LV2_Atom_Forge_Ref ref)
{
	atom_ser_t *ser = handle;

	const uint32_t offset = ref - 1;

	return (LV2_Atom *)(ser->buf + offset);
}

int
_sp_app_state_bundle_save(sp_app_t *app, const char *bundle_path)
{
	//printf("_bundle_save: %s\n", bundle_path);

	if(app->bundle_path)
		free(app->bundle_path);

	app->bundle_path = strdup(bundle_path);
	if(!app->bundle_path)
	{
		sp_app_log_error(app, "%s: path duplication failed\n", __func__);
		return -1;
	}

	char *manifest_dst = _make_path(app->bundle_path, "manifest.ttl");
	char *state_dst = _make_path(app->bundle_path, "state.ttl");
	if(manifest_dst && state_dst)
	{
		// create temporary forge
		LV2_Atom_Forge _forge;
		LV2_Atom_Forge *forge = &_forge;
		memcpy(forge, &app->forge, sizeof(LV2_Atom_Forge));

		LV2_Atom_Forge_Frame pset_frame;
		LV2_Atom_Forge_Frame state_frame;

		if(app->sratom)
		{
			atom_ser_t ser = { .size = 1024, .offset = 0 };
			ser.buf = malloc(ser.size);
			lv2_atom_forge_set_sink(forge, _sink, _deref, &ser);

			if(  ser.buf
				&& lv2_atom_forge_object(forge, &pset_frame, app->regs.synthpod.state.urid, app->regs.pset.preset.urid)
				&& lv2_atom_forge_key(forge, app->regs.core.applies_to.urid)
				&& lv2_atom_forge_urid(forge, app->regs.synthpod.stereo.urid)
				&& lv2_atom_forge_key(forge, app->regs.core.applies_to.urid)
				&& lv2_atom_forge_urid(forge, app->regs.synthpod.monoatom.urid)
				&& lv2_atom_forge_key(forge, app->regs.rdfs.see_also.urid)
				&& lv2_atom_forge_urid(forge, app->regs.synthpod.state.urid) )
			{
				lv2_atom_forge_pop(forge, &pset_frame);

				const LV2_Atom *atom = (const LV2_Atom *)ser.buf;
				_serialize_to_turtle(app->sratom, app->driver->unmap, atom, manifest_dst);
				free(ser.buf);
			}
			else
			{
				sp_app_log_error(app, "%s: forge failed\n", __func__);
			}

			ser.size = 4096;
			ser.offset = 0;
			ser.buf = malloc(ser.size);
			lv2_atom_forge_set_sink(forge, _sink, _deref, &ser);

			// try to extract label from bundle path
			char *rdfs_label = NULL;
			const char *from = strstr(app->bundle_path, "Synthpod_Stereo");
			const char *to = strstr(app->bundle_path, ".preset.lv2");
			if(from && to)
			{
				from += 15 + 1;
				const size_t sz = to - from;
				rdfs_label = malloc(sz + 1);
				if(rdfs_label)
				{
					strncpy(rdfs_label, from, sz);
					rdfs_label[sz] = '\0';
				}
			}

			if(  ser.buf
				&& lv2_atom_forge_object(forge, &pset_frame, app->regs.synthpod.null.urid, app->regs.pset.preset.urid)
				&& lv2_atom_forge_key(forge, app->regs.core.applies_to.urid)
				&& lv2_atom_forge_urid(forge, app->regs.synthpod.stereo.urid)
				&& lv2_atom_forge_key(forge, app->regs.core.applies_to.urid)
				&& lv2_atom_forge_urid(forge, app->regs.synthpod.monoatom.urid)

				&& lv2_atom_forge_key(forge, app->regs.rdfs.label.urid)
				&& lv2_atom_forge_string(forge, rdfs_label ? rdfs_label : app->bundle_path,
					rdfs_label ? strlen(rdfs_label) : strlen(app->bundle_path) )

				&& lv2_atom_forge_key(forge, app->regs.state.state.urid)
				&& lv2_atom_forge_object(forge, &state_frame, 0, 0) )
			{
				// store state
				sp_app_save(app, _state_store, forge,
					LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE,
					sp_app_state_features(app, app->bundle_path));

				lv2_atom_forge_pop(forge, &state_frame);
				lv2_atom_forge_pop(forge, &pset_frame);

				const LV2_Atom *atom = (const LV2_Atom *)ser.buf;
				_serialize_to_turtle(app->sratom, app->driver->unmap, atom, state_dst);
				free(ser.buf);
			}
			else
			{
				sp_app_log_error(app, "%s: forge failed\n", __func__);
			}

			if(rdfs_label)
				free(rdfs_label);
		}
		else
		{
			sp_app_log_error(app, "%s: invalid sratom\n", __func__);
		}

		free(manifest_dst);
		free(state_dst);

		return LV2_STATE_SUCCESS;
	}
	else
	{
		sp_app_log_error(app, "%s: _make_path failed\n", __func__);
	}
	
	if(manifest_dst)
		free(manifest_dst);
	if(state_dst)
		free(state_dst);

	return LV2_STATE_ERR_UNKNOWN;
}

LV2_State_Status
sp_app_save(sp_app_t *app, LV2_State_Store_Function store,
	LV2_State_Handle hndl, uint32_t flags, const LV2_Feature *const *features)
{
	const LV2_State_Make_Path *make_path = NULL;
	const LV2_State_Map_Path *map_path = NULL;

	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_STATE__makePath))
			make_path = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_STATE__mapPath))
			map_path = features[i]->data;
	}

	if(!make_path)
	{
		sp_app_log_error(app, "%s: LV2_STATE__makePath not supported\n", __func__);
		return LV2_STATE_ERR_UNKNOWN;
	}
	if(!map_path)
	{
		sp_app_log_error(app, "%s: LV2_STATE__mapPath not supported\n", __func__);
		return LV2_STATE_ERR_UNKNOWN;
	}

	/*FIXME
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
				// remove whole bundle tree
				rmrf_const(root_path);

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
				remove(manifest_path);

				free(manifest_path);
			}
		}
	}
*/

	// store minor version
	const int32_t minor_version = SYNTHPOD_MINOR_VERSION;
	store(hndl, app->regs.core.minor_version.urid,
		&minor_version, sizeof(int32_t), app->forge.Int,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

	// store micro version
	const int32_t micro_version = SYNTHPOD_MICRO_VERSION;
	store(hndl, app->regs.core.micro_version.urid,
		&micro_version, sizeof(int32_t), app->forge.Int,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

	// create temporary forge
	LV2_Atom_Forge _forge;
	LV2_Atom_Forge *forge = &_forge;
	memcpy(forge, &app->forge, sizeof(LV2_Atom_Forge));

	atom_ser_t ser = { .size = 4096, .offset = 0 };
	ser.buf = malloc(ser.size);
	lv2_atom_forge_set_sink(forge, _sink, _deref, &ser);

	if(ser.buf)
	{
		// spod:moduleList
		{
			LV2_Atom_Forge_Ref ref;
			LV2_Atom_Forge_Frame mod_list_frame;

			if( (ref = lv2_atom_forge_object(forge, &mod_list_frame, 0, 0)) )
			{
				for(unsigned m=0; m<app->num_mods; m++)
				{
					mod_t *mod = app->mods[m];

					char dir [128];
					snprintf(dir, sizeof(dir), "%s/", mod->urn_uri); // only save in new format

					char *path = make_path->path(make_path->handle, dir);
					if(path)
					{
						LilvState *const state = lilv_state_new_from_instance(mod->plug, mod->inst,
							app->driver->map, NULL, NULL, NULL, path,
							_state_get_value, mod, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, NULL);

						if(state)
						{
							lilv_state_set_label(state, "state"); //TODO use path prefix?
							lilv_state_save(app->world, app->driver->map, app->driver->unmap,
								state, NULL, path, "state.ttl");
							lilv_state_free(state);
						}
						else
							sp_app_log_error(app, "%s: invalid state\n", __func__);

						free(path);
					}
					else
						sp_app_log_error(app, "%s: invalid path\n", __func__);

					const LV2_URID uri_urid = app->driver->map->map(app->driver->map->handle, mod->uri_str);

					LV2_Atom_Forge_Frame mod_frame;
					if(  ref
						&& lv2_atom_forge_key(forge, mod->urn)
						&& lv2_atom_forge_object(forge, &mod_frame, 0, uri_urid) )
					{
						ref = lv2_atom_forge_key(forge, app->regs.synthpod.module_position_x.urid)
							&& lv2_atom_forge_float(forge, mod->pos.x);

						if(ref)
						{
							ref = lv2_atom_forge_key(forge, app->regs.synthpod.module_position_y.urid)
								&& lv2_atom_forge_float(forge, mod->pos.y);
						}

						if(ref && strlen(mod->alias))
						{
							ref = lv2_atom_forge_key(forge, app->regs.synthpod.module_alias.urid)
								&& lv2_atom_forge_string(forge, mod->alias, strlen(mod->alias));
						}

						if(ref && mod->ui)
						{
							ref = lv2_atom_forge_key(forge, app->regs.ui.ui.urid)
								&& lv2_atom_forge_urid(forge, mod->ui);
						}

						if(ref && mod->visible)
						{
							ref = lv2_atom_forge_key(forge, app->regs.synthpod.module_visible.urid)
								&& lv2_atom_forge_urid(forge, mod->visible);
						}

						if(ref && mod->disabled)
						{
							ref = lv2_atom_forge_key(forge, app->regs.synthpod.module_disabled.urid)
								&& lv2_atom_forge_bool(forge, mod->disabled);
						}

						if(ref)
							lv2_atom_forge_pop(forge, &mod_frame);
					}
					else
						sp_app_log_error(app, "%s: invalid mod\n", __func__);
				}

				if(ref)
					lv2_atom_forge_pop(forge, &mod_list_frame);
			}
			else
				sp_app_log_error(app, "%s: invalid spod:moduleList\n", __func__);

			const LV2_Atom *atom = (const LV2_Atom *)ser.buf;
			if(ref && atom)
			{
				store(hndl, app->regs.synthpod.module_list.urid,
					LV2_ATOM_BODY_CONST(atom), atom->size, atom->type,
					LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
			}
			else
				sp_app_log_error(app, "%s: invalid ref or atom\n", __func__);
		}

		// reset ser
		ser.offset = 0;
		lv2_atom_forge_set_sink(forge, _sink, _deref, &ser);

		// spod:connectionList
		{
			LV2_Atom_Forge_Ref ref;
			LV2_Atom_Forge_Frame conn_list_frame;

			if( (ref = lv2_atom_forge_tuple(forge, &conn_list_frame)) )
			{
				for(unsigned m=0; m<app->num_mods; m++)
				{
					mod_t *mod = app->mods[m];

					for(unsigned p=0; p<mod->num_ports; p++)
					{
						port_t *port = &mod->ports[p];

						// serialize port connections
						connectable_t *conn = _sp_app_port_connectable(port);
						if(conn)
						{
							for(int j=0; j<conn->num_sources; j++)
							{
								source_t *source = &conn->sources[j];
								port_t *source_port = source->port;

								LV2_Atom_Forge_Frame source_frame;
								if(  ref
									&& lv2_atom_forge_object(forge, &source_frame, 0, 0) )
								{
									ref = lv2_atom_forge_key(forge, app->regs.synthpod.source_module.urid)
										&& lv2_atom_forge_urid(forge, source_port->mod->urn)
										&& lv2_atom_forge_key(forge, app->regs.synthpod.source_symbol.urid)
										&& lv2_atom_forge_string(forge, source_port->symbol, strlen(source_port->symbol))
										&& lv2_atom_forge_key(forge, app->regs.synthpod.sink_module.urid)
										&& lv2_atom_forge_urid(forge, port->mod->urn)
										&& lv2_atom_forge_key(forge, app->regs.synthpod.sink_symbol.urid)
										&& lv2_atom_forge_string(forge, port->symbol, strlen(port->symbol))
										&& lv2_atom_forge_key(forge, app->regs.param.gain.urid)
										&& lv2_atom_forge_float(forge, source->gain);

									if(ref)
										lv2_atom_forge_pop(forge, &source_frame);
								}
							}
						}

					}
				}

				if(ref)
					lv2_atom_forge_pop(forge, &conn_list_frame);
			}
			else
				sp_app_log_error(app, "%s: invalid spod:connectionList\n", __func__);

			const LV2_Atom *atom = (const LV2_Atom *)ser.buf;
			if(ref && atom)
			{
				store(hndl, app->regs.synthpod.connection_list.urid,
					LV2_ATOM_BODY_CONST(atom), atom->size, atom->type,
					LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
			}
			else
				sp_app_log_error(app, "%s: invalid ref or atom\n", __func__);
		}

		// reset ser
		ser.offset = 0;
		lv2_atom_forge_set_sink(forge, _sink, _deref, &ser);

		// spod:nodeList
		{
			LV2_Atom_Forge_Ref ref;
			LV2_Atom_Forge_Frame node_list_frame;

			if( (ref = lv2_atom_forge_tuple(forge, &node_list_frame)) )
			{
				for(unsigned m1=0; m1<app->num_mods; m1++)
				{
					mod_t *snk_mod = app->mods[m1];

					for(unsigned m2=0; m2<app->num_mods; m2++)
					{
						mod_t *src_mod = app->mods[m2];
						bool mods_are_connected = false;
						float x = 0.f;
						float y = 0.f;

						for(unsigned p=0; p<snk_mod->num_ports; p++)
						{
							port_t *port = &snk_mod->ports[p];

							connectable_t *conn = _sp_app_port_connectable(port);
							if(conn)
							{
								for(int j=0; j<conn->num_sources; j++)
								{
									source_t *source = &conn->sources[j];
									port_t *src_port = source->port;

									if(src_port->mod == src_mod)
									{
										mods_are_connected = true;
										x = source->pos.x;
										y = source->pos.y;
										break;
									}
								}
							}

							if(mods_are_connected)
								break;
						}

						if(mods_are_connected)
						{
							LV2_Atom_Forge_Frame source_frame;
							if(  ref
								&& lv2_atom_forge_object(forge, &source_frame, 0, 0) )
							{
								ref = lv2_atom_forge_key(forge, app->regs.synthpod.source_module.urid)
									&& lv2_atom_forge_urid(forge, src_mod->urn)
									&& lv2_atom_forge_key(forge, app->regs.synthpod.sink_module.urid)
									&& lv2_atom_forge_urid(forge, snk_mod->urn)
									&& lv2_atom_forge_key(forge, app->regs.synthpod.node_position_x.urid)
									&& lv2_atom_forge_float(forge, x)
									&& lv2_atom_forge_key(forge, app->regs.synthpod.node_position_y.urid)
									&& lv2_atom_forge_float(forge, y);

								if(ref)
									lv2_atom_forge_pop(forge, &source_frame);
							}
						}
					}
				}

				if(ref)
					lv2_atom_forge_pop(forge, &node_list_frame);
			}
			else
				sp_app_log_error(app, "%s: invalid spod:nodeList\n", __func__);

			const LV2_Atom *atom = (const LV2_Atom *)ser.buf;
			if(ref && atom)
			{
				store(hndl, app->regs.synthpod.node_list.urid,
					LV2_ATOM_BODY_CONST(atom), atom->size, atom->type,
					LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
			}
			else
				sp_app_log_error(app, "%s: invalid ref or atom\n", __func__);
		}

		// reset ser
		ser.offset = 0;
		lv2_atom_forge_set_sink(forge, _sink, _deref, &ser);

		// spod:automationList
		{
			LV2_Atom_Forge_Ref ref;
			LV2_Atom_Forge_Frame conn_list_frame;

			if( (ref = lv2_atom_forge_tuple(forge, &conn_list_frame)) )
			{
				for(unsigned m=0; m<app->num_mods; m++)
				{
					mod_t *mod = app->mods[m];

					for(unsigned i = 0; i < MAX_AUTOMATIONS; i++)
					{
						auto_t *automation = &mod->automations[i];
						port_t *port = &mod->ports[automation->index];

						if(automation->type == AUTO_TYPE_MIDI)
						{
							midi_auto_t *mauto = &automation->midi;

							LV2_Atom_Forge_Frame auto_frame;
							if(  ref
								&& lv2_atom_forge_object(forge, &auto_frame, 0, app->regs.midi.Controller.urid) )
							{
								ref = lv2_atom_forge_key(forge, app->regs.synthpod.sink_module.urid)
									&& lv2_atom_forge_urid(forge, mod->urn)

									&& lv2_atom_forge_key(forge, app->regs.midi.channel.urid)
									&& lv2_atom_forge_int(forge, mauto->channel)

									&& lv2_atom_forge_key(forge, app->regs.midi.controller_number.urid)
									&& lv2_atom_forge_int(forge, mauto->controller)

									&& lv2_atom_forge_key(forge, app->regs.synthpod.source_min.urid)
									&& lv2_atom_forge_double(forge, automation->a)

									&& lv2_atom_forge_key(forge, app->regs.synthpod.source_max.urid)
									&& lv2_atom_forge_double(forge, automation->b)

									&& lv2_atom_forge_key(forge, app->regs.synthpod.sink_min.urid)
									&& lv2_atom_forge_double(forge, automation->c)

									&& lv2_atom_forge_key(forge, app->regs.synthpod.sink_max.urid)
									&& lv2_atom_forge_double(forge, automation->d)

									&& lv2_atom_forge_key(forge, app->regs.synthpod.source_enabled.urid)
									&& lv2_atom_forge_bool(forge, automation->src_enabled)

									&& lv2_atom_forge_key(forge, app->regs.synthpod.sink_enabled.urid)
									&& lv2_atom_forge_bool(forge, automation->snk_enabled);

								if(ref)
								{
									if(automation->property)
									{
										ref = lv2_atom_forge_key(forge, app->regs.patch.property.urid)
											&& lv2_atom_forge_urid(forge, automation->property)
											&& lv2_atom_forge_key(forge, app->regs.rdfs.range.urid)
											&& lv2_atom_forge_urid(forge, automation->range);
									}
									else
									{
										ref = lv2_atom_forge_key(forge, app->regs.synthpod.sink_symbol.urid)
											&& lv2_atom_forge_string(forge, port->symbol, strlen(port->symbol));
									}
								}

								if(ref)
									lv2_atom_forge_pop(forge, &auto_frame);
							}
						}
						else if(automation->type == AUTO_TYPE_OSC)
						{
							osc_auto_t *oauto = &automation->osc;

							LV2_Atom_Forge_Frame auto_frame;
							if(  ref
								&& lv2_atom_forge_object(forge, &auto_frame, 0, app->regs.osc.message.urid) )
							{
								ref = lv2_atom_forge_key(forge, app->regs.synthpod.sink_module.urid)
									&& lv2_atom_forge_urid(forge, mod->urn)

									&& lv2_atom_forge_key(forge, app->regs.osc.path.urid)
									&& lv2_atom_forge_string(forge, oauto->path, strlen(oauto->path))

									&& lv2_atom_forge_key(forge, app->regs.synthpod.source_min.urid)
									&& lv2_atom_forge_double(forge, automation->a)

									&& lv2_atom_forge_key(forge, app->regs.synthpod.source_max.urid)
									&& lv2_atom_forge_double(forge, automation->b)

									&& lv2_atom_forge_key(forge, app->regs.synthpod.sink_min.urid)
									&& lv2_atom_forge_double(forge, automation->c)

									&& lv2_atom_forge_key(forge, app->regs.synthpod.sink_max.urid)
									&& lv2_atom_forge_double(forge, automation->d)

									&& lv2_atom_forge_key(forge, app->regs.synthpod.source_enabled.urid)
									&& lv2_atom_forge_bool(forge, automation->src_enabled)

									&& lv2_atom_forge_key(forge, app->regs.synthpod.sink_enabled.urid)
									&& lv2_atom_forge_bool(forge, automation->snk_enabled);

								if(ref)
								{
									if(automation->property)
									{
										ref = lv2_atom_forge_key(forge, app->regs.patch.property.urid)
											&& lv2_atom_forge_urid(forge, automation->property)
											&& lv2_atom_forge_key(forge, app->regs.rdfs.range.urid)
											&& lv2_atom_forge_urid(forge, automation->range);
									}
									else
									{
										ref = lv2_atom_forge_key(forge, app->regs.synthpod.sink_symbol.urid)
											&& lv2_atom_forge_string(forge, port->symbol, strlen(port->symbol));
									}
								}

								if(ref)
									lv2_atom_forge_pop(forge, &auto_frame);
							}
						}
					}
				}

				if(ref)
					lv2_atom_forge_pop(forge, &conn_list_frame);
			}
			else
				sp_app_log_error(app, "%s: invalid spod:automationList\n", __func__);

			const LV2_Atom *atom = (const LV2_Atom *)ser.buf;
			if(ref && atom)
			{
				store(hndl, app->regs.synthpod.automation_list.urid,
					LV2_ATOM_BODY_CONST(atom), atom->size, atom->type,
					LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
			}
			else
				sp_app_log_error(app, "%s: invalid ref or atom\n", __func__);
		}

		free(ser.buf);

		return LV2_STATE_SUCCESS;
	}
	else
	{
		sp_app_log_error(app, "%s: invalid buffer\n", __func__);
	}

	return LV2_STATE_ERR_UNKNOWN;
}

static mod_t *
_mod_inject(sp_app_t *app, int32_t mod_uid, LV2_URID mod_urn, const LV2_Atom_Object *mod_obj,
	const LV2_State_Map_Path *map_path)
{
	if(  !lv2_atom_forge_is_object_type(&app->forge, mod_obj->atom.type)
		|| !mod_obj->body.otype)
		return NULL;

	const LV2_Atom_Float *mod_pos_x = NULL;
	const LV2_Atom_Float *mod_pos_y = NULL;
	const LV2_Atom_String *mod_alias = NULL;
	const LV2_Atom_URID *mod_ui = NULL;
	const LV2_Atom_Bool *mod_visible = NULL;
	const LV2_Atom_Bool *mod_disabled = NULL;
	lv2_atom_object_get(mod_obj,
		app->regs.synthpod.module_position_x.urid, &mod_pos_x,
		app->regs.synthpod.module_position_y.urid, &mod_pos_y,
		app->regs.synthpod.module_alias.urid, &mod_alias,
		app->regs.ui.ui.urid, &mod_ui,
		app->regs.synthpod.module_visible.urid, &mod_visible,
		app->regs.synthpod.module_disabled.urid, &mod_disabled,
		0);

	const char *mod_uri_str = app->driver->unmap->unmap(app->driver->unmap->handle, mod_obj->body.otype);
	mod_t *mod = _sp_app_mod_add(app, mod_uri_str, mod_urn);
	if(!mod)
	{
		sp_app_log_error(app, "%s: _sp_app_mod_add fialed\n", __func__);
		return NULL;
	}

	// inject module into module graph
	app->mods[app->num_mods] = mod;
	app->num_mods += 1;

	mod->pos.x = mod_pos_x && (mod_pos_x->atom.type == app->forge.Float)
		? mod_pos_x->body : 0.f;
	mod->pos.y = mod_pos_y && (mod_pos_y->atom.type == app->forge.Float)
		? mod_pos_y->body : 0.f;
	mod->visible = mod_visible && (mod_visible->atom.type == app->forge.URID)
		? mod_visible->body : 0;
	mod->disabled = mod_disabled && (mod_disabled->atom.type == app->forge.Bool)
		? mod_disabled->body : false;
	if(mod_alias)
		strncpy(mod->alias, LV2_ATOM_BODY_CONST(&mod_alias->atom), ALIAS_MAX - 1);
	mod->ui = mod_ui && (mod_ui->atom.type == app->forge.URID)
		? mod_ui->body : 0;

	mod->uid = mod_uid;

	char dir [128];
	if(mod->uid) // support for old foramt
		snprintf(dir, sizeof(dir), "%"PRIi32"/state.ttl", mod->uid);
	else
		snprintf(dir, sizeof(dir), "%s/state.ttl", mod->urn_uri);

	char *path = map_path->absolute_path(map_path->handle, dir);
	if(!path)
	{
		sp_app_log_error(app, "%s: invaild path\n", __func__);
		//TODO free mod
		return NULL;
	}

	// strip 'file://'
	const char *tmp = !strncmp(path, "file://", 7)
		? path + 7
		: path;

	LilvState *state = lilv_state_new_from_file(app->world,
		app->driver->map, NULL, tmp);

	if(state)
	{
		_sp_app_state_preset_restore(app, mod, state, false);
		lilv_state_free(state);
	}
	else
		sp_app_log_error(app, "%s: failed to load state from file\n", __func__);

	free(path);

	return mod;
}

LV2_Atom_Object *
sp_app_stash(sp_app_t *app, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle hndl, uint32_t flags, const LV2_Feature *const *features)
{
	const LV2_URID keys [7] = {
		app->regs.core.minor_version.urid,
		app->regs.core.micro_version.urid,
		app->regs.synthpod.module_list.urid,
		app->regs.synthpod.connection_list.urid,
		app->regs.synthpod.node_list.urid,
		app->regs.synthpod.automation_list.urid,
		app->regs.synthpod.graph.urid
	};

	uint8_t *buf = malloc(0x100000); //FIXME
	LV2_Atom_Forge forge = app->forge;
	lv2_atom_forge_set_buffer(&forge, buf, 0x100000);
	LV2_Atom_Forge_Frame frame;
	LV2_Atom_Forge_Ref ref;

	ref = lv2_atom_forge_object(&forge, &frame, 0, 0);

	for(int i = 0; i < 7; i++)
	{
		size_t size;
		uint32_t type;
		uint32_t _flags;

		const void *data = retrieve(hndl, keys[i], &size, &type, &_flags);

		if(!data)
			continue;

		if(ref)
			ref = lv2_atom_forge_key(&forge, keys[i]);
		if(ref)
			ref = lv2_atom_forge_atom(&forge, size, type);
		if(ref)
			ref = lv2_atom_forge_write(&forge, data, size);
	}

	if(ref)
		lv2_atom_forge_pop(&forge, &frame);


	return (LV2_Atom_Object *)buf;
}

LV2_State_Status
sp_app_restore(sp_app_t *app, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle hndl, uint32_t flags, const LV2_Feature *const *features)
{
	const LV2_State_Map_Path *map_path = NULL;

	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_STATE__mapPath))
			map_path = features[i]->data;
	}

	if(!map_path)
	{
		sp_app_log_error(app, "%s: LV2_STATE__mapPath not supported\n", __func__);
		return LV2_STATE_ERR_UNKNOWN;
	}

	size_t size;
	uint32_t _flags;
	uint32_t type;

	// retrieve minor version
	const int32_t *minor_version = retrieve(hndl, app->regs.core.minor_version.urid,
		&size, &type, &_flags);
	if(minor_version && (type == app->forge.Int) && (size == sizeof(int32_t)) )
	{
		//TODO check with running version
	}

	// retrieve micro version
	const int32_t *micro_version = retrieve(hndl, app->regs.core.micro_version.urid,
		&size, &type, &_flags);
	if(micro_version && (type == app->forge.Int) && (size == sizeof(int32_t)) )
	{
		//TODO check with running version
	}

	// retrieve spod:moduleList
	const LV2_Atom_Object_Body *mod_list_body = retrieve(hndl, app->regs.synthpod.module_list.urid,
		&size, &type, &_flags);
	if(  mod_list_body
		&& (type == app->forge.Object)
		&& (_flags & (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE)) )
	{
		_sp_app_reset(app);

		LV2_ATOM_OBJECT_BODY_FOREACH(mod_list_body, size, prop)
		{
			const int32_t mod_index = 0;
			const LV2_URID mod_urn = prop->key;
			LV2_Atom_Object *mod_obj = (LV2_Atom_Object *)&prop->value;

			mod_t *mod = _mod_inject(app, mod_index, mod_urn, mod_obj, map_path);
			if(!mod)
			{
				mod_obj->body.otype = app->regs.synthpod.placeholder.urid;
				mod = _mod_inject(app, mod_index, mod_urn, mod_obj, map_path);
				if(mod)
				{
					snprintf(mod->alias, sizeof(mod->alias), "%s", "!!! Failed to load !!!");
				}
			}
		}

		_sp_app_order(app);
	}
	else
		sp_app_log_error(app, "%s: invaild moduleList\n", __func__);

	// retrieve spod:connectionList
	const LV2_Atom_Object_Body *conn_list_body = retrieve(hndl, app->regs.synthpod.connection_list.urid,
		&size, &type, &_flags);
	if(  conn_list_body
		&& (type == app->forge.Tuple)
		&& (_flags & (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE)) )
	{
		LV2_ATOM_TUPLE_BODY_FOREACH(conn_list_body, size, item)
		{
			const LV2_Atom_Object *obj = (const LV2_Atom_Object *)item;

			if(!lv2_atom_forge_is_object_type(&app->forge, obj->atom.type))
				continue;

			_connection_list_add(app, obj);
		}
	}
	else
		sp_app_log_error(app, "%s: invaild connectionList\n", __func__);

	// retrieve spod:nodeList
	const LV2_Atom_Object_Body *node_list_body = retrieve(hndl, app->regs.synthpod.node_list.urid,
		&size, &type, &_flags);
	if(  node_list_body
		&& (type == app->forge.Tuple)
		&& (_flags & (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE)) )
	{
		LV2_ATOM_TUPLE_BODY_FOREACH(node_list_body, size, item)
		{
			const LV2_Atom_Object *obj = (const LV2_Atom_Object *)item;

			if(!lv2_atom_forge_is_object_type(&app->forge, obj->atom.type))
				continue;

			_node_list_add(app, obj);
		}
	}
	else
		sp_app_log_error(app, "%s: invaild nodeList \n", __func__);

	// retrieve spod:automationList
	const LV2_Atom_Object_Body *auto_list_body = retrieve(hndl, app->regs.synthpod.automation_list.urid,
		&size, &type, &_flags);
	if(  auto_list_body
		&& (type == app->forge.Tuple)
		&& (_flags & (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE)) )
	{
		LV2_ATOM_TUPLE_BODY_FOREACH(auto_list_body, size, item)
		{
			const LV2_Atom_Object *obj = (const LV2_Atom_Object *)item;

			if(!lv2_atom_forge_is_object_type(&app->forge, obj->atom.type))
				continue;

			_automation_list_add(app, obj);
		}
	}
	else
		sp_app_log_error(app, "%s: invaild automationList\n", __func__);

	// retrieve spod:graph // XXX old save format
	const LV2_Atom_Object_Body *graph_body = retrieve(hndl, app->regs.synthpod.graph.urid,
		&size, &type, &_flags);
	if(  graph_body
		&& (type == app->forge.Tuple)
		&& (_flags & (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE)) )
	{
		_sp_app_reset(app);

		LV2_ATOM_TUPLE_BODY_FOREACH(graph_body, size, mod_item)
		{
			LV2_Atom_Object *mod_obj = (LV2_Atom_Object *)mod_item;

			if(  !lv2_atom_forge_is_object_type(&app->forge, mod_obj->atom.type)
				|| !mod_obj->body.otype)
				continue;

			const LV2_Atom_Int *mod_index = NULL;
			lv2_atom_object_get(mod_obj,
				app->regs.core.index.urid, &mod_index,
				0);
		
			if(!mod_index || (mod_index->atom.type != app->forge.Int) )
				continue;

			const LV2_URID mod_urn = 0;
			mod_t *mod = _mod_inject(app, mod_index->body, mod_urn, mod_obj, map_path);
			if(!mod)
			{
				mod_obj->body.otype = app->regs.synthpod.placeholder.urid;
				mod = _mod_inject(app, mod_index->body, mod_urn, mod_obj, map_path);
				if(mod)
				{
					snprintf(mod->alias, sizeof(mod->alias), "%s", "!!! Failed to load !!!");
				}
			}
		}

		_sp_app_order(app);

		LV2_ATOM_TUPLE_BODY_FOREACH(graph_body, size, mod_item)
		{
			const LV2_Atom_Object *mod_obj = (const LV2_Atom_Object *)mod_item;

			if(  !lv2_atom_forge_is_object_type(&app->forge, mod_obj->atom.type)
				|| !mod_obj->body.otype)
				continue;

			const LV2_Atom_Int *mod_index = NULL;
			lv2_atom_object_get(mod_obj,
				app->regs.core.index.urid, &mod_index,
				0);
		
			if(!mod_index || (mod_index->atom.type != app->forge.Int) )
				continue;

			mod_t *mod = _sp_app_mod_get_by_uid(app, mod_index->body);
			if(!mod)
				continue;

			LV2_ATOM_OBJECT_FOREACH(mod_obj, item)
			{
				const LV2_Atom_Object *port_obj = (const LV2_Atom_Object *)&item->value;

				if(  (item->key != app->regs.core.port.urid)
					|| !lv2_atom_forge_is_object_type(&app->forge, port_obj->atom.type)
					|| (port_obj->body.otype != app->regs.core.Port.urid) )
					continue;

				const LV2_Atom_String *port_symbol = NULL;
				lv2_atom_object_get(port_obj,
					app->regs.core.symbol.urid, &port_symbol,
					0);

				if(!port_symbol || (port_symbol->atom.type != app->forge.String) )
					continue;

				const char *port_symbol_str = LV2_ATOM_BODY_CONST(port_symbol);

				for(unsigned i=0; i<mod->num_ports; i++)
				{
					port_t *port = &mod->ports[i];

					// search for matching port symbol
					if(strcmp(port_symbol_str, port->symbol))
						continue;

					LV2_ATOM_OBJECT_FOREACH(port_obj, sub)
					{
						const LV2_Atom_Object *source_obj = (const LV2_Atom_Object *)&sub->value;

						if(  (sub->key != app->regs.core.port.urid)
							|| !lv2_atom_forge_is_object_type(&app->forge, source_obj->atom.type)
							|| (source_obj->body.otype != app->regs.core.Port.urid) )
							continue;

						const LV2_Atom_String *source_symbol = NULL;
						const LV2_Atom_Int *source_index = NULL;
						lv2_atom_object_get(source_obj,
							app->regs.core.symbol.urid, &source_symbol,
							app->regs.core.index.urid, &source_index,
							0);

						if(  !source_symbol || (source_symbol->atom.type != app->forge.String)
							|| !source_index || (source_index->atom.type != app->forge.Int) )
							continue;

						const char *source_symbol_str = LV2_ATOM_BODY_CONST(source_symbol);

						mod_t *source = _sp_app_mod_get_by_uid(app, source_index->body);
						if(!source)
							continue;
					
						for(unsigned j=0; j<source->num_ports; j++)
						{
							port_t *tar = &source->ports[j];

							if(strcmp(source_symbol_str, tar->symbol))
								continue;

							_sp_app_port_connect(app, tar, port, 1.f);

							break;
						}
					}

					break;
				}
			}
		}
	}
	/*
	else
		sp_app_log_error(app, "%s: invaild graph\n", __func__);
	*/

	_toggle_dirty(app);

	return LV2_STATE_SUCCESS;
}
