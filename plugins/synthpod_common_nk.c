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

#include <synthpod_lv2.h>

#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/port-groups/port-groups.h"
#include "lv2/lv2plug.in/ns/ext/presets/presets.h"
#include "lv2/lv2plug.in/ns/ext/patch/patch.h"

#include <math.h>

#include "nk_pugl/nk_pugl.h"

#include <lilv/lilv.h>

#define SEARCH_BUF_MAX 128

typedef enum _property_type_t property_type_t;
typedef enum _selector_main_t selector_main_t;
typedef enum _selector_grid_t selector_grid_t;
typedef enum _selector_search_t selector_search_t;

typedef union _param_union_t param_union_t;

typedef struct _port_t port_t;
typedef struct _param_t param_t;
typedef struct _prop_t prop_t;
typedef struct _mod_t mod_t;
typedef struct _plughandle_t plughandle_t;

enum _property_type_t {
	PROPERTY_TYPE_CONTROL = 0,
	PROPERTY_TYPE_AUDIO,
	PROPERTY_TYPE_CV,
	PROPERTY_TYPE_PARAM,

	PROPERTY_TYPE_MAX
};

enum _selector_main_t {
	SELECTOR_MAIN_GRID = 0,
	SELECTOR_MAIN_MATRIX,

	SELECTOR_MAIN_MAX
};

enum _selector_grid_t {
	SELECTOR_GRID_PLUGINS = 0,
	SELECTOR_GRID_PRESETS,

	SELECTOR_GRID_MAX
};

enum _selector_search_t {
	SELECTOR_SEARCH_NAME = 0,
	SELECTOR_SEARCH_COMMENT,
	SELECTOR_SEARCH_AUTHOR,
	SELECTOR_SEARCH_CLASS,
	SELECTOR_SEARCH_PROJECT,

	SELECTOR_SEARCH_MAX
};

struct _port_t {
	const LilvPort *port;
	LilvNode *group;
	LilvScalePoints *points;
	float min;
	float max;
	float span;
	float val;
	bool is_int;
	bool is_bool;
	bool is_readonly;
};

union _param_union_t {
 int32_t b;
 int32_t i;
 int64_t h;
 float f;
 double d;
};

struct _param_t {
	const LilvNode *param;
	bool is_readonly;
	LV2_URID range;
	param_union_t min;
	param_union_t max;
	param_union_t span;
	param_union_t val;
};

struct _prop_t {
	property_type_t type;

	union {
		port_t port;
		param_t param;
	};
};

struct _mod_t {
	const LilvPlugin *plug;

	unsigned num_ports;
	port_t *ports;
	LilvNodes *groups;
	LilvNodes *banks;

	unsigned num_params;
	param_t *params;
	LilvNodes *readables;
	LilvNodes *writables;

	LilvNodes *presets;
};

struct _plughandle_t {
	LilvWorld *world;

	LV2_Atom_Forge forge;

	LV2_URID atom_eventTransfer;

	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	LV2UI_Write_Function writer;
	LV2UI_Controller controller;

	nk_pugl_window_t win;

	selector_main_t main_selector;
	selector_grid_t grid_selector;
	selector_search_t  search_selector;
	const LilvPlugin *plugin_selector;
	unsigned module_selector;
	const LilvNode *preset_selector;

	unsigned num_mods;
	mod_t *mods;

	char search_buf [SEARCH_BUF_MAX];

	struct {
		LilvNode *pg_group;
		LilvNode *lv2_integer;
		LilvNode *lv2_toggled;
		LilvNode *lv2_minimum;
		LilvNode *lv2_maximum;
		LilvNode *lv2_default;
		LilvNode *pset_Preset;
		LilvNode *pset_bank;
		LilvNode *rdfs_comment;
		LilvNode *rdfs_range;
		LilvNode *doap_name;
		LilvNode *lv2_minorVersion;
		LilvNode *lv2_microVersion;
		LilvNode *doap_license;
		LilvNode *rdfs_label;
		LilvNode *lv2_name;
		LilvNode *lv2_OutputPort;
		LilvNode *patch_readable;
		LilvNode *patch_writable;
	} node;

	float dy;
};

static const char *main_labels [SELECTOR_MAIN_MAX] = {
	[SELECTOR_MAIN_GRID] = "Grid",
	[SELECTOR_MAIN_MATRIX] = "Matrix"
};

static const char *main_tooltips [SELECTOR_MAIN_MAX] = {
	[SELECTOR_MAIN_GRID] = "Ctrl-G",
	[SELECTOR_MAIN_MATRIX] = "Ctrl-M"
};

static const char *grid_labels [SELECTOR_GRID_MAX] = {
	[SELECTOR_GRID_PLUGINS] = "Plugins",
	[SELECTOR_GRID_PRESETS] = "Presets"
};

static const char *grid_tooltips [SELECTOR_GRID_MAX] = {
	[SELECTOR_GRID_PLUGINS] = "Ctrl-P",
	[SELECTOR_GRID_PRESETS] = "Ctrl-R"
};

static const char *search_labels [SELECTOR_SEARCH_MAX] = {
	[SELECTOR_SEARCH_NAME] = "Name",
	[SELECTOR_SEARCH_COMMENT] = "Comment",
	[SELECTOR_SEARCH_AUTHOR] = "Author",
	[SELECTOR_SEARCH_CLASS] = "Class",
	[SELECTOR_SEARCH_PROJECT] = "Project"
};

static void
_register_parameter(plughandle_t *handle, param_t *param, const LilvNode *parameter, bool is_readonly)
{
	param->param = parameter;
	param->is_readonly = is_readonly;

	LilvNode *range = lilv_world_get(handle->world, parameter, handle->node.rdfs_range, NULL);
	if(range)
	{
		param->range = handle->map->map(handle->map->handle, lilv_node_as_uri(range));
		lilv_node_free(range);
	}

	LilvNode *min = lilv_world_get(handle->world, parameter, handle->node.lv2_minimum, NULL);
	if(min)
	{
		param->min.i = lilv_node_as_int(min); //FIXME
		lilv_node_free(min);
	}

	LilvNode *max = lilv_world_get(handle->world, parameter, handle->node.lv2_maximum, NULL);
	if(max)
	{
		param->max.i = lilv_node_as_int(max); //FIXME
		lilv_node_free(max);
	}

	LilvNode *val = lilv_world_get(handle->world, parameter, handle->node.lv2_default, NULL);
	if(val)
	{
		param->val.i = lilv_node_as_int(val); //FIXME
		lilv_node_free(val);
	}

	param->span.i = param->max.i - param->min.i;
}

static void
_mod_add(plughandle_t *handle, const LilvPlugin *plug)
{
	handle->num_mods+= 1;
	handle->mods = realloc(handle->mods, handle->num_mods * sizeof(mod_t));

	mod_t *mod = &handle->mods[handle->num_mods - 1];
	memset(mod, 0x0, sizeof(mod_t));

	mod->plug = plug;
	mod->num_ports = lilv_plugin_get_num_ports(plug);
	mod->ports = calloc(mod->num_ports, sizeof(port_t));

	for(unsigned p=0; p<mod->num_ports; p++)
	{
		port_t *port = &mod->ports[p];

		port->port = lilv_plugin_get_port_by_index(plug, p);

		LilvNodes *port_groups = lilv_port_get_value(plug, port->port, handle->node.pg_group);
		if(port_groups)
		{
			const LilvNode *port_group = lilv_nodes_size(port_groups)
				? lilv_nodes_get_first(port_groups) : NULL;
			if(port_group)
			{
				port->group = lilv_node_duplicate(port_group);
				if(!lilv_nodes_contains(mod->groups, port_group))
				{
					LilvNodes *mod_groups = lilv_nodes_merge(mod->groups, port_groups);
					lilv_nodes_free(mod->groups);
					mod->groups = mod_groups;
				}
			}
			lilv_nodes_free(port_groups);
		}

		port->is_readonly = lilv_port_is_a(plug, port->port, handle->node.lv2_OutputPort);
		port->is_int = lilv_port_has_property(plug, port->port, handle->node.lv2_integer);
		port->is_bool = lilv_port_has_property(plug, port->port, handle->node.lv2_toggled);
		port->points = lilv_port_get_scale_points(plug, port->port);

		port->val = 0.f;
		port->min = 0.f;
		port->max = 1.f;
		LilvNode *val = NULL;
		LilvNode *min = NULL;
		LilvNode *max = NULL;
		lilv_port_get_range(plug, port->port, &val, &min, &max);

		if(val)
		{
			if(port->is_int)
				port->val = lilv_node_as_int(val);
			else if(port->is_bool)
				port->val = lilv_node_as_bool(val);
			else
				port->val = lilv_node_as_float(val);
			lilv_node_free(val);
		}
		if(min)
		{
			if(port->is_int)
				port->min = lilv_node_as_int(min);
			else if(port->is_bool)
				port->min = 0.f;
			else
				port->min = lilv_node_as_float(min);
			lilv_node_free(min);
		}
		if(max)
		{
			if(port->is_int)
				port->max = lilv_node_as_int(max);
			else if(port->is_bool)
				port->max = 1.f;
			else
				port->max = lilv_node_as_float(max);
			lilv_node_free(max);
		}

		port->span = port->max - port->min;

		if(port->is_int && (port->min == 0.f) && (port->max == 1.f) )
		{
			port->is_int = false;
			port->is_bool = true;
		}
	}

	mod->presets = lilv_plugin_get_related(plug, handle->node.pset_Preset);
	if(mod->presets)
	{
		LILV_FOREACH(nodes, i, mod->presets)
		{
			const LilvNode *preset = lilv_nodes_get(mod->presets, i);
			lilv_world_load_resource(handle->world, preset);
		}

		LILV_FOREACH(nodes, i, mod->presets)
		{
			const LilvNode *preset = lilv_nodes_get(mod->presets, i);

			LilvNodes *banks = lilv_world_find_nodes(handle->world, preset, handle->node.pset_bank, NULL);
			if(banks)
			{
				const LilvNode *bank = lilv_nodes_size(banks)
					? lilv_nodes_get_first(banks) : NULL;

				if(bank)
				{
					if(!lilv_nodes_contains(mod->banks, bank))
					{
						LilvNodes *mod_banks = lilv_nodes_merge(mod->banks, banks);
						lilv_nodes_free(mod->banks);
						mod->banks = mod_banks;
					}
				}
				lilv_nodes_free(banks);
			}
		}
	}

	mod->readables = lilv_plugin_get_value(plug, handle->node.patch_readable);
	mod->writables = lilv_plugin_get_value(plug, handle->node.patch_writable);

	mod->num_params = lilv_nodes_size(mod->readables) + lilv_nodes_size(mod->writables);
	mod->params = calloc(mod->num_params, sizeof(param_t));

	param_t *param = mod->params;
	LILV_FOREACH(nodes, i, mod->readables)
	{
		const LilvNode *parameter = lilv_nodes_get(mod->readables, i);

		_register_parameter(handle, param, parameter, true);
		param++;
	}
	LILV_FOREACH(nodes, i, mod->writables)
	{
		const LilvNode *parameter = lilv_nodes_get(mod->readables, i);

		_register_parameter(handle, param, parameter, false);
		param++;
	}

	nk_pugl_post_redisplay(&handle->win); //FIXME
}

static void
_mod_free(plughandle_t *handle, mod_t *mod)
{
	for(unsigned p=0; p<mod->num_ports; p++)
	{
		port_t *port = &mod->ports[p];

		if(port->group)
			lilv_node_free(port->group);
		if(port->points)
			lilv_scale_points_free(port->points);
	}
	free(mod->ports);

	if(mod->presets)
	{
		LILV_FOREACH(nodes, i, mod->presets)
		{
			const LilvNode *preset = lilv_nodes_get(mod->presets, i);
			lilv_world_unload_resource(handle->world, preset);
		}

		lilv_nodes_free(mod->presets);
	}

	if(mod->banks)
		lilv_nodes_free(mod->banks);

	if(mod->groups)
		lilv_nodes_free(mod->groups);

	free(mod->params);

	if(mod->readables)
		lilv_nodes_free(mod->readables);

	if(mod->writables)
		lilv_nodes_free(mod->writables);
}

static void
_load(plughandle_t *handle)
{
	switch(handle->grid_selector)
	{
		case SELECTOR_GRID_PLUGINS:
		{
			_mod_add(handle, handle->plugin_selector);
		} break;
		case SELECTOR_GRID_PRESETS:
		{
			//TODO
		} break;

		default: break;
	}
}

static bool
_tooltip_visible(struct nk_context *ctx)
{
	return nk_widget_has_mouse_click_down(ctx, NK_BUTTON_RIGHT, nk_true)
		|| (nk_widget_is_hovered(ctx) && nk_input_is_key_down(&ctx->input, NK_KEY_CTRL));
}

static void
_expose_main_header(plughandle_t *handle, struct nk_context *ctx, float dy)
{
	nk_layout_row_dynamic(ctx, dy, 7);
	{
		if(_tooltip_visible(ctx))
			nk_tooltip(ctx, "Ctrl-N");
		nk_button_label(ctx, "New");

		if(_tooltip_visible(ctx))
			nk_tooltip(ctx, "Ctrl-O");
		nk_button_label(ctx, "Open");

		if(_tooltip_visible(ctx))
			nk_tooltip(ctx, "Ctrl-S");
		nk_button_label(ctx, "Save");

		if(_tooltip_visible(ctx))
			nk_tooltip(ctx, "Ctrl-Shift-S");
		nk_button_label(ctx, "Save As");

		if(_tooltip_visible(ctx))
			nk_tooltip(ctx, "Ctrl-Q");
		nk_button_label(ctx, "Quit");

		for(unsigned i=0; i<SELECTOR_MAIN_MAX; i++)
		{
			const enum nk_symbol_type symbol = (handle->main_selector == i)
				? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_CIRCLE_OUTLINE;
			if(_tooltip_visible(ctx))
				nk_tooltip(ctx, main_tooltips[i]);
			if(nk_button_symbol_label(ctx, symbol, main_labels[i], NK_TEXT_RIGHT))
				handle->main_selector = i;
		}
	}
}

static void
_expose_main_plugin_list(plughandle_t *handle, struct nk_context *ctx)
{
	const LilvPlugins *plugs = lilv_world_get_all_plugins(handle->world);

	LilvNode *p = NULL;
	if(handle->search_selector == SELECTOR_SEARCH_COMMENT)
		p = handle->node.rdfs_comment;
	else if(handle->search_selector == SELECTOR_SEARCH_PROJECT)
		p = handle->node.doap_name;

	bool selector_visible = false;
	LILV_FOREACH(plugins, i, plugs)
	{
		const LilvPlugin *plug = lilv_plugins_get(plugs, i);

		LilvNode *name_node = lilv_plugin_get_name(plug);
		if(name_node)
		{
			const char *name_str = lilv_node_as_string(name_node);
			bool visible = strlen(handle->search_buf) == 0;

			if(!visible)
			{
				switch(handle->search_selector)
				{
					case SELECTOR_SEARCH_NAME:
					{
						if(strcasestr(name_str, handle->search_buf))
							visible = true;
					} break;
					case SELECTOR_SEARCH_COMMENT:
					{
						LilvNodes *label_nodes = p ? lilv_plugin_get_value(plug, p) : NULL;
						if(label_nodes)
						{
							const LilvNode *label_node = lilv_nodes_size(label_nodes)
								? lilv_nodes_get_first(label_nodes) : NULL;
							if(label_node)
							{
								if(strcasestr(lilv_node_as_string(label_node), handle->search_buf))
									visible = true;
							}
							lilv_nodes_free(label_nodes);
						}
					} break;
					case SELECTOR_SEARCH_AUTHOR:
					{
						LilvNode *author_node = lilv_plugin_get_author_name(plug);
						if(author_node)
						{
							if(strcasestr(lilv_node_as_string(author_node), handle->search_buf))
								visible = true;
							lilv_node_free(author_node);
						}
					} break;
					case SELECTOR_SEARCH_CLASS:
					{
						const LilvPluginClass *class = lilv_plugin_get_class(plug);
						if(class)
						{
							const LilvNode *label_node = lilv_plugin_class_get_label(class);
							if(label_node)
							{
								if(strcasestr(lilv_node_as_string(label_node), handle->search_buf))
									visible = true;
							}
						}
					} break;
					case SELECTOR_SEARCH_PROJECT:
					{
						LilvNode *project = lilv_plugin_get_project(plug);
						if(project)
						{
							LilvNode *label_node = p ? lilv_world_get(handle->world, lilv_plugin_get_uri(plug), p, NULL) : NULL;
							if(label_node)
							{
								if(strcasestr(lilv_node_as_string(label_node), handle->search_buf))
									visible = true;
								lilv_node_free(label_node);
							}
							lilv_node_free(project);
						}
					} break;

					default: break;
				}
			}

			if(visible)
			{
				if(nk_widget_is_mouse_clicked(ctx, NK_BUTTON_RIGHT))
				{
					handle->plugin_selector = plug;
					_load(handle);
				}

				int selected = plug == handle->plugin_selector;
				if(nk_selectable_label(ctx, name_str, NK_TEXT_LEFT, &selected))
				{
					handle->plugin_selector = plug;
				}

				if(plug == handle->plugin_selector)
					selector_visible = true;
			}

			lilv_node_free(name_node);
		}
	}

	if(!selector_visible)
		handle->plugin_selector = NULL;
}

static void
_expose_main_plugin_info(plughandle_t *handle, struct nk_context *ctx)
{
	const LilvPlugin *plug = handle->plugin_selector;

	if(!plug)
		return;

	LilvNode *name_node = lilv_plugin_get_name(plug);
	const LilvNode *uri_node = lilv_plugin_get_uri(plug);
	const LilvNode *bundle_node = lilv_plugin_get_bundle_uri(plug);
	LilvNode *author_name_node = lilv_plugin_get_author_name(plug);
	LilvNode *author_email_node = lilv_plugin_get_author_email(plug);
	LilvNode *author_homepage_node = lilv_plugin_get_author_homepage(plug);
	LilvNodes *minor_nodes = lilv_plugin_get_value(plug, handle->node.lv2_minorVersion);
	LilvNodes *micro_nodes = lilv_plugin_get_value(plug, handle->node.lv2_microVersion);
	LilvNodes *license_nodes = lilv_plugin_get_value(plug, handle->node.doap_license);

	if(name_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Name:    %s", lilv_node_as_string(name_node));
	if(uri_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "URI:     %s", lilv_node_as_uri(uri_node));
	if(bundle_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Bundle:  %s", lilv_node_as_uri(bundle_node));
	if(author_name_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Author:  %s", lilv_node_as_string(author_name_node));
	if(author_email_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Email:   %s", lilv_node_as_string(author_email_node));
	if(author_homepage_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Web:     %s", lilv_node_as_string(author_homepage_node));
	if(lilv_nodes_size(minor_nodes) && lilv_nodes_size(micro_nodes))
		nk_labelf(ctx, NK_TEXT_LEFT, "Version: %i.%i",
			lilv_node_as_int(lilv_nodes_get_first(minor_nodes)),
			lilv_node_as_int(lilv_nodes_get_first(micro_nodes)) );
	if(lilv_nodes_size(license_nodes))
		nk_labelf(ctx, NK_TEXT_LEFT, "License: %s",
			lilv_node_as_uri(lilv_nodes_get_first(license_nodes)) );
	//TODO project

	if(name_node)
		lilv_node_free(name_node);
	if(author_name_node)
		lilv_node_free(author_name_node);
	if(author_email_node)
		lilv_node_free(author_email_node);
	if(author_homepage_node)
		lilv_node_free(author_homepage_node);
	if(minor_nodes)
		lilv_nodes_free(minor_nodes);
	if(micro_nodes)
		lilv_nodes_free(micro_nodes);
	if(license_nodes)
		lilv_nodes_free(license_nodes);
}

static void
_expose_main_preset_list_for_bank(plughandle_t *handle, struct nk_context *ctx,
	LilvNodes *presets, const LilvNode *preset_bank)
{
	bool search = strlen(handle->search_buf) != 0;

	LILV_FOREACH(nodes, i, presets)
	{
		const LilvNode *preset = lilv_nodes_get(presets, i);

		bool visible = false;

		LilvNode *bank = lilv_world_get(handle->world, preset, handle->node.pset_bank, NULL);
		if(bank)
		{
			if(lilv_node_equals(preset_bank, bank))
				visible = true;

			lilv_node_free(bank);
		}
		else if(!preset_bank)
			visible = true;

		if(visible)
		{
			LilvNode *label_node = lilv_world_get(handle->world, preset, handle->node.rdfs_label, NULL);
			if(label_node)
			{
				const char *label_str = lilv_node_as_string(label_node);

				if(!search || strcasestr(label_str, handle->search_buf))
				{
					if(nk_widget_is_mouse_clicked(ctx, NK_BUTTON_RIGHT))
					{
						handle->preset_selector = preset;
						_load(handle);
					}

					int selected = preset == handle->preset_selector;
					if(nk_selectable_label(ctx, label_str, NK_TEXT_LEFT, &selected))
					{
						handle->preset_selector = preset;
					}
				}

				lilv_node_free(label_node);
			}
		}
	}
}

static void
_expose_main_preset_list(plughandle_t *handle, struct nk_context *ctx)
{
	mod_t *mod = handle->module_selector < handle->num_mods
		? &handle->mods[handle->module_selector] : NULL;

	if(mod && mod->presets)
	{
		LILV_FOREACH(nodes, i, mod->banks)
		{
			const LilvNode *bank = lilv_nodes_get(mod->banks, i);

			LilvNode *label_node = lilv_world_get(handle->world, bank, handle->node.rdfs_label, NULL);
			if(label_node)
			{
				nk_label(ctx, lilv_node_as_string(label_node), NK_TEXT_CENTERED);
				_expose_main_preset_list_for_bank(handle, ctx, mod->presets, bank);
				lilv_node_free(label_node);
			}
		}

		nk_label(ctx, "Unbanked", NK_TEXT_CENTERED);
		_expose_main_preset_list_for_bank(handle, ctx, mod->presets, NULL);
	}
}

static void
_expose_main_preset_info(plughandle_t *handle, struct nk_context *ctx)
{
	const LilvNode *preset = handle->preset_selector;

	if(!preset)
		return;

	//FIXME
	LilvNode *name_node = lilv_world_get(handle->world, preset, handle->node.rdfs_label, NULL);
	LilvNode *comment_node = lilv_world_get(handle->world, preset, handle->node.rdfs_comment, NULL);
	LilvNode *bank_node = lilv_world_get(handle->world, preset, handle->node.pset_bank, NULL);
	LilvNode *minor_node = lilv_world_get(handle->world, preset, handle->node.lv2_minorVersion, NULL);
	LilvNode *micro_node = lilv_world_get(handle->world, preset, handle->node.lv2_microVersion, NULL);
	LilvNode *license_node = lilv_world_get(handle->world, preset, handle->node.doap_license, NULL);

	if(name_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Name:    %s", lilv_node_as_string(name_node));
	if(preset)
		nk_labelf(ctx, NK_TEXT_LEFT, "URI:     %s", lilv_node_as_uri(preset));
	if(comment_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Comment: %s", lilv_node_as_string(comment_node));
	if(bank_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Bank:    %s", lilv_node_as_uri(bank_node));
	if(minor_node && micro_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Version: %i.%i",
			lilv_node_as_int(minor_node), lilv_node_as_int(micro_node) );
	if(license_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "License: %s", lilv_node_as_uri(license_node));
	//TODO author, project

	if(name_node)
		lilv_node_free(name_node);
	if(comment_node)
		lilv_node_free(comment_node);
	if(bank_node)
		lilv_node_free(bank_node);
	if(minor_node)
		lilv_node_free(minor_node);
	if(micro_node)
		lilv_node_free(micro_node);
	if(license_node)
		lilv_node_free(license_node);
}

static void
_expose_port(struct nk_context *ctx, mod_t *mod, port_t *port, float dy)
{
	LilvNode *name_node = lilv_port_get_name(mod->plug, port->port);
	if(name_node)
	{
		const char *name_str = lilv_node_as_string(name_node);

		if(port->is_int)
		{
			if(port->is_readonly)
			{
				nk_value_int(ctx, name_str, port->val);
			}
			else // !readonly
			{
				const float inc = port->span / nk_widget_width(ctx);
				int val = port->val;
				nk_property_int(ctx, name_str, port->min, &val, port->max, 1.f, inc);
				port->val = val;
			}
		}
		else if(port->is_bool)
		{
			if(port->is_readonly)
			{
				nk_value_bool(ctx, name_str, port->val);
			}
			else // !readonly
			{
				int val = port->val;
				nk_checkbox_label(ctx, name_str, &val);
				port->val = val;
			}
		}
		else if(port->points)
		{
			int val = 0;

			const int count = lilv_scale_points_size(port->points);
			const char *items [count];
			float values [count];

			const char **item_ptr = items;
			float *value_ptr = values;
			LILV_FOREACH(scale_points, i, port->points)
			{
				const LilvScalePoint *point = lilv_scale_points_get(port->points, i);
				const LilvNode *label_node = lilv_scale_point_get_label(point);
				const LilvNode *value_node = lilv_scale_point_get_value(point);
				*item_ptr = lilv_node_as_string(label_node);
				*value_ptr = lilv_node_as_float(value_node); //FIXME

				if(*value_ptr == port->val)
					val = value_ptr - values;
			
				item_ptr++;
				value_ptr++;
			}

			nk_combobox(ctx, items, count, &val, dy, nk_vec2(nk_widget_width(ctx), 5*dy));
			port->val = values[val];
		}
		else // is_float
		{
			if(port->is_readonly)
			{
				nk_value_float(ctx, name_str, port->val);
			}
			else // !readonly
			{
				const float step = port->span / 100.f;
				const float inc = port->span / nk_widget_width(ctx);
				nk_property_float(ctx, name_str, port->min, &port->val, port->max, step, inc);
			}
		}

		lilv_node_free(name_node);
	}
}

static void
_expose_param(plughandle_t *handle, struct nk_context *ctx, param_t *param, float dy)
{
	LilvNode *name_node = lilv_world_get(handle->world, param->param, handle->node.rdfs_label, NULL);
	if(name_node)
	{
		const char *name_str = lilv_node_as_string(name_node);

		if(param->is_readonly)
		{
			nk_value_int(ctx, name_str, param->val.i);
		}
		else
		{
			const float step = param->span.i / 100.f;
			const float inc = param->span.i / nk_widget_width(ctx);
			nk_property_int(ctx, name_str, param->min.i, &param->val.i, param->max.i, step, inc);
		}

		lilv_node_free(name_node);
	}

}

static void
_expose_main_body(plughandle_t *handle, struct nk_context *ctx, float dh, float dy)
{
	const struct nk_vec2 group_padding = ctx->style.window.group_padding;

	switch(handle->main_selector)
	{
		case SELECTOR_MAIN_GRID:
		{
			nk_layout_row_begin(ctx, NK_DYNAMIC, dh, 3);

			nk_layout_row_push(ctx, 0.25);
			if(nk_group_begin(ctx, "Rack", NK_WINDOW_BORDER | NK_WINDOW_TITLE))
			{
				for(unsigned m=0; m<handle->num_mods; m++)
				{
					mod_t *mod = &handle->mods[m];
					const LilvPlugin *plug = mod->plug;

					LilvNode *name_node = lilv_plugin_get_name(plug);
					if(name_node)
					{
						nk_layout_row_dynamic(ctx, dy, 2);
						int selected = m == handle->module_selector;
						if(nk_selectable_label(ctx, lilv_node_as_string(name_node), NK_TEXT_LEFT,
							&selected))
						{
							handle->module_selector = m;
							handle->preset_selector = NULL;
						}
						nk_labelf(ctx, NK_TEXT_RIGHT, "%4.1f|%4.1f|%4.1f%%", 1.1f, 2.2f, 5.5f);

						lilv_node_free(name_node);
					}
				}

				nk_group_end(ctx);
			}

			nk_layout_row_push(ctx, 0.50);
			if(nk_group_begin(ctx, "Controls", NK_WINDOW_BORDER | NK_WINDOW_TITLE))
			{
				mod_t *mod = handle->module_selector < handle->num_mods
					? &handle->mods[handle->module_selector] : NULL;

				if(mod)
				{
					LILV_FOREACH(nodes, i, mod->groups)
					{
						const LilvNode *group = lilv_nodes_get(mod->groups, i);

						LilvNode *label_node = lilv_world_get(handle->world, group, handle->node.lv2_name, NULL);
						if(label_node)
						{
							nk_layout_row_dynamic(ctx, dy, 1);
							nk_label(ctx, lilv_node_as_string(label_node), NK_TEXT_CENTERED);
							lilv_node_free(label_node);

							nk_layout_row_dynamic(ctx, dy, 3);
							for(unsigned p=0; p<mod->num_ports; p++)
							{
								port_t *port = &mod->ports[p];
								if(!port->group || !lilv_node_equals(port->group, group))
									continue;

								_expose_port(ctx, mod, port, dy);
							}
						}
					}

					nk_layout_row_dynamic(ctx, dy, 1);
					nk_label(ctx, "Ungrouped", NK_TEXT_CENTERED);

					nk_layout_row_dynamic(ctx, dy, 3);
					for(unsigned p=0; p<mod->num_ports; p++)
					{
						port_t *port = &mod->ports[p];
						if(port->group)
							continue;

						_expose_port(ctx, mod, port, dy);
					}

					nk_layout_row_dynamic(ctx, dy, 1);
					nk_label(ctx, "Parameter", NK_TEXT_CENTERED);

					nk_layout_row_dynamic(ctx, dy, 3);
					for(unsigned p=0; p<mod->num_params; p++)
					{
						param_t *param = &mod->params[p];

						_expose_param(handle, ctx, param, dy);
					}
				}

				nk_group_end(ctx);
			}

			nk_layout_row_push(ctx, 0.25);
			if(nk_group_begin(ctx, "Plugins/Presets", NK_WINDOW_BORDER))
			{
				const struct nk_panel *panel = nk_window_get_panel(ctx);

				nk_layout_row_dynamic(ctx, dy, SELECTOR_MAIN_MAX);
				for(unsigned i=0; i<SELECTOR_MAIN_MAX; i++)
				{
					const enum nk_symbol_type symbol = (handle->grid_selector == i)
						? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_CIRCLE_OUTLINE;
					if(_tooltip_visible(ctx))
						nk_tooltip(ctx, grid_tooltips[i]);
					if(nk_button_symbol_label(ctx, symbol, grid_labels[i], NK_TEXT_RIGHT))
					{
						handle->grid_selector = i;
						handle->search_buf[0] = '\0';
					}
				}

				nk_layout_row_dynamic(ctx, dy, 2);
				handle->search_selector = nk_combo(ctx, search_labels, SELECTOR_SEARCH_MAX,
					handle->search_selector, dy, nk_vec2(nk_widget_width(ctx), 7*dy));
				nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, handle->search_buf, SEARCH_BUF_MAX, nk_filter_default);

				const float content_h2 = panel->bounds.h - 6*group_padding.y - 3*dy;
				nk_layout_row_dynamic(ctx, content_h2*0.75, 1);
				if(nk_group_begin(ctx, "List", NK_WINDOW_BORDER))
				{
					switch(handle->grid_selector)
					{
						case SELECTOR_GRID_PLUGINS:
						{
							nk_layout_row_dynamic(ctx, dy, 1);
							_expose_main_plugin_list(handle, ctx);
						} break;
						case SELECTOR_GRID_PRESETS:
						{
							nk_layout_row_dynamic(ctx, dy, 1);
							_expose_main_preset_list(handle, ctx);
						} break;

						default: break;
					}
					nk_group_end(ctx);
				}

				nk_layout_row_dynamic(ctx, content_h2*0.25, 1);
				if(nk_group_begin(ctx, "Info", NK_WINDOW_BORDER))
				{
					switch(handle->grid_selector)
					{
						case SELECTOR_GRID_PLUGINS:
						{
							nk_layout_row_dynamic(ctx, dy, 1);
							_expose_main_plugin_info(handle, ctx);
						} break;
						case SELECTOR_GRID_PRESETS:
						{
							nk_layout_row_dynamic(ctx, dy, 1);
							_expose_main_preset_info(handle, ctx);
						} break;

						default: break;
					}
					nk_group_end(ctx);
				}

				nk_layout_row_dynamic(ctx, dy, 1);
				if(nk_button_label(ctx, "Load") && handle->plugin_selector)
				{
					_load(handle);
				}

				nk_group_end(ctx);
			}

			nk_layout_row_end(ctx);
		} break;
		case SELECTOR_MAIN_MATRIX:
		{
			nk_layout_row_begin(ctx, NK_DYNAMIC, dy, 3);

			nk_layout_row_push(ctx, 0.25);
			if(nk_group_begin(ctx, "Sources", NK_WINDOW_BORDER | NK_WINDOW_TITLE))
			{
				//TODO

				nk_group_end(ctx);
			}

			nk_layout_row_push(ctx, 0.50);
			if(nk_group_begin(ctx, "Connections",
				NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR))
			{
				//TODO

				nk_group_end(ctx);
			}

			nk_layout_row_push(ctx, 0.25);
			if(nk_group_begin(ctx, "Sinks", NK_WINDOW_BORDER | NK_WINDOW_TITLE))
			{
				//TODO

				nk_group_end(ctx);
			}

			nk_layout_row_end(ctx);
		} break;

		default: break;
	}
}

static void
_expose_main_footer(plughandle_t *handle, struct nk_context *ctx, float dy)
{
	const unsigned n_row = 1;

	nk_layout_row_dynamic(ctx, dy, n_row);
	{
		nk_label(ctx, "Synthpod: "SYNTHPOD_VERSION, NK_TEXT_RIGHT);
	}
}

static void
_expose(struct nk_context *ctx, struct nk_rect wbounds, void *data)
{
	plughandle_t *handle = data;

	if(nk_begin(ctx, "synthpod", wbounds, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_window_set_bounds(ctx, wbounds);

    const float w_padding = ctx->style.window.padding.y;
		const float dy = handle->dy;
		const unsigned n_paddings = 4;
		const float dh = nk_window_get_height(ctx)
			- n_paddings*w_padding - (n_paddings - 2)*dy;

		_expose_main_header(handle, ctx, dy);
		_expose_main_body(handle, ctx, dh, dy);
		_expose_main_footer(handle, ctx, dy);
	}
	nk_end(ctx);
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor, const char *plugin_uri,
	const char *bundle_path, LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	void *parent = NULL;
	LV2UI_Resize *host_resize = NULL;
	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_UI__parent))
			parent = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_UI__resize))
			host_resize = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->unmap = features[i]->data;
	}

	if(!parent)
	{
		fprintf(stderr,
			"%s: Host does not support ui:parent\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->map)
	{
		fprintf(stderr,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->unmap)
	{
		fprintf(stderr,
			"%s: Host does not support urid:unmap\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	lv2_atom_forge_init(&handle->forge, handle->map);

	handle->atom_eventTransfer = handle->map->map(handle->map->handle, LV2_ATOM__eventTransfer);

	handle->controller = controller;
	handle->writer = write_function;

	handle->world = lilv_world_new();
	if(handle->world)
	{
		LilvNode *node_false = lilv_new_bool(handle->world, false);
		if(node_false)
		{
			lilv_world_set_option(handle->world, LILV_OPTION_DYN_MANIFEST, node_false);
			lilv_node_free(node_false);
		}
		lilv_world_load_all(handle->world);
		LilvNode *synthpod_bundle = lilv_new_file_uri(handle->world, NULL, SYNTHPOD_BUNDLE_DIR"/");
		if(synthpod_bundle)
		{
			lilv_world_load_bundle(handle->world, synthpod_bundle);
			lilv_node_free(synthpod_bundle);
		}

		handle->node.pg_group = lilv_new_uri(handle->world, LV2_PORT_GROUPS__group);
		handle->node.lv2_integer = lilv_new_uri(handle->world, LV2_CORE__integer);
		handle->node.lv2_toggled = lilv_new_uri(handle->world, LV2_CORE__toggled);
		handle->node.lv2_minimum = lilv_new_uri(handle->world, LV2_CORE__minimum);
		handle->node.lv2_maximum = lilv_new_uri(handle->world, LV2_CORE__maximum);
		handle->node.lv2_default = lilv_new_uri(handle->world, LV2_CORE__default);
		handle->node.pset_Preset = lilv_new_uri(handle->world, LV2_PRESETS__Preset);
		handle->node.pset_bank = lilv_new_uri(handle->world, LV2_PRESETS__bank);
		handle->node.rdfs_comment = lilv_new_uri(handle->world, LILV_NS_RDFS"comment");
		handle->node.rdfs_range = lilv_new_uri(handle->world, LILV_NS_RDFS"range");
		handle->node.doap_name = lilv_new_uri(handle->world, LILV_NS_DOAP"name");
		handle->node.lv2_minorVersion = lilv_new_uri(handle->world, LV2_CORE__minorVersion);
		handle->node.lv2_microVersion = lilv_new_uri(handle->world, LV2_CORE__microVersion);
		handle->node.doap_license = lilv_new_uri(handle->world, LILV_NS_DOAP"license");
		handle->node.rdfs_label = lilv_new_uri(handle->world, LILV_NS_RDFS"label");
		handle->node.lv2_name = lilv_new_uri(handle->world, LV2_CORE__name);
		handle->node.lv2_OutputPort = lilv_new_uri(handle->world, LV2_CORE__OutputPort);
		handle->node.patch_readable = lilv_new_uri(handle->world, LV2_PATCH__readable);
		handle->node.patch_writable = lilv_new_uri(handle->world, LV2_PATCH__writable);
	}

	const char *NK_SCALE = getenv("NK_SCALE");
	const float scale = NK_SCALE ? atof(NK_SCALE) : 1.f;
	handle->dy = 20.f * scale;

	nk_pugl_config_t *cfg = &handle->win.cfg;
	cfg->width = 1280 * scale;
	cfg->height = 720 * scale;
	cfg->resizable = true;
	cfg->ignore = false;
	cfg->class = "synthpod";
	cfg->title = "Synthpod";
	cfg->parent = (intptr_t)parent;
	cfg->data = handle;
	cfg->expose = _expose;

	char *path;
	if(asprintf(&path, "%sCousine-Regular.ttf", bundle_path) == -1)
		path = NULL;

	cfg->font.face = path;
	cfg->font.size = 13 * scale;
	
	*(intptr_t *)widget = nk_pugl_init(&handle->win);
	nk_pugl_show(&handle->win);

	if(path)
		free(path);

	if(host_resize)
		host_resize->ui_resize(host_resize->handle, cfg->width, cfg->height);

	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	nk_pugl_hide(&handle->win);
	nk_pugl_shutdown(&handle->win);

	for(unsigned m=0; m<handle->num_mods; m++)
	{
		mod_t *mod = &handle->mods[m];

		_mod_free(handle, mod);
	}
	free(handle->mods);

	if(handle->world)
	{
		lilv_node_free(handle->node.pg_group);
		lilv_node_free(handle->node.lv2_integer);
		lilv_node_free(handle->node.lv2_toggled);
		lilv_node_free(handle->node.lv2_minimum);
		lilv_node_free(handle->node.lv2_maximum);
		lilv_node_free(handle->node.lv2_default);
		lilv_node_free(handle->node.pset_Preset);
		lilv_node_free(handle->node.pset_bank);
		lilv_node_free(handle->node.rdfs_comment);
		lilv_node_free(handle->node.rdfs_range);
		lilv_node_free(handle->node.doap_name);
		lilv_node_free(handle->node.lv2_minorVersion);
		lilv_node_free(handle->node.lv2_microVersion);
		lilv_node_free(handle->node.doap_license);
		lilv_node_free(handle->node.rdfs_label);
		lilv_node_free(handle->node.lv2_name);
		lilv_node_free(handle->node.lv2_OutputPort);
		lilv_node_free(handle->node.patch_readable);
		lilv_node_free(handle->node.patch_writable);

		lilv_world_free(handle->world);
	}

	free(handle);
}

static void
port_event(LV2UI_Handle instance, uint32_t port_index, uint32_t size,
	uint32_t format, const void *buffer)
{
	//plughandle_t *handle = instance;

	// nothing
}

static int
_idle(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	return nk_pugl_process_events(&handle->win);
}

static const LV2UI_Idle_Interface idle_ext = {
	.idle = _idle
};

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_UI__idleInterface))
		return &idle_ext;
		
	return NULL;
}

const LV2UI_Descriptor synthpod_common_4_nk = {
	.URI						= SYNTHPOD_COMMON_NK_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= extension_data
};

const LV2UI_Descriptor synthpod_root_4_nk = {
	.URI						= SYNTHPOD_ROOT_NK_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= extension_data
};
