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
#include <inttypes.h>

#include "lv2/lv2plug.in/ns/ext/log/log.h"
#include "lv2/lv2plug.in/ns/ext/log/logger.h"
#include "lv2/lv2plug.in/ns/ext/options/options.h"

#include <lilv/lilv.h>

#include <synthpod_lv2.h>
#include <synthpod_common.h>
#include <synthpod_private.h>
#include <synthpod_patcher.h>

#include <d2tk/frontend_pugl.h>

#define ATOM_BUF_MAX 0x100000 // 1M
#define CONTROL 14 //FIXME
#define NOTIFY 15 //FIXME
#define MAX_MODS 0x200
#define MASK_MODS (MAX_MODS - 1)

#define ATOM_BOOL_VAL(ATOM) ((const LV2_Atom_Bool *)(ATOM))->body
#define ATOM_INT_VAL(ATOM) ((const LV2_Atom_Int *)(ATOM))->body
#define ATOM_LONG_VAL(ATOM) ((const LV2_Atom_Long *)(ATOM))->body
#define ATOM_FLOAT_VAL(ATOM) ((const LV2_Atom_Float *)(ATOM))->body
#define ATOM_DOUBLE_VAL(ATOM) ((const LV2_Atom_Double *)(ATOM))->body
#define ATOM_STRING_VAL(ATOM) (const char *)LV2_ATOM_BODY_CONST((ATOM))

#define DBG_NOW fprintf(stderr, ":: %s\n", __func__)
#if 0
#	define DBG DBG_NOW
#else
#	define DBG
#endif

typedef struct _stat_label_t stat_label_t;
typedef struct _entry_t entry_t;
typedef struct _status_t status_t;
typedef struct _prof_t prof_t;
typedef struct _mod_t mod_t;
typedef struct _plughandle_t plughandle_t;

struct _stat_label_t {
	ssize_t len;
	char buf[64];
};

struct _entry_t {
	const void *data;
	stat_label_t name;
};

struct _status_t {
	int32_t cpus_available;
	int32_t cpus_used;
	int32_t period_size;
	int32_t num_periods;
	float sample_rate;
	float update_rate;

	stat_label_t label [3];
};

struct _prof_t {
	float min;
	float avg;
	float max;
};

struct _mod_t {
	LV2_URID urn;
	LV2_URID subj;
	const LilvPlugin *plug;

	stat_label_t name;
	stat_label_t alias;

	bool selected;
	d2tk_pos_t pos;

	struct {
		unsigned nin;
		unsigned nout;
	} audio;
	struct {
		unsigned nin;
		unsigned nout;
	} cv;
	struct {
		unsigned nin;
		unsigned nout;
	} control;
	struct {
		unsigned nin;
		unsigned nout;
	} atom;
};

struct _plughandle_t {
	LilvWorld *world;
	reg_t regs;
	union {
		LV2_Atom atom;
		uint8_t buf [ATOM_BUF_MAX];
	};

	const LilvPlugins *plugs;
	LilvIter *iplugs;
	unsigned nplugs;
	entry_t *lplugs;
	char pplugs[32];
	const LilvPlugin *plug_info;

	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	LV2_Atom_Forge forge;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	d2tk_pugl_config_t config;
	d2tk_frontend_t *dpugl;

	LV2UI_Controller *controller;
	LV2UI_Write_Function writer;

	stat_label_t message;

	d2tk_style_t button_style [2];

	status_t status;
	prof_t prof;

	d2tk_pos_t nxt_pos;
	mod_t hmods [MAX_MODS];

	unsigned nmods;
	mod_t *mods [MAX_MODS];
};

static inline void
_status_labels_update(plughandle_t *handle)
{
	DBG;
	{
		stat_label_t *label = &handle->status.label[0];

		const float khz = handle->status.sample_rate * 1e-3;
		const float ms = handle->status.period_size
			* handle->status.num_periods / khz;

		label->len = snprintf(label->buf, sizeof(label->buf),
			"DEV: %"PRIi32" x %"PRIi32" @ %.1f kHz (%.2f ms)",
			handle->status.period_size, handle->status.num_periods, khz, ms);
	}

	{
		stat_label_t *label = &handle->status.label[1];

		label->len = snprintf(label->buf, sizeof(label->buf),
			"DSP: %04.1f | %04.1f | %04.1f %%",
			handle->prof.min, handle->prof.avg, handle->prof.max);
	}

	{
		stat_label_t *label = &handle->status.label[2];

		label->len = snprintf(label->buf, sizeof(label->buf),
			"CPU: %"PRIi32" / %"PRIi32,
			handle->status.cpus_used, handle->status.cpus_available);
	}
}

static inline bool
_message_request(plughandle_t *handle)
{
	DBG;
	lv2_atom_forge_set_buffer(&handle->forge, handle->buf, ATOM_BUF_MAX);
	return true;
}

static inline void
_message_write(plughandle_t *handle)
{
	DBG;
	handle->writer(handle->controller, CONTROL, lv2_atom_total_size(&handle->atom),
		handle->regs.port.event_transfer.urid, &handle->atom);
}

static inline void
_status_message_set(plughandle_t *handle, const char *message)
{
	DBG;
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
	DBG;
	handle->message.len = 0;
}

static inline bool
_initializing(plughandle_t *handle)
{
	DBG;
	return handle->world ? false : true;
}

static inline bool
_lazy_loading(plughandle_t *handle)
{
	DBG;
	return handle->iplugs ? true : false;
}

static inline int
_plug_cmp_name(const void *a, const void *b)
{
	DBG;
	const entry_t *entry_a = (const entry_t *)a;
	const entry_t *entry_b = (const entry_t *)b;

	return strcasecmp(entry_a->name.buf, entry_b->name.buf);
}

static inline void
_plug_populate(plughandle_t *handle, const char *pattern)
{
	DBG;
	if(_lazy_loading(handle)) // initial lazy loading
	{
		for(unsigned i = 0;
				(i < 600/25/4) && !lilv_plugins_is_end(handle->plugs, handle->iplugs);
				i++, handle->iplugs = lilv_plugins_next(handle->plugs, handle->iplugs) )
		{
				const LilvPlugin *plug = lilv_plugins_get(handle->plugs, handle->iplugs);
				LilvNode *name_node = lilv_plugin_get_name(plug);
				if(!name_node)
				{
					continue;
				}

				const char *name = lilv_node_as_string(name_node);

				entry_t *entry = &handle->lplugs[handle->nplugs++];
				entry->data = plug;
				entry->name.len = snprintf(entry->name.buf, sizeof(entry->name.buf),
					"%s", name);

				lilv_node_free(name_node);
		}

		if(lilv_plugins_is_end(handle->plugs, handle->iplugs))
		{
			handle->iplugs = NULL; // initial lazy loading is done
			_status_message_clear(handle);
		}
		else
		{
			d2tk_frontend_redisplay(handle->dpugl); // schedule redisplay until done
		}
	}
	else // normal operation
	{
		pattern = pattern ? pattern : "**";
		handle->nplugs = 0;

		LILV_FOREACH(plugins, iplugs, handle->plugs)
		{
			const LilvPlugin *plug = lilv_plugins_get(handle->plugs, iplugs);
			LilvNode *name_node = lilv_plugin_get_name(plug);
			if(!name_node)
			{
				continue;
			}

			const char *name = lilv_node_as_string(name_node);

			if(fnmatch(pattern, name, FNM_CASEFOLD | FNM_EXTMATCH) == 0)
			{
				entry_t *entry = &handle->lplugs[handle->nplugs++];
				entry->data = plug;
				entry->name.len = snprintf(entry->name.buf, sizeof(entry->name.buf),
					"%s", name);
			}

			lilv_node_free(name_node);
		}
	}

	qsort(handle->lplugs, handle->nplugs, sizeof(entry_t), _plug_cmp_name);
}

static inline void
_expose_plugin_list_header(plughandle_t *handle, const d2tk_rect_t *rect)
{
	DBG;
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	if(_initializing(handle) || _lazy_loading(handle)) // still loading ?
	{
		return;
	}

	if(d2tk_base_text_field_is_changed(base, D2TK_ID, rect,
		sizeof(handle->pplugs), handle->pplugs,
		D2TK_ALIGN_MIDDLE | D2TK_ALIGN_LEFT, NULL))
	{
		_plug_populate(handle, handle->pplugs);
	}
}

static inline void
_expose_plugin_list_body(plughandle_t *handle, const d2tk_rect_t *rect)
{
	DBG;
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	const unsigned dd = 24;
	const unsigned dn = rect->h / dd;

	handle->plug_info = NULL;

	const uint32_t max [2] = { 0, handle->nplugs };
	const uint32_t num [2] = { 0, dn };
	D2TK_BASE_SCROLLBAR(base, rect, D2TK_ID, D2TK_FLAG_SCROLL_Y, max, num, vscroll)
	{
		const float voffset = d2tk_scrollbar_get_offset_y(vscroll);
		const d2tk_rect_t *col = d2tk_scrollbar_get_rect(vscroll);

		D2TK_BASE_TABLE(col, col->w, dd, D2TK_FLAG_TABLE_ABS, trow)
		{
			const unsigned k = d2tk_table_get_index_y(trow) + voffset;

			if(k >= handle->nplugs)
			{
				break;
			}

			const d2tk_rect_t *row = d2tk_table_get_rect(trow);
			const d2tk_id_t id = D2TK_ID_IDX(k);
			entry_t *entry = &handle->lplugs[k];

			d2tk_base_set_style(base, &handle->button_style[k % 2]);

			const d2tk_state_t state = d2tk_base_button_label(base, id,
				entry->name.len, entry->name.buf,
				D2TK_ALIGN_MIDDLE | D2TK_ALIGN_LEFT, row);
			if(d2tk_state_is_focused(state))
			{
				handle->plug_info = entry->data;
			}
		}

		d2tk_base_set_style(base, NULL);
	}
}

static inline void
_expose_plugin_list(plughandle_t *handle, const d2tk_rect_t *rect)
{
	DBG;
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	static const d2tk_coord_t vfrac [3] = { 24, 0 };
	D2TK_BASE_LAYOUT(rect, 2, vfrac, D2TK_FLAG_LAYOUT_Y_ABS, vlay)
	{
		const d2tk_rect_t *vrect = d2tk_layout_get_rect(vlay);
		const uint32_t vy = d2tk_layout_get_index(vlay);

		switch(vy)
		{
			case 0:
			{
				_expose_plugin_list_header(handle, vrect);
			} break;
			case 1:
			{
				_expose_plugin_list_body(handle, vrect);
			} break;
		}
	}
}

static inline void
_expose_sidebar_top(plughandle_t *handle, const d2tk_rect_t *rect)
{
	DBG;

	_expose_plugin_list(handle, rect);
	//FIXME
}

static inline void
_xdg_open(const LilvNode *node)
{
	char *buf = NULL;

	if(asprintf(&buf, "xdg-open %s", lilv_node_as_uri(node)) == -1)
	{
		return;
	}

	system(buf);

	free(buf);
}

static inline void
_expose_plugin_property(d2tk_base_t *base, unsigned idx, const char *key,
	const LilvNode *node, const d2tk_rect_t *rect)
{
	const d2tk_coord_t hfrac [2] = { 1, 5 };
	D2TK_BASE_LAYOUT(rect, 2, hfrac, D2TK_FLAG_LAYOUT_X_REL, hlay)
	{
		const d2tk_rect_t *hrect = d2tk_layout_get_rect(hlay);
		const unsigned k = d2tk_layout_get_index(hlay);

		switch(k)
		{
			case 0:
			{
				d2tk_base_label(base, -1, key, 1.f, hrect,
					D2TK_ALIGN_MIDDLE | D2TK_ALIGN_LEFT);
			} break;
			case 1:
			{
				if(lilv_node_is_uri(node))
				{
					if(d2tk_base_link_is_changed(base, D2TK_ID_IDX(idx),
						-1, lilv_node_as_uri(node), 1.f, hrect,
						D2TK_ALIGN_MIDDLE | D2TK_ALIGN_LEFT))
					{
						_xdg_open(node);
					}
				}
				else if(lilv_node_is_string(node))
				{
					d2tk_base_label(base, -1, lilv_node_as_string(node), 1.f, hrect,
						D2TK_ALIGN_MIDDLE | D2TK_ALIGN_LEFT);
				}
				else
				{
					d2tk_base_label(base, 1, "-", 1.f, hrect,
						D2TK_ALIGN_MIDDLE | D2TK_ALIGN_LEFT);
				}
			} break;
		}
	}
}

static inline void
_expose_sidebar_bottom(plughandle_t *handle, const d2tk_rect_t *rect)
{
	DBG;
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	if(!handle->plug_info)
	{
		return;
	}

	const unsigned dd = 16;
	const unsigned dn = rect->h / dd;
	const unsigned en = 9;

	const uint32_t max [2] = { 0, en };
	const uint32_t num [2] = { 0, dn };
	D2TK_BASE_SCROLLBAR(base, rect, D2TK_ID, D2TK_FLAG_SCROLL_Y, max, num, vscroll)
	{
		const float voffset = d2tk_scrollbar_get_offset_y(vscroll);
		const d2tk_rect_t *col = d2tk_scrollbar_get_rect(vscroll);

		D2TK_BASE_TABLE(col, col->w, dd, D2TK_FLAG_TABLE_ABS, trow)
		{
			const unsigned k = d2tk_table_get_index_y(trow) + voffset;

			if(k >= en)
			{
				break;
			}

			const d2tk_rect_t *row = d2tk_table_get_rect(trow);

			switch (k)
			{
				case 0: // name
				{
					LilvNode *node = lilv_plugin_get_name(handle->plug_info);

					_expose_plugin_property(base, k, "Name", node, row);

					lilv_node_free(node);
				} break;
				case 1: // class
				{
					const LilvPluginClass *class = lilv_plugin_get_class(handle->plug_info);
					const LilvNode *node = class
						? lilv_plugin_class_get_label(class)
						: NULL;

					_expose_plugin_property(base, k, "Class", node, row);
				} break;
				case 2: // uri
				{
					const LilvNode *node = lilv_plugin_get_uri(handle->plug_info);

					_expose_plugin_property(base, k, "URI", node, row);
				} break;

				case 3: // separator
				{
					// skip
				} break;

				case 4: // author name
				{
					LilvNode *node = lilv_plugin_get_author_name(handle->plug_info);

					_expose_plugin_property(base, k, "Author", node, row);

					lilv_node_free(node);
				} break;
				case 5: // author email
				{
					LilvNode *node = lilv_plugin_get_author_email(handle->plug_info);

					_expose_plugin_property(base, k, "Email", node, row);

					lilv_node_free(node);
				} break;

				case 6: // separator
				{
					// skip
				} break;

				case 7: // project
				{
					LilvNode *node = lilv_plugin_get_project(handle->plug_info);

					_expose_plugin_property(base, k, "Project", node, row);

					lilv_node_free(node);
				} break;
				case 8: // bundle
				{
					const LilvNode *node = lilv_plugin_get_bundle_uri(handle->plug_info);

					_expose_plugin_property(base, k, "Bundle", node, row);
				} break;
			}
		}

		d2tk_base_set_style(base, NULL);
	}
	//FIXME
}

static inline void
_expose_sidebar(plughandle_t *handle, const d2tk_rect_t *rect)
{
	DBG;
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	D2TK_BASE_PANE(base, rect, D2TK_ID, D2TK_FLAG_PANE_Y,
		0.6f, 1.f, 0.05f, vpane)
	{
		const d2tk_rect_t *prect =  d2tk_pane_get_rect(vpane);
		const uint32_t py = d2tk_pane_get_index(vpane);

		switch(py)
		{
			case 0:
			{
				_expose_sidebar_top(handle, prect);
			} break;
			case 1:
			{
				_expose_sidebar_bottom(handle, prect);
			} break;
		}
	}
}

static inline void
_expose_patchmatrix_mod(plughandle_t *handle, mod_t *mod,
	const d2tk_rect_t *rect)
{
	d2tk_base_t *base = d2tk_frontend_get_base(handle->dpugl);
	const stat_label_t *label = mod->alias.len
		? &mod->alias
		: &mod->name;

	D2TK_BASE_FRAME(base, rect, label->len, label->buf, frm)
	{
		const d2tk_rect_t *frect = d2tk_frame_get_rect(frm);

		//FIXME inline display
	}
}

static inline void
_expose_patchmatrix_connection(plughandle_t *handle, unsigned o,
	mod_t *mod_src, mod_t *mod_snk, const d2tk_rect_t *rect)
{
	d2tk_base_t *base = d2tk_frontend_get_base(handle->dpugl);

	D2TK_BASE_FRAME(base, rect, 0, NULL, frm)
	{
		const d2tk_rect_t *frect = d2tk_frame_get_rect(frm);

		if(!mod_snk->audio.nin || !mod_src->audio.nout) //FIXME in d2tk_base_table_*
		{
			break;
		}

		D2TK_BASE_TABLE(frect, mod_snk->audio.nin, mod_src->audio.nout,
			D2TK_FLAG_TABLE_REL, tab)
		{
			const d2tk_rect_t *trect = d2tk_table_get_rect(tab);
			const unsigned k = d2tk_table_get_index(tab);
			bool val = false;

			if(d2tk_base_dial_bool_is_changed(base, D2TK_ID_IDX(o*512 + k), trect, &val))
			{
				//FIXME
			}
		}
	}
}

static inline void
_expose_patchmatrix(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_base_t *base = d2tk_frontend_get_base(handle->dpugl);
	const unsigned dd = 128;
	const unsigned N = handle->nmods;

	if(!N) //FIXME in d2tk_base_table_*
	{
		return;
	}

	const unsigned dw = rect->w / dd;
	const unsigned dh = rect->h / dd;

	const uint32_t max [2] = { N, N };
	const uint32_t num [2] = { dw, dh };
	D2TK_BASE_SCROLLBAR(base, rect, D2TK_ID,
		D2TK_FLAG_SCROLL_X | D2TK_FLAG_SCROLL_Y, max, num, vscroll)
	{
		const float hoffset = d2tk_scrollbar_get_offset_x(vscroll);
		const float voffset = d2tk_scrollbar_get_offset_y(vscroll);
		const d2tk_rect_t *col = d2tk_scrollbar_get_rect(vscroll);

		D2TK_BASE_TABLE(col, dd, dd, D2TK_FLAG_TABLE_ABS, tab)
		{
			const d2tk_rect_t *trect = d2tk_table_get_rect(tab);
			const unsigned x = d2tk_table_get_index_x(tab);
			const unsigned y = d2tk_table_get_index_y(tab);
			const unsigned i = x + hoffset;
			const unsigned j = y + voffset;
			const unsigned k = j*N + i;

			if(i >= N)
			{
				continue;
			}
			else if(j >= N)
			{
				break;
			}
			else if(i == j)
			{
				mod_t *mod = handle->mods[i];

				_expose_patchmatrix_mod(handle, mod, trect);
			}
			else
			{
				mod_t *mod_src = handle->mods[j];
				mod_t *mod_dst = handle->mods[i];

				_expose_patchmatrix_connection(handle, k, mod_src, mod_dst, trect);
			}
		}
	}
	//FIXME
}

static inline void
_expose_patchbay(plughandle_t *handle, const d2tk_rect_t *rect)
{
	DBG;
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	D2TK_BASE_FLOWMATRIX(base, rect, D2TK_ID, flowm)
	{
		// draw arcs
		//FIXME

		// draw nodes
		for(unsigned m = 0; m < handle->nmods; m++)
		{
			mod_t *mod = handle->mods[m];

			d2tk_state_t state = D2TK_STATE_NONE;
			D2TK_BASE_FLOWMATRIX_NODE(base, flowm, &mod->pos, node, &state)
			{
				const d2tk_rect_t *bnd = d2tk_flowmatrix_node_get_rect(node);
				const d2tk_id_t id = D2TK_ID_IDX(m);

				const stat_label_t *label = mod->alias.len
					? &mod->alias
					: &mod->name;

				state = d2tk_base_toggle_label(base, id, label->len, label->buf,
					D2TK_ALIGN_CENTERED, bnd, &mod->selected);

				if(d2tk_base_get_modmask(base, D2TK_MODMASK_CTRL, false))
				{
					continue;
				}

				if(d2tk_state_is_active(state))
				{
					d2tk_flowmatrix_set_src(flowm, id, &mod->pos);
				}
				if(d2tk_state_is_over(state))
				{
					d2tk_flowmatrix_set_dst(flowm, id, &mod->pos);
				}
				if(d2tk_state_is_up(state))
				{
					const d2tk_id_t dst_id = d2tk_flowmatrix_get_dst(flowm, NULL);

					if(dst_id)
					{
						fprintf(stderr, "Connecting nodes %016"PRIx64" -> %016"PRIx64"\n",
							id, dst_id);
					}
				}
				state = D2TK_STATE_NONE;
			}
		}
	}
}

static inline void
_expose_status_bar(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	static const d2tk_coord_t hfrac [5] = { 4, 1, 1, 1, 1 };
	D2TK_BASE_LAYOUT(rect, 5, hfrac, D2TK_FLAG_LAYOUT_X_REL, hlay)
	{
		const d2tk_rect_t *hrect = d2tk_layout_get_rect(hlay);
		const uint32_t vx = d2tk_layout_get_index(hlay);

		switch(vx)
		{
			case 0:
			{
				if(!handle->message.len)
					break;

				d2tk_base_label(base, handle->message.len, handle->message.buf, 1.f,
					hrect, D2TK_ALIGN_MIDDLE | D2TK_ALIGN_LEFT);
			} break;
			case 1:
				// fall-through
			case 2:
				// fall-through
			case 3:
			{
				stat_label_t *label = &handle->status.label[vx - 1];

				if(!label->len)
					break;

				d2tk_base_label(base, label->len, label->buf, 1.f,
					hrect, D2TK_ALIGN_MIDDLE | D2TK_ALIGN_LEFT);
			} break;
			case 4:
			{
				d2tk_base_label(base, -1, "Synthpod "SYNTHPOD_VERSION, 1.f,
					hrect, D2TK_ALIGN_MIDDLE | D2TK_ALIGN_RIGHT);
			} break;
		}
	}
}

static inline void
_expose_main_area(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	D2TK_BASE_PANE(base, rect, D2TK_ID, D2TK_FLAG_PANE_X,
		0.0f, 0.2f, 0.05f, hpane)
	{
		const d2tk_rect_t *prect =  d2tk_pane_get_rect(hpane);
		const uint32_t px = d2tk_pane_get_index(hpane);

		switch(px)
		{
			case 0:
			{
				if(prect->w > 0) //FIXME in detk
				{
					_expose_sidebar(handle, prect);
				}
			} break;
			case 1:
			{
#if 0
				_expose_patchbay(handle, prect);
#else
				_expose_patchmatrix(handle, prect);
#endif
			} break;
		}
	}
}

static int
_expose(void *data, d2tk_coord_t w, d2tk_coord_t h)
{
	DBG;
	plughandle_t *handle = data;
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);
	const d2tk_rect_t rect = D2TK_RECT(0, 0, w, h);

	if(_lazy_loading(handle))
	{
		_plug_populate(handle, handle->pplugs);
	}

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
				_expose_main_area(handle, vrect);
			} break;
			case 2:
			{
				_expose_status_bar(handle, vrect);
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
	DBG;
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	void *parent = NULL;
	LV2UI_Resize *host_resize = NULL;
	LV2_Options_Option *opts = NULL;
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
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_OPTIONS__options))
			opts = features[i]->data;
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

	const LV2_URID atom_float = handle->map->map(handle->map->handle,
		LV2_ATOM__Float);
	const LV2_URID params_sample_rate = handle->map->map(handle->map->handle,
		LV2_PARAMETERS__sampleRate);
	const LV2_URID ui_update_rate= handle->map->map(handle->map->handle,
		LV2_UI__updateRate);

	handle->status.sample_rate = 48000.f; // fall-back
	handle->status.update_rate = 25.f; // fall-back

	for(LV2_Options_Option *opt = opts;
		opt && (opt->key != 0) && (opt->value != NULL);
		opt++)
	{
		if( (opt->key == params_sample_rate) && (opt->type == atom_float) )
			handle->status.sample_rate = *(float*)opt->value;
		else if( (opt->key == ui_update_rate) && (opt->type == atom_float) )
			handle->status.update_rate = *(float*)opt->value;
		//TODO handle more options
	}

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
	DBG;
	plughandle_t *handle = instance;

	d2tk_frontend_free(handle->dpugl);

	sp_regs_deinit(&handle->regs);

	free(handle->lplugs);

	lilv_world_free(handle->world);

	free(handle);
}

static inline mod_t *
_mod_find_by_urn(plughandle_t *handle, LV2_URID urn, bool claim)
{
	DBG;
	for(unsigned i = 0, idx = (urn + i*i) & MASK_MODS;
		i < MAX_MODS;
		i++, idx = (urn + i*i) & MASK_MODS)
	{
		mod_t *mod = &handle->hmods[idx];

		if(mod->urn == 0)
		{
			if(claim)
			{
				mod->urn = urn;
			}
			else
			{
				break;
			}
		}

		if(mod->urn == urn)
		{
			return mod;
		}
	}

	return NULL;
}

static int
_mod_cmp_pos(const void *a, const void *b)
{
	const mod_t **ptr_a = (const mod_t **)a;
	const mod_t **ptr_b = (const mod_t **)b;

	const mod_t *mod_a = *ptr_a;
	const mod_t *mod_b = *ptr_b;

	if(mod_a->pos.x < mod_b->pos.x)
	{
		return -1;
	}
	else if(mod_a->pos.x > mod_b->pos.x)
	{
		return 1;
	}

	return 0;
}

static inline void
_mod_filter(plughandle_t *handle)
{
	handle->nmods = 0;

	for(unsigned i = 0; i < MAX_MODS; i++)
	{
		mod_t *mod = &handle->hmods[i];

		if(mod->urn == 0)
		{
			continue;
		}

		handle->mods[handle->nmods++] = mod;
	}

	qsort(handle->mods, handle->nmods, sizeof(mod_t *), _mod_cmp_pos);
}

static inline void
_mod_add(plughandle_t *handle, LV2_URID urn)
{
	DBG;
	mod_t *mod = _mod_find_by_urn(handle, urn, true);
	if(!mod)
	{
		return;
	}

	mod->pos.x = handle->nxt_pos.x;
	mod->pos.y = handle->nxt_pos.y;

	handle->nxt_pos.y += 30;
	//FIXME

	_mod_filter(handle);
}

static inline void
_add_mod(plughandle_t *handle, const LV2_Atom_URID *urn)
{
	DBG;
	mod_t *mod = _mod_find_by_urn(handle, urn->body, false);
	if(mod)
	{
		return;
	}

	_mod_add(handle, urn->body);

	// get information for each of those, FIXME only if not already available
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			urn->body, 0, 0) )
	{
		_message_write(handle);
	}
}

static inline void
_mod_free(plughandle_t *handle, mod_t *mod)
{
	DBG;
	mod->urn = 0;
	//FIXME
}

static inline void
_port_event_set_module_list(plughandle_t *handle, const LV2_Atom_Tuple *tup)
{
	DBG;
#if 0
	_set_module_selector(handle, NULL);
#endif

	for(unsigned m = 0; m<MAX_MODS; m++)
	{
		mod_t *mod = &handle->hmods[m];

		_mod_free(handle, mod);
	}

	_mod_filter(handle);

	LV2_ATOM_TUPLE_FOREACH(tup, itm)
	{
		const LV2_Atom_URID *urn = (const LV2_Atom_URID *)itm;

		_add_mod(handle, urn);
	}

	// patch:Get [patch:property spod:connectionList]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.connection_list.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:nodeList]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.node_list.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property pset:preset]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.pset.preset.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:automationList]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.automation_list.urid) )
	{
		_message_write(handle);
	}
}

static inline void
_port_event_set_connection_list(plughandle_t *handle, const LV2_Atom_Tuple *tup)
{
	DBG;
	//FIXME
}

static inline void
_port_event_set_node_list(plughandle_t *handle, const LV2_Atom_Tuple *tup)
{
	DBG;
	//FIXME
}

static inline void
_port_event_set_automation_list(plughandle_t *handle, const LV2_Atom_Tuple *tup)
{
	DBG;
	//FIXME
}

static inline void
_port_event_set(plughandle_t *handle, const LV2_Atom_Object *obj)
{
	DBG;
	const LV2_Atom_URID *subject = NULL;
	const LV2_Atom_URID *property = NULL;
	const LV2_Atom *value = NULL;

	lv2_atom_object_get(obj,
		handle->regs.patch.subject.urid, &subject,
		handle->regs.patch.property.urid, &property,
		handle->regs.patch.value.urid, &value,
		0);

	const LV2_URID subj = subject && (subject->atom.type == handle->forge.URID)
		? subject->body
		: 0;
	const LV2_URID prop = property && (property->atom.type == handle->forge.URID)
		? property->body
		: 0;

	if(!prop || !value)
	{
		return;
	}

	if(  (prop == handle->regs.synthpod.module_list.urid)
		&& (value->type == handle->forge.Tuple) )
	{
		_port_event_set_module_list(handle, (const LV2_Atom_Tuple *)value);
	}
	else if( (prop == handle->regs.synthpod.connection_list.urid)
		&& (value->type == handle->forge.Tuple) )
	{
		_port_event_set_connection_list(handle, (const LV2_Atom_Tuple *)value);
	}
	else if( (prop == handle->regs.synthpod.node_list.urid)
		&& (value->type == handle->forge.Tuple) )
	{
		_port_event_set_node_list(handle, (const LV2_Atom_Tuple *)value);
	}
	else if( (prop == handle->regs.synthpod.automation_list.urid)
		&& (value->type == handle->forge.Tuple) )
	{
		_port_event_set_automation_list(handle, (const LV2_Atom_Tuple *)value);
	}
	else if( (prop == handle->regs.pset.preset.urid)
		&& (value->type == handle->forge.URID) )
	{
		//FIXME
	}
	else if( (prop == handle->regs.synthpod.dsp_profiling.urid)
		&& (value->type == handle->forge.Vector) )
	{
		const float *f32 = LV2_ATOM_CONTENTS_CONST(LV2_Atom_Vector, value);

		handle->prof.min = f32[0];
		handle->prof.avg = f32[1];
		handle->prof.max= f32[2];

		_status_labels_update(handle);
	}
	else if( (prop == handle->regs.synthpod.module_profiling.urid)
		&& (value->type == handle->forge.Vector)
		&& subj )
	{
		//FIXME
	}
	else if( (prop == handle->regs.synthpod.graph_position_x.urid)
		&& (value->type == handle->forge.Float) )
	{
		//FIXME
	}
	else if( (prop == handle->regs.synthpod.graph_position_y.urid)
		&& (value->type == handle->forge.Float) )
	{

	}
	else if( (prop == handle->regs.synthpod.column_enabled.urid)
		&& (value->type == handle->forge.Bool) )
	{
		//FIXME
	}
	else if( (prop == handle->regs.synthpod.row_enabled.urid)
		&& (value->type == handle->forge.Bool) )
	{
		//FIXME
	}
	else if( (prop == handle->regs.synthpod.cpus_used.urid)
		&& (value->type == handle->forge.Int) )
	{
		handle->status.cpus_used = ATOM_INT_VAL(value);

		_status_labels_update(handle);
	}
	else if( (prop == handle->regs.synthpod.cpus_available.urid)
		&& (value->type == handle->forge.Int) )
	{
		handle->status.cpus_available = ATOM_INT_VAL(value);

		_status_labels_update(handle);
	}
	else if( (prop == handle->regs.synthpod.period_size.urid)
		&& (value->type == handle->forge.Int) )
	{
		handle->status.period_size = ATOM_INT_VAL(value);

		_status_labels_update(handle);
	}
	else if( (prop == handle->regs.synthpod.num_periods.urid)
		&& (value->type == handle->forge.Int) )
	{
		handle->status.num_periods = ATOM_INT_VAL(value);

		_status_labels_update(handle);
	}
	else if( (prop == handle->regs.idisp.surface.urid)
		&& (value->type == handle->forge.Tuple)
		&& subj )
	{
		//FIXME
	}
	//FIXME
}

static inline void
_mod_init(plughandle_t *handle, mod_t *mod, const LilvPlugin *plug)
{
	DBG;
	if(mod->plug) // already initialized
	{
		return;
	}

	mod->plug = plug;

	LilvNode *name_node = lilv_plugin_get_name(plug);
	if(name_node)
	{
		const char *name = lilv_node_as_string(name_node);

		mod->name.len = snprintf(mod->name.buf, sizeof(mod->name.buf), "%s", name);

		lilv_node_free(name_node);
	}

	const unsigned num_ports = lilv_plugin_get_num_ports(plug) + 2; // + automation ports

	mod->audio.nin = 0;
	mod->audio.nout= 0;
	mod->cv.nin = 0;
	mod->cv.nout= 0;
	mod->control.nin = 0;
	mod->control.nout= 0;
	mod->atom.nin = 0;
	mod->atom.nout= 0;

	for(unsigned p=0; p<num_ports - 2; p++) // - automation ports
	{
		const LilvPort *port = lilv_plugin_get_port_by_index(plug, p);

		LilvNode *lv2_AudioPort = lilv_new_uri(handle->world, LV2_CORE__AudioPort);
		LilvNode *lv2_CVPort = lilv_new_uri(handle->world, LV2_CORE__CVPort);
		LilvNode *lv2_ControlPort = lilv_new_uri(handle->world, LV2_CORE__ControlPort);
		LilvNode *atom_AtomPort = lilv_new_uri(handle->world, LV2_ATOM__AtomPort);
		LilvNode *lv2_OutputPort= lilv_new_uri(handle->world, LV2_CORE__OutputPort);

		const bool is_audio = lilv_port_is_a(plug, port, lv2_AudioPort);
		const bool is_cv = lilv_port_is_a(plug, port, lv2_CVPort);
		const bool is_control = lilv_port_is_a(plug, port, lv2_ControlPort);
		const bool is_atom = lilv_port_is_a(plug, port, atom_AtomPort);
		const bool is_output = lilv_port_is_a(plug, port, lv2_OutputPort);

		if(is_audio)
		{
			if(is_output)
			{
				mod->audio.nout++;
			}
			else
			{
				mod->audio.nin++;
			}
		}
		else if(is_cv)
		{
			if(is_output)
			{
				mod->cv.nout++;
			}
			else
			{
				mod->cv.nin++;
			}
		}
		else if(is_control)
		{
			if(is_output)
			{
				mod->control.nout++;
			}
			else
			{
				mod->control.nin++;
			}
		}
		else if(is_atom)
		{
			if(is_output)
			{
				mod->atom.nout++;
			}
			else
			{
				mod->atom.nin++;
			}
		}

		lilv_node_free(lv2_AudioPort);
		lilv_node_free(lv2_CVPort);
		lilv_node_free(lv2_ControlPort);
		lilv_node_free(atom_AtomPort);
		lilv_node_free(lv2_OutputPort);
		//FIXME
	}
}

static inline void
_port_event_put(plughandle_t *handle, const LV2_Atom_Object *obj)
{
DBG;
	const LV2_Atom_URID *subject = NULL;
	const LV2_Atom_Object *body = NULL;

	lv2_atom_object_get(obj,
		handle->regs.patch.subject.urid, &subject,
		handle->regs.patch.body.urid, &body,
		0);

	const LV2_URID subj = subject && (subject->atom.type == handle->forge.URID)
		? subject->body
		: 0;

	if(!subj || !body)
	{
		return;
	}

	//printf("got patch:Put for %u\n", subj);

	const LV2_Atom_URID *plugin = NULL;
	const LV2_Atom_Float *mod_pos_x = NULL;
	const LV2_Atom_Float *mod_pos_y = NULL;
	const LV2_Atom_String *mod_alias = NULL;
	const LV2_Atom_URID *ui_uri = NULL;

	lv2_atom_object_get(body,
		handle->regs.core.plugin.urid, &plugin,
		handle->regs.synthpod.module_position_x.urid, &mod_pos_x,
		handle->regs.synthpod.module_position_y.urid, &mod_pos_y,
		handle->regs.synthpod.module_alias.urid, &mod_alias,
		handle->regs.ui.ui.urid, &ui_uri, //FIXME use this
		0); //FIXME query more

	const LV2_URID urid = plugin
		? plugin->body
		: 0;
	if(!urid)
	{
		return;
	}

	const char *uri = handle->unmap->unmap(handle->unmap->handle, urid);
	if(!uri)
	{
		return;
	}

	mod_t *mod = _mod_find_by_urn(handle, subj, false);
	if(!mod)
	{
		return;
	}

	LilvNode *uri_node = lilv_new_uri(handle->world, uri);
	if(!uri_node)
	{
		return;
	}

	const LilvPlugin *plug = NULL;
	plug = lilv_plugins_get_by_uri(handle->plugs, uri_node);
	lilv_node_free(uri_node);

	if(!plug)
	{
		return;
	}

	_mod_init(handle, mod, plug);

	bool needs_filtering = false;

	if(  mod_pos_x
		&& (mod_pos_x->atom.type == handle->forge.Float)
		&& (mod_pos_x->body != 0.f) )
	{
		mod->pos.x = mod_pos_x->body;
		needs_filtering = true;
	}
	else if(  _message_request(handle)
		&&  synthpod_patcher_set(&handle->regs, &handle->forge,
			mod->urn, 0, handle->regs.synthpod.module_position_x.urid,
			sizeof(float), handle->forge.Float, &mod->pos.x) )
	{
		_message_write(handle);
	}

	if(  mod_pos_y
		&& (mod_pos_y->atom.type == handle->forge.Float)
		&& (mod_pos_y->body != 0.f) )
	{
		mod->pos.y = mod_pos_y->body;
		needs_filtering = true;
	}
	else if(  _message_request(handle)
		&& synthpod_patcher_set(&handle->regs, &handle->forge,
			mod->urn, 0, handle->regs.synthpod.module_position_y.urid,
			sizeof(float), handle->forge.Float, &mod->pos.y) )
	{
		_message_write(handle);
	}

	if(  mod_alias
		&& (mod_alias->atom.type == handle->forge.String) )
	{
		mod->alias.len = snprintf(mod->alias.buf, sizeof(mod->alias.buf), "%s",
			ATOM_STRING_VAL(&mod_alias->atom));
	}

	if(needs_filtering)
	{
		_mod_filter(handle);
	}

	const LV2_URID ui_urn = ui_uri
		? ui_uri->body
		: 0;
	if(!ui_urn)
	{
		return;
	}

#if 0
	// look for ui, and run it
	HASH_FOREACH(&mod->uis, mod_ui_itr)
	{
		mod_ui_t *mod_ui = *mod_ui_itr;

		if(_mod_ui_is_running(mod_ui))
			_mod_ui_stop(mod_ui, false);

		if(mod_ui->urn == ui_urn)
			_mod_ui_run(mod_ui, false);
	}
#endif
}

static inline void
_port_event_patch(plughandle_t *handle, const LV2_Atom_Object *obj)
{
	DBG;
	//FIXME
}

static inline void
_port_event_copy(plughandle_t *handle, const LV2_Atom_Object *obj)
{
	DBG;
	//FIXME
}

static void
port_event(LV2UI_Handle instance, uint32_t port_index, uint32_t size,
	uint32_t format, const void *buffer)
{
	DBG;
	plughandle_t *handle = instance;

	if(port_index != NOTIFY)
	{
		return;
	}

	if(format != handle->regs.port.event_transfer.urid)
	{
		return;
	}

	const LV2_Atom_Object *obj = buffer;
	if(!lv2_atom_forge_is_object_type(&handle->forge, obj->atom.type))
	{
		return;
	}

	if(obj->body.otype == handle->regs.patch.set.urid)
	{
		_port_event_set(handle, obj);
	}
	else if(obj->body.otype == handle->regs.patch.put.urid)
	{
		_port_event_put(handle, obj);
	}
	else if(obj->body.otype == handle->regs.patch.patch.urid)
	{
		_port_event_patch(handle, obj);
	}
	else if(obj->body.otype == handle->regs.patch.copy.urid)
	{
		_port_event_copy(handle, obj);
	}
	else
	{
		return;
	}

	d2tk_frontend_redisplay(handle->dpugl); //FIXME only do when needed
}

static void
_init(plughandle_t *handle)
{
	DBG;
	handle->world = lilv_world_new();

	LilvNode *node_false = lilv_new_bool(handle->world, false);
	if(node_false)
	{
		lilv_world_set_option(handle->world, LILV_OPTION_DYN_MANIFEST, node_false);
		lilv_node_free(node_false);
	}
	lilv_world_load_all(handle->world);

	LilvNode *synthpod_bundle = lilv_new_file_uri(handle->world, NULL, SYNTHPOD_BUNDLE_DIR);
	if(synthpod_bundle)
	{
		lilv_world_load_bundle(handle->world, synthpod_bundle);
		lilv_node_free(synthpod_bundle);
	}

	handle->plugs = lilv_world_get_all_plugins(handle->world);
	handle->iplugs = lilv_plugins_begin(handle->plugs);
	const unsigned nplugs = lilv_plugins_size(handle->plugs);
	handle->lplugs = calloc(1, nplugs * sizeof(entry_t));

	sp_regs_init(&handle->regs, handle->world, handle->map);

	// patch:Get [patch:property spod:moduleList]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.module_list.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:CPUsAvailable]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.cpus_available.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:CPUsUsed]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.cpus_used.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:periodSize]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.period_size.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:numPeriods]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.num_periods.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:graphPositionX]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.graph_position_x.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:graphPositionY]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.graph_position_y.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:columnEnabled]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.column_enabled.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:rowEnabled]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.row_enabled.urid) )
	{
		_message_write(handle);
	}
}

static int
_idle(LV2UI_Handle instance)
{
	DBG;
	plughandle_t *handle = instance;

	const int res = d2tk_frontend_step(handle->dpugl);

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
	DBG;
	plughandle_t *handle = instance;

	return d2tk_frontend_set_size(handle->dpugl, width, height);
}

static const LV2UI_Resize resize_ext = {
	.ui_resize = _resize
};

static const void *
extension_data(const char *uri)
{
	DBG;
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
