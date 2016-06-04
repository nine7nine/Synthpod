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

void
_modgrid_item_size_update(sp_ui_t *ui)
{
	int w, h;
	evas_object_geometry_get(ui->modgrid, NULL, NULL, &w, &h);

	const int iw = w / ui->ncols;
	const int ih = (h - 20) / ui->nrows;
	elm_gengrid_item_size_set(ui->modgrid, iw, ih);
}

static int
_preset_label_cmp(mod_t *mod, const LilvNode *pset1, const LilvNode *pset2)
{
	if(!pset1 || !pset2 || !mod)
		return 1;

	sp_ui_t *ui = mod->ui;
	LilvNode *lbl1 = lilv_world_get(ui->world, pset1, ui->regs.rdfs.label.node, NULL);
	if(!lbl1)
		return 1;

	LilvNode *lbl2 = lilv_world_get(ui->world, pset2, ui->regs.rdfs.label.node, NULL);
	if(!lbl2)
	{
		lilv_node_free(lbl1);
		return 1;
	}

	const char *uri1 = lilv_node_as_string(lbl1);
	const char *uri2 = lilv_node_as_string(lbl2);

	int res = uri1 && uri2
		? strcasecmp(uri1, uri2)
		: 1;

	lilv_node_free(lbl1);
	lilv_node_free(lbl2);

	return res;
}

static int
_itmitc_cmp(const void *data1, const void *data2)
{
	const Elm_Object_Item *itm1 = data1;
	const Elm_Object_Item *itm2 = data2;
	if(!itm1 || !itm2)
		return 1;

	const Elm_Object_Item *par2 = elm_genlist_item_parent_get(itm1); // psetitc
	if(!par2)
		return 1;

	const Elm_Genlist_Item_Class *class1 = elm_genlist_item_item_class_get(itm1);
	const Elm_Genlist_Item_Class *class2 = elm_genlist_item_item_class_get(itm2);
	if(class1 != class2)
		return -1; // banks before presets

	const LilvNode *pset1 = elm_object_item_data_get(itm1);
	const LilvNode *pset2 = elm_object_item_data_get(itm2);
	mod_t *mod = elm_object_item_data_get(par2);

	return _preset_label_cmp(mod, pset1, pset2);
}

static int
_bnkitc_cmp(const void *data1, const void *data2)
{
	const Elm_Object_Item *itm1 = data1;
	const Elm_Object_Item *itm2 = data2;
	if(!itm1 || !itm2)
		return 1;

	const Elm_Object_Item *par1 = elm_genlist_item_parent_get(itm1); // bnkitc
	if(!par1)
		return 1;

	const Elm_Object_Item *par2 = elm_genlist_item_parent_get(par1); // psetitc
	if(!par2)
		return 1;

	const LilvNode *pset1 = elm_object_item_data_get(itm1);
	const LilvNode *pset2 = elm_object_item_data_get(itm2);
	mod_t *mod = elm_object_item_data_get(par2);

	return _preset_label_cmp(mod, pset1, pset2);
}

static void
_modgrid_expanded(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;
	Elm_Object_Item *elmnt;

	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);

	if(class == ui->psetitc) // is presets item
	{
		mod_t *mod = elm_object_item_data_get(itm);

		if(mod->banks)
		{
			Eina_List *l;
			LilvNode *bank;
			EINA_LIST_FOREACH(mod->banks, l, bank)
			{
				elmnt = elm_genlist_item_sorted_insert(mod->std.list, ui->psetbnkitc, bank, itm,
					ELM_GENLIST_ITEM_TREE, _itmitc_cmp, NULL, NULL);
				elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_DEFAULT);
			}
		}

		LILV_FOREACH(nodes, i, mod->presets)
		{
			const LilvNode* preset = lilv_nodes_get(mod->presets, i);
			if(!preset)
				continue;

			LilvNode *bank = lilv_world_get(ui->world, preset,
				ui->regs.pset.preset_bank.node, NULL);
			if(bank)
			{
				lilv_node_free(bank);
				continue; // ignore presets which are part of a bank
			}

			elmnt = elm_genlist_item_sorted_insert(mod->std.list, ui->psetitmitc, preset, itm,
				ELM_GENLIST_ITEM_NONE, _itmitc_cmp, NULL, NULL);
			elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_DEFAULT);
		}

		elmnt = elm_genlist_item_append(mod->std.list, ui->psetsaveitc, mod, itm,
			ELM_GENLIST_ITEM_NONE, NULL, NULL);
		elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_DEFAULT);
	}
	else if(class == ui->psetbnkitc) // is preset bank item
	{
		LilvNode *bank = elm_object_item_data_get(itm);
		Elm_Object_Item *parent = elm_genlist_item_parent_get(itm); // psetitc
		mod_t *mod = elm_object_item_data_get(parent);

		LilvNodes *presets = lilv_world_find_nodes(ui->world, NULL,
			ui->regs.pset.preset_bank.node, bank);
		LILV_FOREACH(nodes, i, presets)
		{
			const LilvNode *preset = lilv_nodes_get(presets, i);

			// lookup and reference corresponding preset in mod->presets
			const LilvNode *ref = NULL;
			LILV_FOREACH(nodes, j, mod->presets)
			{
				const LilvNode *_preset = lilv_nodes_get(mod->presets, j);
				if(lilv_node_equals(preset, _preset))
				{
					ref = _preset;
					break;
				}
			}

			if(ref)
			{
				elmnt = elm_genlist_item_sorted_insert(mod->std.list, ui->psetitmitc, ref, itm,
					ELM_GENLIST_ITEM_NONE, _bnkitc_cmp, NULL, NULL);
				elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_DEFAULT);
			}
		}
		lilv_nodes_free(presets);
	}
}

static void
_modgrid_contracted(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	// clear items
	elm_genlist_item_subitems_clear(itm);

	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);
	if(class == ui->moditc)
	{
		mod_t *mod = elm_object_item_data_get(itm);
		eina_hash_free(mod->groups);
		mod->groups = NULL;
	}
}

static void
_modgrid_activated(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);

	if(class == ui->psetitmitc) // is presets item
	{
		// get parent item
		Elm_Object_Item *parent = elm_genlist_item_parent_get(itm); // psetbnkitc || psetitc
		if(!parent)
			return;

		const Elm_Genlist_Item_Class *parent_class = elm_genlist_item_item_class_get(parent);
		if(parent_class == ui->psetbnkitc)
		{
			parent = elm_genlist_item_parent_get(parent); // psetitc

			if(!parent)
				return;
		}

		mod_t *mod = elm_object_item_data_get(parent);
		if(!mod)
			return;

		const LilvNode* preset = elm_object_item_data_get(itm);
		if(!preset)
			return;

		const char *uri = lilv_node_as_uri(preset);
		if(!uri)
			return;

		// signal app
		size_t size = sizeof(transmit_module_preset_load_t)
			+ lv2_atom_pad_size(strlen(uri) + 1);
		transmit_module_preset_load_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transmit_module_preset_load_fill(&ui->regs, &ui->forge, trans, size, mod->uid, uri);
			_sp_ui_to_app_advance(ui, size);
		}

		// contract parent list item
		//evas_object_smart_callback_call(obj, "contract,request", parent);
	}

	//TODO toggle checkboxes on modules and ports
}

static void
_modgrid_changed(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	_modgrid_item_size_update(ui);
}

static void
_modgrid_mouse_in(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(ui->modgrid)
		elm_scroller_movement_block_set(ui->modgrid,
			ELM_SCROLLER_MOVEMENT_BLOCK_HORIZONTAL | ELM_SCROLLER_MOVEMENT_BLOCK_VERTICAL);
}

static void
_modgrid_mouse_out(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(ui->modgrid)
		elm_scroller_movement_block_set(ui->modgrid, ELM_SCROLLER_MOVEMENT_NO_BLOCK);
}

static void
_list_expand_request(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;

	elm_genlist_item_expanded_set(itm, EINA_TRUE);
}

static void
_list_contract_request(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;

	elm_genlist_item_expanded_set(itm, EINA_FALSE);
}

static void
_content_del(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;

	if(mod)
		mod->std.list = NULL;
}

static Evas_Object *
_modgrid_content_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	Evas_Object *frame = elm_frame_add(obj);
	if(frame)
	{
		elm_object_text_set(frame, mod->name);
		evas_object_event_callback_add(frame, EVAS_CALLBACK_DEL, _content_del, mod);
		evas_object_size_hint_weight_set(frame, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(frame, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(frame);
		evas_object_show(frame);

		Evas_Object *modlist = elm_genlist_add(frame);
		if(modlist)
		{
			elm_genlist_homogeneous_set(modlist, EINA_TRUE); // needef for lazy-loading
			elm_genlist_mode_set(modlist, ELM_LIST_LIMIT);
			elm_genlist_block_count_set(modlist, 64); // needef for lazy-loading
			//elm_genlist_select_mode_set(modlist, ELM_OBJECT_SELECT_MODE_NONE);
			elm_genlist_reorder_mode_set(modlist, EINA_FALSE);
			evas_object_smart_callback_add(modlist, "expand,request",
				_list_expand_request, ui);
			evas_object_smart_callback_add(modlist, "contract,request",
				_list_contract_request, ui);
			evas_object_smart_callback_add(modlist, "expanded",
				_modgrid_expanded, ui);
			evas_object_smart_callback_add(modlist, "contracted",
				_modgrid_contracted, ui);
			evas_object_smart_callback_add(modlist, "activated",
				_modgrid_activated, ui);
			evas_object_data_set(modlist, "ui", ui);
			evas_object_size_hint_weight_set(modlist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(modlist, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(modlist);
			elm_object_content_set(frame, modlist);
			mod->std.list = modlist;

			// port groups
			mod->groups = eina_hash_string_superfast_new(NULL); //TODO check

			// port entries
			for(unsigned i=0; i<mod->num_ports; i++)
			{
				port_t *port = &mod->ports[i];

				Elm_Object_Item *parent;
				group_t *group;

				if(port->group)
				{
					const char *group_lbl = lilv_node_as_string(port->group);
					group = _mod_group_get(mod, group_lbl, GROUP_TYPE_PORT, port->group, &parent, false);
				}
				else
				{
					const char *group_lbl = "*Ungrouped*";
					group = _mod_group_get(mod, group_lbl, GROUP_TYPE_PORT, NULL, &parent, false);
				}

				// append port to corresponding group
				if(group)
					group->children = eina_list_append(group->children, port);
			}

			{
				const char *group_lbl = "*Properties*";
				Elm_Object_Item *parent;
				group_t *group = _mod_group_get(mod, group_lbl, GROUP_TYPE_PROPERTY, NULL, &parent, false);

				Eina_List *l;
				property_t *prop;
				EINA_LIST_FOREACH(mod->static_properties, l, prop)
				{
					// append property to corresponding group
					if(group)
						group->children = eina_list_append(group->children, prop);
				}
			}

			// presets //FIXME put in vbox 
			Elm_Object_Item *elmnt = elm_genlist_item_sorted_insert(mod->std.list, ui->psetitc, mod, NULL,
				ELM_GENLIST_ITEM_TREE, _grpitc_cmp, NULL, NULL);
			elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_DEFAULT);

			// expand all groups by default
			eina_hash_foreach(mod->groups, _groups_foreach, ui);

			// refresh state
			_module_patch_get_all(mod);
		} // modlist
	}

	return frame;
}

static void
_modgrid_del(void *data, Evas_Object *obj)
{
	mod_t *mod = data;

	if(mod)
	{
		mod->std.grid = NULL; // clear item pointer

		// refresh modlist item
		if(mod->std.elmnt)
			elm_genlist_item_update(mod->std.elmnt);
	}
}

void
_modgrid_itc_add(sp_ui_t *ui)
{
	ui->griditc = elm_gengrid_item_class_new();
	if(ui->griditc)
	{
		ui->griditc->item_style = "synthpod";
		ui->griditc->func.text_get = NULL;
		ui->griditc->func.content_get = _modgrid_content_get;
		ui->griditc->func.state_get = NULL;
		ui->griditc->func.del = _modgrid_del;
	}
}

void
_modgrid_set_callbacks(sp_ui_t *ui)
{
	evas_object_smart_callback_add(ui->modgrid, "changed",
		_modgrid_changed, ui);
}
