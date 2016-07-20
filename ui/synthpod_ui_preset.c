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

static const char *new_preset_label = "(enter label and store as new preset)";
static const char *default_preset = "Default Factory Preset";

static int
_pset_cmp(const void *data1, const void *data2)
{
	const Elm_Object_Item *itm1 = data1;
	const Elm_Object_Item *itm2 = data2;
	if(!itm1 || !itm2)
		return 1;

	const LilvNode *node1= elm_object_item_data_get(itm1);
	const LilvNode *node2= elm_object_item_data_get(itm2);
	if(!node1 || !node2)
		return 1;

	Evas_Object *obj = elm_object_item_widget_get(itm1);
	if(!obj)
		return 1;

	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	if(!ui)
		return 1;

	LilvNode *lbl1 = lilv_world_get(ui->world, node1, ui->regs.rdfs.label.node, NULL);
	if(!lbl1)
		return 1;

	LilvNode *lbl2 = lilv_world_get(ui->world, node2, ui->regs.rdfs.label.node, NULL);
	if(!lbl2)
	{
		lilv_node_free(lbl1);
		return 1;
	}

	const char *uri1 = lilv_node_as_string(lbl1);
	const char *uri2 = lilv_node_as_string(lbl2);

	const int res = uri1 && uri2
		? strcasecmp(uri1, uri2)
		: 1;

	lilv_node_free(lbl1);
	lilv_node_free(lbl2);

	return res;
}

static void
_pset_save_markup(void *data, Evas_Object *obj, char **txt)
{
	// intercept enter
	if(!strcmp(*txt, "<tab/>") || !strcmp(*txt, " "))
	{
		free(*txt);
		*txt = strdup("_");
	}
}

static void
_pset_save_activated(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	mod_t *mod = ui->psetmod;

	const char *chunk = elm_entry_entry_get(obj);
	char *utf8 = elm_entry_markup_to_utf8(chunk);
	if(!utf8)
		return;

	// signal app
	size_t size = sizeof(transmit_module_preset_save_t)
		+ lv2_atom_pad_size(strlen(utf8) + 1);
	transmit_module_preset_save_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_preset_save_fill(&ui->regs, &ui->forge, trans, size, mod->uid, utf8);
		_sp_ui_to_app_advance(ui, size);
	}

	free(utf8);

	elm_entry_entry_set(obj, new_preset_label);
}

static char * 
_presetlist_label_get(void *data, Evas_Object *obj, const char *part)
{
	const LilvNode* preset = data;
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	if(!ui)
		return NULL;
	mod_t *mod = ui->psetmod;

	if(!strcmp(part, "elm.text"))
	{
		char *lbl = NULL;

		if(preset == lilv_plugin_get_uri(mod->plug))
			return strdup(default_preset);

		LilvNode *label = lilv_world_get(ui->world, preset,
			ui->regs.rdfs.label.node, NULL);
		if(label)
		{
			const char *label_str = lilv_node_as_string(label);
			if(label_str)
				lbl = strdup(label_str);
			lilv_node_free(label);
		}

		return lbl;
	}

	return NULL;
}

static Evas_Object *
_presetlist_content_get(void *data, Evas_Object *obj, const char *part)
{
	const LilvNode* preset = data;
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	if(!ui)
		return NULL;

	const char *uri = lilv_node_as_uri(preset);
	if(!strcmp(part, "elm.swallow.end"))
	{
		if(!strncmp(uri, "file://", 7) && ecore_file_can_write(&uri[7]) ) // is this a user preset?
		{
			Evas_Object *icon = elm_icon_add(obj);
			if(icon)
			{
				elm_icon_standard_set(icon, "user-home");
				evas_object_size_hint_weight_set(icon, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(icon, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(icon);

				return icon;
			}
		}
	}

	return NULL;
}

static char * 
_banklist_label_get(void *data, Evas_Object *obj, const char *part)
{
	const LilvNode* bank = data;
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	if(!ui)
		return NULL;

	if(!strcmp(part, "elm.text"))
	{
		char *lbl = NULL;

		if(!bank)
			return strdup("w/o bank");

		LilvNode *label = lilv_world_get(ui->world, bank,
			ui->regs.rdfs.label.node, NULL);
		if(label)
		{
			const char *label_str = lilv_node_as_string(label);
			if(label_str)
				lbl = strdup(label_str);
			lilv_node_free(label);
		}

		return lbl;
	}

	return NULL;
}

static void
_banklist_del(void *data, Evas_Object *obj)
{
	LilvNode *bank = data;

	lilv_node_free(bank);
}

static void
_menu_preset_del(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	ui->presetwin = NULL;
	ui->psetentry = NULL;
	ui->psetlist = NULL;
	ui->psetinfo = NULL;
}

static inline bool
_psetlist_populate_name(sp_ui_t *ui, const LilvNode *preset, const char *match)
{
	bool valid = false;

	LilvNode *label = lilv_world_get(ui->world, preset,
		ui->regs.rdfs.label.node, NULL);
	if(label)
	{
		if(lilv_node_is_string(label) && strcasestr(lilv_node_as_string(label), match))
			valid = true;

		lilv_node_free(label);
	}

	return valid;
}

static inline  Elm_Object_Item *
_psetlist_get_bank(sp_ui_t *ui, const LilvNode *bank)
{
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(ui->psetlist);
		itm;
		itm = elm_genlist_item_next_get(itm))
	{
		const LilvNode *ref = elm_object_item_data_get(itm);

		if(lilv_node_equals(bank, ref))
			return itm;
	}

	// create new
	LilvNode *bank_dup = bank ? lilv_node_duplicate(bank) : NULL;
	Elm_Object_Item *elmnt = elm_genlist_item_append(ui->psetlist, ui->bankitc, bank_dup, NULL,
		ELM_GENLIST_ITEM_GROUP, NULL, NULL);
	elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_NONE);

	return elmnt;
}

void
_psetlist_populate(sp_ui_t *ui, const char *match)
{
	if(!ui || !ui->psetmod || !ui->psetlist || !ui->presetitc)
		return;

	if(ui->psetinfo)
		elm_genlist_clear(ui->psetinfo);

	mod_t *mod = ui->psetmod;

	LILV_FOREACH(nodes, itr, mod->presets)
	{
		const LilvNode *preset= lilv_nodes_get(mod->presets, itr);
		if(!preset)
			continue;

		if( (strlen(match) == 0) || _psetlist_populate_name(ui, preset, match))
		{
			LilvNodes *preset_banks = lilv_world_find_nodes(ui->world,
				preset, ui->regs.pset.preset_bank.node, NULL);
			if(preset_banks)
			{
				LILV_FOREACH(nodes, j, preset_banks)
				{
					const LilvNode *bank = lilv_nodes_get(preset_banks, j);
					if(!bank)
						continue;

					Elm_Object_Item *parent =  _psetlist_get_bank(ui, bank);
					Elm_Object_Item *elmnt = elm_genlist_item_sorted_insert(ui->psetlist, ui->presetitc, preset, parent,
						ELM_GENLIST_ITEM_NONE, _pset_cmp, NULL, NULL);
					(void)elmnt;
				}
				lilv_nodes_free(preset_banks);
			}
			else // not part of any bank
			{
				Elm_Object_Item *parent =  _psetlist_get_bank(ui, NULL);
				Elm_Object_Item *elmnt = elm_genlist_item_sorted_insert(ui->psetlist, ui->presetitc, preset, parent,
					ELM_GENLIST_ITEM_NONE, _pset_cmp, NULL, NULL);
				(void)elmnt;
			}
		}
	}

	// add default state
	if( (strlen(match) == 0) || strcasestr(default_preset, match) )
	{
		Elm_Object_Item *parent =  _psetlist_get_bank(ui, NULL);
		Elm_Object_Item *elmnt = elm_genlist_item_sorted_insert(ui->psetlist, ui->presetitc, lilv_plugin_get_uri(mod->plug), parent,
			ELM_GENLIST_ITEM_NONE, _pset_cmp, NULL, NULL);
		(void)elmnt;
	}
}

static void
_psetentry_changed(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	if(!ui || !ui->psetlist)
		return;

	const char *chunk = elm_entry_entry_get(obj);
	char *match = elm_entry_markup_to_utf8(chunk);

	elm_genlist_clear(ui->psetlist);
	_psetlist_populate(ui, match); // populate with matching plugins
	free(match);
}

static void
_psetlist_activated(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	mod_t *mod = ui->psetmod;
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
}

static inline Evas_Object *
_menu_preset_new(sp_ui_t *ui)
{
	const char *title = "Preset";
	Evas_Object *win = elm_win_add(ui->win, title, ELM_WIN_BASIC);
	if(win)
	{
		elm_win_title_set(win, title);
		elm_win_autodel_set(win, EINA_TRUE);
		evas_object_smart_callback_add(win, "delete,request", _menu_preset_del, ui);
		evas_object_resize(win, 480, 480);
		evas_object_show(win);

		Evas_Object *bg = elm_bg_add(win);
		if(bg)
		{
			elm_bg_color_set(bg, 64, 64, 64);
			evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(bg, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(bg);
			elm_win_resize_object_add(win, bg);
		} // bg

		Evas_Object *psetbox = elm_box_add(win);
		if(psetbox)
		{
			elm_box_horizontal_set(psetbox, EINA_FALSE);
			elm_box_homogeneous_set(psetbox, EINA_FALSE);
			evas_object_data_set(psetbox, "ui", ui);
			evas_object_size_hint_weight_set(psetbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(psetbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(psetbox);
			elm_win_resize_object_add(win, psetbox);

			ui->psetentry = elm_entry_add(psetbox);
			if(ui->psetentry)
			{
				elm_entry_entry_set(ui->psetentry, "");
				elm_entry_editable_set(ui->psetentry, EINA_TRUE);
				elm_entry_single_line_set(ui->psetentry, EINA_TRUE);
				elm_entry_scrollable_set(ui->psetentry, EINA_TRUE);
				evas_object_smart_callback_add(ui->psetentry, "changed,user", _psetentry_changed, ui);
				evas_object_data_set(ui->psetentry, "ui", ui);
				evas_object_size_hint_align_set(ui->psetentry, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(ui->psetentry);
				elm_box_pack_end(psetbox, ui->psetentry);
			} // psetentry

			ui->psetlist = elm_genlist_add(psetbox);
			if(ui->psetlist)
			{
				elm_genlist_homogeneous_set(ui->psetlist, EINA_TRUE); // needef for lazy-loading
				elm_genlist_mode_set(ui->psetlist, ELM_LIST_LIMIT);
				elm_genlist_block_count_set(ui->psetlist, 64); // needef for lazy-loading
				evas_object_smart_callback_add(ui->psetlist, "activated",
					_psetlist_activated, ui);
				evas_object_data_set(ui->psetlist, "ui", ui);
				evas_object_size_hint_weight_set(ui->psetlist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(ui->psetlist, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(ui->psetlist);
				elm_box_pack_end(psetbox, ui->psetlist);
			} // psetlist

			_psetlist_populate(ui, ""); // populate with everything

			Evas_Object *entry = elm_entry_add(psetbox);
			if(entry)
			{
				elm_entry_entry_set(entry, new_preset_label);
				elm_entry_editable_set(entry, EINA_TRUE);
				elm_entry_single_line_set(entry, EINA_TRUE);
				elm_entry_scrollable_set(entry, EINA_TRUE);
				elm_entry_markup_filter_append(entry, _pset_save_markup, ui);
				evas_object_smart_callback_add(entry, "activated", _pset_save_activated, ui);
				evas_object_data_set(entry, "ui", ui);
				evas_object_size_hint_align_set(entry, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(entry);
				elm_box_pack_end(psetbox, entry);
			}
		} // psetbox

		return win;
	}

	return NULL;
}

void
_menu_preset(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(!ui->presetwin)
		ui->presetwin = _menu_preset_new(ui);
}

void
_presetlist_itc_add(sp_ui_t *ui)
{
	ui->presetitc = elm_genlist_item_class_new();
	if(ui->presetitc)
	{
		ui->presetitc->item_style = "default";
		ui->presetitc->func.text_get = _presetlist_label_get;
		ui->presetitc->func.content_get = _presetlist_content_get;
		ui->presetitc->func.state_get = NULL;
		ui->presetitc->func.del = NULL;
	}

	ui->bankitc = elm_genlist_item_class_new();
	if(ui->bankitc)
	{
		ui->bankitc->item_style = "group_index";
		ui->bankitc->func.text_get = _banklist_label_get;
		ui->bankitc->func.content_get = NULL;
		ui->bankitc->func.state_get = NULL;
		ui->bankitc->func.del = _banklist_del;
	}
}
