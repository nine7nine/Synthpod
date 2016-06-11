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


#define INFO_PRE "<color=#bbb font=Mono>"
#define INFO_POST "</color>"
#define LINK_PRE "<underline=on underline_color=#bbb>"
#define LINK_POST "</underline>"

typedef bool (*populate_t)(sp_ui_t *ui, const LilvPlugin *plug, const char *match);

static char * 
_pluglist_label_get(void *data, Evas_Object *obj, const char *part)
{
	const plug_info_t *info = data;
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	if(!ui || !info)
		return NULL;

	switch(info->type)
	{
		case PLUG_INFO_TYPE_NAME:
		{
			LilvNode *node = lilv_plugin_get_name(info->plug);

			char *str = NULL;
			asprintf(&str, "%s", node && lilv_node_is_string(node)
				? lilv_node_as_string(node)
				: "");
			if(node)
				lilv_node_free(node);

			// is this module supported at all?
			const LilvNode *uri_node = lilv_plugin_get_uri(info->plug);
			if(uri_node)
			{
				const char *uri_str = lilv_node_as_string(uri_node);

				size_t size = sizeof(transmit_module_supported_t)
					+ lv2_atom_pad_size(strlen(uri_str) + 1);
				transmit_module_supported_t *trans = _sp_ui_to_app_request(ui, size);
				if(trans)
				{
					_sp_transmit_module_supported_fill(&ui->regs, &ui->forge, trans, size, -1, uri_str);
					_sp_ui_to_app_advance(ui, size);
				}
			}

			return str;
		}
		case PLUG_INFO_TYPE_URI:
		{
			const LilvNode *node = lilv_plugin_get_uri(info->plug);

			char *str = NULL;
			asprintf(&str, INFO_PRE"URI     "INFO_POST" "LINK_PRE"%s"LINK_POST, node && lilv_node_is_uri(node)
				? lilv_node_as_uri(node)
				: "");

			return str;
		}
		case PLUG_INFO_TYPE_CLASS:
		{
			const LilvPluginClass *class = lilv_plugin_get_class(info->plug);
			const LilvNode *node = class
				? lilv_plugin_class_get_label(class)
				: NULL;

			char *str = NULL;
			asprintf(&str, INFO_PRE"Class   "INFO_POST" %s", node && lilv_node_is_string(node)
				? lilv_node_as_string(node)
				: "");
		
			return str;
		}
		case PLUG_INFO_TYPE_VERSION:
		{
			LilvNodes *nodes = lilv_plugin_get_value(info->plug,
				ui->regs.core.minor_version.node);
			LilvNode *node = nodes
				? lilv_nodes_get_first(nodes)
				: NULL;
			LilvNodes *nodes2 = lilv_plugin_get_value(info->plug,
				ui->regs.core.micro_version.node);
			LilvNode *node2 = nodes2
				? lilv_nodes_get_first(nodes2)
				: NULL;

			char *str = NULL;
			if(node && node2 && lilv_node_is_int(node) && lilv_node_is_int(node2))
			{
				const int minor = lilv_node_as_int(node);
				const int micro = lilv_node_as_int(node2);
				asprintf(&str, INFO_PRE"Version "INFO_POST" %i . %i", minor, micro);
			}
			else
				asprintf(&str, INFO_PRE"Version "INFO_POST);
			if(nodes)
				lilv_nodes_free(nodes);
			if(nodes2)
				lilv_nodes_free(nodes2);

			return str;
		}
		case PLUG_INFO_TYPE_LICENSE:
		{
			LilvNodes *nodes = lilv_plugin_get_value(info->plug,
				ui->regs.doap.license.node);
			LilvNode *node = nodes
				? lilv_nodes_get_first(nodes)
				: NULL;

			char *str = NULL;
			asprintf(&str, INFO_PRE"License "INFO_POST" "LINK_PRE"%s"LINK_POST, node && lilv_node_is_uri(node)
				? lilv_node_as_uri(node)
				: "");
			if(nodes)
				lilv_nodes_free(nodes);

			return str;
		}
		case PLUG_INFO_TYPE_BUNDLE_URI:
		{
			const LilvNode *node = lilv_plugin_get_bundle_uri(info->plug);

			char *str = NULL;
			asprintf(&str, INFO_PRE"Bundle  "INFO_POST" "LINK_PRE"%s"LINK_POST, node && lilv_node_is_uri(node)
				? lilv_node_as_uri(node)
				: "");

			return str;
		}
		case PLUG_INFO_TYPE_PROJECT:
		{
			LilvNode *node = lilv_plugin_get_project(info->plug);
			LilvNode *name = node
				? lilv_world_get(ui->world, node, ui->regs.doap.name.node, NULL)
				: NULL;

			char *str = NULL;
			if(node && lilv_node_is_uri(node))
			{
				if(name && lilv_node_is_string(name))
				{
					asprintf(&str, INFO_PRE"Project "INFO_POST" "LINK_PRE"%s"LINK_POST" (%s)",
						lilv_node_as_uri(node), lilv_node_as_string(name));
				}
				else
				{
					asprintf(&str, INFO_PRE"Project "INFO_POST" "LINK_PRE"%s"LINK_POST,
						lilv_node_as_uri(node));
				}
			}
			else
			{
				asprintf(&str, INFO_PRE"Project "INFO_POST);
			}
			if(name)
				lilv_node_free(name);
			if(node)
				lilv_node_free(node);

			return str;
		}
		case PLUG_INFO_TYPE_AUTHOR_NAME:
		{
			LilvNode *node = lilv_plugin_get_author_name(info->plug);

			char *str = NULL;
			asprintf(&str, INFO_PRE"Author  "INFO_POST" %s", node && lilv_node_is_string(node)
				? lilv_node_as_string(node)
				: "");
			if(node)
				lilv_node_free(node);

			return str;
		}
		case PLUG_INFO_TYPE_AUTHOR_EMAIL:
		{
			LilvNode *node = lilv_plugin_get_author_email(info->plug);

			char *str = NULL;
			asprintf(&str, INFO_PRE"Email   "INFO_POST" "LINK_PRE"%s"LINK_POST, node && lilv_node_is_uri(node)
				? lilv_node_as_uri(node)
				: "");
			if(node)
				lilv_node_free(node);

			return str;
		}
		case PLUG_INFO_TYPE_AUTHOR_HOMEPAGE:
		{
			LilvNode *node = lilv_plugin_get_author_homepage(info->plug);

			char *str = NULL;
			asprintf(&str, INFO_PRE"Homepage"INFO_POST" "LINK_PRE"%s"LINK_POST, node && lilv_node_is_uri(node)
				? lilv_node_as_uri(node)
				: "");
			if(node)
				lilv_node_free(node);

			return str;
		}
		case PLUG_INFO_TYPE_COMMENT:
		{
			LilvNodes *nodes = lilv_plugin_get_value(info->plug,
				ui->regs.rdfs.comment.node);
			LilvNode *node = nodes
				? lilv_nodes_get_first(nodes)
				: NULL;

			char *str = NULL;
			asprintf(&str, INFO_PRE"Comment "INFO_POST" %s", node && lilv_node_is_string(node)
				? lilv_node_as_string(node)
				: "");
			if(nodes)
				lilv_nodes_free(nodes);

			return str;
		}
		default:
			return NULL;
	}
}

static void
_pluglist_del(void *data, Evas_Object *obj)
{
	plug_info_t *info = data;

	if(info)
		free(info);
}

static void
_pluglist_activated(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;
	plug_info_t *info = elm_object_item_data_get(itm);
	if(!info)
		return;

	const LilvNode *uri_node = lilv_plugin_get_uri(info->plug);
	if(!uri_node)
		return;
	const char *uri_str = lilv_node_as_string(uri_node);

	size_t size = sizeof(transmit_module_add_t)
		+ lv2_atom_pad_size(strlen(uri_str) + 1);
	transmit_module_add_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_add_fill(&ui->regs, &ui->forge, trans, size, 0, uri_str);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_pluglist_selected(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;
	plug_info_t *info = elm_object_item_data_get(itm);
	if(!info)
		return;

	if(ui->pluginfo)
	{
		elm_genlist_clear(ui->pluginfo);

		plug_info_t *child;
		Elm_Object_Item *elmnt;

		for(int t=1; t<PLUG_INFO_TYPE_MAX; t++)
		{
			child = calloc(1, sizeof(plug_info_t));
			if(child)
			{
				child->type = t;
				child->plug = info->plug;
				elmnt = elm_genlist_item_append(ui->pluginfo, ui->plugitc,
					child, NULL, ELM_GENLIST_ITEM_NONE, NULL, NULL);
			}
		}
	}
}

static inline void
_open_uri(const LilvNode *node)
{
	if(!node || !lilv_node_is_uri(node))
		return;

	const char *uri = lilv_node_as_uri(node);
	if(!uri)
		return;

	char *cmd = NULL;
	if(asprintf(&cmd, "xdg-open %s", uri) != -1) //FIXME make this platform independent
	{
		ecore_exe_run(cmd, NULL); //TODO do we need to call ecore_exe_del?
		free(cmd);
	}
}

static void
_pluginfo_activated(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;
	plug_info_t *info = elm_object_item_data_get(itm);
	if(!info)
		return;

	switch(info->type)
	{
		case PLUG_INFO_TYPE_URI:
		{
			const LilvNode *node = lilv_plugin_get_uri(info->plug);
			if(node)
				_open_uri(node);
			break;
		}
		case PLUG_INFO_TYPE_LICENSE:
		{
			LilvNodes *nodes = lilv_plugin_get_value(info->plug,
				ui->regs.doap.license.node);
			LilvNode *node = nodes
				? lilv_nodes_get_first(nodes)
				: NULL;
			if(node)
				_open_uri(node);
			if(nodes)
				lilv_nodes_free(nodes);
			break;
		}
		case PLUG_INFO_TYPE_BUNDLE_URI:
		{
			const LilvNode *node = lilv_plugin_get_bundle_uri(info->plug);
			if(node)
				_open_uri(node);
			break;
		}
		case PLUG_INFO_TYPE_PROJECT:
		{
			LilvNode *node = lilv_plugin_get_project(info->plug);
			if(node)
			{
				_open_uri(node);
				lilv_node_free(node);
			}
			break;
		}
		case PLUG_INFO_TYPE_AUTHOR_EMAIL:
		{
			LilvNode *node = lilv_plugin_get_author_email(info->plug);
			if(node)
			{
				_open_uri(node);
				lilv_node_free(node);
			}
			break;
		}
		case PLUG_INFO_TYPE_AUTHOR_HOMEPAGE:
		{
			LilvNode *node = lilv_plugin_get_author_homepage(info->plug);
			if(node)
			{
				_open_uri(node);
				lilv_node_free(node);
			}
			break;
		}
		default:
			break;
	}
}

static inline bool
_pluglist_populate_name(sp_ui_t *ui, const LilvPlugin *plug, const char *match)
{
	bool valid = false;

	LilvNode *name = lilv_plugin_get_name(plug);
	if(name)
	{
		if(lilv_node_is_string(name) && strcasestr(lilv_node_as_string(name), match))
			valid = true;

		lilv_node_free(name);
	}

	return valid;
}

static inline bool
_pluglist_populate_class(sp_ui_t *ui, const LilvPlugin *plug, const char *match)
{
	bool valid = false;

	const LilvPluginClass *class = lilv_plugin_get_class(plug);
	if(class)
	{
		const LilvNode *label = lilv_plugin_class_get_label(class);
		if(label && lilv_node_is_string(label))
		{
			const char *label_str = lilv_node_as_string(label);

			if(strcasestr(label_str, match))
				valid = true;
		}
	}

	return valid;
}

static inline bool
_pluglist_populate_project(sp_ui_t *ui, const LilvPlugin *plug, const char *match)
{
	bool valid = false;

	LilvNode *project = lilv_plugin_get_project(plug);
	if(project)
	{
		LilvNode *name = lilv_world_get(ui->world, project, ui->regs.doap.name.node, NULL);
		if(name)
		{
			if(lilv_node_is_string(name) && strcasestr(lilv_node_as_string(name), match))
				valid = true;

			lilv_node_free(name);
		}
		lilv_node_free(project);
	}

	return valid;
}

static inline bool
_pluglist_populate_author(sp_ui_t *ui, const LilvPlugin *plug, const char *match)
{
	bool valid = false;

	LilvNode *author = lilv_plugin_get_author_name(plug);
	if(author)
	{
		if(lilv_node_is_string(author) && strcasestr(lilv_node_as_string(author), match))
			valid = true;

		lilv_node_free(author);
	}

	return valid;
}

static void
_pluglist_populate(sp_ui_t *ui, const char *match)
{
	if(!ui || !ui->plugs || !ui->pluglist || !ui->plugitc || !ui->plugbar)
		return;

	if(ui->pluginfo)
		elm_genlist_clear(ui->pluginfo);

	populate_t populate = NULL;
	Elm_Object_Item *tool = elm_toolbar_selected_item_get(ui->plugbar);
	//FIXME won't work with internationalization, use tool pointer instead 
	if(!strcmp(elm_object_item_text_get(tool), "Name"))
		populate = _pluglist_populate_name;
	else if(!strcmp(elm_object_item_text_get(tool), "Class"))
		populate = _pluglist_populate_class;
	else if(!strcmp(elm_object_item_text_get(tool), "Project"))
		populate = _pluglist_populate_project;
	else if(!strcmp(elm_object_item_text_get(tool), "Author"))
		populate = _pluglist_populate_author;

	if(!populate)
		return;

	LILV_FOREACH(plugins, itr, ui->plugs)
	{
		const LilvPlugin *plug = lilv_plugins_get(ui->plugs, itr);
		if(!plug)
			continue;

		if(populate(ui, plug, match))
		{
			plug_info_t *info = calloc(1, sizeof(plug_info_t));
			if(info)
			{
				info->type = PLUG_INFO_TYPE_NAME;
				info->plug = plug;
				Elm_Object_Item *elmnt = elm_genlist_item_append(ui->pluglist, ui->plugitc, info, NULL,
					ELM_GENLIST_ITEM_NONE, NULL, NULL);
				(void)elmnt;
			}
		}
	}
}

static void
_plugentry_changed(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	if(!ui || !ui->pluglist)
		return;

	const char *chunk = elm_entry_entry_get(obj);
	char *match = elm_entry_markup_to_utf8(chunk);

	elm_genlist_clear(ui->pluglist);
	_pluglist_populate(ui, match); // populate with matching plugins
	free(match);
}

static void
_menu_plugin_del(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	ui->plugwin = NULL;
	ui->pluglist = NULL;
	ui->plugentry = NULL;
	ui->plugbar = NULL;
	ui->pluginfo = NULL;
}

static void
_plugbar_selected(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	_plugentry_changed(ui, ui->plugentry, NULL);
}

static inline Evas_Object *
_menu_plugin_new(sp_ui_t *ui)
{
	const char *title = "Plugin";
	Evas_Object *win = elm_win_add(ui->win, title, ELM_WIN_BASIC);
	if(win)
	{
		elm_win_title_set(win, title);
		elm_win_autodel_set(win, EINA_TRUE);
		evas_object_smart_callback_add(win, "delete,request", _menu_plugin_del, ui);
		evas_object_resize(win, 960, 480);
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

		Evas_Object *plugpane = elm_panes_add(win);
		if(plugpane)
		{
			elm_panes_horizontal_set(plugpane, EINA_FALSE);
			elm_panes_content_left_size_set(plugpane, 0.4);
			evas_object_size_hint_weight_set(plugpane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(plugpane, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(plugpane);
			elm_win_resize_object_add(win, plugpane);

			Evas_Object *plugbox = elm_box_add(win);
			if(plugbox)
			{
				elm_box_horizontal_set(plugbox, EINA_FALSE);
				elm_box_homogeneous_set(plugbox, EINA_FALSE);
				evas_object_data_set(plugbox, "ui", ui);
				evas_object_size_hint_weight_set(plugbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(plugbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(plugbox);
				elm_object_part_content_set(plugpane, "left", plugbox);

				ui->plugbar = elm_toolbar_add(plugbox);
				if(ui->plugbar)
				{
					elm_toolbar_horizontal_set(ui->plugbar, EINA_TRUE);
					elm_toolbar_homogeneous_set(ui->plugbar, EINA_TRUE);
					elm_toolbar_align_set(ui->plugbar, 0.f);
					elm_toolbar_select_mode_set(ui->plugbar, ELM_OBJECT_SELECT_MODE_ALWAYS);
					elm_toolbar_shrink_mode_set(ui->plugbar, ELM_TOOLBAR_SHRINK_SCROLL);
					evas_object_smart_callback_add(ui->plugbar, "selected", _plugbar_selected, ui);
					//evas_object_size_hint_weight_set(ui->plugbar, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
					evas_object_size_hint_align_set(ui->plugbar, EVAS_HINT_FILL, EVAS_HINT_FILL);
					evas_object_show(ui->plugbar);
					elm_box_pack_end(plugbox, ui->plugbar);

					Elm_Object_Item *elmnt;

					elmnt = elm_toolbar_item_append(ui->plugbar, NULL, "Name", NULL, NULL);
					elm_toolbar_item_selected_set(elmnt, EINA_TRUE);
					elm_object_item_tooltip_text_set(elmnt, "Ctrl + 'N'");

					elmnt = elm_toolbar_item_append(ui->plugbar, NULL, "Class", NULL, NULL);
					elm_object_item_tooltip_text_set(elmnt, "Ctrl + 'C'");

					elmnt = elm_toolbar_item_append(ui->plugbar, NULL, "Project", NULL, NULL);
					elm_object_item_tooltip_text_set(elmnt, "Ctrl + 'B'");

					elmnt = elm_toolbar_item_append(ui->plugbar, NULL, "Author", NULL, NULL);
					elm_object_item_tooltip_text_set(elmnt, "Ctrl + 'A'");
				} // plugbar

				ui->plugentry = elm_entry_add(plugbox);
				if(ui->plugentry)
				{
					elm_entry_entry_set(ui->plugentry, "");
					elm_entry_editable_set(ui->plugentry, EINA_TRUE);
					elm_entry_single_line_set(ui->plugentry, EINA_TRUE);
					elm_entry_scrollable_set(ui->plugentry, EINA_TRUE);
					evas_object_smart_callback_add(ui->plugentry, "changed,user", _plugentry_changed, ui);
					evas_object_data_set(ui->plugentry, "ui", ui);
					//evas_object_size_hint_weight_set(ui->plugentry, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
					evas_object_size_hint_align_set(ui->plugentry, EVAS_HINT_FILL, EVAS_HINT_FILL);
					evas_object_show(ui->plugentry);
					elm_box_pack_end(plugbox, ui->plugentry);
				} // plugentry

				ui->pluglist = elm_genlist_add(plugbox);
				if(ui->pluglist)
				{
					elm_genlist_homogeneous_set(ui->pluglist, EINA_TRUE); // needef for lazy-loading
					elm_genlist_mode_set(ui->pluglist, ELM_LIST_LIMIT);
					elm_genlist_block_count_set(ui->pluglist, 64); // needef for lazy-loading
					evas_object_smart_callback_add(ui->pluglist, "activated",
						_pluglist_activated, ui);
					evas_object_smart_callback_add(ui->pluglist, "selected",
						_pluglist_selected, ui);
					evas_object_data_set(ui->pluglist, "ui", ui);
					evas_object_size_hint_weight_set(ui->pluglist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
					evas_object_size_hint_align_set(ui->pluglist, EVAS_HINT_FILL, EVAS_HINT_FILL);
					evas_object_show(ui->pluglist);
					elm_box_pack_end(plugbox, ui->pluglist);
				} // pluglist

				_pluglist_populate(ui, ""); // populate with everything
			} // plugbox

			ui->pluginfo = elm_genlist_add(plugpane);
			if(ui->pluginfo)
			{
				elm_genlist_homogeneous_set(ui->pluginfo, EINA_TRUE); // needef for lazy-loading
				elm_genlist_mode_set(ui->pluginfo, ELM_LIST_COMPRESS);
				elm_genlist_block_count_set(ui->pluginfo, 64); // needef for lazy-loading
				evas_object_smart_callback_add(ui->pluginfo, "activated",
					_pluginfo_activated, ui);
				evas_object_data_set(ui->pluginfo, "ui", ui);
				evas_object_size_hint_weight_set(ui->pluginfo, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(ui->pluginfo, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(ui->pluginfo);
				elm_object_part_content_set(plugpane, "right", ui->pluginfo);
			} // pluginfo
		} // plugpane

		return win;
	}

	return NULL;
}

void
_menu_plugin(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(!ui->plugwin)
		ui->plugwin = _menu_plugin_new(ui);
}

void
_pluglist_itc_add(sp_ui_t *ui)
{
	ui->plugitc = elm_genlist_item_class_new();
	if(ui->plugitc)
	{
		ui->plugitc->item_style = "default_style";
		ui->plugitc->func.text_get = _pluglist_label_get;
		ui->plugitc->func.content_get = NULL;
		ui->plugitc->func.state_get = NULL;
		ui->plugitc->func.del = _pluglist_del;
	}
}
