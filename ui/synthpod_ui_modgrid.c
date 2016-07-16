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
