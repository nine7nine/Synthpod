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

#include <math.h>

#include "nk_pugl/nk_pugl.h"

typedef struct _plughandle_t plughandle_t;

enum {
	SELECTOR_MAIN_GRID = 0,
	SELECTOR_MAIN_MATRIX,

	SELECTOR_MAIN_MAX
};

enum {
	SELECTOR_GRID_PLUGINS = 0,
	SELECTOR_GRID_PRESETS,

	SELECTOR_GRID_MAX
};

enum {
	SELECTOR_SEARCH_NAME = 0,
	SELECTOR_SEARCH_AUTHOR,
	SELECTOR_SEARCH_CLASS,
	SELECTOR_SEARCH_BUNDLE,
	SELECTOR_SEARCH_LICENSE,

	SELECTOR_SEARCH_MAX
};

struct _plughandle_t {
	LV2_Atom_Forge forge;

	LV2_URID atom_eventTransfer;

	LV2_URID_Map *map;
	LV2UI_Write_Function writer;
	LV2UI_Controller controller;

	nk_pugl_window_t win;

	unsigned main_selector;
	unsigned grid_selector;
	int search_selector;

	char search_buf [32];
	int search_len;
};

static const char *main_labels [SELECTOR_MAIN_MAX] = {
	[SELECTOR_MAIN_GRID] = "Grid (C-g)",
	[SELECTOR_MAIN_MATRIX] = "Matrix (C-m)"
};

static const char *grid_labels [SELECTOR_GRID_MAX] = {
	[SELECTOR_GRID_PLUGINS] = "Plugins (C-p)",
	[SELECTOR_GRID_PRESETS] = "Presets (C-r)"
};

static const char *search_labels [SELECTOR_SEARCH_MAX] = {
	[SELECTOR_SEARCH_NAME] = "Name",
	[SELECTOR_SEARCH_AUTHOR] = "Author",
	[SELECTOR_SEARCH_CLASS] = "Class",
	[SELECTOR_SEARCH_BUNDLE] = "Bundle",
	[SELECTOR_SEARCH_LICENSE] = "License"
};

static void
_expose_main_header(plughandle_t *handle, struct nk_context *ctx, float dy)
{
	nk_layout_row_dynamic(ctx, dy, 7);
	{
		nk_button_label(ctx, "New (C-n)");
		nk_button_label(ctx, "Open (C-o)");
		nk_button_label(ctx, "Save (C-s)");
		nk_button_label(ctx, "Save As (C-S)");
		nk_button_label(ctx, "Quit (C-q)");

		for(unsigned i=0; i<SELECTOR_MAIN_MAX; i++)
		{
			const enum nk_symbol_type symbol = (handle->main_selector == i)
				? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_CIRCLE_OUTLINE;
			if(nk_button_symbol_label(ctx, symbol, main_labels[i], NK_TEXT_RIGHT))
				handle->main_selector = i;
		}
	}
}

static void
_expose_main_body(plughandle_t *handle, struct nk_context *ctx, float dy)
{
	switch(handle->main_selector)
	{
		case SELECTOR_MAIN_GRID:
		{
			nk_layout_row_begin(ctx, NK_DYNAMIC, dy, 3);

			nk_layout_row_push(ctx, 0.25);
			if(nk_group_begin(ctx, "Rack", NK_WINDOW_BORDER | NK_WINDOW_TITLE))
			{
				for(unsigned i=0; i<4; i++)
				{
					nk_layout_row_dynamic(ctx, 60.f, 1);
					if(nk_group_begin(ctx, "Moony A1 x A1",
						NK_WINDOW_TITLE | NK_WINDOW_CLOSABLE | NK_WINDOW_NO_SCROLLBAR))
					{
						nk_layout_row_dynamic(ctx, 20.f, 6);
						nk_button_label(ctx, "Enable");
						nk_button_label(ctx, "Selected");
						nk_button_label(ctx, "Controls");
						nk_button_label(ctx, "GUI");
						nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_UP);
						nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_DOWN);
						//TODO

						nk_group_end(ctx);
					}
				}

				nk_group_end(ctx);
			}

			nk_layout_row_push(ctx, 0.50);
			if(nk_group_begin(ctx, "Controls", NK_WINDOW_BORDER | NK_WINDOW_TITLE))
			{
				//TODO

				nk_group_end(ctx);
			}

			nk_layout_row_push(ctx, 0.25);
			if(nk_group_begin(ctx, "Plugins/Presets", NK_WINDOW_BORDER))
			{
				nk_layout_row_dynamic(ctx, 20.f, SELECTOR_MAIN_MAX);
				for(unsigned i=0; i<SELECTOR_MAIN_MAX; i++)
				{
					const enum nk_symbol_type symbol = (handle->grid_selector == i)
						? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_CIRCLE_OUTLINE;
					if(nk_button_symbol_label(ctx, symbol, grid_labels[i], NK_TEXT_RIGHT))
						handle->grid_selector = i;
				}

				nk_layout_row_dynamic(ctx, 20.f, 2);
				nk_combobox(ctx, search_labels, SELECTOR_SEARCH_MAX, &handle->search_selector, 20.f, nk_vec2(120.f, 140.f));
				nk_edit_string(ctx, NK_EDIT_FIELD, handle->search_buf, &handle->search_len, 32, nk_filter_default);

				switch(handle->grid_selector)
				{
					case SELECTOR_GRID_PLUGINS:
					{
						nk_layout_row_dynamic(ctx, 20.f, 1);
						for(unsigned j=0; j<20; j++)
							nk_labelf(ctx, NK_TEXT_LEFT, "Plugin #%u", j);
						//TODO
					} break;
					case SELECTOR_GRID_PRESETS:
					{
						nk_layout_row_dynamic(ctx, 20.f, 1);
						for(unsigned j=0; j<20; j++)
							nk_labelf(ctx, NK_TEXT_LEFT, "Preset #%u", j);
						//TODO
					} break;
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
		const float dy_line = 20.f;
		const unsigned n_paddings = 4;
		const float dy_body= nk_window_get_height(ctx)
			- n_paddings*w_padding - (n_paddings - 2)*dy_line;

		_expose_main_header(handle, ctx, dy_line);
		_expose_main_body(handle, ctx, dy_body);
		_expose_main_footer(handle, ctx, dy_line);
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

	lv2_atom_forge_init(&handle->forge, handle->map);

	handle->atom_eventTransfer = handle->map->map(handle->map->handle, LV2_ATOM__eventTransfer);

	handle->controller = controller;
	handle->writer = write_function;

	nk_pugl_config_t *cfg = &handle->win.cfg;
	cfg->width = 1280;
	cfg->height = 720;
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
	cfg->font.size = 13;
	
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
