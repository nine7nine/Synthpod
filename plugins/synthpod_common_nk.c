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

#ifdef Bool
#	undef Bool // interferes with atom forge
#endif

typedef enum _property_type_t property_type_t;
typedef enum _selector_main_t selector_main_t;
typedef enum _selector_grid_t selector_grid_t;
typedef enum _selector_search_t selector_search_t;

typedef union _param_union_t param_union_t;

typedef struct _hash_t hash_t;
typedef struct _scale_point_t scale_point_t;
typedef struct _control_port_t control_port_t;
typedef struct _audio_port_t audio_port_t;
typedef struct _port_t port_t;
typedef struct _param_t param_t;
typedef struct _prop_t prop_t;
typedef struct _mod_t mod_t;
typedef struct _plughandle_t plughandle_t;

enum _property_type_t {
	PROPERTY_TYPE_CONTROL = 0,
	PROPERTY_TYPE_AUDIO,
	PROPERTY_TYPE_CV,
	PROPERTY_TYPE_ATOM,
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

struct _hash_t {
	void **nodes;
	unsigned size;
};

union _param_union_t {
 int32_t b;
 int32_t i;
 int64_t h;
 float f;
 double d;
};

struct _scale_point_t {
	char *label;
	param_union_t val;
};

struct _control_port_t {
	hash_t points;
	int points_ref;
	param_union_t min;
	param_union_t max;
	param_union_t span;
	param_union_t val;
	bool is_int;
	bool is_bool;
	bool is_readonly;
};

struct _audio_port_t {
	float peak;
	float gain;
};

struct _port_t {
	property_type_t type;
	const LilvPort *port;
	LilvNodes *groups;

	union {
		control_port_t control;
		audio_port_t audio;
	};
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
	union {
		port_t port;
		param_t param;
	};
};

struct _mod_t {
	const LilvPlugin *plug;

	hash_t ports;
	hash_t groups;
	hash_t banks;
	hash_t params;

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
	const LilvPlugin *plugin_selector;
	unsigned module_selector;
	const LilvNode *preset_selector;

	unsigned num_mods;
	mod_t *mods;

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
		LilvNode *lv2_AudioPort;
		LilvNode *lv2_CVPort;
		LilvNode *lv2_ControlPort;
		LilvNode *atom_AtomPort;
		LilvNode *patch_readable;
		LilvNode *patch_writable;
		LilvNode *rdf_type;
		LilvNode *lv2_Plugin;
	} node;

	float dy;

	enum nk_collapse_states plugin_collapse_states;
	enum nk_collapse_states preset_import_collapse_states;
	enum nk_collapse_states preset_export_collapse_states;
	enum nk_collapse_states plugin_info_collapse_states;
	enum nk_collapse_states preset_info_collapse_states;

	selector_search_t plugin_search_selector;
	selector_search_t preset_search_selector;

	hash_t plugin_matches;
	hash_t preset_matches;

	char plugin_search_buf [SEARCH_BUF_MAX];
	char preset_search_buf [SEARCH_BUF_MAX];

	bool first;
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
_hash_clear(hash_t *hash, void (*free_cb)(void *node))
{
	if(free_cb)
	{
		for(unsigned i = 0; i < hash->size; i++)
			free_cb(hash->nodes[i]);
	}

	free(hash->nodes);
	hash->nodes = NULL;

	hash->size = 0;
}

static bool
_hash_empty(hash_t *hash)
{
	return hash->size == 0;
}

static void
_hash_add(hash_t *hash, void *node)
{
	hash->nodes = realloc(hash->nodes, (hash->size + 1)*sizeof(void *));
	hash->nodes[hash->size] = node;
	hash->size++;
}

static void
_hash_sort(hash_t *hash, int (*cmp)(const void *a, const void *b))
{
	if(hash->size)
		qsort(hash->nodes, hash->size, sizeof(void *), cmp);
}

static void
_hash_sort_r(hash_t *hash, int (*cmp)(const void *a, const void *b, void *data), void *data)
{
	if(hash->size)
		qsort_r(hash->nodes, hash->size, sizeof(void *), cmp, data);
}

static size_t
_hash_offset(hash_t *hash, void **itr)
{
	return itr - hash->nodes;
}

#define HASH_FOREACH(hash, itr) \
	for(void **(itr) = (hash)->nodes; (itr) - (hash)->nodes < (hash)->size; (itr)++)

static int
_node_as_int(const LilvNode *node, int dflt)
{
	if(lilv_node_is_int(node))
		return lilv_node_as_int(node);
	else if(lilv_node_is_float(node))
		return floorf(lilv_node_as_float(node));
	else if(lilv_node_is_bool(node))
		return lilv_node_as_bool(node) ? 1 : 0;
	else
		return dflt;
}

static float
_node_as_float(const LilvNode *node, float dflt)
{
	if(lilv_node_is_int(node))
		return lilv_node_as_int(node);
	else if(lilv_node_is_float(node))
		return lilv_node_as_float(node);
	else if(lilv_node_is_bool(node))
		return lilv_node_as_bool(node) ? 1.f : 0.f;
	else
		return dflt;
}

static int32_t
_node_as_bool(const LilvNode *node, int32_t dflt)
{
	if(lilv_node_is_int(node))
		return lilv_node_as_int(node) != 0;
	else if(lilv_node_is_float(node))
		return lilv_node_as_float(node) != 0.f;
	else if(lilv_node_is_bool(node))
		return lilv_node_as_bool(node) ? 1 : 0;
	else
		return dflt;
}

static void
_register_parameter(plughandle_t *handle, mod_t *mod, const LilvNode *parameter, bool is_readonly)
{
	param_t *param = calloc(1, sizeof(param_t));
	if(!param)
		return;

	_hash_add(&mod->params, param);

	param->param = parameter;
	param->is_readonly = is_readonly;

	param->range = 0;
	LilvNode *range = lilv_world_get(handle->world, parameter, handle->node.rdfs_range, NULL);
	if(range)
	{
		param->range = handle->map->map(handle->map->handle, lilv_node_as_uri(range));
		lilv_node_free(range);
	}

	if(!param->range)
		return;

	LilvNode *min = lilv_world_get(handle->world, parameter, handle->node.lv2_minimum, NULL);
	if(min)
	{
		if(param->range == handle->forge.Int)
			param->min.i = _node_as_int(min, 0);
		else if(param->range == handle->forge.Long)
			param->min.i = _node_as_int(min, 0);
		else if(param->range == handle->forge.Bool)
			param->min.i = _node_as_bool(min, false);
		else if(param->range == handle->forge.Float)
			param->min.i = _node_as_float(min, 0.f);
		else if(param->range == handle->forge.Double)
			param->min.i = _node_as_float(min, 0.f);
		//FIXME
		lilv_node_free(min);
	}

	LilvNode *max = lilv_world_get(handle->world, parameter, handle->node.lv2_maximum, NULL);
	if(max)
	{
		if(param->range == handle->forge.Int)
			param->max.i = _node_as_int(max, 1);
		else if(param->range == handle->forge.Long)
			param->min.i = _node_as_int(min, 1);
		else if(param->range == handle->forge.Bool)
			param->min.i = _node_as_bool(min, true);
		else if(param->range == handle->forge.Float)
			param->min.i = _node_as_float(min, 1.f);
		else if(param->range == handle->forge.Double)
			param->min.i = _node_as_float(min, 1.f);
		//FIXME
		lilv_node_free(max);
	}

	LilvNode *val = lilv_world_get(handle->world, parameter, handle->node.lv2_default, NULL);
	if(val)
	{
		if(param->range == handle->forge.Int)
			param->val.i = _node_as_int(val, 0);
		else if(param->range == handle->forge.Long)
			param->min.i = _node_as_int(min, 0);
		else if(param->range == handle->forge.Bool)
			param->min.i = _node_as_bool(min, false);
		else if(param->range == handle->forge.Float)
			param->min.i = _node_as_float(min, 0.f);
		else if(param->range == handle->forge.Double)
			param->min.i = _node_as_float(min, 0.f);
		//FIXME
		lilv_node_free(val);
	}

	if(param->range == handle->forge.Int)
		param->span.i = param->max.i - param->min.i;
	else if(param->range == handle->forge.Long)
		param->span.h = param->max.h - param->min.h;
	else if(param->range == handle->forge.Bool)
		param->span.i = param->max.i - param->min.i;
	else if(param->range == handle->forge.Float)
		param->span.f = param->max.f - param->min.f;
	else if(param->range == handle->forge.Double)
		param->span.d = param->max.d - param->min.d;
	//FIXME
}

static inline float
dBFSp6(float peak)
{
	const float d = 6.f + 20.f * log10f(fabsf(peak) / 2.f);
	const float e = (d + 64.f) / 70.f;
	return NK_CLAMP(0.f, e, 1.f);
}

#if 0
static inline float
dBFS(float peak)
{
	const float d = 20.f * log10f(fabsf(peak));
	const float e = (d + 70.f) / 70.f;
	return NK_CLAMP(0.f, e, 1.f);
}
#endif

static int
_sort_rdfs_label(const void *a, const void *b, void *data)
{
	plughandle_t *handle = data;

	const LilvNode *group_a = *(const LilvNode **)a;
	const LilvNode *group_b = *(const LilvNode **)b;

	const char *name_a = NULL;
	const char *name_b = NULL;

	LilvNode *node_a = lilv_world_get(handle->world, group_a, handle->node.rdfs_label, NULL);
	if(!node_a)
		node_a = lilv_world_get(handle->world, group_a, handle->node.lv2_name, NULL);

	LilvNode *node_b = lilv_world_get(handle->world, group_b, handle->node.rdfs_label, NULL);
	if(!node_b)
		node_b = lilv_world_get(handle->world, group_b, handle->node.lv2_name, NULL);

	if(node_a)
		name_a = lilv_node_as_string(node_a);
	if(node_b)
		name_b = lilv_node_as_string(node_b);

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	if(node_a)
		lilv_node_free(node_a);
	if(node_b)
		lilv_node_free(node_b);

	return ret;
}

static int
_sort_port_name(const void *a, const void *b, void *data)
{
	mod_t *mod = data;

	const port_t *port_a = *(const port_t **)a;
	const port_t *port_b = *(const port_t **)b;

	const char *name_a = NULL;
	const char *name_b = NULL;

	LilvNode *node_a = lilv_port_get_name(mod->plug, port_a->port);
	LilvNode *node_b = lilv_port_get_name(mod->plug, port_b->port);

	if(node_a)
		name_a = lilv_node_as_string(node_a);
	if(node_b)
		name_b = lilv_node_as_string(node_b);

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	if(node_a)
		lilv_node_free(node_a);
	if(node_b)
		lilv_node_free(node_b);

	return ret;
}

static int
_sort_scale_point_name(const void *a, const void *b)
{
	const scale_point_t *scale_point_a = *(const scale_point_t **)a;
	const scale_point_t *scale_point_b = *(const scale_point_t **)b;

	const char *name_a = scale_point_a->label;
	const char *name_b = scale_point_b->label;

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	return ret;
}

static int
_sort_param_name(const void *a, const void *b, void *data)
{
	plughandle_t *handle = data;

	const param_t *param_a = *(const param_t **)a;
	const param_t *param_b = *(const param_t **)b;

	const char *name_a = NULL;
	const char *name_b = NULL;

	LilvNode *node_a = lilv_world_get(handle->world, param_a->param, handle->node.rdfs_label, NULL);
	if(!node_a)
		node_a = lilv_world_get(handle->world, param_a->param, handle->node.lv2_name, NULL);

	LilvNode *node_b = lilv_world_get(handle->world, param_b->param, handle->node.rdfs_label, NULL);
	if(!node_b)
		node_b = lilv_world_get(handle->world, param_b->param, handle->node.lv2_name, NULL);

	if(node_a)
		name_a = lilv_node_as_string(node_a);
	if(node_b)
		name_b = lilv_node_as_string(node_b);

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	if(node_a)
		lilv_node_free(node_a);
	if(node_b)
		lilv_node_free(node_b);

	return ret;
}

static void
_scale_point_free(void *data)
{
	scale_point_t *point = data;

	if(point->label)
		free(point->label);

	free(data);
}

static void
_port_free(void *data)
{
	port_t *port = data;

	if(port->groups)
		lilv_nodes_free(port->groups);

	if(port->type == PROPERTY_TYPE_CONTROL)
		_hash_clear(&port->control.points, _scale_point_free);

	free(port);
}

static void
_param_free(void *data)
{
	param_t *param = data;

	free(param);
}

static void
_mod_add(plughandle_t *handle, const LilvPlugin *plug)
{
	handle->num_mods+= 1;
	handle->mods = realloc(handle->mods, handle->num_mods * sizeof(mod_t));

	mod_t *mod = &handle->mods[handle->num_mods - 1];
	memset(mod, 0x0, sizeof(mod_t));

	mod->plug = plug;
	const unsigned num_ports = lilv_plugin_get_num_ports(plug);

	for(unsigned p=0; p<num_ports; p++)
	{
		port_t *port = calloc(1, sizeof(port_t));
		if(!port)
			continue;

		_hash_add(&mod->ports, port);

		port->port = lilv_plugin_get_port_by_index(plug, p);
		port->groups = lilv_port_get_value(plug, port->port, handle->node.pg_group);

		LILV_FOREACH(nodes, i, port->groups)
		{
			const LilvNode *port_group = lilv_nodes_get(port->groups, i);

			bool match = false;
			HASH_FOREACH(&mod->groups, itr)
			{
				const LilvNode *mod_group = *itr;

				if(lilv_node_equals(mod_group, port_group))
				{
					match = true;
					break;
				}
			}

			if(!match)
				_hash_add(&mod->groups, (void *)port_group);
		}

		const bool is_audio = lilv_port_is_a(plug, port->port, handle->node.lv2_AudioPort);
		const bool is_cv = lilv_port_is_a(plug, port->port, handle->node.lv2_CVPort);
		const bool is_control = lilv_port_is_a(plug, port->port, handle->node.lv2_ControlPort);
		const bool is_atom = lilv_port_is_a(plug, port->port, handle->node.atom_AtomPort);
		const bool is_output = lilv_port_is_a(plug, port->port, handle->node.lv2_OutputPort);

		if(is_audio)
		{
			port->type = PROPERTY_TYPE_AUDIO;
			audio_port_t *audio = &port->audio;

			audio->peak = dBFSp6(2.f * rand() / RAND_MAX);
			audio->gain = (float)rand() / RAND_MAX;
			//TODO
		}
		else if(is_cv)
		{
			port->type = PROPERTY_TYPE_CV;
			audio_port_t *audio = &port->audio;

			audio->peak = dBFSp6(2.f * rand() / RAND_MAX);
			audio->gain = (float)rand() / RAND_MAX;
			//TODO
		}
		else if(is_control)
		{
			port->type = PROPERTY_TYPE_CONTROL;
			control_port_t *control = &port->control;

			control->is_readonly = is_output;
			control->is_int = lilv_port_has_property(plug, port->port, handle->node.lv2_integer);
			control->is_bool = lilv_port_has_property(plug, port->port, handle->node.lv2_toggled);

			LilvNode *val = NULL;
			LilvNode *min = NULL;
			LilvNode *max = NULL;
			lilv_port_get_range(plug, port->port, &val, &min, &max);

			if(val)
			{
				if(control->is_int)
					control->val.i = _node_as_int(val, 0);
				else if(control->is_bool)
					control->val.i = _node_as_bool(val, false);
				else
					control->val.f = _node_as_float(val, 0.f);

				lilv_node_free(val);
			}
			if(min)
			{
				if(control->is_int)
					control->min.i = _node_as_int(min, 0);
				else if(control->is_bool)
					control->min.i = _node_as_bool(min, false);
				else
					control->min.f = _node_as_float(min, 0.f);

				lilv_node_free(min);
			}
			if(max)
			{
				if(control->is_int)
					control->max.i = _node_as_int(max, 1);
				else if(control->is_bool)
					control->max.i = _node_as_bool(max, true);
				else
					control->max.f = _node_as_float(max, 1.f);

				lilv_node_free(max);
			}

			if(control->is_int)
				control->span.i = control->max.i - control->min.i;
			else if(control->is_bool)
				control->span.i = control->max.i - control->min.i;
			else
				control->span.f = control->max.f - control->min.f;

			if(control->is_int && (control->min.i == 0) && (control->max.i == 1) )
			{
				control->is_int = false;
				control->is_bool = true;
			}

			LilvScalePoints *port_points = lilv_port_get_scale_points(plug, port->port);
			if(port_points)
			{
				LILV_FOREACH(scale_points, i, port_points)
				{
					const LilvScalePoint *port_point = lilv_scale_points_get(port_points, i);
					const LilvNode *label_node = lilv_scale_point_get_label(port_point);
					const LilvNode *value_node = lilv_scale_point_get_value(port_point);

					if(label_node && value_node)
					{
						scale_point_t *point = calloc(1, sizeof(scale_point_t));
						if(!point)
							continue;

						_hash_add(&port->control.points, point);

						point->label = strdup(lilv_node_as_string(label_node));

						if(control->is_int)
						{
							point->val.i = _node_as_int(value_node, 0);
						}
						else if(control->is_bool)
						{
							point->val.i = _node_as_bool(value_node, false);
						}
						else // is_float
						{
							point->val.f = _node_as_float(value_node, 0.f);
						}
					}
				}

				control->points_ref = 0;
				HASH_FOREACH(&port->control.points, itr)
				{
					scale_point_t *point = *itr;

					if(point->val.i == control->val.i) //FIXME
					{
						control->points_ref = _hash_offset(&port->control.points, itr);
						break;
					}
				}

				_hash_sort(&port->control.points, _sort_scale_point_name);

				lilv_scale_points_free(port_points);
			}
		}
		else if(is_atom)
		{
			port->type = PROPERTY_TYPE_ATOM;
			//TODO
		}
	}

	_hash_sort_r(&mod->ports, _sort_port_name, mod);
	_hash_sort_r(&mod->groups, _sort_rdfs_label, handle);

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
					bool match = false;
					HASH_FOREACH(&mod->banks, itr)
					{
						const LilvNode *mod_bank = *itr;

						if(lilv_node_equals(mod_bank, bank))
						{
							match = true;
							break;
						}
					}

					if(!match)
					{
						_hash_add(&mod->banks, lilv_node_duplicate(bank));
					}
				}
				lilv_nodes_free(banks);
			}
		}

		_hash_sort_r(&mod->banks, _sort_rdfs_label, handle);
	}

	mod->readables = lilv_plugin_get_value(plug, handle->node.patch_readable);
	mod->writables = lilv_plugin_get_value(plug, handle->node.patch_writable);

	LILV_FOREACH(nodes, i, mod->readables)
	{
		const LilvNode *parameter = lilv_nodes_get(mod->readables, i);

		_register_parameter(handle, mod, parameter, true);
	}
	LILV_FOREACH(nodes, i, mod->writables)
	{
		const LilvNode *parameter = lilv_nodes_get(mod->readables, i);

		_register_parameter(handle, mod, parameter, false);
	}

	_hash_sort_r(&mod->params, _sort_param_name, handle);

	nk_pugl_post_redisplay(&handle->win); //FIXME
}

static void
_node_free(void *data)
{
	LilvNode *node = data;
	lilv_node_free(node);
}

static void
_mod_free(plughandle_t *handle, mod_t *mod)
{
	_hash_clear(&mod->ports, _port_free);
	_hash_clear(&mod->banks, _node_free);
	_hash_clear(&mod->groups, NULL);
	_hash_clear(&mod->params, _param_free);

	if(mod->presets)
	{
		LILV_FOREACH(nodes, i, mod->presets)
		{
			const LilvNode *preset = lilv_nodes_get(mod->presets, i);
			lilv_world_unload_resource(handle->world, preset);
		}

		lilv_nodes_free(mod->presets);
	}

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

static int
_sort_plugin_name(const void *a, const void *b)
{
	const LilvPlugin *plug_a = *(const LilvPlugin **)a;
	const LilvPlugin *plug_b = *(const LilvPlugin **)b;

	const char *name_a = NULL;
	const char *name_b = NULL;

	LilvNode *node_a = lilv_plugin_get_name(plug_a);
	LilvNode *node_b = lilv_plugin_get_name(plug_b);

	if(node_a)
		name_a = lilv_node_as_string(node_a);
	if(node_b)
		name_b = lilv_node_as_string(node_b);

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	if(node_a)
		lilv_node_free(node_a);
	if(node_b)
		lilv_node_free(node_b);

	return ret;
}

static void
_refresh_main_plugin_list(plughandle_t *handle)
{
	_hash_clear(&handle->plugin_matches, NULL);

	const LilvPlugins *plugs = lilv_world_get_all_plugins(handle->world);

	LilvNode *p = NULL;
	if(handle->plugin_search_selector == SELECTOR_SEARCH_COMMENT)
		p = handle->node.rdfs_comment;
	else if(handle->plugin_search_selector == SELECTOR_SEARCH_PROJECT)
		p = handle->node.doap_name;

	int count = 0;
	bool selector_visible = false;
	LILV_FOREACH(plugins, i, plugs)
	{
		const LilvPlugin *plug = lilv_plugins_get(plugs, i);

		LilvNode *name_node = lilv_plugin_get_name(plug);
		if(name_node)
		{
			const char *name_str = lilv_node_as_string(name_node);
			bool visible = strlen(handle->plugin_search_buf) == 0;

			if(!visible)
			{
				switch(handle->plugin_search_selector)
				{
					case SELECTOR_SEARCH_NAME:
					{
						if(strcasestr(name_str, handle->plugin_search_buf))
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
								if(strcasestr(lilv_node_as_string(label_node), handle->plugin_search_buf))
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
							if(strcasestr(lilv_node_as_string(author_node), handle->plugin_search_buf))
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
								if(strcasestr(lilv_node_as_string(label_node), handle->plugin_search_buf))
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
								if(strcasestr(lilv_node_as_string(label_node), handle->plugin_search_buf))
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
				_hash_add(&handle->plugin_matches, (void *)plug);
			}

			lilv_node_free(name_node);
		}
	}

	_hash_sort(&handle->plugin_matches, _sort_plugin_name);
}

static void
_expose_main_plugin_list(plughandle_t *handle, struct nk_context *ctx,
	bool find_matches)
{
	if(_hash_empty(&handle->plugin_matches) || find_matches)
		_refresh_main_plugin_list(handle);

	const LilvPlugins *plugs = lilv_world_get_all_plugins(handle->world);

	int count = 0;
	bool selector_visible = false;
	HASH_FOREACH(&handle->plugin_matches, itr)
	{
		const LilvPlugin *plug = *itr;
		if(plug)
		{
			LilvNode *name_node = lilv_plugin_get_name(plug);
			if(name_node)
			{
				const char *name_str = lilv_node_as_string(name_node);

				if(nk_widget_is_mouse_clicked(ctx, NK_BUTTON_RIGHT))
				{
					handle->plugin_selector = plug;
					_load(handle);
				}

				nk_style_push_style_item(ctx, &ctx->style.selectable.normal, (count++ % 2)
					? nk_style_item_color(nk_rgb(40, 40, 40))
					: nk_style_item_color(nk_rgb(45, 45, 45))); // NK_COLOR_WINDOW
				nk_style_push_style_item(ctx, &ctx->style.selectable.hover,
					nk_style_item_color(nk_rgb(35, 35, 35)));
				nk_style_push_style_item(ctx, &ctx->style.selectable.pressed,
					nk_style_item_color(nk_rgb(30, 30, 30)));
				nk_style_push_style_item(ctx, &ctx->style.selectable.hover_active,
					nk_style_item_color(nk_rgb(35, 35, 35)));
				nk_style_push_style_item(ctx, &ctx->style.selectable.pressed_active,
					nk_style_item_color(nk_rgb(30, 30, 30)));

				const int selected = plug == handle->plugin_selector;
				if(nk_select_label(ctx, name_str, NK_TEXT_LEFT, selected))
				{
					handle->plugin_selector = plug;
				}

				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);

				if(plug == handle->plugin_selector)
					selector_visible = true;

				lilv_node_free(name_node);
			}
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
_refresh_main_preset_list_for_bank(plughandle_t *handle,
	LilvNodes *presets, const LilvNode *preset_bank)
{
	bool search = strlen(handle->preset_search_buf) != 0;

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
			//FIXME support other search criteria
			LilvNode *label_node = lilv_world_get(handle->world, preset, handle->node.rdfs_label, NULL);
			if(!label_node)
				label_node = lilv_world_get(handle->world, preset, handle->node.lv2_name, NULL);
			if(label_node)
			{
				const char *label_str = lilv_node_as_string(label_node);

				if(!search || strcasestr(label_str, handle->preset_search_buf))
				{
					_hash_add(&handle->preset_matches, (void *)preset);
				}

				lilv_node_free(label_node);
			}
		}
	}
}

static void
_refresh_main_preset_list(plughandle_t *handle, mod_t *mod)
{
	_hash_clear(&handle->preset_matches, NULL);

	HASH_FOREACH(&mod->banks, itr)
	{
		const LilvNode *bank = *itr;

		_refresh_main_preset_list_for_bank(handle, mod->presets, bank);
	}

	_refresh_main_preset_list_for_bank(handle, mod->presets, NULL);

	_hash_sort_r(&handle->preset_matches, _sort_rdfs_label, handle);
}

static void
_expose_main_preset_list_for_bank(plughandle_t *handle, struct nk_context *ctx,
	const LilvNode *preset_bank)
{
	bool first = true;
	int count = 0;
	HASH_FOREACH(&handle->preset_matches, itr)
	{
		const LilvNode *preset = *itr;

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
			if(!label_node)
				label_node = lilv_world_get(handle->world, preset, handle->node.lv2_name, NULL);
			if(label_node)
			{
				if(first)
				{
					struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
					LilvNode *bank_label_node = NULL;
					if(preset_bank)
					{
						bank_label_node = lilv_world_get(handle->world, preset_bank, handle->node.rdfs_label, NULL);
						if(!bank_label_node)
							bank_label_node = lilv_world_get(handle->world, preset_bank, handle->node.lv2_name, NULL);
					}
					const char *bank_label = bank_label_node
						? lilv_node_as_string(bank_label_node)
						: "Unbanked";

					const struct nk_rect bounds = nk_widget_bounds(ctx);
					nk_fill_rect(canvas, bounds, 0, nk_rgb(16, 16, 16));
					nk_label(ctx, bank_label, NK_TEXT_CENTERED);

					if(bank_label_node)
						lilv_node_free(bank_label_node);

					first = false;
				}

				const char *label_str = lilv_node_as_string(label_node);

				if(nk_widget_is_mouse_clicked(ctx, NK_BUTTON_RIGHT))
				{
					handle->preset_selector = preset;
					_load(handle);
				}

				nk_style_push_style_item(ctx, &ctx->style.selectable.normal, (count++ % 2)
					? nk_style_item_color(nk_rgb(40, 40, 40))
					: nk_style_item_color(nk_rgb(45, 45, 45))); // NK_COLOR_WINDOW
				nk_style_push_style_item(ctx, &ctx->style.selectable.hover,
					nk_style_item_color(nk_rgb(35, 35, 35)));
				nk_style_push_style_item(ctx, &ctx->style.selectable.pressed,
					nk_style_item_color(nk_rgb(30, 30, 30)));
				nk_style_push_style_item(ctx, &ctx->style.selectable.hover_active,
					nk_style_item_color(nk_rgb(35, 35, 35)));
				nk_style_push_style_item(ctx, &ctx->style.selectable.pressed_active,
					nk_style_item_color(nk_rgb(30, 30, 30)));

				int selected = preset == handle->preset_selector;
				if(nk_selectable_label(ctx, label_str, NK_TEXT_LEFT, &selected))
				{
					handle->preset_selector = preset;
				}

				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);

				lilv_node_free(label_node);
			}
		}
	}
}

static void
_expose_main_preset_list(plughandle_t *handle, struct nk_context *ctx,
	bool find_matches)
{
	mod_t *mod = handle->module_selector < handle->num_mods
		? &handle->mods[handle->module_selector] : NULL;

	if(mod && mod->presets)
	{
		if(_hash_empty(&handle->preset_matches) || find_matches)
			_refresh_main_preset_list(handle, mod);

		HASH_FOREACH(&mod->banks, itr)
		{
			const LilvNode *bank = *itr;

			_expose_main_preset_list_for_bank(handle, ctx, bank);
		}

		_expose_main_preset_list_for_bank(handle, ctx, NULL);
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
	if(!name_node)
		name_node = lilv_world_get(handle->world, preset, handle->node.lv2_name, NULL);
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

static int
_dial_bool(struct nk_context *ctx, int32_t *val, struct nk_color color, bool editable)
{
	const int32_t tmp = *val;
	struct nk_rect bounds = nk_layout_space_bounds(ctx);
	const bool left_mouse_click_in_cursor = nk_widget_is_mouse_clicked(ctx, NK_BUTTON_LEFT);
	const enum nk_widget_layout_states layout_states = nk_widget(&bounds, ctx);

	if(layout_states != NK_WIDGET_INVALID)
	{
		enum nk_widget_states states = NK_WIDGET_STATE_INACTIVE;
		struct nk_input *in = ( (layout_states == NK_WIDGET_ROM)
			|| (ctx->current->layout->flags & NK_WINDOW_ROM) ) ? 0 : &ctx->input;

		if(in && editable && (layout_states == NK_WIDGET_VALID) )
		{
			bool mouse_has_scrolled = false;

			if(left_mouse_click_in_cursor)
			{
				states = NK_WIDGET_STATE_ACTIVED;
			}
			else if(nk_input_is_mouse_hovering_rect(in, bounds))
			{
				if(in->mouse.scroll_delta != 0.f) // has scrolling
				{
					mouse_has_scrolled = true;
					in->mouse.scroll_delta = 0.f;
				}

				states = NK_WIDGET_STATE_HOVER;
			}

			if(left_mouse_click_in_cursor || mouse_has_scrolled)
			{
				*val = !*val;
			}
		}

		const struct nk_style_item *fg = NULL;
		const struct nk_style_item *bg = NULL;

		switch(states)
		{
			case NK_WIDGET_STATE_HOVER:
			{
				bg = &ctx->style.progress.hover;
				fg = &ctx->style.progress.cursor_hover;
			}	break;
			case NK_WIDGET_STATE_ACTIVED:
			{
				bg = &ctx->style.progress.active;
				fg = &ctx->style.progress.cursor_active;
			}	break;
			default:
			{
				bg = &ctx->style.progress.normal;
				fg = &ctx->style.progress.cursor_normal;
			}	break;
		}

		const struct nk_color bg_color = bg->data.color;
		struct nk_color fg_color = fg->data.color;

		fg_color.r = (int)fg_color.r * color.r / 0xff;
		fg_color.g = (int)fg_color.g * color.g / 0xff;
		fg_color.b = (int)fg_color.b * color.b / 0xff;
		fg_color.a = (int)fg_color.a * color.a / 0xff;

		struct nk_command_buffer *canv= nk_window_get_canvas(ctx);
		const float w2 = bounds.w/2;
		const float h2 = bounds.h/2;
		const float r1 = NK_MIN(w2, h2);
		const float r2 = r1 / 2;
		const float cx = bounds.x + w2;
		const float cy = bounds.y + h2;

		nk_fill_arc(canv, cx, cy, r2, 0.f, 2*M_PI, fg_color);
		nk_fill_arc(canv, cx, cy, r2 - 2, 0.f, 2*M_PI, ctx->style.window.background);
		nk_fill_arc(canv, cx, cy, r2 - 4, 0.f, 2*M_PI,
			*val ? fg_color : bg_color);
	}

	return tmp != *val;
}

static float
_dial_numeric_behavior(struct nk_context *ctx, struct nk_rect bounds,
	enum nk_widget_states *states, int *divider, struct nk_input *in)
{
	const struct nk_mouse_button *btn = &in->mouse.buttons[NK_BUTTON_LEFT];;
	const bool left_mouse_down = btn->down;
	const bool left_mouse_click_in_cursor = nk_input_has_mouse_click_down_in_rect(in,
		NK_BUTTON_LEFT, bounds, nk_true);

	float dd = 0.f;
	if(left_mouse_down && left_mouse_click_in_cursor)
	{
		const float dx = in->mouse.delta.x;
		const float dy = in->mouse.delta.y;
		dd = fabs(dx) > fabs(dy) ? dx : -dy;

		*states = NK_WIDGET_STATE_ACTIVED;
	}
	else if(nk_input_is_mouse_hovering_rect(in, bounds))
	{
		if(in->mouse.scroll_delta != 0.f) // has scrolling
		{
			dd = in->mouse.scroll_delta;
			in->mouse.scroll_delta = 0.f;
		}

		*states = NK_WIDGET_STATE_HOVER;
	}

	if(nk_input_is_key_down(in, NK_KEY_CTRL))
		*divider *= 4;
	if(nk_input_is_key_down(in, NK_KEY_SHIFT))
		*divider *= 4;

	return dd;
}

static void
_dial_numeric_draw(struct nk_context *ctx, struct nk_rect bounds,
	enum nk_widget_states states, float perc, struct nk_color color)
{
	struct nk_command_buffer *canv= nk_window_get_canvas(ctx);
	const struct nk_style_item *bg = NULL;
	const struct nk_style_item *fg = NULL;

	switch(states)
	{
		case NK_WIDGET_STATE_HOVER:
		{
			bg = &ctx->style.progress.hover;
			fg = &ctx->style.progress.cursor_hover;
		}	break;
		case NK_WIDGET_STATE_ACTIVED:
		{
			bg = &ctx->style.progress.active;
			fg = &ctx->style.progress.cursor_active;
		}	break;
		default:
		{
			bg = &ctx->style.progress.normal;
			fg = &ctx->style.progress.cursor_normal;
		}	break;
	}

	const struct nk_color bg_color = bg->data.color;
	struct nk_color fg_color = fg->data.color;

	fg_color.r = (int)fg_color.r * color.r / 0xff;
	fg_color.g = (int)fg_color.g * color.g / 0xff;
	fg_color.b = (int)fg_color.b * color.b / 0xff;
	fg_color.a = (int)fg_color.a * color.a / 0xff;

	const float w2 = bounds.w/2;
	const float h2 = bounds.h/2;
	const float r1 = NK_MIN(w2, h2);
	const float r2 = r1 / 2;
	const float cx = bounds.x + w2;
	const float cy = bounds.y + h2;
	const float aa = M_PI/6;
	const float a1 = M_PI/2 + aa;
	const float a2 = 2*M_PI + M_PI/2 - aa;
	const float a3 = a1 + (a2 - a1)*perc;

	nk_fill_arc(canv, cx, cy, r1, a1, a2, bg_color);
	nk_fill_arc(canv, cx, cy, r1, a1, a3, fg_color);
	nk_fill_arc(canv, cx, cy, r2, 0.f, 2*M_PI, ctx->style.window.background);
}

static int
_dial_double(struct nk_context *ctx, double min, double *val, double max, float mul,
	struct nk_color color, bool editable)
{
	const double tmp = *val;
	struct nk_rect bounds = nk_layout_space_bounds(ctx);
	const enum nk_widget_layout_states layout_states = nk_widget(&bounds, ctx);

	if(layout_states != NK_WIDGET_INVALID)
	{
		enum nk_widget_states states = NK_WIDGET_STATE_INACTIVE;
		const double range = max - min;
		struct nk_input *in = ( (layout_states == NK_WIDGET_ROM)
			|| (ctx->current->layout->flags & NK_WINDOW_ROM) ) ? 0 : &ctx->input;

		if(in && editable && (layout_states == NK_WIDGET_VALID) )
		{
			int divider = 1;
			const float dd = _dial_numeric_behavior(ctx, bounds, &states, &divider, in);

			if(dd != 0.f) // update value
			{
				const double per_pixel_inc = mul * range / bounds.w / divider;

				*val += dd * per_pixel_inc;
				*val = NK_CLAMP(min, *val, max);
			}
		}

		const float perc = (*val - min) / range;
		_dial_numeric_draw(ctx, bounds, states, perc, color);
	}

	return tmp != *val;
}

static int
_dial_long(struct nk_context *ctx, int64_t min, int64_t *val, int64_t max, float mul,
	struct nk_color color, bool editable)
{
	const int64_t tmp = *val;
	struct nk_rect bounds = nk_layout_space_bounds(ctx);
	const enum nk_widget_layout_states layout_states = nk_widget(&bounds, ctx);

	if(layout_states != NK_WIDGET_INVALID)
	{
		enum nk_widget_states states = NK_WIDGET_STATE_INACTIVE;
		const int64_t range = max - min;
		struct nk_input *in = ( (layout_states == NK_WIDGET_ROM)
			|| (ctx->current->layout->flags & NK_WINDOW_ROM) ) ? 0 : &ctx->input;

		if(in && editable && (layout_states == NK_WIDGET_VALID) )
		{
			int divider = 1;
			const float dd = _dial_numeric_behavior(ctx, bounds, &states, &divider, in);

			if(dd != 0.f) // update value
			{
				const double per_pixel_inc = mul * range / bounds.w / divider;

				const double diff = dd * per_pixel_inc;
				*val += diff < 0.0 ? floor(diff) : ceil(diff);
				*val = NK_CLAMP(min, *val, max);
			}
		}

		const float perc = (float)(*val - min) / range;
		_dial_numeric_draw(ctx, bounds, states, perc, color);
	}

	return tmp != *val;
}

static int
_dial_float(struct nk_context *ctx, float min, float *val, float max, float mul,
	struct nk_color color, bool editable)
{
	double tmp = *val;
	const int res = _dial_double(ctx, min, &tmp, max, mul, color, editable);
	*val = tmp;

	return res;
}

static int
_dial_int(struct nk_context *ctx, int32_t min, int32_t *val, int32_t max, float mul,
	struct nk_color color, bool editable)
{
	int64_t tmp = *val;
	const int res = _dial_long(ctx, min, &tmp, max, mul, color, editable);
	*val = tmp;

	return res;
}

static void
_expose_atom_port(struct nk_context *ctx, mod_t *mod, audio_port_t *audio,
	float dy, const char *name_str, bool is_cv)
{
	const float DY = nk_window_get_content_region(ctx).h
		- 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);
		nk_label(ctx, name_str, NK_TEXT_LEFT);

		//FIXME

		nk_group_end(ctx);
	}

	//FIXME
}

static void
_expose_audio_port(struct nk_context *ctx, mod_t *mod, audio_port_t *audio,
	float dy, const char *name_str, bool is_cv)
{
	const float DY = nk_window_get_content_region(ctx).h
		- 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);
		nk_label(ctx, name_str, NK_TEXT_LEFT);

		struct nk_rect bounds;
		const enum nk_widget_layout_states states = nk_widget(&bounds, ctx);
		if(states != NK_WIDGET_INVALID)
		{
			struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

			const struct nk_color bg = ctx->style.property.normal.data.color;
			nk_fill_rect(canvas, bounds, ctx->style.property.rounding, bg);
			nk_stroke_rect(canvas, bounds, ctx->style.property.rounding, ctx->style.property.border, bg);

			const struct nk_rect orig = bounds;
			struct nk_rect outline;
			const float mx1 = 58.f / 70.f;
			const float mx2 = 12.f / 70.f;
			const uint8_t alph = 0x7f;

			// <= -6dBFS
			{
				const float dbfs = NK_MIN(audio->peak, mx1);
				const uint8_t dcol = 0xff * dbfs / mx1;
				const struct nk_color left = is_cv
					? nk_rgba(0xff, 0x00, 0xff, alph)
					: nk_rgba(0x00, 0xff, 0xff, alph);
				const struct nk_color bottom = left;
				const struct nk_color right = is_cv
					? nk_rgba(0xff, dcol, 0xff-dcol, alph)
					: nk_rgba(dcol, 0xff, 0xff-dcol, alph);
				const struct nk_color top = right;

				const float ox = ctx->style.font->height/2 + ctx->style.property.border + ctx->style.property.padding.x;
				const float oy = ctx->style.property.border + ctx->style.property.padding.y;
				bounds.x += ox;
				bounds.y += oy;
				bounds.w -= 2*ox;
				bounds.h -= 2*oy;
				outline = bounds;
				bounds.w *= dbfs;

				nk_fill_rect_multi_color(canvas, bounds, left, top, right, bottom);
			}

			// > 6dBFS
			if(audio->peak > mx1)
			{
				const float dbfs = audio->peak - mx1;
				const uint8_t dcol = 0xff * dbfs / mx2;
				const struct nk_color left = nk_rgba(0xff, 0xff, 0x00, alph);
				const struct nk_color bottom = left;
				const struct nk_color right = nk_rgba(0xff, 0xff - dcol, 0x00, alph);
				const struct nk_color top = right;

				bounds = outline;
				bounds.x += bounds.w * mx1;
				bounds.w *= dbfs;
				nk_fill_rect_multi_color(canvas, bounds, left, top, right, bottom);
			}

			// draw 6dBFS lines from -60 to +6
			for(unsigned i = 4; i <= 70; i += 6)
			{
				const bool is_zero = (i == 64);
				const float dx = outline.w * i / 70.f;

				const float x0 = outline.x + dx;
				const float y0 = is_zero ? orig.y + 2.f : outline.y;

				const float border = (is_zero ? 2.f : 1.f) * ctx->style.window.group_border;

				const float x1 = x0;
				const float y1 = is_zero ? orig.y + orig.h - 2.f : outline.y + outline.h;

				nk_stroke_line(canvas, x0, y0, x1, y1, border, ctx->style.window.group_border_color);
			}

			nk_stroke_rect(canvas, outline, 0.f, ctx->style.window.group_border, ctx->style.window.group_border_color);
		}

		nk_group_end(ctx);
	}

	if(_dial_float(ctx, 0.f, &audio->gain, 1.f, 1.f, nk_rgb(0xff, 0xff, 0xff), true))
	{
		//FIXME
	}
}

const char *lab = "#"; //FIXME

static void
_expose_control_port(struct nk_context *ctx, mod_t *mod, control_port_t *control,
	float dy, const char *name_str)
{
	const float DY = nk_window_get_content_region(ctx).h
		- 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);
		nk_label(ctx, name_str, NK_TEXT_LEFT);

		if(control->is_int)
		{
			if(control->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%"PRIi32, control->val.i);
			}
			else // !readonly
			{
				const float inc = control->span.i / nk_widget_width(ctx);
				int val = control->val.i;
				nk_property_int(ctx, lab, control->min.i, &val, control->max.i, 1.f, inc);
				control->val.i = val;
			}
		}
		else if(control->is_bool)
		{
			nk_spacing(ctx, 1);
		}
		else if(!_hash_empty(&control->points))
		{
			scale_point_t *ref = control->points.nodes[control->points_ref];
			if(nk_combo_begin_label(ctx, ref->label, nk_vec2(nk_widget_width(ctx), 7*dy)))
			{
				nk_layout_row_dynamic(ctx, dy, 1);
				HASH_FOREACH(&control->points, itr)
				{
					scale_point_t *point = *itr;

					if(nk_combo_item_label(ctx, point->label, NK_TEXT_LEFT) && !control->is_readonly)
					{
						control->points_ref = _hash_offset(&control->points, itr);
						control->val = point->val;
						//FIXME
					}
				}

				nk_combo_end(ctx);
			}
		}
		else // is_float
		{
			if(control->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%f", control->val.f);
			}
			else // !readonly
			{
				const float step = control->span.f / 100.f;
				const float inc = control->span.f / nk_widget_width(ctx);
				nk_property_float(ctx, lab, control->min.f, &control->val.f, control->max.f, step, inc);
			}
		}

		nk_group_end(ctx);
	}

	if(control->is_int)
	{
		if(_dial_int(ctx, control->min.i, &control->val.i, control->max.i, 1.f, nk_rgb(0xff, 0xff, 0xff), !control->is_readonly))
		{
			//FIXME
		}
	}
	else if(control->is_bool)
	{
		if(_dial_bool(ctx, &control->val.i, nk_rgb(0xff, 0xff, 0xff), !control->is_readonly))
		{
			//FIXME
		}
	}
	else if(!_hash_empty(&control->points))
	{
		nk_spacing(ctx, 1);
	}
	else // is_float
	{
		if(_dial_float(ctx, control->min.f, &control->val.f, control->max.f, 1.f, nk_rgb(0xff, 0xff, 0xff), !control->is_readonly))
		{
			//FIXME
		}
	}
}

static void
_expose_port(struct nk_context *ctx, mod_t *mod, port_t *port, float dy)
{
	LilvNode *name_node = lilv_port_get_name(mod->plug, port->port);
	const char *name_str = name_node ? lilv_node_as_string(name_node) : "Unknown";

	if(nk_group_begin(ctx, name_str, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
	{
		switch(port->type)
		{
			case PROPERTY_TYPE_AUDIO:
			{
				_expose_audio_port(ctx, mod, &port->audio, dy, name_str, false);
			} break;
			case PROPERTY_TYPE_CV:
			{
				_expose_audio_port(ctx, mod, &port->audio, dy, name_str, true);
			} break;
			case PROPERTY_TYPE_CONTROL:
			{
				_expose_control_port(ctx, mod, &port->control, dy, name_str);
			} break;
			case PROPERTY_TYPE_ATOM:
			{
				_expose_atom_port(ctx, mod, &port->audio, dy, name_str, false);
			} break;
		}

		nk_group_end(ctx);
	}

	if(name_node)
		lilv_node_free(name_node);
}

static void
_expose_param_inner(struct nk_context *ctx, param_t *param, plughandle_t *handle,
	float dy, const char *name_str)
{
	const float DY = nk_window_get_content_region(ctx).h
		- 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);
		nk_label(ctx, name_str, NK_TEXT_LEFT);

		if(param->range == handle->forge.Int)
		{
			if(param->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%"PRIi32, param->val.i);
			}
			else // !readonly
			{
				const float inc = param->span.i / nk_widget_width(ctx);
				int val = param->val.i;
				nk_property_int(ctx, lab, param->min.i, &val, param->max.i, 1.f, inc);
				param->val.i = val;
			}
		}
		else if(param->range == handle->forge.Long)
		{
			if(param->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%"PRIi64, param->val.h);
			}
			else // !readonly
			{
				const float inc = param->span.h / nk_widget_width(ctx);
				int val = param->val.h;
				nk_property_int(ctx, lab, param->min.h, &val, param->max.h, 1.f, inc);
				param->val.h = val;
			}
		}
		else if(param->range == handle->forge.Bool)
		{
			nk_spacing(ctx, 1);
		}
		else if(param->range == handle->forge.Float)
		{
			if(param->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%f", param->val.f);
			}
			else // !readonly
			{
				const float step = param->span.f / 100.f;
				const float inc = param->span.f / nk_widget_width(ctx);
				nk_property_float(ctx, lab, param->min.f, &param->val.f, param->max.f, step, inc);
			}
		}
		else if(param->range == handle->forge.Double)
		{
			if(param->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%lf", param->val.d);
			}
			else // !readonly
			{
				const double step = param->span.d / 100.0;
				const float inc = param->span.d / nk_widget_width(ctx);
				nk_property_double(ctx, lab, param->min.d, &param->val.d, param->max.d, step, inc);
			}
		}
		else
		{
			nk_spacing(ctx, 1);
		}
		//FIXME

		nk_group_end(ctx);
	}

	if(param->range == handle->forge.Int)
	{
		if(_dial_int(ctx, param->min.i, &param->val.i, param->max.i, 1.f, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			//FIXME
		}
	}
	else if(param->range == handle->forge.Long)
	{
		if(_dial_long(ctx, param->min.h, &param->val.h, param->max.h, 1.f, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			//FIXME
		}
	}
	else if(param->range == handle->forge.Bool)
	{
		if(_dial_bool(ctx, &param->val.i, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			//FIXME
		}
	}
	else if(param->range == handle->forge.Float)
	{
		if(_dial_float(ctx, param->min.f, &param->val.f, param->max.f, 1.f, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			//FIXME
		}
	}
	else if(param->range == handle->forge.Double)
	{
		if(_dial_double(ctx, param->min.d, &param->val.d, param->max.d, 1.f, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			//FIXME
		}
	}
	else
	{
		nk_spacing(ctx, 1);
	}
	//FIXME
}

static void
_expose_param(plughandle_t *handle, struct nk_context *ctx, param_t *param, float dy)
{
	LilvNode *name_node = lilv_world_get(handle->world, param->param, handle->node.rdfs_label, NULL);
	if(!name_node)
		name_node = lilv_world_get(handle->world, param->param, handle->node.lv2_name, NULL);
	const char *name_str = name_node ? lilv_node_as_string(name_node) : "Unknown";

	if(nk_group_begin(ctx, name_str, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
	{
		_expose_param_inner(ctx, param, handle, dy, name_str);

		nk_group_end(ctx);
	}

	if(name_node)
		lilv_node_free(name_node);
}

static void
_expose_main_body(plughandle_t *handle, struct nk_context *ctx, float dh, float dy)
{
	const struct nk_vec2 group_padding = ctx->style.window.group_padding;

	switch(handle->main_selector)
	{
		case SELECTOR_MAIN_GRID:
		{
			bool plugin_find_matches = false;
			bool preset_find_matches = false;
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
							preset_find_matches = true;
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
					const float DY = dy*2 + 6*ctx->style.window.group_padding.y + 2*ctx->style.window.group_border;

					struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
					HASH_FOREACH(&mod->groups, itr)
					{
						const LilvNode *mod_group = *itr;

						LilvNode *group_label_node = lilv_world_get(handle->world, mod_group, handle->node.rdfs_label, NULL);
						if(!group_label_node)
							group_label_node = lilv_world_get(handle->world, mod_group, handle->node.lv2_name, NULL);
						if(group_label_node)
						{
							nk_layout_row_dynamic(ctx, dy, 1);
							const struct nk_rect bounds = nk_widget_bounds(ctx);
							nk_fill_rect(canvas, bounds, 0, nk_rgb(16, 16, 16));
							nk_label(ctx, lilv_node_as_string(group_label_node), NK_TEXT_CENTERED);
							lilv_node_free(group_label_node);

							nk_layout_row_dynamic(ctx, DY, 3);
							HASH_FOREACH(&mod->ports, port_itr)
							{
								port_t *port = *port_itr;
								if(!lilv_nodes_contains(port->groups, mod_group))
									continue;

								_expose_port(ctx, mod, port, dy);
							}
						}
					}

					nk_layout_row_dynamic(ctx, dy, 1);
					struct nk_rect bounds = nk_widget_bounds(ctx);
					nk_fill_rect(canvas, bounds, 0, nk_rgb(16, 16, 16));
					nk_label(ctx, "Ungrouped", NK_TEXT_CENTERED);

					nk_layout_row_dynamic(ctx, DY, 3);
					HASH_FOREACH(&mod->ports, itr)
					{
						port_t *port = *itr;
						if(lilv_nodes_size(port->groups))
							continue;

						_expose_port(ctx, mod, port, dy);
					}

					nk_layout_row_dynamic(ctx, dy, 1);
					bounds = nk_widget_bounds(ctx);
					nk_fill_rect(canvas, bounds, 0, nk_rgb(16, 16, 16));
					nk_label(ctx, "Parameter", NK_TEXT_CENTERED);

					nk_layout_row_dynamic(ctx, DY, 3);
					HASH_FOREACH(&mod->params, itr)
					{
						param_t *param = *itr;

						_expose_param(handle, ctx, param, dy);
					}
				}

				nk_group_end(ctx);
			}

			nk_layout_row_push(ctx, 0.25);
			if(nk_group_begin(ctx, "Selectables", NK_WINDOW_BORDER | NK_WINDOW_TITLE))
			{
				if(nk_tree_state_push(ctx, NK_TREE_TAB, "Plugin Import", &handle->plugin_collapse_states))
				{
					const float dim [2] = {0.4, 0.6};
					nk_layout_row(ctx, NK_DYNAMIC, dy, 2, dim);
					const selector_search_t old_sel = handle->plugin_search_selector;
					handle->plugin_search_selector = nk_combo(ctx, search_labels, SELECTOR_SEARCH_MAX,
						handle->plugin_search_selector, dy, nk_vec2(nk_widget_width(ctx), 7*dy));
					if(old_sel != handle->plugin_search_selector)
						plugin_find_matches = true;
					const size_t old_len = strlen(handle->plugin_search_buf);
					const nk_flags flags = nk_edit_string_zero_terminated(ctx,
						NK_EDIT_FIELD | NK_EDIT_SIG_ENTER,
						handle->plugin_search_buf, SEARCH_BUF_MAX, nk_filter_default);
					if( (flags & NK_EDIT_COMMITED) || (old_len != strlen(handle->plugin_search_buf)) )
						plugin_find_matches = true;

					nk_layout_row_dynamic(ctx, dy*20, 1);
					if(nk_group_begin(ctx, "Plugins_List", NK_WINDOW_BORDER))
					{
						nk_layout_row_dynamic(ctx, dy, 1);
						_expose_main_plugin_list(handle, ctx, plugin_find_matches);

						nk_group_end(ctx);
					}

					if(nk_tree_state_push(ctx, NK_TREE_NODE, "Info", &handle->plugin_info_collapse_states))
					{
						_expose_main_plugin_info(handle, ctx);

						nk_tree_state_pop(ctx);
					}

					nk_tree_state_pop(ctx);
				}

				if(nk_tree_state_push(ctx, NK_TREE_TAB, "Preset Import", &handle->preset_import_collapse_states))
				{
					const float dim [2] = {0.4, 0.6};
					nk_layout_row(ctx, NK_DYNAMIC, dy, 2, dim);
					const selector_search_t old_sel = handle->preset_search_selector;
					handle->preset_search_selector = nk_combo(ctx, search_labels, SELECTOR_SEARCH_MAX,
						handle->preset_search_selector, dy, nk_vec2(nk_widget_width(ctx), 7*dy));
					if(old_sel != handle->preset_search_selector)
						preset_find_matches = true;
					const size_t old_len = strlen(handle->preset_search_buf);
					const nk_flags flags = nk_edit_string_zero_terminated(ctx,
						NK_EDIT_FIELD | NK_EDIT_SIG_ENTER,
						handle->preset_search_buf, SEARCH_BUF_MAX, nk_filter_default);
					if( (flags & NK_EDIT_COMMITED) || (old_len != strlen(handle->preset_search_buf)) )
						preset_find_matches = true;

					nk_layout_row_dynamic(ctx, dy*20, 1);
					if(nk_group_begin(ctx, "Presets_List", NK_WINDOW_BORDER))
					{
						nk_layout_row_dynamic(ctx, dy, 1);
						_expose_main_preset_list(handle, ctx, preset_find_matches);

						nk_group_end(ctx);
					}

					if(nk_tree_state_push(ctx, NK_TREE_NODE, "Info", &handle->preset_info_collapse_states))
					{
						_expose_main_preset_info(handle, ctx);

						nk_tree_state_pop(ctx);
					}

					nk_tree_state_pop(ctx);
				}

				if(nk_tree_state_push(ctx, NK_TREE_TAB, "Preset Export", &handle->preset_export_collapse_states))
				{
					//TODO

					nk_tree_state_pop(ctx);
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
_init(plughandle_t *handle)
{
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
		handle->node.lv2_AudioPort = lilv_new_uri(handle->world, LV2_CORE__AudioPort);
		handle->node.lv2_CVPort = lilv_new_uri(handle->world, LV2_CORE__CVPort);
		handle->node.lv2_ControlPort = lilv_new_uri(handle->world, LV2_CORE__ControlPort);
		handle->node.atom_AtomPort = lilv_new_uri(handle->world, LV2_ATOM__AtomPort);
		handle->node.patch_readable = lilv_new_uri(handle->world, LV2_PATCH__readable);
		handle->node.patch_writable = lilv_new_uri(handle->world, LV2_PATCH__writable);
		handle->node.rdf_type = lilv_new_uri(handle->world, LILV_NS_RDF"type");
		handle->node.lv2_Plugin = lilv_new_uri(handle->world, LV2_CORE__Plugin);
	}
}

static void
_deinit(plughandle_t *handle)
{
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
		lilv_node_free(handle->node.lv2_AudioPort);
		lilv_node_free(handle->node.lv2_CVPort);
		lilv_node_free(handle->node.lv2_ControlPort);
		lilv_node_free(handle->node.atom_AtomPort);
		lilv_node_free(handle->node.patch_readable);
		lilv_node_free(handle->node.patch_writable);
		lilv_node_free(handle->node.rdf_type);
		lilv_node_free(handle->node.lv2_Plugin);

		lilv_world_free(handle->world);
	}
}

static void
_expose(struct nk_context *ctx, struct nk_rect wbounds, void *data)
{
	plughandle_t *handle = data;

	if(nk_begin(ctx, "synthpod", wbounds, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_window_set_bounds(ctx, wbounds);

		// reduce group padding
		nk_style_push_vec2(ctx, &ctx->style.window.group_padding, nk_vec2(2.f, 2.f));

		if(handle->first)
		{
			nk_layout_row_dynamic(ctx, wbounds.h, 1);
			nk_label(ctx, "loading ...", NK_TEXT_CENTERED);

			_init(handle);

			nk_pugl_post_redisplay(&handle->win);
			handle->first = false;
		}
		else
		{
			const float w_padding = ctx->style.window.padding.y;
			const float dy = handle->dy;
			const unsigned n_paddings = 4;
			const float dh = nk_window_get_height(ctx)
				- n_paddings*w_padding - (n_paddings - 2)*dy;

			_expose_main_header(handle, ctx, dy);
			_expose_main_body(handle, ctx, dh, dy);
			_expose_main_footer(handle, ctx, dy);
		}

		nk_style_pop_vec2(ctx);
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
	cfg->font.size = 12 * scale;

	*(intptr_t *)widget = nk_pugl_init(&handle->win);
	nk_pugl_show(&handle->win);

	if(path)
		free(path);

	if(host_resize)
		host_resize->ui_resize(host_resize->handle, cfg->width, cfg->height);

	handle->plugin_collapse_states = NK_MAXIMIZED;
	handle->preset_import_collapse_states = NK_MAXIMIZED;
	handle->preset_export_collapse_states = NK_MINIMIZED;
	handle->plugin_info_collapse_states = NK_MINIMIZED;
	handle->preset_info_collapse_states = NK_MINIMIZED;

	handle->first = true;

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

	_hash_clear(&handle->plugin_matches, NULL);
	_hash_clear(&handle->preset_matches, NULL);

	_deinit(handle);

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
