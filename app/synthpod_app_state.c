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

#if 1
	mod->state_feature_list[0].URI = LV2_WORKER__schedule;
	mod->state_feature_list[0].data = &mod->state_worker;

	mod->state_features[0] = &mod->state_feature_list[0];
	mod->state_features[1] = NULL;
#else
	mod->state_features[0] = NULL;
#endif

	return (const LV2_Feature *const *)mod->state_features;
}

static const LV2_Feature *const *
_state_features(sp_app_t *app, void *prefix_path)
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
		return;

	const LilvPort *port = lilv_plugin_get_port_by_symbol(mod->plug, symbol_uri);
	lilv_node_free(symbol_uri);
	if(!port)
		return;

	uint32_t index = lilv_port_get_index(mod->plug, port);
	port_t *tar = &mod->ports[index];

	if(tar->type == PORT_TYPE_CONTROL)
	{
		control_port_t *control = &tar->control;
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

		// FIXME not rt-safe
		float *buf_ptr = PORT_BASE_ALIGNED(tar);
		*buf_ptr = val;
		control->last = val - 0.1; // triggers notification
		// FIXME not rt-safe

		_sp_app_port_control_stash(tar); // FIXME needs blocking access
	}
}

__non_realtime static const void *
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
		&& (tar->type == PORT_TYPE_CONTROL) )
	{
		control_port_t *control = &tar->control;

		_sp_app_port_spin_lock(control); // concurrent acess from worker and rt threads

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

int
_sp_app_state_preset_load(sp_app_t *app, mod_t *mod, const char *uri, bool async)
{
	LilvNode *preset = lilv_new_uri(app->world, uri);

	if(!preset) // preset not existing
		return -1;

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
		return -1;

	lilv_state_restore(state, mod->inst, _state_set_value, mod,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, _preset_features(mod, async));

	lilv_state_free(state);

	return 0; // success
}

int
_sp_app_state_preset_save(sp_app_t *app, mod_t *mod, const char *target)
{
	const LilvNode *name_node = lilv_plugin_get_name(mod->plug);
	if(!name_node)
		return -1;

	const char *name = lilv_node_as_string(name_node);
	char *dir = NULL;
	char *filename = NULL;
	char *bndl = NULL;

	// create bundle path
	asprintf(&dir, "%s/.lv2/%s_%s.preset.lv2", app->dir.home, name, target);
	if(!dir)
		return -1;

	// replace spaces with underscore
	for(char *c = strstr(dir, ".lv2"); *c; c++)
		if(isspace(*c))
			*c = '_';

	mkpath(dir);

	// create plugin state file name
	asprintf(&filename, "%s.ttl", target);
	if(!filename)
	{
		free(dir);
		return -1;
	}
	
	// create bundle path URI
	asprintf(&bndl, "%s/", dir);
	if(!bndl)
	{
		free(dir);
		free(filename);
		return -1;
	}
	
	//printf("preset save: %s, %s, %s\n", dir, filename, bndl);

	LilvState *const state = lilv_state_new_from_instance(mod->plug, mod->inst,
		app->driver->map, NULL, NULL, NULL, dir,
		_state_get_value, mod, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE,
		NULL);

	if(state)
	{
		// actually save the state to disk
		lilv_state_set_label(state, target);

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

		lilv_state_save(app->world, app->driver->map, app->driver->unmap,
			state, NULL, dir, filename);
		lilv_state_free(state);

		// reload presets for this module
		mod->presets = _preset_reload(app->world, &app->regs, mod->plug,
			mod->presets, bndl);

#if 0
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
#endif
	}
	
	// cleanup
	free(dir);
	free(filename);
	free(bndl);

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
		serd_env_set_prefix_from_strings(env, CUINT8("state"), CUINT8(LV2_STATE_PREFIX));
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

__non_realtime static const void *
_state_retrieve(LV2_State_Handle state, uint32_t key, size_t *size,
	uint32_t *type, uint32_t *flags)
{
	const LV2_Atom_Object *obj = state;

	const LV2_Atom *atom = NULL;
	LV2_Atom_Object_Query obj_q[] = {
		{ key, &atom },
		{ 0, NULL }
	};
	lv2_atom_object_query(obj, obj_q);

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
		return -1;

	if(app->bundle_path)
		free(app->bundle_path);

	app->bundle_path = strdup(bundle_path);
	if(!app->bundle_path)
		return -1;

	char *state_dst = _make_path(app->bundle_path, "state.ttl");
	if(!state_dst)
		return -1;

	LV2_Atom_Object *obj = _deserialize_from_turtle(app->sratom, app->driver->unmap, state_dst);
	if(obj) // existing project
	{
		// restore state
		sp_app_restore(app, _state_retrieve, obj,
			LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, 
			_state_features(app, app->bundle_path));

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
		return -1;

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

			ser.size = 4096;
			ser.offset = 0;
			ser.buf = malloc(ser.size);
			lv2_atom_forge_set_sink(forge, _sink, _deref, &ser);

			if(  ser.buf
				&& lv2_atom_forge_object(forge, &pset_frame, app->regs.synthpod.null.urid, app->regs.pset.preset.urid)
				&& lv2_atom_forge_key(forge, app->regs.core.applies_to.urid)
				&& lv2_atom_forge_urid(forge, app->regs.synthpod.stereo.urid)
				&& lv2_atom_forge_key(forge, app->regs.core.applies_to.urid)
				&& lv2_atom_forge_urid(forge, app->regs.synthpod.monoatom.urid)

				&& lv2_atom_forge_key(forge, app->regs.rdfs.label.urid)
				&& lv2_atom_forge_string(forge, app->bundle_path, strlen(app->bundle_path))

				&& lv2_atom_forge_key(forge, app->regs.state.state.urid)
				&& lv2_atom_forge_object(forge, &state_frame, 0, 0) )
			{
				// store state
				sp_app_save(app, _state_store, forge,
					LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE,
					_state_features(app, app->bundle_path));

				lv2_atom_forge_pop(forge, &state_frame);
				lv2_atom_forge_pop(forge, &pset_frame);

				const LV2_Atom *atom = (const LV2_Atom *)ser.buf;
				_serialize_to_turtle(app->sratom, app->driver->unmap, atom, state_dst);
				free(ser.buf);
			}
		}

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

	// store grid cols
	store(hndl, app->regs.synthpod.grid_cols.urid,
		&app->ncols, sizeof(int32_t), app->forge.Int,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

	// store grid rows
	store(hndl, app->regs.synthpod.grid_rows.urid,
		&app->nrows, sizeof(int32_t), app->forge.Int,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

	// store pane left 
	store(hndl, app->regs.synthpod.pane_left.urid,
		&app->nleft, sizeof(float), app->forge.Float,
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
		LV2_Atom_Forge_Ref ref;
		LV2_Atom_Forge_Frame graph_frame;
		if( (ref = lv2_atom_forge_tuple(forge, &graph_frame)) )
		{
			for(unsigned m=0; m<app->num_mods; m++)
			{
				mod_t *mod = app->mods[m];

				char uid [64];
				sprintf(uid, "%u/", mod->uid);
				char *path = make_path->path(make_path->handle, uid);
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
						fprintf(stderr, "sp_app_save: invalid state\n");

					free(path);
				}
				else
					fprintf(stderr, "sp_app_save: invalid path\n");

				const LV2_URID uri_urid = app->driver->map->map(app->driver->map->handle, mod->uri_str);

				LV2_Atom_Forge_Frame mod_frame;
				if(  ref
					&& lv2_atom_forge_object(forge, &mod_frame, 0, uri_urid))
				{
					ref = lv2_atom_forge_key(forge, app->regs.rdf.subject.urid)
						&& lv2_atom_forge_urid(forge, mod->urn);

					ref = lv2_atom_forge_key(forge, app->regs.core.index.urid)
						&& lv2_atom_forge_int(forge, mod->uid);

					ref = lv2_atom_forge_key(forge, app->regs.synthpod.module_position_x.urid)
						&& lv2_atom_forge_float(forge, mod->pos.x);

					ref = lv2_atom_forge_key(forge, app->regs.synthpod.module_position_y.urid)
						&& lv2_atom_forge_float(forge, mod->pos.y);

					if(ref && mod->selected)
					{
						ref = lv2_atom_forge_key(forge, app->regs.synthpod.module_selected.urid)
							&& lv2_atom_forge_bool(forge, mod->selected);
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

					if(ref && mod->embedded)
					{
						ref = lv2_atom_forge_key(forge, app->regs.synthpod.module_embedded.urid)
							&& lv2_atom_forge_bool(forge, mod->embedded);
					}

					for(unsigned i=0; i<mod->num_ports; i++)
					{
						port_t *port = &mod->ports[i];

						LV2_Atom_Forge_Frame port_frame;
						if(  ref
							&& lv2_atom_forge_key(forge, app->regs.core.port.urid)
							&& lv2_atom_forge_object(forge, &port_frame, 0, app->regs.core.Port.urid) )
						{
							ref = lv2_atom_forge_key(forge, app->regs.core.symbol.urid)
								&& lv2_atom_forge_string(forge, port->symbol, strlen(port->symbol));

							if(ref && port->selected)
							{
								ref = lv2_atom_forge_key(forge, app->regs.synthpod.port_selected.urid)
									&& lv2_atom_forge_bool(forge, port->selected);
							}

							if(ref && port->monitored)
							{
								ref = lv2_atom_forge_key(forge, app->regs.synthpod.port_monitored.urid)
									&& lv2_atom_forge_bool(forge, port->monitored);
							}

							// serialize port connections
							connectable_t *conn = _sp_app_port_connectable(port);
							if(conn)
							{
								for(int j=0; j<conn->num_sources; j++)
								{
									port_t *source = conn->sources[j].port;

									LV2_Atom_Forge_Frame source_frame;
									if(  ref
										&& lv2_atom_forge_key(forge, app->regs.core.port.urid)
										&& lv2_atom_forge_object(forge, &source_frame, 0, app->regs.core.Port.urid) )
									{
										ref = lv2_atom_forge_key(forge, app->regs.core.index.urid)
											&& lv2_atom_forge_int(forge, source->mod->uid)
											&& lv2_atom_forge_key(forge, app->regs.core.symbol.urid)
											&& lv2_atom_forge_string(forge, source->symbol, strlen(source->symbol));

										if(ref)
											lv2_atom_forge_pop(forge, &source_frame);
									}
								}
							}

							// serialize port automations
							if(port->type == PORT_TYPE_CONTROL)
							{
								auto_t *automation = &port->control.automation;

								if(automation->type == AUTO_TYPE_MIDI)
								{
									midi_auto_t *mauto = &automation->midi;

									LV2_Atom_Forge_Frame auto_frame;
									if(  ref
										&& lv2_atom_forge_key(forge, app->regs.synthpod.automation_list.urid) //FIXME
										&& lv2_atom_forge_object(forge, &auto_frame, 0, app->regs.port.midi.urid) )
									{
										ref = lv2_atom_forge_key(forge, app->regs.midi.channel.urid)
											&& lv2_atom_forge_int(forge, mauto->channel)
											&& lv2_atom_forge_key(forge, app->regs.midi.controller.urid)
											&& lv2_atom_forge_int(forge, mauto->controller)
											&& lv2_atom_forge_key(forge, app->regs.core.minimum.urid)
											&& lv2_atom_forge_int(forge, mauto->min)
											&& lv2_atom_forge_key(forge, app->regs.core.maximum.urid)
											&& lv2_atom_forge_int(forge, mauto->max);

										if(ref)
											lv2_atom_forge_pop(forge, &auto_frame);
									}
								}
								else if(automation->type == AUTO_TYPE_OSC)
								{
									//FIXME write me
								}
							}

							if(ref)
								lv2_atom_forge_pop(forge, &port_frame);
						}
						else
							fprintf(stderr, "sp_app_save: invalid port\n");
					}

					if(ref)
						lv2_atom_forge_pop(forge, &mod_frame);
				}
				else
					fprintf(stderr, "sp_app_save: invalid mod\n");
			}

			if(ref)
				lv2_atom_forge_pop(forge, &graph_frame);
		}
		else
			fprintf(stderr, "sp_app_save: invalid graph\n");

		const LV2_Atom *atom = (const LV2_Atom *)ser.buf;
		if(ref && atom)
		{
			store(hndl, app->regs.synthpod.graph.urid,
				LV2_ATOM_BODY_CONST(atom), atom->size, atom->type,
				LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
		}
		else
			fprintf(stderr, "sp_app_save: invalid ref or atom\n");

		free(ser.buf);

		return LV2_STATE_SUCCESS;
	}

	return LV2_STATE_ERR_UNKNOWN;
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
		fprintf(stderr, "sp_app_restore: LV2_STATE__mapPath not supported.");
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

	// retrieve grid cols
	const int32_t *grid_cols = retrieve(hndl, app->regs.synthpod.grid_cols.urid,
		&size, &type, &_flags);
	if(grid_cols && (type == app->forge.Int) && (size == sizeof(int32_t)) )
		app->ncols = *grid_cols;

	// retrieve grid rows
	const int32_t *grid_rows = retrieve(hndl, app->regs.synthpod.grid_rows.urid,
		&size, &type, &_flags);
	if(grid_rows && (type == app->forge.Int) && (size == sizeof(int32_t)) )
		app->nrows = *grid_rows;

	// retrieve grid rows
	const float *pane_left = retrieve(hndl, app->regs.synthpod.pane_left.urid,
		&size, &type, &_flags);
	if(pane_left && (type == app->forge.Float) && (size == sizeof(float)) )
		app->nleft = *pane_left;

	// retrieve graph
	const LV2_Atom_Tuple *graph_body = retrieve(hndl, app->regs.synthpod.graph.urid,
		&size, &type, &_flags);
	
	if(!graph_body)
		return LV2_STATE_ERR_UNKNOWN;

	if(type != app->forge.Tuple)
		return LV2_STATE_ERR_BAD_TYPE;

	if(!(_flags & (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE)))
		return LV2_STATE_ERR_BAD_FLAGS;

	_sp_app_reset(app);

	LV2_ATOM_TUPLE_BODY_FOREACH(graph_body, size, iter)
	{
		const LV2_Atom_Object *mod_obj = (const LV2_Atom_Object *)iter;

		if(  !lv2_atom_forge_is_object_type(&app->forge, mod_obj->atom.type)
			|| !mod_obj->body.otype)
			continue;

		const LV2_Atom_Int *mod_index = NULL;
		const LV2_Atom_URID *mod_subject = NULL;
		const LV2_Atom_Float *mod_pos_x = NULL;
		const LV2_Atom_Float *mod_pos_y = NULL;
		const LV2_Atom_Bool *mod_selected = NULL;
		const LV2_Atom_Bool *mod_visible = NULL;
		const LV2_Atom_Bool *mod_disabled = NULL;
		const LV2_Atom_Bool *mod_embedded = NULL;
		LV2_Atom_Object_Query mod_q[] = {
			{ app->regs.core.index.urid, (const LV2_Atom **)&mod_index },
			{ app->regs.rdf.subject.urid, (const LV2_Atom **)&mod_subject },
			{ app->regs.synthpod.module_position_x.urid, (const LV2_Atom **)&mod_pos_x },
			{ app->regs.synthpod.module_position_y.urid, (const LV2_Atom **)&mod_pos_y },
			{ app->regs.synthpod.module_selected.urid, (const LV2_Atom **)&mod_selected },
			{ app->regs.synthpod.module_visible.urid, (const LV2_Atom **)&mod_visible },
			{ app->regs.synthpod.module_disabled.urid, (const LV2_Atom **)&mod_disabled },
			{ app->regs.synthpod.module_embedded.urid, (const LV2_Atom **)&mod_embedded },
			{ 0, NULL }
		};
		lv2_atom_object_query(mod_obj, mod_q);
	
		if(!mod_index || (mod_index->atom.type != app->forge.Int) )
			continue;

		if(mod_subject && (mod_subject->atom.type != app->forge.URID) )
			continue;

		const char *mod_uri_str = app->driver->unmap->unmap(app->driver->unmap->handle, mod_obj->body.otype);
		const u_id_t mod_uid = mod_index->body;
		const LV2_URID mod_urn = mod_subject
			? mod_subject->body
			: 0;
		mod_t *mod = _sp_app_mod_add(app, mod_uri_str, mod_uid, mod_urn);
		if(!mod)
			continue;

		// inject module into module graph
		app->mods[app->num_mods] = mod;
		app->num_mods += 1;

		mod->pos.x = mod_pos_x && (mod_pos_x->atom.type == app->forge.Float)
			? mod_pos_x->body : 0.f;
		mod->pos.y = mod_pos_y && (mod_pos_y->atom.type == app->forge.Float)
			? mod_pos_y->body : 0.f;
		mod->selected = mod_selected && (mod_selected->atom.type == app->forge.Bool)
			? mod_selected->body : false;
		mod->visible = mod_visible && (mod_visible->atom.type == app->forge.URID)
			? mod_visible->body : 0;
		mod->disabled = mod_disabled && (mod_disabled->atom.type == app->forge.Bool)
			? mod_disabled->body : false;
		mod->embedded = mod_embedded && (mod_embedded->atom.type == app->forge.Bool)
			? mod_embedded->body : false;

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
				LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, _preset_features(mod, false));
		}
		else
			fprintf(stderr, "failed to load state from file\n");

		lilv_state_free(state);
		free(path);
	}

	_sp_app_order(app);

	LV2_ATOM_TUPLE_BODY_FOREACH(graph_body, size, iter)
	{
		const LV2_Atom_Object *mod_obj = (const LV2_Atom_Object *)iter;

		if(  !lv2_atom_forge_is_object_type(&app->forge, mod_obj->atom.type)
			|| !mod_obj->body.otype)
			continue;

		const LV2_Atom_Int *mod_index = NULL;
		LV2_Atom_Object_Query mod_q[] = {
			{ app->regs.core.index.urid, (const LV2_Atom **)&mod_index },
			{ 0, NULL }
		};
		lv2_atom_object_query(mod_obj, mod_q);
	
		if(!mod_index || (mod_index->atom.type != app->forge.Int) )
			continue;

		const u_id_t mod_uid = mod_index->body;
		mod_t *mod = _sp_app_mod_get(app, mod_uid);
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
			const LV2_Atom_Bool *port_selected = NULL;
			const LV2_Atom_Bool *port_monitored = NULL;
			const LV2_Atom_Object *auto_list = NULL;
			LV2_Atom_Object_Query port_q[] = {
				{ app->regs.core.symbol.urid, (const LV2_Atom **)&port_symbol },
				{ app->regs.synthpod.port_selected.urid, (const LV2_Atom **)&port_selected },
				{ app->regs.synthpod.port_monitored.urid, (const LV2_Atom **)&port_monitored },
				{ app->regs.synthpod.automation_list.urid, (const LV2_Atom **)&auto_list },
				{ 0, NULL }
			};
			lv2_atom_object_query(port_obj, port_q);

			if(!port_symbol || (port_symbol->atom.type != app->forge.String) )
				continue;

			const char *port_symbol_str = LV2_ATOM_BODY_CONST(port_symbol);

			for(unsigned i=0; i<mod->num_ports; i++)
			{
				port_t *port = &mod->ports[i];

				// search for matching port symbol
				if(strcmp(port_symbol_str, port->symbol))
					continue;

				port->selected = port_selected && (port_selected->atom.type == app->forge.Bool) ? port_selected->body : 0;
				port->monitored = port_monitored && (port_monitored->atom.type == app->forge.Bool) ? port_monitored->body : 0;

				if(  (port->type == PORT_TYPE_CONTROL)
					&& auto_list
					&& (auto_list->atom.type == app->forge.Object))
				{
					if(auto_list->body.otype == app->regs.port.midi.urid)
					{
						const LV2_Atom_Int *midi_channel = NULL;
						const LV2_Atom_Int *midi_controller = NULL;
						const LV2_Atom_Int *core_minimum = NULL;
						const LV2_Atom_Int *core_maximum = NULL;

						LV2_Atom_Object_Query auto_q[] = {
							{app->regs.midi.channel.urid, (const LV2_Atom **)&midi_channel },
							{app->regs.midi.controller.urid, (const LV2_Atom **)&midi_controller },
							{app->regs.core.minimum.urid, (const LV2_Atom **)&core_minimum },
							{app->regs.core.maximum.urid, (const LV2_Atom **)&core_maximum },
							{ 0, NULL }
						};
						lv2_atom_object_query(auto_list, auto_q);

						auto_t *automation = &port->control.automation;
						automation->type = AUTO_TYPE_MIDI;

						midi_auto_t *mauto = &automation->midi;
						mauto->channel = midi_channel ? midi_channel->body : -1;
						mauto->controller = midi_controller ? midi_controller->body : -1;
						mauto->min = core_minimum ? core_minimum->body : 0x0;
						mauto->max = core_maximum ? core_maximum->body : 0x7f;
						const int range = mauto->max - mauto->min;
						mauto->range_1 = range
							? 1.f / range
							: 0.f;
					}
					//FIXME handle OSC
				}

				LV2_ATOM_OBJECT_FOREACH(port_obj, sub)
				{
					const LV2_Atom_Object *source_obj = (const LV2_Atom_Object *)&sub->value;

					if(  (sub->key != app->regs.core.port.urid)
						|| !lv2_atom_forge_is_object_type(&app->forge, source_obj->atom.type)
						|| (source_obj->body.otype != app->regs.core.Port.urid) )
						continue;

					const LV2_Atom_String *source_symbol = NULL;
					const LV2_Atom_Int *source_index = NULL;
					LV2_Atom_Object_Query source_q[] = {
						{ app->regs.core.symbol.urid, (const LV2_Atom **)&source_symbol },
						{ app->regs.core.index.urid, (const LV2_Atom **)&source_index },
						{ 0, NULL }
					};
					lv2_atom_object_query(source_obj, source_q);

					if(  !source_symbol || (source_symbol->atom.type != app->forge.String)
						|| !source_index || (source_index->atom.type != app->forge.Int) )
						continue;

					const uint32_t source_uid = source_index->body;
					const char *source_symbol_str = LV2_ATOM_BODY_CONST(source_symbol);

					mod_t *source = _sp_app_mod_get(app, source_uid);
					if(!source)
						continue;
				
					for(unsigned j=0; j<source->num_ports; j++)
					{
						port_t *tar = &source->ports[j];

						if(strcmp(source_symbol_str, tar->symbol))
							continue;

						_sp_app_port_connect(app, tar, port);

						break;
					}
				}

				break;
			}
		}
	}

	_toggle_dirty(app);

	return LV2_STATE_SUCCESS;
}
