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

#include <synthpod_ui_private.h>

int
_propitc_cmp(const void *data1, const void *data2)
{
	const Elm_Object_Item *itm1 = data1;
	const Elm_Object_Item *itm2 = data2;
	if(!itm1 || !itm2)
		return 1;

	property_t *prop1 = elm_object_item_data_get(itm1);
	property_t *prop2 = elm_object_item_data_get(itm2);
	if(!prop1 || !prop2)
		return 1;

	// compare property URIDs
	return prop1->tar_urid < prop2->tar_urid
		? -1
		: (prop1->tar_urid > prop2->tar_urid
			? 1
			: 0);
}

void
_ui_property_tooltip_add(sp_ui_t *ui, Elm_Object_Item *elmnt, property_t *prop)
{
	if(prop->comment)
		elm_object_item_tooltip_text_set(elmnt, prop->comment);
}

void
_property_free(property_t *prop)
{
	if(prop->label)
		free(prop->label); // strdup

	if(prop->comment)
		free(prop->comment); // strdup

	point_t *p;
	EINA_LIST_FREE(prop->scale_points, p)
	{
		if(p->label)
			free(p->label);

		if(p->s)
			free(p->s);

		free(p);
	}

	free(prop);
}

void
_property_remove(mod_t *mod, group_t *group, property_t *prop)
{
	if(group)
		group->children = eina_list_remove(group->children, prop);

	mod->dynamic_properties = eina_list_remove(mod->dynamic_properties, prop);

	if(prop->std.elmnt)
		elm_object_item_del(prop->std.elmnt);
}

void
_mod_set_property(mod_t *mod, LV2_URID property_val, const LV2_Atom *value)
{
	sp_ui_t *ui = mod->ui;

	//printf("ui got patch:Set: %u %u\n",
	//	mod->uid, property_val);

	property_t *prop;
	if(  (prop = eina_list_search_sorted(mod->static_properties, _urid_find, &property_val))
		|| (prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &property_val)) )
	{
		if(prop->std.widget &&
			(    (prop->type_urid == value->type)
				|| (prop->type_urid + value->type == ui->forge.Int + ui->forge.Bool)
			) )
		{
			if(prop->scale_points)
			{
				if(prop->type_urid == ui->forge.String)
				{
					smart_spinner_key_set(prop->std.widget, LV2_ATOM_BODY_CONST(value));
				}
				else if(prop->type_urid == ui->forge.Int)
				{
					int32_t val = ((const LV2_Atom_Int *)value)->body;
					smart_spinner_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Float)
				{
					float val = ((const LV2_Atom_Float *)value)->body;
					smart_spinner_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Long)
				{
					int64_t val = ((const LV2_Atom_Long *)value)->body;
					smart_spinner_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Double)
				{
					double val = ((const LV2_Atom_Double *)value)->body;
					smart_spinner_value_set(prop->std.widget, val);
				}
				//TODO do other types
			}
			else // !scale_points
			{
				if(  (prop->type_urid == ui->forge.String)
					|| (prop->type_urid == ui->forge.URI) )
				{
					const char *val = LV2_ATOM_BODY_CONST(value);
					if(prop->editable)
						elm_entry_entry_set(prop->std.entry, val);
					else
						elm_object_text_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Literal)
				{
					const char *val = LV2_ATOM_CONTENTS_CONST(LV2_Atom_Literal, value);
					if(prop->editable)
						elm_entry_entry_set(prop->std.entry, val);
					else
						elm_object_text_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.URID)
				{
					uint32_t val = ((const LV2_Atom_URID *)value)->body;
					const char *uri = ui->driver->unmap->unmap(ui->driver->unmap->handle, val);
					if(prop->editable)
						elm_entry_entry_set(prop->std.entry, uri);
					else
						elm_object_text_set(prop->std.widget, uri);
				}
				else if(prop->type_urid == ui->forge.Path)
				{
					const char *val = LV2_ATOM_BODY_CONST(value);
					//elm_object_text_set(prop->std.widget, val); TODO ellipsis on button text
					if(prop->editable)
						elm_fileselector_path_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Chunk)
				{
					char *sz = NULL;
					if(asprintf(&sz, "sz: %u", value->size) != -1)
					{
						elm_object_text_set(prop->std.widget, sz);
						free(sz);
					}
				}
				else if(prop->type_urid == ui->forge.Int)
				{
					int32_t val = ((const LV2_Atom_Int *)value)->body;
					if(prop->is_bitmask)
						smart_bitmask_value_set(prop->std.widget, val);
					else
						smart_slider_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Long)
				{
					int64_t val = ((const LV2_Atom_Long *)value)->body;
					if(prop->is_bitmask)
						smart_bitmask_value_set(prop->std.widget, val);
					else
						smart_slider_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Float)
				{
					float val = ((const LV2_Atom_Float *)value)->body;
					smart_slider_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Double)
				{
					double val = ((const LV2_Atom_Double *)value)->body;
					smart_slider_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Bool)
				{
					int val = ((const LV2_Atom_Bool *)value)->body;
					smart_toggle_value_set(prop->std.widget, val);
				}
			}
		}
	}
}

static void
_property_path_chosen(void *data, Evas_Object *obj, void *event_info)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	const char *path = event_info;
	if(!path)
		return;

	//printf("_property_path_chosen: %s\n", path);

	size_t strsize = strlen(path) + 1;
	size_t len = sizeof(transfer_patch_set_obj_t) + lv2_atom_pad_size(strsize);

	for(unsigned index=0; index<mod->num_ports; index++)
	{
		port_t *port = &mod->ports[index];

		// only consider event ports which support patch:Message
		if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
			|| (port->direction != PORT_DIRECTION_INPUT)
			|| !port->patchable)
		{
			continue; // skip
		}

		transfer_patch_set_obj_t *trans = malloc(len);
		if(trans)
		{
			LV2_Atom *atom = _sp_transfer_patch_set_obj_fill(&ui->regs,
				&ui->forge, trans, strsize,
				mod->subject, prop->tar_urid, prop->type_urid);
			if(atom)
			{
				strcpy(LV2_ATOM_BODY(atom), path);

				_std_ui_write_function(mod, index, lv2_atom_total_size(&trans->obj.atom),
					ui->regs.port.event_transfer.urid, &trans->obj);
			}
			free(trans);
		}
	}
}

static inline uint8_t *
_file_content(const char *path, size_t *fsize)
{
	uint8_t *chunk = NULL;

	// load file
	FILE *f = fopen(path, "rb");
	if(f)
	{
		fseek(f, 0, SEEK_END);
		*fsize = ftell(f);
		fseek(f, 0, SEEK_SET);

		chunk = malloc(*fsize);
		if(chunk)
		{
			if(fread(chunk, *fsize, 1, f) != 1) // failed to read
			{
				*fsize = 0;
				free(chunk);
				chunk = NULL;
			}
		}

		fclose(f);
	}

	return chunk;
}

static void
_property_chunk_chosen(void *data, Evas_Object *obj, void *event_info)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	const char *path = event_info;
	if(!path)
		return;

	//printf("_property_chunk_chosen: %s\n", path);
	size_t fsize;
	uint8_t *chunk = _file_content(path, &fsize);

	if(!chunk)
		return;

	char *sz= NULL;
	if(asprintf(&sz, "sz: %zu", fsize) != -1)
	{
		elm_object_text_set(prop->std.widget, sz);
		free(sz);
	}

	size_t len = sizeof(transfer_patch_set_obj_t) + lv2_atom_pad_size(fsize);

	for(unsigned index=0; index<mod->num_ports; index++)
	{
		port_t *port = &mod->ports[index];

		// only consider event ports which support patch:Message
		if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
			|| (port->direction != PORT_DIRECTION_INPUT)
			|| !port->patchable)
		{
			continue; // skip
		}

		transfer_patch_set_obj_t *trans = malloc(len);
		if(trans)
		{
			LV2_Atom *atom = _sp_transfer_patch_set_obj_fill(&ui->regs,
				&ui->forge, trans, fsize,
				mod->subject, prop->tar_urid, prop->type_urid);
			if(atom)
			{
				uint8_t *dst = LV2_ATOM_BODY(atom);
				memcpy(dst, chunk, fsize);

				_std_ui_write_function(mod, index, lv2_atom_total_size(&trans->obj.atom),
					ui->regs.port.event_transfer.urid, &trans->obj);
			}
			free(trans);
		}
	}

	free(chunk);
}

static void
_property_string_activated(void *data, Evas_Object *obj, void *event_info)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	const char *entered = elm_entry_entry_get(obj);
	if(!entered)
		return;

	//printf("_property_string_activated: %s\n", entered);

	size_t bodysize;
	if(prop->type_urid == ui->forge.URID)
		bodysize = sizeof(LV2_URID);
	else if(prop->type_urid == ui->forge.Literal)
		bodysize = sizeof(LV2_Atom_Literal_Body) + strlen(entered) + 1;
	else
		bodysize = strlen(entered) + 1;
	size_t len = sizeof(transfer_patch_set_obj_t) + lv2_atom_pad_size(bodysize);

	for(unsigned index=0; index<mod->num_ports; index++)
	{
		port_t *port = &mod->ports[index];

		// only consider event ports which support patch:Message
		if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
			|| (port->direction != PORT_DIRECTION_INPUT)
			|| !port->patchable)
		{
			continue; // skip
		}

		transfer_patch_set_obj_t *trans = malloc(len);
		if(trans)
		{
			LV2_Atom *atom = _sp_transfer_patch_set_obj_fill(&ui->regs,
				&ui->forge, trans, bodysize,
				mod->subject, prop->tar_urid, prop->type_urid);
			if(atom)
			{
				if(prop->type_urid == ui->forge.URID)
				{
					((LV2_Atom_URID *)atom)->body = ui->driver->map->map(ui->driver->map->handle, entered);
				}
				else if(prop->type_urid == ui->forge.Literal)
				{
					((LV2_Atom_Literal *)atom)->body.datatype = 0; //TODO
					((LV2_Atom_Literal *)atom)->body.lang = 0; //TODO
					strcpy(LV2_ATOM_CONTENTS(LV2_Atom_Literal, atom), entered);
				}
				else
				{
					strcpy(LV2_ATOM_BODY(atom), entered);
				}

				_std_ui_write_function(mod, index, lv2_atom_total_size(&trans->obj.atom),
					ui->regs.port.event_transfer.urid, &trans->obj);
			}
			free(trans);
		}
	}
}

static void
_property_sldr_changed(void *data, Evas_Object *obj, void *event_info)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	double value = smart_slider_value_get(obj);

	size_t body_size = 0;
	if(  (prop->type_urid == ui->forge.Int)
		|| (prop->type_urid == ui->forge.Float) )
	{
		body_size = sizeof(int32_t);
	}
	else if(  (prop->type_urid == ui->forge.Long)
		|| (prop->type_urid == ui->forge.Double) )
	{
		body_size = sizeof(int64_t);
	}

	size_t len = sizeof(transfer_patch_set_obj_t) + lv2_atom_pad_size(body_size);

	for(unsigned index=0; index<mod->num_ports; index++)
	{
		port_t *port = &mod->ports[index];

		// only consider event ports which support patch:Message
		if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
			|| (port->direction != PORT_DIRECTION_INPUT)
			|| !port->patchable)
		{
			continue; // skip
		}

		transfer_patch_set_obj_t *trans = malloc(len);
		if(trans)
		{
			LV2_Atom *atom = _sp_transfer_patch_set_obj_fill(&ui->regs,
				&ui->forge, trans, body_size,
				mod->subject, prop->tar_urid, prop->type_urid);
			if(atom)
			{
				if(prop->type_urid == ui->forge.Int)
					((LV2_Atom_Int *)atom)->body = value;
				else if(prop->type_urid == ui->forge.Long)
					((LV2_Atom_Long *)atom)->body = value;
				else if(prop->type_urid == ui->forge.Float)
					((LV2_Atom_Float *)atom)->body = value;
				else if(prop->type_urid == ui->forge.Double)
					((LV2_Atom_Double *)atom)->body = value;

				_std_ui_write_function(mod, index, lv2_atom_total_size(&trans->obj.atom),
					ui->regs.port.event_transfer.urid, &trans->obj);
			}
			free(trans);
		}
	}
}

static void
_property_bitmask_changed(void *data, Evas_Object *obj, void *event_info)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	int64_t value = smart_bitmask_value_get(obj);

	size_t body_size = 0;
	if(prop->type_urid == ui->forge.Int)
		body_size = sizeof(int32_t);
	else if(prop->type_urid == ui->forge.Long)
		body_size = sizeof(int64_t);
	else
		return; // unsupported type

	size_t len = sizeof(transfer_patch_set_obj_t) + lv2_atom_pad_size(body_size);

	for(unsigned index=0; index<mod->num_ports; index++)
	{
		port_t *port = &mod->ports[index];

		// only consider event ports which support patch:Message
		if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
			|| (port->direction != PORT_DIRECTION_INPUT)
			|| !port->patchable)
		{
			continue; // skip
		}

		transfer_patch_set_obj_t *trans = malloc(len);
		if(trans)
		{
			LV2_Atom *atom = _sp_transfer_patch_set_obj_fill(&ui->regs,
				&ui->forge, trans, body_size,
				mod->subject, prop->tar_urid, prop->type_urid);
			if(atom)
			{
				if(prop->type_urid == ui->forge.Int)
					((LV2_Atom_Int *)atom)->body = value;
				else if(prop->type_urid == ui->forge.Long)
					((LV2_Atom_Long *)atom)->body = value;

				_std_ui_write_function(mod, index, lv2_atom_total_size(&trans->obj.atom),
					ui->regs.port.event_transfer.urid, &trans->obj);
			}
			free(trans);
		}
	}
}

static void
_property_check_changed(void *data, Evas_Object *obj, void *event_info)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	int value = smart_toggle_value_get(obj);

	size_t body_size = sizeof(int32_t);
	size_t len = sizeof(transfer_patch_set_obj_t) + lv2_atom_pad_size(body_size);

	for(unsigned index=0; index<mod->num_ports; index++)
	{
		port_t *port = &mod->ports[index];

		// only consider event ports which support patch:Message
		if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
			|| (port->direction != PORT_DIRECTION_INPUT)
			|| !port->patchable)
		{
			continue; // skip
		}

		transfer_patch_set_obj_t *trans = malloc(len);
		if(trans)
		{
			LV2_Atom *atom = _sp_transfer_patch_set_obj_fill(&ui->regs,
				&ui->forge, trans, body_size,
				mod->subject, prop->tar_urid, prop->type_urid);
			if(atom)
			{
				((LV2_Atom_Bool *)atom)->body = value;

				_std_ui_write_function(mod, index, lv2_atom_total_size(&trans->obj.atom),
					ui->regs.port.event_transfer.urid, &trans->obj);
			}
			free(trans);
		}
	}
}

static void
_property_spinner_changed(void *data, Evas_Object *obj, void *event_info)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	const char *key = NULL;
	float value = 0.f;

	if(prop->type_urid == ui->forge.String)
		key = smart_spinner_key_get(obj);
	else
		value = smart_spinner_value_get(obj);

	size_t body_size = 0;
	if(prop->type_urid == ui->forge.String)
		body_size = strlen(key) + 1;
	else if(prop->type_urid == ui->forge.Int)
		body_size = sizeof(int32_t);
	else if(prop->type_urid == ui->forge.Float)
		body_size = sizeof(float);
	else if(prop->type_urid == ui->forge.Long)
		body_size = sizeof(int64_t);
	else if(prop->type_urid == ui->forge.Double)
		body_size = sizeof(double);
	//TODO do other types
	size_t len = sizeof(transfer_patch_set_obj_t) + lv2_atom_pad_size(body_size);

	for(unsigned index=0; index<mod->num_ports; index++)
	{
		port_t *port = &mod->ports[index];

		// only consider event ports which support patch:Message
		if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
			|| (port->direction != PORT_DIRECTION_INPUT)
			|| !port->patchable)
		{
			continue; // skip
		}

		transfer_patch_set_obj_t *trans = malloc(len);
		if(trans)
		{
			LV2_Atom *atom = _sp_transfer_patch_set_obj_fill(&ui->regs,
				&ui->forge, trans, body_size,
				mod->subject, prop->tar_urid, prop->type_urid);
			if(atom)
			{
				if(prop->type_urid == ui->forge.String)
					strcpy(LV2_ATOM_BODY(atom), key);
				else if(prop->type_urid == ui->forge.Int)
					((LV2_Atom_Int *)atom)->body = value;
				else if(prop->type_urid == ui->forge.Float)
					((LV2_Atom_Float *)atom)->body = value;
				else if(prop->type_urid == ui->forge.Long)
					((LV2_Atom_Long *)atom)->body = value;
				else if(prop->type_urid == ui->forge.Double)
					((LV2_Atom_Double *)atom)->body = value;
				//TODO do other types

				_std_ui_write_function(mod, index, lv2_atom_total_size(&trans->obj.atom),
					ui->regs.port.event_transfer.urid, &trans->obj);
			}
			free(trans);
		}
	}
}

static void
_property_del(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	property_t *prop = data;

	if(prop)
		prop->std.elmnt = NULL;

	// we don't free it here, as this is only a reference from group->children
}

static Evas_Object *
_property_content_get(void *data, Evas_Object *obj, const char *part)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	if(!prop->type_urid) // type not yet set, e.g. for dynamic properties
		return NULL;

	Evas_Object *lay = elm_layout_add(obj);
	if(lay)
	{
		elm_layout_file_set(lay, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/modlist/port");
		evas_object_event_callback_add(lay, EVAS_CALLBACK_DEL, _property_del, prop);
		evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(lay);

		// link
		elm_layout_signal_emit(lay, "link,hide", ""); //TODO or "link,on"

		// monitor
		elm_layout_signal_emit(lay, "monitor,hide", ""); //TODO or "monitor,on"

		char col [7];
		sprintf(col, "col,%02i", mod->col);

		// source/sink
		elm_layout_signal_emit(lay, col, MODLIST_UI);
		if(!prop->editable)
		{
			elm_layout_signal_emit(lay, "source,on", "");
		}
		else
		{
			elm_layout_signal_emit(lay, "source,off", "");
		}

		if(prop->label)
			elm_layout_text_set(lay, "elm.text", prop->label);

		Evas_Object *child = NULL;

		if(!prop->scale_points)
		{
			if(  (prop->type_urid == ui->forge.String)
				|| (prop->type_urid == ui->forge.Literal)
				|| (prop->type_urid == ui->forge.URI)
				|| (prop->type_urid == ui->forge.URID) )
			{
				if(prop->editable)
				{
					child = elm_layout_add(lay);
					if(child)
					{
						elm_layout_file_set(child, SYNTHPOD_DATA_DIR"/synthpod.edj",
							"/synthpod/entry/theme");
						elm_layout_signal_emit(child, col, "/synthpod/entry/ui");

						prop->std.entry = elm_entry_add(child);
						if(prop->std.entry)
						{
							elm_entry_single_line_set(prop->std.entry, EINA_TRUE);
							evas_object_smart_callback_add(prop->std.entry, "activated",
								_property_string_activated, prop);
							evas_object_show(prop->std.entry);
							elm_layout_content_set(child, "elm.swallow.content", prop->std.entry);
						}
					}
				}
				else // !editable
				{
					child = elm_label_add(lay);
					if(child)
						evas_object_size_hint_align_set(child, 0.f, EVAS_HINT_FILL);
				}
			}
			else if(prop->type_urid == ui->forge.Path)
			{
				if(prop->editable)
				{
					child = elm_fileselector_button_add(lay);
					if(child)
					{
						elm_fileselector_button_inwin_mode_set(child, EINA_FALSE);
						elm_fileselector_button_window_title_set(child, "Select file");

						const LilvNode *bundle_uri = lilv_plugin_get_bundle_uri(mod->plug);
#if defined(LILV_0_22)
						char *mod_path = lilv_file_uri_parse(lilv_node_as_uri(bundle_uri), NULL);
#else
						const char *mod_path = lilv_uri_to_path(lilv_node_as_uri(bundle_uri));
#endif
						if(mod_path)
						{
							elm_fileselector_path_set(child, mod_path);
#if defined(LILV_0_22)
							free(mod_path);
#endif
						}
						elm_fileselector_is_save_set(child, EINA_FALSE);
						elm_fileselector_folder_only_set(child, EINA_FALSE);
						elm_fileselector_expandable_set(child, EINA_TRUE);
						elm_fileselector_multi_select_set(child, EINA_FALSE);
						elm_fileselector_hidden_visible_set(child, EINA_TRUE);
						elm_object_text_set(child, "Select file");
						evas_object_smart_callback_add(child, "file,chosen",
							_property_path_chosen, prop);
						//TODO MIME type
					}
				}
				else // !editable
				{
					child = elm_label_add(lay);
					if(child)
						evas_object_size_hint_align_set(child, 0.f, EVAS_HINT_FILL);
				}
			}
			else if(prop->type_urid == ui->forge.Chunk)
			{
				if(prop->editable)
				{
					child = elm_fileselector_button_add(lay);
					if(child)
					{
						elm_fileselector_button_inwin_mode_set(child, EINA_FALSE);
						elm_fileselector_button_window_title_set(child, "Select file");

						const LilvNode *bundle_uri = lilv_plugin_get_bundle_uri(mod->plug);
#if defined(LILV_0_22)
						char *mod_path = lilv_file_uri_parse(lilv_node_as_uri(bundle_uri), NULL);
#else
						const char *mod_path = lilv_uri_to_path(lilv_node_as_uri(bundle_uri));
#endif
						if(mod_path)
						{
							elm_fileselector_path_set(child, mod_path);
#if defined(LILV_0_22)
							free(mod_path);
#endif
						}
						elm_fileselector_is_save_set(child, EINA_FALSE);
						elm_fileselector_folder_only_set(child, EINA_FALSE);
						elm_fileselector_expandable_set(child, EINA_TRUE);
						elm_fileselector_multi_select_set(child, EINA_FALSE);
						elm_fileselector_hidden_visible_set(child, EINA_TRUE);
						elm_object_text_set(child, "Select file");
						evas_object_smart_callback_add(child, "file,chosen",
							_property_chunk_chosen, prop);
						//TODO MIME type
					}
				}
				else // !editable
				{
					child = elm_label_add(lay);
					if(child)
						evas_object_size_hint_align_set(child, 0.f, EVAS_HINT_FILL);
				}
			}
			else if( (prop->type_urid == ui->forge.Int)
				|| (prop->type_urid == ui->forge.Long)
				|| (prop->type_urid == ui->forge.Float)
				|| (prop->type_urid == ui->forge.Double) )
			{
				if(prop->is_bitmask)
				{
					child = smart_bitmask_add(evas_object_evas_get(lay));
					if(child)
					{
						smart_bitmask_color_set(child, mod->col);
						smart_bitmask_disabled_set(child, !prop->editable);
						const int nbits = log2(prop->maximum + 1);
						smart_bitmask_bits_set(child, nbits);
						if(prop->editable)
							evas_object_smart_callback_add(child, "changed", _property_bitmask_changed, prop);
						evas_object_smart_callback_add(child, "cat,in", _smart_mouse_in, mod);
						evas_object_smart_callback_add(child, "cat,out", _smart_mouse_out, mod);
					}
				}
				else // !is_bitmask
				{
					child = smart_slider_add(evas_object_evas_get(lay));
					if(child)
					{
						int integer = (prop->type_urid == ui->forge.Int)
							|| (prop->type_urid == ui->forge.Long);
						double min = prop->minimum;
						double max = prop->maximum;
						double dflt = prop->minimum; //FIXME

						smart_slider_range_set(child, min, max, dflt);
						smart_slider_color_set(child, mod->col);
						smart_slider_integer_set(child, integer);
						//smart_slider_logarithmic_set(child, logarithmic); //TODO
						smart_slider_format_set(child, integer ? "%.0f %s" : "%.4f %s"); //TODO handle MIDI notes
						smart_slider_disabled_set(child, !prop->editable);
						if(prop->unit)
						{
							if(prop->unit == ui->regs.units.midiController.urid)
							{
								smart_slider_lookup_set(child, _midi_controller_lookup);
							}
							else if(prop->unit == ui->regs.units.midiNote.urid)
							{
								smart_slider_lookup_set(child, _midi_note_lookup);
							}
							else
							{
								const char *uri = ui->driver->unmap->unmap(ui->driver->unmap->handle, prop->unit);
								LilvNode *unit = uri ? lilv_new_uri(ui->world, uri) : NULL;
								if(unit)
								{
									LilvNode *symbol = lilv_world_get(ui->world, unit, ui->regs.units.symbol.node, NULL);
									if(symbol)
									{
										smart_slider_unit_set(child, lilv_node_as_string(symbol));
										lilv_node_free(symbol);
									}
									
									lilv_node_free(unit);
								}
							}
						}
						if(prop->editable)
							evas_object_smart_callback_add(child, "changed", _property_sldr_changed, prop);
						evas_object_smart_callback_add(child, "cat,in", _smart_mouse_in, mod);
						evas_object_smart_callback_add(child, "cat,out", _smart_mouse_out, mod);
					}
				}
			}
			else if(prop->type_urid == ui->forge.Bool)
			{
				child = smart_toggle_add(evas_object_evas_get(lay));
				if(child)
				{
					smart_toggle_color_set(child, mod->col);
					smart_toggle_disabled_set(child, !prop->editable);
					if(prop->editable)
						evas_object_smart_callback_add(child, "changed", _property_check_changed, prop);
					evas_object_smart_callback_add(child, "cat,in", _smart_mouse_in, mod);
					evas_object_smart_callback_add(child, "cat,out", _smart_mouse_out, mod);
				}
			}
		}
		else // scale_points
		{
			child = smart_spinner_add(evas_object_evas_get(lay));
			if(child)
			{
				smart_spinner_color_set(child, mod->col);
				smart_spinner_disabled_set(child, !prop->editable);
				Eina_List *l;
				point_t *p;
				EINA_LIST_FOREACH(prop->scale_points, l, p)
				{
					if(prop->type_urid == ui->forge.String)
						smart_spinner_key_add(child, p->s, p->label);
					else
						smart_spinner_value_add(child, *p->d, p->label);
				}
				if(prop->editable)
					evas_object_smart_callback_add(child, "changed", _property_spinner_changed, prop);
				evas_object_smart_callback_add(child, "cat,in", _smart_mouse_in, mod);
				evas_object_smart_callback_add(child, "cat,out", _smart_mouse_out, mod);
			}
		}

		// send patch:Get
		size_t len = sizeof(transfer_patch_get_t);
		for(unsigned index=0; index<mod->num_ports; index++)
		{
			port_t *port = &mod->ports[index];

			// only consider event ports which support patch:Message
			if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
				|| (port->direction != PORT_DIRECTION_INPUT)
				|| !port->patchable)
			{
				continue; // skip
			}

			transfer_patch_get_t *trans = _sp_ui_to_app_request(ui, len);
			if(trans)
			{
				_sp_transfer_patch_get_fill(&ui->regs,
					&ui->forge, trans, mod->uid, index,
					mod->subject, prop->tar_urid);
				_sp_ui_to_app_advance(ui, len);
			}
		}

		if(child)
		{
			evas_object_show(child);
			elm_layout_content_set(lay, "elm.swallow.content", child);
		}

		prop->std.widget = child; //FIXME reset to NULL + std.entry + std.elmnt
	} // lay

	return lay;
}

void
_property_itc_add(sp_ui_t *ui)
{
	ui->propitc = elm_genlist_item_class_new();
	if(ui->propitc)
	{
		ui->propitc->item_style = "full";
		ui->propitc->func.text_get = NULL;
		ui->propitc->func.content_get = _property_content_get;
		ui->propitc->func.state_get = NULL;
		ui->propitc->func.del = NULL;
	}
}
