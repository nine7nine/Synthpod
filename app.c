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
#include <ctype.h>

#include <app.h>
#include <patcher.h>

#include <cJSON.h>

// include lv2 core header
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>

// include lv2 extension headers
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

//XXX XXX XXX XXX XXX XXX XXX XXX
static void
_state_set_value(const char *symbol, void *data,
	const void *value, uint32_t size, uint32_t type)
{
	mod_t *mod = data;
	app_t *app = mod->app;

	LilvNode *symbol_uri = lilv_new_string(app->world, symbol);
	const LilvPort *port = lilv_plugin_get_port_by_symbol(mod->plug, symbol_uri);
	lilv_node_free(symbol_uri);
	uint32_t index = lilv_port_get_index(mod->plug, port);

	float val = 0.f;

	if(type == mod->forge.Int)
		val = *(const int32_t *)value;
	else if(type == mod->forge.Long)
		val = *(const int64_t *)value;
	else if(type == mod->forge.Float)
		val = *(const float *)value;
	else if(type == mod->forge.Double)
		val = *(const double *)value;
	else
		return; //TODO warning

	//printf("%u %f\n", index, val);

	// to rt-thread
	_ui_write_function(mod, index, sizeof(float),
		app->regs.port.float_protocol.urid, &val);
}

void
app_load(app_t *app, const char *path)
{
	Eet_File *eet = eet_open(path, EET_FILE_MODE_READ);
	if(!eet)
		return;

	int size;
	char *root_str = eet_read(eet, "synthpod", &size);
	if(!root_str)
		return;

	cJSON *root_json = cJSON_Parse(root_str);
	// iterate over mods, create and apply states
	for(cJSON *mod_json = cJSON_GetObjectItem(root_json, "items")->child;
		mod_json;
		mod_json = mod_json->next)
	{
		const char *mod_uri_str = cJSON_GetObjectItem(mod_json, "uri")->valuestring;

		mod_t *mod = app_mod_add(app, mod_uri_str);
		if(!mod)
			continue;

		const char *mod_uuid_str = cJSON_GetObjectItem(mod_json, "uuid")->valuestring;
		uuid_parse(mod_uuid_str, mod->uuid);
		mod->selected = cJSON_GetObjectItem(mod_json, "selected")->type == cJSON_True;

		mod->ui.std.itm = elm_genlist_item_append(app->ui.modlist, app->ui.moditc, mod, NULL,
			ELM_GENLIST_ITEM_TREE, NULL, NULL);
	
		if(mod->ui.eo.ui) // has EoUI
		{
			mod->ui.eo.itm = elm_gengrid_item_append(app->ui.modgrid, app->ui.griditc, mod,
				NULL, NULL);
		}

		char *state_str = eet_read(eet, mod_uuid_str, &size);
		LilvState *state = lilv_state_new_from_string(app->world,
			ext_urid_map_get(app->ext_urid), state_str);
		lilv_state_restore(state, mod->inst, _state_set_value, mod,
			LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, NULL);
		lilv_state_free(state);
		free(state_str);

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

				if(!strcmp(port_symbol_str, lilv_node_as_string(port_symbol_node)))
				{
					const char *port_uuid_str = cJSON_GetObjectItem(port_json, "uuid")->valuestring;
					uuid_parse(port_uuid_str, port->uuid);
					port->selected = cJSON_GetObjectItem(port_json, "selected")->valueint;

					for(cJSON *source_json = cJSON_GetObjectItem(port_json, "sources")->child;
						source_json;
						source_json = source_json->next)
					{
						uuid_t source_uuid;
						uuid_parse(source_json->valuestring, source_uuid);
					
						for(Elm_Object_Item *itm = elm_genlist_first_item_get(app->ui.modlist);
							itm != NULL;
							itm = elm_genlist_item_next_get(itm))
						{
							mod_t *source = elm_object_item_data_get(itm);

							for(int j=0; j<source->num_ports; j++)
							{
								port_t *tar = &source->ports[j];
								if(!uuid_compare(source_uuid, tar->uuid))
								{
									void *ptr;
									if( (ptr = varchunk_write_request(app->rt.to, JOB_SIZE)) )
									{
										job_t *job = ptr;

										job->type = JOB_TYPE_CONN_ADD;
										job->payload.conn.source = tar;
										job->payload.conn.sink = port;

										varchunk_write_advance(app->rt.to, JOB_SIZE);
									}
									else
										fprintf(stderr, "rt varchunk buffer overrun");
								}
							}
						}
					}

					break;
				}
			}
		}
	}
	cJSON_Delete(root_json);
	free(root_str);

	eet_close(eet);

	_patches_update(app); // TODO connections are not yet shown 
}

static const void *
_state_get_value(const char *symbol, void *data, uint32_t *size, uint32_t *type)
{
	mod_t *mod = data;
	app_t *app = mod->app;
	
	LilvNode *symbol_uri = lilv_new_string(app->world, symbol);
	const LilvPort *port = lilv_plugin_get_port_by_symbol(mod->plug, symbol_uri);
	lilv_node_free(symbol_uri);
	uint32_t index = lilv_port_get_index(mod->plug, port);

	port_t *tar = &mod->ports[index];

	if(  (tar->direction == PORT_DIRECTION_INPUT)
		&& (tar->type == PORT_TYPE_CONTROL) )
	{
		*size = sizeof(float);
		*type = mod->forge.Float;
		return tar->buf;
	}

	*size = 0;
	*type = 0;
	return NULL;
}

void
app_save(app_t *app, const char *path)
{
	Eet_File *eet = eet_open(path, EET_FILE_MODE_WRITE);
	if(!eet)
		return;

	// create json object
	cJSON *root_json = cJSON_CreateObject();
	cJSON *arr_json = cJSON_CreateArray();
	cJSON_AddItemToObject(root_json, "items", arr_json);

	for(Elm_Object_Item *itm = elm_genlist_first_item_get(app->ui.modlist);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		mod_t *mod = elm_object_item_data_get(itm);

		if(!mod || !mod->plug || !mod->inst)
			continue;

		LilvState *const state = lilv_state_new_from_instance(mod->plug, mod->inst,
			ext_urid_map_get(app->ext_urid), NULL, NULL, NULL, NULL,
			_state_get_value, mod, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, NULL);

		char *state_str = lilv_state_to_string(app->world,
			ext_urid_map_get(app->ext_urid), ext_urid_unmap_get(app->ext_urid),
			state, "http://open-music-kontrollers.ch/lv2/synthpod/state", NULL);
		
		const LilvNode *uri_node = lilv_state_get_plugin_uri(state);
		const char *uri_str = lilv_node_as_string(uri_node);

		char uuid_str [37];
		uuid_unparse(mod->uuid, uuid_str);

		// write mod state
		eet_write(eet, uuid_str, state_str, strlen(state_str) + 1, 0);
		free(state_str);
		lilv_state_free(state);

		// fill mod json object
		cJSON *mod_json = cJSON_CreateObject();
		cJSON *mod_uuid_json = cJSON_CreateString(uuid_str);
		cJSON *mod_uri_json = cJSON_CreateString(uri_str);
		cJSON *mod_selected_json = cJSON_CreateBool(mod->selected);
		cJSON *mod_ports_json = cJSON_CreateArray();
		for(int i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];
			uuid_unparse(port->uuid, uuid_str);

			cJSON *port_json = cJSON_CreateObject();
			cJSON *port_uuid_json = cJSON_CreateString(uuid_str);
			cJSON *port_symbol_json = cJSON_CreateString(lilv_node_as_string(lilv_port_get_symbol(mod->plug, port->tar)));
			cJSON *port_selected_json = cJSON_CreateBool(port->selected);
			cJSON *port_sources_json = cJSON_CreateArray();
			for(int j=0; j<port->num_sources; j++)
			{
				port_t *source = port->sources[j];
				uuid_unparse(source->uuid, uuid_str);
				cJSON *source_uuid_json = cJSON_CreateString(uuid_str);
				cJSON_AddItemToArray(port_sources_json, source_uuid_json);
			}
			cJSON_AddItemToObject(port_json, "uuid", port_uuid_json);
			cJSON_AddItemToObject(port_json, "symbol", port_symbol_json);
			cJSON_AddItemToObject(port_json, "selected", port_selected_json);
			cJSON_AddItemToObject(port_json, "sources", port_sources_json);
			cJSON_AddItemToArray(mod_ports_json, port_json);
		}
		cJSON_AddItemToObject(mod_json, "uuid", mod_uuid_json);
		cJSON_AddItemToObject(mod_json, "uri", mod_uri_json);
		cJSON_AddItemToObject(mod_json, "selected", mod_selected_json);
		cJSON_AddItemToObject(mod_json, "ports", mod_ports_json);
		cJSON_AddItemToArray(arr_json, mod_json);
	}

	// serialize json object
	char *root_str = cJSON_Print(root_json);	
	eet_write(eet, "synthpod", root_str, strlen(root_str) + 1, 0);
	free(root_str);
	cJSON_Delete(root_json);

	eet_sync(eet);
	eet_close(eet);
}
