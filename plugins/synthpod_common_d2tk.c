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

#include <fnmatch.h>

#include "lv2/lv2plug.in/ns/ext/log/log.h"
#include "lv2/lv2plug.in/ns/ext/log/logger.h"

#include <lilv/lilv.h>

#include <synthpod_lv2.h>
#include <synthpod_common.h>

#include <d2tk/frontend_pugl.h>

typedef enum _view_type_t {
	VIEW_TYPE_PLUGIN_LIST = 0,
	VIEW_TYPE_PRESET_LIST,
	VIEW_TYPE_PATCH_BAY,

	VIEW_TYPE_MAX
} view_type_t;

typedef struct _dyn_label_t dyn_label_t;
typedef struct _stat_label_t stat_label_t;
typedef struct _entry_t entry_t;
typedef struct _view_t view_t;
typedef struct _plughandle_t plughandle_t;

struct _dyn_label_t {
	ssize_t len;
	const char *buf;
};

struct _stat_label_t {
	ssize_t len;
	char buf[256];
};

struct _entry_t {
	const void *data;
	dyn_label_t name;
};

struct _view_t {
	view_type_t type;
	bool selector [VIEW_TYPE_MAX];
};

struct _plughandle_t {
	LilvWorld *world;

	const LilvPlugins *plugs;
	LilvIter *iplugs;
	unsigned nplugs;
	entry_t *lplugs;
	char pplugs[32];

	LV2_URID_Map *map;
	LV2_Atom_Forge forge;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	d2tk_pugl_config_t config;
	d2tk_pugl_t *dpugl;

	LV2UI_Controller *controller;
	LV2UI_Write_Function writer;

	stat_label_t message;

	d2tk_style_t button_style [2];

	unsigned nviews;
	view_t views [32];
};

static inline void
_status_message_set(plughandle_t *handle, const char *message)
{
	handle->message.len = snprintf(handle->message.buf, sizeof(handle->message.buf),
		"%s ...", message);

	if(handle->message.len < 0)
	{
		handle->message.len = 0;
	}
}

static inline void
_status_message_clear(plughandle_t *handle)
{
	handle->message.len = 0;
}

static inline bool
_initializing(plughandle_t *handle)
{
	return handle->world ? false : true;
}

static inline bool
_lazy_loading(plughandle_t *handle)
{
	return handle->iplugs ? true : false;
}

static int
_plug_cmp_name(const void *a, const void *b)
{
	const entry_t *entry_a = (const entry_t *)a;
	const entry_t *entry_b = (const entry_t *)b;

	return strcasecmp(entry_a->name.buf, entry_b->name.buf);
}

static int
_plug_populate(plughandle_t *handle, const char *pattern)
{
	if(_lazy_loading(handle)) // initial lazy loading
	{
		for(unsigned i = 0;
				(i < 600/25/4) && !lilv_plugins_is_end(handle->plugs, handle->iplugs);
				i++, handle->iplugs = lilv_plugins_next(handle->plugs, handle->iplugs) )
		{
				const LilvPlugin *plug = lilv_plugins_get(handle->plugs, handle->iplugs);
				LilvNode *node = lilv_plugin_get_name(plug);
				const char *name = lilv_node_as_string(node);

				entry_t *entry = &handle->lplugs[handle->nplugs++];
				entry->data = plug;
				entry->name.buf = name;
				entry->name.len= strlen(name);
		}

		if(lilv_plugins_is_end(handle->plugs, handle->iplugs))
		{
			handle->iplugs = NULL; // initial lazy loading is done
			_status_message_clear(handle);
		}
		else
		{
			d2tk_pugl_redisplay(handle->dpugl); // schedule redisplay until done
		}
	}
	else // normal operation
	{
		pattern = pattern ? pattern : "**";
		handle->nplugs = 0;

		LILV_FOREACH(plugins, iplugs, handle->plugs)
		{
			const LilvPlugin *plug = lilv_plugins_get(handle->plugs, iplugs);
			LilvNode *node = lilv_plugin_get_name(plug);
			const char *name = lilv_node_as_string(node);

			if(fnmatch(pattern, name, FNM_CASEFOLD | FNM_EXTMATCH) == 0)
			{
				entry_t *entry = &handle->lplugs[handle->nplugs++];
				entry->data = plug;
				entry->name.buf = name;
				entry->name.len = strlen(name);
			}
		}
	}

	qsort(handle->lplugs, handle->nplugs, sizeof(entry_t), _plug_cmp_name);

	return 0;
}

static int
_expose_view(plughandle_t *handle, unsigned iview, const d2tk_rect_t *rect)
{
	d2tk_pugl_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_pugl_get_base(dpugl);
	view_t *view = &handle->views[iview];

	static const d2tk_coord_t vfrac [3] = { 24, 0, 16 };
	D2TK_BASE_LAYOUT(rect, 3, vfrac, D2TK_FLAG_LAYOUT_Y_ABS, vlay)
	{
		const d2tk_rect_t *vrect = d2tk_layout_get_rect(vlay);
		const uint32_t vy = d2tk_layout_get_index(vlay);

		switch(vy)
		{
			case 0:
			{
				if(_initializing(handle) || _lazy_loading(handle)) // still loading ?
				{
					break;
				}

				if(d2tk_base_text_field_is_changed(base, D2TK_ID_IDX(iview), vrect,
					sizeof(handle->pplugs), handle->pplugs,
					D2TK_ALIGN_MIDDLE | D2TK_ALIGN_LEFT, NULL))
				{
					_plug_populate(handle, handle->pplugs);
				}
			} break;
			case 1:
			{
				switch(view->type)
				{
					case VIEW_TYPE_PLUGIN_LIST:
					{
						const unsigned dn = 25;

						if(_lazy_loading(handle))
						{
							_plug_populate(handle, handle->pplugs);
						}

						D2TK_BASE_SCROLLBAR(base, vrect, D2TK_ID_IDX(iview), D2TK_FLAG_SCROLL_Y,
							0, handle->nplugs, 0, dn, vscroll)
						{
							const float voffset = d2tk_scrollbar_get_offset_y(vscroll);
							const d2tk_rect_t *col = d2tk_scrollbar_get_rect(vscroll);

							D2TK_BASE_TABLE(col, 1, dn, trow)
							{
								const unsigned k = d2tk_table_get_index_y(trow) + voffset;

								if(k >= handle->nplugs)
								{
									break;
								}

								const d2tk_rect_t *row = d2tk_table_get_rect(trow);
								const d2tk_id_t id = D2TK_ID_IDX(iview*dn + k);
								entry_t *entry = &handle->lplugs[k];

								d2tk_base_set_style(base, &handle->button_style[k % 2]);

								if(d2tk_base_button_label_is_changed(base, id,
									entry->name.len, entry->name.buf,
									D2TK_ALIGN_MIDDLE | D2TK_ALIGN_LEFT, row))
								{
									//FIXME
								}
							}

							d2tk_base_set_style(base, NULL);
						}
					} break;
					case VIEW_TYPE_PRESET_LIST:
					{
						//FIXME
					} break;
					case VIEW_TYPE_PATCH_BAY:
					{
						//FIXME
					} break;

					case VIEW_TYPE_MAX:
					{
						// never reached
					} break;
				}
			} break;
			case 2:
			{
				static const d2tk_coord_t hfrac [VIEW_TYPE_MAX + 1] = {
					16, 16, 16,
					0
				};

				D2TK_BASE_LAYOUT(vrect, VIEW_TYPE_MAX + 1, hfrac, D2TK_FLAG_LAYOUT_X_ABS, hlay)
				{
					const d2tk_rect_t *hrect = d2tk_layout_get_rect(hlay);
					const uint32_t vx = d2tk_layout_get_index(hlay);
					const d2tk_id_t id = D2TK_ID_IDX(iview*VIEW_TYPE_MAX + vx);

					switch(vx)
					{
						case VIEW_TYPE_PLUGIN_LIST:
							// fall-through
						case VIEW_TYPE_PRESET_LIST:
							// fall-through
						case VIEW_TYPE_PATCH_BAY:
						{
							if(d2tk_base_toggle_is_changed(base, id, hrect, &view->selector[vx]))
							{
								//FIXME
							}
						} break;
						case VIEW_TYPE_MAX:
						{
							// never reached
						} break;
					}
				}
			} break;
		}
	}

	return 0;
}

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
				d2tk_base_label(base, -1, "Menu", 1.f, vrect,
					D2TK_ALIGN_MIDDLE | D2TK_ALIGN_LEFT);
			} break;
			case 1:
			{
				D2TK_BASE_PANE(base, vrect, D2TK_ID, D2TK_FLAG_PANE_X,
					0.1f, 0.9f, 0.1f, hpane)
				{
					const d2tk_rect_t *prect =  d2tk_pane_get_rect(hpane);
					const uint32_t px = d2tk_pane_get_index(hpane);

					_expose_view(handle, px, prect);
				}
			} break;
			case 2:
			{
				d2tk_base_label(base, -1, "Synthpod "SYNTHPOD_VERSION, 1.f, vrect,
					D2TK_ALIGN_MIDDLE| D2TK_ALIGN_RIGHT);

				if(handle->message.len)
				{
					d2tk_base_label(base, handle->message.len, handle->message.buf, 1.f, vrect,
						D2TK_ALIGN_MIDDLE | D2TK_ALIGN_LEFT);
				}
			} break;
		}
	}

	return 0;
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

	const d2tk_coord_t w = 1280;
	const d2tk_coord_t h = 720;

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

	strncpy(handle->pplugs, "*", sizeof(handle->pplugs));

	_status_message_set(handle, "Scanning for plugins");

	handle->button_style[0] = *d2tk_base_get_default_style();
	handle->button_style[0].fill_color[D2TK_TRIPLE_NONE] =
	handle->button_style[0].fill_color[D2TK_TRIPLE_FOCUS] = 0x4f4f4fff;

	handle->button_style[1] = *d2tk_base_get_default_style();
	handle->button_style[1].fill_color[D2TK_TRIPLE_NONE] =
	handle->button_style[1].fill_color[D2TK_TRIPLE_FOCUS] = 0x3f3f3fff;

	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	d2tk_pugl_free(handle->dpugl);

	free(handle->lplugs);

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

static void
_init(plughandle_t *handle)
{
	handle->world = lilv_world_new();

	LilvNode *node_false = lilv_new_bool(handle->world, false);
	if(node_false)
	{
		lilv_world_set_option(handle->world, LILV_OPTION_DYN_MANIFEST, node_false);
		lilv_node_free(node_false);
	}
	lilv_world_load_all(handle->world);

	handle->plugs = lilv_world_get_all_plugins(handle->world);
	handle->iplugs = lilv_plugins_begin(handle->plugs);
	const unsigned nplugs = lilv_plugins_size(handle->plugs);
	handle->lplugs = calloc(1, nplugs * sizeof(entry_t));
}

static int
_idle(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	const int res = d2tk_pugl_step(handle->dpugl);

	if(_initializing(handle))
	{
		_init(handle);
	}

	return res;
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
