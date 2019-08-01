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

#define ATOM_BOOL_VAL(ATOM) ((LV2_Atom_Bool *)(ATOM))->body;
#define ATOM_INT_VAL(ATOM) ((LV2_Atom_Int *)(ATOM))->body;
#define ATOM_LONG_VAL(ATOM) ((LV2_Atom_Long *)(ATOM))->body;
#define ATOM_FLOAT_VAL(ATOM) ((LV2_Atom_Float *)(ATOM))->body;
#define ATOM_DOUBLE_VAL(ATOM) ((LV2_Atom_Double *)(ATOM))->body;

#define DBG_NOW fprintf(stderr, ":: %s\n", __func__)
#if 0
#	define DBG DBG_NOW
#else
#	define DBG
#endif

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
typedef struct _status_t status_t;
typedef struct _prof_t prof_t;
typedef struct _plughandle_t plughandle_t;

struct _dyn_label_t {
	ssize_t len;
	const char *buf;
};

struct _stat_label_t {
	ssize_t len;
	char buf[64];
};

struct _entry_t {
	const void *data;
	dyn_label_t name;
};

struct _view_t {
	view_type_t type;
	bool selector [VIEW_TYPE_MAX];
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

	status_t status;
	prof_t prof;
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

static int
_plug_cmp_name(const void *a, const void *b)
{
	DBG;
	const entry_t *entry_a = (const entry_t *)a;
	const entry_t *entry_b = (const entry_t *)b;

	return strcasecmp(entry_a->name.buf, entry_b->name.buf);
}

static int
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
	DBG;
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
_expose_patch(plughandle_t *handle, unsigned iview, const d2tk_rect_t *rect)
{
	DBG;
	d2tk_pugl_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_pugl_get_base(dpugl);

#define N 4
	static d2tk_pos_t pos_nodes [N] = {
		[0] = { .x = -500, .y =  200 },
		[1] = { .x = -250, .y = -100 },
		[2] = { .x =    0, .y =  100 },
		[3] = { .x =  500, .y =    0 }
	};
	static d2tk_pos_t pos_arcs [N][N] = {
		[0] = {
			[3] = { .x = 150, .y = 250 }
		}
	};
	static bool value [N][N][N*N];
	static bool toggle [N];

	D2TK_BASE_FLOWMATRIX(base, rect, D2TK_ID, flowm)
	{
		// draw arcs
		for(unsigned i = 0; i < N; i++)
		{
			const unsigned nin = i + 1;

			for(unsigned j = i + 1; j < N; j++)
			{
				const unsigned nout = j + 1;

				d2tk_state_t state = D2TK_STATE_NONE;
				D2TK_BASE_FLOWMATRIX_ARC(base, flowm, nin, nout, &pos_nodes[i],
					&pos_nodes[j], &pos_arcs[i][j], arc, &state)
				{
					const d2tk_rect_t *bnd = d2tk_flowmatrix_arc_get_rect(arc);
					const unsigned k = d2tk_flowmatrix_arc_get_index(arc);
					const d2tk_id_t id = D2TK_ID_IDX((i*N + j)*N*N + k);
					const unsigned x = d2tk_flowmatrix_arc_get_index_x(arc);
					const unsigned y = d2tk_flowmatrix_arc_get_index_y(arc);

					if(y == nout) // source label
					{
						char lbl [16];
						const ssize_t lbl_len = snprintf(lbl, sizeof(lbl), "Source port %u", x);

						d2tk_base_label(base, lbl_len, lbl, 0.8f, bnd,
							D2TK_ALIGN_BOTTOM | D2TK_ALIGN_RIGHT);
					}
					else if(x == nin) // sink label
					{
						char lbl [16];
						const ssize_t lbl_len = snprintf(lbl, sizeof(lbl), "Sink port %u", y);

						d2tk_base_label(base, lbl_len, lbl, 0.8f, bnd,
							D2TK_ALIGN_BOTTOM | D2TK_ALIGN_LEFT);
					}
					else // connector
					{
						bool *val = &value[i][j][k];

						state = d2tk_base_dial_bool(base, id, bnd, val);
						if(d2tk_state_is_changed(state))
						{
							fprintf(stderr, "Arc %u/%u %s\n", x, y, *val ? "ON" : "OFF");
						}
					}
				}
			}
		}

		// draw nodes
		for(unsigned i = 0; i < N; i++)
		{
			d2tk_state_t state = D2TK_STATE_NONE;
			D2TK_BASE_FLOWMATRIX_NODE(base, flowm, &pos_nodes[i], node, &state)
			{
				char lbl [32];
				const ssize_t lbl_len = snprintf(lbl, sizeof(lbl), "Node %u", i);
				const d2tk_rect_t *bnd = d2tk_flowmatrix_node_get_rect(node);
				const d2tk_id_t id = D2TK_ID_IDX(i);
				bool *val = &toggle[i];

				state = d2tk_base_toggle_label(base, id, lbl_len, lbl,
					D2TK_ALIGN_CENTERED, bnd, val);
				if(d2tk_state_is_active(state))
				{
					d2tk_flowmatrix_set_src(flowm, id, &pos_nodes[i]);
				}
				if(d2tk_state_is_over(state))
				{
					d2tk_flowmatrix_set_dst(flowm, id, &pos_nodes[i]);
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
				//else if(d2tk_state_is_changed(state))
				//{
				//	fprintf(stderr, "Node %u %s\n", i, *val ? "ON" : "OFF");
				//}
			}
		}
	}
#undef N

	return 0;
}

static int
_expose(void *data, d2tk_coord_t w, d2tk_coord_t h)
{
	DBG;
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

					switch(px)
					{
						case 0:
						{
							_expose_view(handle, px, prect);
						} break;
						case 1:
						{
							_expose_patch(handle, px, prect);
						} break;
					}
				}
			} break;
			case 2:
			{
				static const d2tk_coord_t hfrac [5] = { 4, 1, 1, 1, 1 };
				D2TK_BASE_LAYOUT(vrect, 5, hfrac, D2TK_FLAG_LAYOUT_X_REL, hlay)
				{
					const d2tk_rect_t *hrect = d2tk_layout_get_rect(hlay);
					const uint32_t vx = d2tk_layout_get_index(hlay);

					switch(vx)
					{
						case 0:
						{
							if(handle->message.len)
							{
								d2tk_base_label(base, handle->message.len, handle->message.buf, 1.f, hrect,
									D2TK_ALIGN_MIDDLE | D2TK_ALIGN_LEFT);
							}
						} break;
						case 1:
							// fall-through
						case 2:
							// fall-through
						case 3:
						{
							stat_label_t *label = &handle->status.label[vx - 1];

							if(label->len)
							{
								d2tk_base_label(base, label->len, label->buf, 1.f, hrect,
									D2TK_ALIGN_MIDDLE| D2TK_ALIGN_LEFT);
							}
						} break;
						case 4:
						{
							d2tk_base_label(base, -1, "Synthpod "SYNTHPOD_VERSION, 1.f, hrect,
								D2TK_ALIGN_MIDDLE| D2TK_ALIGN_RIGHT);
						} break;
					}
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

	d2tk_pugl_free(handle->dpugl);

	sp_regs_deinit(&handle->regs);

	free(handle->lplugs);

	lilv_world_free(handle->world);

	free(handle);
}

static inline void
_port_event_set_module_list(plughandle_t *handle, const LV2_Atom_Tuple *tup)
{
	DBG;
	//FIXME
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
		const LV2_Atom_Vector *vec = (const LV2_Atom_Vector *)value;
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
_port_event_put(plughandle_t *handle, const LV2_Atom_Object *obj)
{
	DBG;
	//FIXME
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

	d2tk_pugl_redisplay(handle->dpugl); //FIXME only do when needed
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
	DBG;
	plughandle_t *handle = instance;

	return d2tk_pugl_resize(handle->dpugl, width, height);
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
