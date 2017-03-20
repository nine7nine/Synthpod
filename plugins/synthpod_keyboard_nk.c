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

#define NK_PUGL_API
#include "nk_pugl/nk_pugl.h"

typedef struct _midi_atom_t midi_atom_t;
typedef struct _plughandle_t plughandle_t;

struct _midi_atom_t {
	LV2_Atom atom;
	uint8_t midi [3];
};

struct _plughandle_t {
	LV2_Atom_Forge forge;

	LV2_URID atom_eventTransfer;
	LV2_URID midi_MidiEvent;

	LV2_URID_Map *map;
	LV2UI_Write_Function writer;
	LV2UI_Controller controller;

	nk_pugl_window_t win;
	bool down;
	int8_t key;
};

static const uint8_t white_offset [7] = {
	0, 2, 4, 5, 7, 9, 11
};

static const uint8_t black_offset [5] = {
	1, 3, 6, 8, 10
};
static const struct nk_color grey_col= {
	.r = 0x33, .g = 0x33, .b = 0x33, .a = 0xff
};

static const struct nk_color black_col = {
	.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff
};

static const struct nk_color white_col = {
	.r = 0xff, .g = 0xff, .b = 0xff, .a = 0xff
};

static const struct nk_color hover_col = {
	.r = 0x7f, .g = 0x00, .b = 0x00, .a = 0xff
};

static const struct nk_color active_col = {
	.r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff
};

static void
_midi(plughandle_t *handle, uint8_t msg, uint8_t dat1, uint8_t dat2)
{
	const midi_atom_t midi_atom = {
		.atom = {
			.size = 3,
			.type = handle->midi_MidiEvent
		},
		.midi[0] = msg,
		.midi[1] = dat1,
		.midi[2] = dat2
	};

	handle->writer(handle->controller, 0, sizeof(LV2_Atom) + 3,
		handle->atom_eventTransfer, &midi_atom);
}

static void
_expose(struct nk_context *ctx, struct nk_rect wbounds, void *data)
{
	plughandle_t *handle = data;

	if(nk_begin(ctx, "Keyboard", wbounds, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_window_set_bounds(ctx, wbounds);
		const struct nk_rect reg = nk_window_get_content_region(ctx);
		const float dy = reg.h - 10;

		nk_layout_row_dynamic(ctx, dy, 1);
		struct nk_rect bounds = {0, 0, 0, 0};
		if(nk_widget(&bounds, ctx) != NK_WIDGET_INVALID)
		{
			struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
			struct nk_input *in = &ctx->input;
			const unsigned noct = 8;
			const unsigned nwhite = noct * 7;
			const float sx = 1.f; // inter-key space
			const unsigned dxw = floor( (bounds.w - nwhite*sx) / nwhite);
			const unsigned dxb = dxw*0.6f;
			const unsigned dxb2 = dxb/2;
			const float dyb = dy*0.6f;
			const float rounding = dxb2 < 4.f ? dxb2 : 4.f;
			const bool down = nk_input_is_mouse_down(in, NK_BUTTON_LEFT);

			// send note off
			if(!down && handle->down)
			{
				_midi(handle, LV2_MIDI_MSG_NOTE_OFF, handle->key, 0x7f);
			}

			for(unsigned o=0; o<noct; o++)
			{
				struct nk_rect bbounds [5];
				bool bhover [5];
				bool black_hover = false;

				// check black keys
				for(unsigned b=0; b<5; b++)
				{
					const unsigned i = o*7 + (b > 1 ? b + 2 : b + 1);

					struct nk_rect *bb = &bbounds[b];
					bb->x = bounds.x + i*(sx + dxw) - dxb2;
					bb->y = bounds.y;
					bb->w = dxb;
					bb->h = dyb;

					bhover[b] = nk_input_is_mouse_hovering_rect(in, *bb);
					if(bhover[b])
						black_hover = true;
				}

				// check/draw white keys
				for(unsigned w=0; w<7; w++)
				{
					const unsigned i = o*7 + w;

					struct nk_rect bb;
					bb.x = bounds.x + i*(sx + dxw);
					bb.y = bounds.y;
					bb.w = dxw;
					bb.h = dy;

					const bool hovering = !black_hover && nk_input_is_mouse_hovering_rect(in, bb);
					if(hovering && down)
					{
						const uint8_t k = o*12 + white_offset[w];

						if(!handle->down || (handle->key != k) )
						{
							// send note off
							if(handle->down)
								_midi(handle, LV2_MIDI_MSG_NOTE_OFF, handle->key, 0x7f);

							// send note on
							handle->key = k;
							_midi(handle, LV2_MIDI_MSG_NOTE_ON, handle->key, 0x7f);
						}

						// send note pressure
						const uint8_t touch = (in->mouse.pos.y - bb.y) / bb.h * 0x7f;
						_midi(handle, LV2_MIDI_MSG_NOTE_PRESSURE, handle->key, touch);
					}

					// draw key
					const struct nk_color col = hovering ? (down ? active_col : hover_col) : white_col;
					nk_fill_rect(canvas, bb, rounding, col);
				}

				// draw black keys
				for(unsigned b=0; b<5; b++)
				{
					struct nk_rect *bb = &bbounds[b];
					const struct nk_rect bbb = {
						.x = bb->x - sx,
						.y = bb->y - sx,
						.w = bb->w + 2*sx,
						.h = bb->h + 2*sx
					};

					const bool hovering = bhover[b];
					if(hovering && down)
					{
						const uint8_t k = o*12 + black_offset[b];

						if(!handle->down || (handle->key != k) )
						{
							// send note off
							if(handle->down)
								_midi(handle, LV2_MIDI_MSG_NOTE_OFF, handle->key, 0x7f);

							// send note on
							handle->key = k;
							_midi(handle, LV2_MIDI_MSG_NOTE_ON, handle->key, 0x7f);
						}

						// send note pressure
						const uint8_t touch = (in->mouse.pos.y - bb->y) / bb->h * 0x7f;
						_midi(handle, LV2_MIDI_MSG_NOTE_PRESSURE, handle->key, touch);
					}

					// draw key
					const struct nk_color col = hovering ? (down ? active_col : hover_col) : black_col;
					nk_fill_rect(canvas, bbb, rounding, grey_col);
					nk_fill_rect(canvas, *bb, rounding, col);
				}
			}

			bounds.y -= 1;
			bounds.h = rounding;
			nk_fill_rect(canvas, bounds, 0.f, grey_col);

			handle->down = down;
		}
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

	handle->midi_MidiEvent = handle->map->map(handle->map->handle, LV2_MIDI__MidiEvent);
	handle->atom_eventTransfer = handle->map->map(handle->map->handle, LV2_ATOM__eventTransfer);

	handle->controller = controller;
	handle->writer = write_function;

	nk_pugl_config_t *cfg = &handle->win.cfg;
	cfg->width = 1304;
	cfg->height = 128;
	cfg->resizable = true;
	cfg->fixed_aspect = true;
	cfg->ignore = false;
	cfg->class = "keyboard";
	cfg->title = "Keyboard";
	cfg->parent = (intptr_t)parent;
	cfg->data = handle;
	cfg->expose = _expose;
	cfg->font.face = NULL;
	cfg->font.size = 16;
	
	*(intptr_t *)widget = nk_pugl_init(&handle->win);
	nk_pugl_show(&handle->win);

	if(host_resize)
		host_resize->ui_resize(host_resize->handle, cfg->width, cfg->height);

	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle->win.cfg.font.face)
		free(handle->win.cfg.font.face);
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

const LV2UI_Descriptor synthpod_keyboard_4_nk = {
	.URI						= SYNTHPOD_KEYBOARD_NK_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= extension_data
};
