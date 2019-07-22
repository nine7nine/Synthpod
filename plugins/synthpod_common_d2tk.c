/*
 * Copyright (c) 2015-2019 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#include "lv2/lv2plug.in/ns/ext/log/log.h"
#include "lv2/lv2plug.in/ns/ext/log/logger.h"

#include <lilv/lilv.h>

#include <synthpod_lv2.h>
#include <synthpod_common.h>

#include <d2tk/frontend_pugl.h>

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	LilvWorld *world;
	const LilvPlugins *plugs;
	unsigned nplugs;
	const LilvPlugin **hplugs;

	LV2_URID_Map *map;
	LV2_Atom_Forge forge;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	d2tk_pugl_config_t config;
	d2tk_pugl_t *dpugl;

	LV2UI_Controller *controller;
	LV2UI_Write_Function writer;
};

static int
_expose(void *data, d2tk_coord_t w, d2tk_coord_t h)
{
	plughandle_t *handle = data;

	d2tk_pugl_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_pugl_get_base(dpugl);
	const d2tk_rect_t rect = D2TK_RECT(0, 0, w, h);

	static const d2tk_coord_t vfrac [3] = { 24, 0, 16 };
	D2TK_BASE_LAYOUT(&rect, 3, vfrac, D2TK_FLAG_LAYOUT_Y_ABS, vlay)
	{
		const d2tk_rect_t *vrect = d2tk_layout_get_rect(vlay);
		const uint32_t vy = d2tk_layout_get_index(vlay);

		switch(vy)
		{
			case 0:
			{
				static const d2tk_coord_t hfrac [17] = {
					24, 24, 24, 24,
					24, 24, 24, 24,
					24, 24, 24, 24,
					24, 24, 24, 24,
					0
				};

				D2TK_BASE_LAYOUT(vrect, 17, hfrac, D2TK_FLAG_LAYOUT_X_ABS, hlay)
				{
					const d2tk_rect_t *hrect = d2tk_layout_get_rect(hlay);
					const uint32_t vx = d2tk_layout_get_index(hlay);
					const d2tk_id_t id = D2TK_ID_IDX(vx);

					switch(vx)
					{
						case 0:
						case 1:
						case 2:
						case 3:
						case 4:
						case 5:
						case 6:
						case 7:
						case 8:
						case 9:
						case 10:
						case 11:
						case 12:
						case 13:
						case 14:
						case 15:
						{
							if(d2tk_base_button_is_changed(base, id, hrect))
							{
								//FIXME
							}
						} break;
					}
				}
			} break;
			case 1:
			{
				D2TK_BASE_PANE(base, vrect, D2TK_ID, D2TK_FLAG_PANE_X,
					0.05f, 0.95f, 0.05f, hpane)
				{
					const d2tk_rect_t *prect =  d2tk_pane_get_rect(hpane);
					const uint32_t px = d2tk_pane_get_index(hpane);

					switch(px)
					{
						case 0:
						{
							const unsigned n = lilv_plugins_size(handle->plugs);
							const unsigned dn = 40;

							D2TK_BASE_SCROLLBAR(base, prect, D2TK_ID, D2TK_FLAG_SCROLL_Y,
								0, n, 0, dn, vscroll)
							{
								const float voffset = d2tk_scrollbar_get_offset_y(vscroll);
								const d2tk_rect_t *col = d2tk_scrollbar_get_rect(vscroll);

								D2TK_BASE_TABLE(col, 1, dn, trow)
								{
									const d2tk_rect_t *row = d2tk_table_get_rect(trow);
									const unsigned k = d2tk_table_get_index_y(trow) + voffset;
									const d2tk_id_t id = D2TK_ID_IDX(k);

									const LilvPlugin *plug = handle->hplugs[k];
									LilvNode *plug_name = lilv_plugin_get_name(plug);
									const char *plug_name_str = lilv_node_as_string(plug_name);

									if(d2tk_base_button_label_is_changed(base, id, -1, plug_name_str, row))
									{
										//FIXME
									}
								}
							}
						} break;
						case 1:
						{
							//FIXME
						} break;
					}
				}
			} break;
			case 2:
			{
				d2tk_base_label(base, -1, "Synthpod "SYNTHPOD_VERSION, 1.f, vrect,
					D2TK_ALIGN_MIDDLE| D2TK_ALIGN_RIGHT);
			} break;
		}
	}

	return 0;
}

static int
_plug_cmp_name(const void *a, const void *b)
{
	const LilvPlugin **plug_a = (const LilvPlugin **)a;
	const LilvPlugin **plug_b = (const LilvPlugin **)b;

	LilvNode *name_a = lilv_plugin_get_name(*plug_a);
	LilvNode *name_b = lilv_plugin_get_name(*plug_b);

	const int res = strcasecmp(lilv_node_as_string(name_a), lilv_node_as_string(name_b));

	lilv_node_free(name_a);
	lilv_node_free(name_b);

	return res;
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
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
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

	if(handle->log)
		lv2_log_logger_init(&handle->logger, handle->map, handle->log);

	lv2_atom_forge_init(&handle->forge, handle->map);

	handle->controller = controller;
	handle->writer = write_function;

	const d2tk_coord_t w = 800;
	const d2tk_coord_t h = 450;

	d2tk_pugl_config_t *config = &handle->config;
	config->parent = (uintptr_t)parent;
	config->bundle_path = bundle_path;
	config->min_w = w/2;
	config->min_h = h/2;
	config->w = w;
	config->h = h;
	config->fixed_size = false;
	config->fixed_aspect = false;
	config->expose = _expose;
	config->data = handle;

	handle->dpugl = d2tk_pugl_new(config, (uintptr_t *)widget);
	if(!handle->dpugl)
	{
		free(handle);
		return NULL;
	}

	if(host_resize)
	{
		host_resize->ui_resize(host_resize->handle, w, h);
	}

	handle->world = lilv_world_new();

	LilvNode *node_false = lilv_new_bool(handle->world, false);
	if(node_false)
	{
		lilv_world_set_option(handle->world, LILV_OPTION_DYN_MANIFEST, node_false);
		lilv_node_free(node_false);
	}
	lilv_world_load_all(handle->world);

	handle->plugs = lilv_world_get_all_plugins(handle->world);
	handle->nplugs = lilv_plugins_size(handle->plugs);
	handle->hplugs = calloc(1, handle->nplugs * sizeof(LilvPlugin *));
	const LilvPlugin **hplug = handle->hplugs;

	LILV_FOREACH(plugins, i, handle->plugs)
	{
		const LilvPlugin *plug = lilv_plugins_get(handle->plugs, i);

		*hplug++ = plug;
	}

	qsort(handle->hplugs, handle->nplugs, sizeof(LilvPlugin *), _plug_cmp_name);

	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	d2tk_pugl_free(handle->dpugl);

	free(handle->hplugs);

	lilv_world_free(handle->world);

	free(handle);
}

static void
port_event(LV2UI_Handle instance, uint32_t port_index, uint32_t size,
	uint32_t format, const void *buffer)
{
	plughandle_t *handle = instance;

	d2tk_pugl_redisplay(handle->dpugl);
}

static int
_idle(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	return d2tk_pugl_step(handle->dpugl);
}

static const LV2UI_Idle_Interface idle_ext = {
	.idle = _idle
};

static int
_resize(LV2UI_Handle instance, int width, int height)
{
	plughandle_t *handle = instance;

	return d2tk_pugl_resize(handle->dpugl, width, height);
}

static const LV2UI_Resize resize_ext = {
	.ui_resize = _resize
};

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_UI__idleInterface))
		return &idle_ext;
	else if(!strcmp(uri, LV2_UI__resize))
		return &resize_ext;

	return NULL;
}

const LV2UI_Descriptor synthpod_common_5_d2tk = {
	.URI						= SYNTHPOD_COMMON_D2TK_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= extension_data
};

const LV2UI_Descriptor synthpod_root_5_d2tk = {
	.URI						= SYNTHPOD_ROOT_D2TK_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= extension_data
};
