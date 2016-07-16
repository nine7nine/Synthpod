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
_grpitc_cmp(const void *data1, const void *data2)
{
	const Elm_Object_Item *itm1 = data1;
	const Elm_Object_Item *itm2 = data2;

	group_t *grp1 = elm_object_item_data_get(itm1);
	group_t *grp2 = elm_object_item_data_get(itm2);

	// compare group type or property module uid
	return grp1->type < grp2->type
		? -1
		: (grp1->type > grp2->type
			? 1
			: 0);
}

Eina_Bool
_groups_foreach(const Eina_Hash *hash, const void *key, void *data, void *fdata)
{
	Elm_Object_Item *itm = data;
	sp_ui_t *ui = fdata;
	Elm_Object_Item *elmnt;
	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);

	if(class == ui->grpitc) // is group
	{
		group_t *group = elm_object_item_data_get(itm);
		mod_t *mod = group->mod;
		
		if(group->type == GROUP_TYPE_PORT)
		{
			Eina_List *l;
			port_t *port;
			EINA_LIST_FOREACH(group->children, l, port)
			{
				elmnt = elm_genlist_item_sorted_insert(mod->std.list, ui->stditc, port, itm,
					ELM_GENLIST_ITEM_NONE, _stditc_cmp, NULL, NULL);
				if(elmnt)
				{
					_ui_port_tooltip_add(ui, elmnt, port);
					elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_NONE);
				}
			}
		}
		else if(group->type == GROUP_TYPE_PROPERTY)
		{
			Eina_List *l;
			property_t *prop;
			EINA_LIST_FOREACH(group->children, l, prop)
			{
				elmnt = elm_genlist_item_sorted_insert(mod->std.list, ui->propitc, prop, itm,
					ELM_GENLIST_ITEM_NONE, _propitc_cmp, NULL, NULL);
				if(elmnt)
				{
					int select_mode = prop->editable
						? ( (prop->type_urid == ui->forge.String) || (prop->type_urid == ui->forge.URI)
							? ELM_OBJECT_SELECT_MODE_DEFAULT
							: ELM_OBJECT_SELECT_MODE_NONE)
						: ELM_OBJECT_SELECT_MODE_NONE;
					elm_genlist_item_select_mode_set(elmnt, select_mode);
					_ui_property_tooltip_add(ui, elmnt, prop);
					prop->std.elmnt = elmnt;
				}
			}
		}
	}
	else
	{
		printf("is not a group, expanding\n"); //FIXME not needed
		elm_genlist_item_expanded_set(itm, EINA_TRUE);
	}

	return EINA_TRUE;
}

static Evas_Object *
_group_content_get(void *data, Evas_Object *obj, const char *part)
{
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	group_t *group = data;
	if(!group || !ui)
		return NULL;

	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	Evas_Object *lay = elm_layout_add(obj);
	if(lay)
	{
		elm_layout_file_set(lay, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/group/theme");
		char col [7];
		sprintf(col, "col,%02i", group->mod->col);
		elm_layout_signal_emit(lay, col, "/synthpod/group/ui");
		evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(lay);

		if(group->node)
		{
			LilvNode *label = lilv_world_get(ui->world, group->node,
				ui->regs.core.name.node, NULL);
			if(label)
			{
				const char *label_str = lilv_node_as_string(label);

				if(label_str)
					elm_object_part_text_set(lay, "elm.text", label_str);

				lilv_node_free(label);
			}
		}
		else
		{
			if(group->type == GROUP_TYPE_PORT)
				elm_object_part_text_set(lay, "elm.text", "Ports");
			else if(group->type == GROUP_TYPE_PROPERTY)
				elm_object_part_text_set(lay, "elm.text", "Properties");
		}
	}

	return lay;
}

static void
_group_del(void *data, Evas_Object *obj)
{
	group_t *group = data;

	if(group)
	{
		if(group->children)
			eina_list_free(group->children);
		free(group);
	}
}

void
_group_itc_add(sp_ui_t *ui)
{
	ui->grpitc = elm_genlist_item_class_new();
	if(ui->grpitc)
	{
		ui->grpitc->item_style = "full";
		ui->grpitc->func.text_get = NULL;
		ui->grpitc->func.content_get = _group_content_get;
		ui->grpitc->func.state_get = NULL;
		ui->grpitc->func.del = _group_del;
	}
}
