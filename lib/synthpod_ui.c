/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#include <stdlib.h>
#include <string.h>

#include <uv.h>

#include <synthpod_ui.h>
#include <synthpod_private.h>
#include <patcher.h>

#define NUM_UI_FEATURES 5

typedef struct _mod_t mod_t;
typedef struct _port_t port_t;

struct _mod_t {
	sp_ui_t *ui;
	u_id_t uid;
	int selected;
	
	// features
	LV2_Feature feature_list [NUM_UI_FEATURES];
	const LV2_Feature *features [NUM_UI_FEATURES + 1];
	
	// self
	const LilvPlugin *plug;
	LilvUIs *all_uis;

	// ports
	uint32_t num_ports;
	port_t *ports;
		
	int col;

	// Eo UI
	struct {
		const LilvUI *ui;
		uv_lib_t lib;

		const LV2UI_Descriptor *descriptor;
		LV2UI_Handle handle;
		Elm_Object_Item *itm;
		Evas_Object *widget;

		// LV2UI_Port_Map extention
		LV2UI_Port_Map port_map;
	
		// LV2UI_Port_Subscribe extension
		LV2UI_Port_Subscribe port_subscribe;
		
		// LV2UI_Idle_Interface extension
		const LV2UI_Idle_Interface *idle_interface;
	} eo;

	// standard "automatic" UI
	struct {
		LV2UI_Descriptor descriptor;
		Elm_Object_Item *itm;
	} std;

	struct {
		int source;
		int sink;
	} system;
};

struct _port_t {
	mod_t *mod;
	int selected;

	const LilvPort *tar;
	uint32_t index;

	port_direction_t direction; // input, output
	port_type_t type; // audio, CV, control, atom
	port_buffer_type_t buffer_type; // none, sequence

	LilvScalePoints *points;

	float dflt;
	float min;
	float max;
			
	struct {
		Evas_Object *widget;
	} std;
};

struct _sp_ui_t {
	sp_ui_driver_t *driver;
	void *data;

	LilvWorld *world;
	const LilvPlugins *plugs;

	reg_t regs;
	LV2_Atom_Forge forge;

	Evas_Object *win;
	Evas_Object *plugpane;
	Evas_Object *modpane;
	Evas_Object *patchpane;

	Evas_Object *pluglist;
	Evas_Object *modlist;
	Evas_Object *modgrid;
	Evas_Object *patchbox;
	Evas_Object *matrix[PORT_TYPE_NUM];

	Elm_Genlist_Item_Class *plugitc;
	Elm_Genlist_Item_Class *moditc;
	Elm_Genlist_Item_Class *stditc;
	Elm_Gengrid_Item_Class *griditc;
		
	Elm_Object_Item *sink_itm;
};

typedef struct _ui_write_t ui_write_t;

struct _ui_write_t {
	uint32_t size;
	uint32_t protocol;
	uint32_t port;
};

#define JOB_SIZE ( sizeof(job_t) )

#define UI_WRITE_SIZE ( sizeof(ui_write_t) )
#define UI_WRITE_PADDED ( (UI_WRITE_SIZE + 7U) & (~7U) )

#define COLORS_MAX 20 // FIXME read from theme
static uint8_t color_cnt = 1;

static inline int
_next_color()
{
	int col = color_cnt++;
	if(color_cnt >= COLORS_MAX)
		color_cnt = 1;
	return col;
}

static inline void *
_sp_ui_to_app_request(sp_ui_t *ui, size_t size)
{
	if(ui->driver->to_app_request)
		return ui->driver->to_app_request(size, ui->data);
	else
		return NULL;
}
static inline void
_sp_ui_to_app_advance(sp_ui_t *ui, size_t size)
{
	if(ui->driver->to_app_advance)
		ui->driver->to_app_advance(size, ui->data);
}

static inline int
_match_port_protocol(port_t *port, uint32_t protocol, uint32_t size)
{
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	if(  (protocol == ui->regs.port.float_protocol.urid)
		&& (port->type == PORT_TYPE_CONTROL)
		&& (size == sizeof(float)) )
	{
		return 1;
	}
	else if ( (protocol == ui->regs.port.peak_protocol.urid)
		&& ((port->type == PORT_TYPE_AUDIO) || (port->type == PORT_TYPE_CV)) )
	{
		return 1;
	}
	else if( (protocol == ui->regs.port.atom_transfer.urid)
				&& (port->type == PORT_TYPE_ATOM) )
	{
		return 1;
	}
	else if( (protocol == ui->regs.port.event_transfer.urid)
				&& (port->type == PORT_TYPE_ATOM)
				&& (port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) )
	{
		return 1;
	}

	return 0;
}

static inline void
_std_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;
	port_t *port = &mod->ports[index];

	//printf("_std_port_event: %u %u %u\n", index, size, protocol);

	if(protocol == 0)
		protocol = ui->regs.port.float_protocol.urid;

	// check for expanded list
	if(!elm_genlist_item_expanded_get(mod->std.itm))
		return;

	// check for realized port widget
	if(!port->std.widget)
		return;

	if(protocol == ui->regs.port.float_protocol.urid)
	{
		const float val = *(float *)buf;
		int toggled = lilv_port_has_property(mod->plug, port->tar, ui->regs.port.toggled.node);

		if(toggled)
			elm_check_state_set(port->std.widget, val > 0.f ? EINA_TRUE : EINA_FALSE);
		else if(port->points)
			elm_spinner_value_set(port->std.widget, val);
		else // integer or float
			elm_slider_value_set(port->std.widget, val);
	}
	else if(protocol == ui->regs.port.peak_protocol.urid)
	{
		const LV2UI_Peak_Data *peak_data = buf;
		//printf("peak: %f\n", peak_data->peak);
		elm_progressbar_value_set(port->std.widget, peak_data->peak);
	}
	else
		; //TODO atom, sequence
}

static inline void
_eo_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;

	//printf("_eo_port_event: %u %u %u\n", index, size, protocol);

	if(  mod->eo.ui
		&& mod->eo.descriptor
		&& mod->eo.descriptor->port_event
		&& mod->eo.handle)
	{
		mod->eo.descriptor->port_event(mod->eo.handle,
			index, size, protocol, buf);
	}
}

static uint32_t
_port_index(LV2UI_Feature_Handle handle, const char *symbol)
{
	mod_t *mod = handle;
	LilvNode *symbol_uri = lilv_new_string(mod->ui->world, symbol);
	const LilvPort *port = lilv_plugin_get_port_by_symbol(mod->plug, symbol_uri);
	lilv_node_free(symbol_uri);

	return port
		? lilv_port_get_index(mod->plug, port)
		: LV2UI_INVALID_PORT_INDEX;
}

static uint32_t
_port_subscribe(LV2UI_Feature_Handle handle, uint32_t index, uint32_t protocol,
	const LV2_Feature *const *features)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;
			
	if(protocol == 0)
		protocol = ui->regs.port.float_protocol.urid;

	size_t size = sizeof(transmit_port_subscribed_t);
	transmit_port_subscribed_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_subscribed_fill(&ui->regs, &ui->forge, trans, size,
			mod->uid, index, protocol, 1);
		_sp_ui_to_app_advance(ui, size);
	}

	return 0;
}

static uint32_t
_port_unsubscribe(LV2UI_Feature_Handle handle, uint32_t index, uint32_t protocol,
	const LV2_Feature *const *features)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;

	if(protocol == 0)
		protocol = ui->regs.port.float_protocol.urid;

	size_t size = sizeof(transmit_port_subscribed_t);
	transmit_port_subscribed_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_subscribed_fill(&ui->regs, &ui->forge, trans, size,
			mod->uid, index, protocol, 0);
		_sp_ui_to_app_advance(ui, size);
	}

	return 0;
}

static mod_t *
_sp_ui_mod_add(sp_ui_t *ui, const char *uri, u_id_t uid)
{
	LilvNode *uri_node = lilv_new_uri(ui->world, uri);
	const LilvPlugin *plug = lilv_plugins_get_by_uri(ui->plugs, uri_node);
	lilv_node_free(uri_node);

	const LilvNode *plugin_uri = lilv_plugin_get_uri(plug);
	const char *plugin_string = lilv_node_as_string(plugin_uri);
			
	if(!plug || !lilv_plugin_verify(plug))
		return NULL;

	mod_t *mod = calloc(1, sizeof(mod_t));

	// populate port_map
	mod->eo.port_map.handle = mod;
	mod->eo.port_map.port_index = _port_index;

	// populate port_subscribe
	mod->eo.port_subscribe.handle = mod;
	mod->eo.port_subscribe.subscribe = _port_subscribe;
	mod->eo.port_subscribe.unsubscribe = _port_unsubscribe;

	// populate port_event for StdUI
	mod->std.descriptor.port_event = _std_port_event;

	// populate UI feature list
	mod->feature_list[0].URI = LV2_URID__map;
	mod->feature_list[0].data = ui->driver->map;
	mod->feature_list[1].URI = LV2_URID__unmap;
	mod->feature_list[1].data = ui->driver->unmap;
	mod->feature_list[2].URI = LV2_UI__parent;
	mod->feature_list[2].data = ui->pluglist;
	mod->feature_list[3].URI = LV2_UI__portMap;
	mod->feature_list[3].data = &mod->eo.port_map;
	mod->feature_list[4].URI = LV2_UI__portSubscribe;
	mod->feature_list[4].data = &mod->eo.port_subscribe;
	
	for(int i=0; i<NUM_UI_FEATURES; i++)
		mod->features[i] = &mod->feature_list[i];
	mod->features[NUM_UI_FEATURES] = NULL; // sentinel

	mod->ui = ui;
	mod->uid = uid;
	mod->plug = plug;
	mod->num_ports = lilv_plugin_get_num_ports(plug);

	// discover system modules
	if(!strcmp(uri, "http://open-music-kontrollers.ch/lv2/synthpod#source"))
		mod->system.source = 1;
	else if(!strcmp(uri, "http://open-music-kontrollers.ch/lv2/synthpod#sink"))
		mod->system.sink = 1;

	mod->ports = calloc(mod->num_ports, sizeof(port_t));
	for(uint32_t i=0; i<mod->num_ports; i++)
	{
		port_t *tar = &mod->ports[i];
		const LilvPort *port = lilv_plugin_get_port_by_index(plug, i);

		tar->mod = mod;
		tar->tar = port;
		tar->index = i;
		tar->direction = lilv_port_is_a(plug, port, ui->regs.port.input.node)
			? PORT_DIRECTION_INPUT
			: PORT_DIRECTION_OUTPUT;

		if(lilv_port_is_a(plug, port, ui->regs.port.audio.node))
		{
			tar->type =  PORT_TYPE_AUDIO;
			tar->selected = 1;
		}
		else if(lilv_port_is_a(plug, port, ui->regs.port.cv.node))
		{
			tar->type = PORT_TYPE_CV;
			tar->selected = 1;
		}
		else if(lilv_port_is_a(plug, port, ui->regs.port.control.node))
		{
			tar->type = PORT_TYPE_CONTROL;
		
			LilvNode *dflt_node;
			LilvNode *min_node;
			LilvNode *max_node;
			lilv_port_get_range(mod->plug, tar->tar, &dflt_node, &min_node, &max_node);
			tar->dflt = dflt_node ? lilv_node_as_float(dflt_node) : 0.f;
			tar->min = min_node ? lilv_node_as_float(min_node) : 0.f;
			tar->max = max_node ? lilv_node_as_float(max_node) : 1.f;
			lilv_node_free(dflt_node);
			lilv_node_free(min_node);
			lilv_node_free(max_node);
		}
		else if(lilv_port_is_a(plug, port, ui->regs.port.atom.node)) 
		{
			tar->type = PORT_TYPE_ATOM;
			tar->buffer_type = PORT_BUFFER_TYPE_SEQUENCE;
			//tar->buffer_type = lilv_port_is_a(plug, port, ui->regs.port.sequence.node)
			//	? PORT_BUFFER_TYPE_SEQUENCE
			//	: PORT_BUFFER_TYPE_NONE; //TODO
			tar->selected = 1;
		}

		// ignore dummy system ports
		if(mod->system.source && (tar->direction == PORT_DIRECTION_INPUT) )
			tar->selected = 0;
		if(mod->system.sink && (tar->direction == PORT_DIRECTION_OUTPUT) )
			tar->selected = 0;
	}
		
	//ui
	mod->all_uis = lilv_plugin_get_uis(mod->plug);
	LILV_FOREACH(uis, ptr, mod->all_uis)
	{
		const LilvUI *lui = lilv_uis_get(mod->all_uis, ptr);
		if(lilv_ui_is_a(lui, ui->regs.ui.eo.node))
		{
			mod->eo.ui = lui;
			break;
		}
	}
	
	if(mod->system.source || mod->system.sink)
		mod->col = 0; // reserved color for system ports
	else
		mod->col = _next_color();
	
	return mod;
}

void
_sp_ui_mod_del(sp_ui_t *ui, mod_t *mod)
{
	if(mod->all_uis)
		lilv_uis_free(mod->all_uis);

	free(mod);
}

static char * 
_pluglist_label_get(void *data, Evas_Object *obj, const char *part)
{
	const LilvPlugin *plug = data;

	if(!strcmp(part, "elm.text"))
	{
		LilvNode *name_node = lilv_plugin_get_name(plug);
		const char *name_str = lilv_node_as_string(name_node);
		lilv_node_free(name_node);

		return strdup(name_str);
	}
	else if(!strcmp(part, "elm.text.sub"))
	{
		const LilvNode *uri_node = lilv_plugin_get_uri(plug);
		const char *uri_str = lilv_node_as_string(uri_node);

		return strdup(uri_str);
	}
	else
		return NULL;
}

static void
_pluglist_activated(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;
	const LilvPlugin *plug = elm_object_item_data_get(itm);;
		
	const LilvNode *uri_node = lilv_plugin_get_uri(plug);
	const char *uri_str = lilv_node_as_string(uri_node);

	size_t size = sizeof(transmit_module_add_t) + strlen(uri_str) + 1;
	transmit_module_add_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_add_fill(&ui->regs, &ui->forge, trans, size, 0, uri_str);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_list_expand_request(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	Eina_Bool selected = elm_genlist_item_selected_get(itm);
	elm_genlist_item_expanded_set(itm, EINA_TRUE);
	elm_genlist_item_selected_set(itm, !selected); // preserve selection
}

static void
_list_contract_request(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	Eina_Bool selected = elm_genlist_item_selected_get(itm);
	elm_genlist_item_expanded_set(itm, EINA_FALSE);
	elm_genlist_item_selected_set(itm, !selected); // preserve selection
}

static void
_modlist_expanded(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	mod_t *mod = elm_object_item_data_get(itm);
	sp_ui_t *ui = data;

	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		// ignore dummy system ports
		if(mod->system.source && (port->direction == PORT_DIRECTION_INPUT) )
			continue;
		if(mod->system.sink && (port->direction == PORT_DIRECTION_OUTPUT) )
			continue;

		// only add control, audio, cv ports
		Elm_Object_Item *elmnt;
		elmnt = elm_genlist_item_append(ui->modlist, ui->stditc, port, itm,
			ELM_GENLIST_ITEM_NONE, NULL, NULL);
		elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_NONE);
	}

	// separator
	Elm_Object_Item *elmnt;
	elmnt = elm_genlist_item_append(ui->modlist, ui->stditc, NULL, itm,
		ELM_GENLIST_ITEM_NONE, NULL, NULL);
	elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_NONE);
}

static void
_modlist_contracted(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	mod_t *mod = elm_object_item_data_get(itm);
	sp_ui_t *ui = data;

	// clear items
	elm_genlist_item_subitems_clear(itm);
}

static char * 
_modlist_label_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;
	const LilvPlugin *plug = mod->plug;

	if(!strcmp(part, "elm.text"))
	{
		LilvNode *name_node = lilv_plugin_get_name(plug);
		const char *name_str = lilv_node_as_string(name_node);
		lilv_node_free(name_node);

		return strdup(name_str);
	}
	else if(!strcmp(part, "elm.text.sub"))
	{
		const LilvNode *uri_node = lilv_plugin_get_uri(plug);
		const char *uri_str = lilv_node_as_string(uri_node);

		return strdup(uri_str);
	}
	else
		return NULL;
}

static void
_modlist_icon_clicked(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;
	
	size_t size = sizeof(transmit_module_del_t);
	transmit_module_del_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_del_fill(&ui->regs, &ui->forge, trans, size, mod->uid);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_patches_update(sp_ui_t *ui)
{
	int count [PORT_DIRECTION_NUM][PORT_TYPE_NUM];
	// clear counters
	memset(&count, 0, PORT_DIRECTION_NUM*PORT_TYPE_NUM*sizeof(int));

	// count input|output ports per type
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(ui->modlist);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != ui->moditc)
			continue; // ignore port items

		mod_t *mod = elm_object_item_data_get(itm);
		if(!mod->selected)
			continue; // ignore unselected mods

		for(int i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];
			if(!port->selected)
				continue; // ignore unselected ports

			count[port->direction][port->type] += 1;
		}
	}

	// set dimension of patchers
	for(int t=0; t<PORT_TYPE_NUM; t++)
	{
		patcher_object_dimension_set(ui->matrix[t], 
			count[PORT_DIRECTION_OUTPUT][t], // sources
			count[PORT_DIRECTION_INPUT][t]); // sinks
	}

	// clear counters
	memset(&count, 0, PORT_DIRECTION_NUM*PORT_TYPE_NUM*sizeof(int));

	// populate patchers
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(ui->modlist);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != ui->moditc)
			continue; // ignore port items

		mod_t *mod = elm_object_item_data_get(itm);
		if(!mod->selected)
			continue; // ignore unselected mods

		for(int i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];
			if(!port->selected)
				continue; // ignore unselected ports

			LilvNode *name_node = lilv_port_get_name(mod->plug, port->tar);
			const char *name_str = lilv_node_as_string(name_node);
			
			if(port->direction == PORT_DIRECTION_OUTPUT) // source
			{
				patcher_object_source_data_set(ui->matrix[port->type],
					count[port->direction][port->type], port);
				patcher_object_source_color_set(ui->matrix[port->type],
					count[port->direction][port->type], mod->col);
				patcher_object_source_label_set(ui->matrix[port->type],
					count[port->direction][port->type], name_str);
			}
			else // sink
			{
				patcher_object_sink_data_set(ui->matrix[port->type],
					count[port->direction][port->type], port);
				patcher_object_sink_color_set(ui->matrix[port->type],
					count[port->direction][port->type], mod->col);
				patcher_object_sink_label_set(ui->matrix[port->type],
					count[port->direction][port->type], name_str);
			}
			
			lilv_node_free(name_node);
		
			count[port->direction][port->type] += 1;
		}
	}

	for(int t=0; t<PORT_TYPE_NUM; t++)
		patcher_object_realize(ui->matrix[t]);
}

static void
_modlist_check_changed(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	mod->selected = elm_check_state_get(obj);
	_patches_update(ui);
}

static Evas_Object *
_modlist_content_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;

	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	Evas_Object *lay = elm_layout_add(obj);
	elm_layout_file_set(lay, "/usr/local/share/synthpod/synthpod.edj",
		"/synthpod/modlist/module");
	evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(lay);

	LilvNode *name_node = lilv_plugin_get_name(mod->plug);
	const char *name_str = lilv_node_as_string(name_node);
	lilv_node_free(name_node);
	elm_layout_text_set(lay, "elm.text", name_str);
	
	char col [7];
	sprintf(col, "col,%02i", mod->col);
	elm_layout_signal_emit(lay, col, PATCHER_UI);

	Evas_Object *check = elm_check_add(lay);
	elm_check_state_set(check, mod->selected);
	evas_object_smart_callback_add(check, "changed", _modlist_check_changed, mod);
	evas_object_show(check);
	elm_layout_icon_set(lay, check);
	elm_layout_content_set(lay, "elm.swallow.icon", check);

	if(!mod->system.source && !mod->system.sink)
	{
		Evas_Object *icon = elm_icon_add(lay);
		elm_icon_standard_set(icon, "close");
		evas_object_smart_callback_add(icon, "clicked", _modlist_icon_clicked, mod);
		evas_object_show(icon);
		elm_layout_content_set(lay, "elm.swallow.end", icon);
	}
	else
		; // system mods cannot be removed

	return lay;
}

static void
_ui_update_request(mod_t *mod, uint32_t index)
{
	sp_ui_t *ui = mod->ui;

	size_t size = sizeof(transmit_port_refresh_t);
	transmit_port_refresh_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_refresh_fill(&ui->regs, &ui->forge, trans, size, mod->uid, index);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_ui_write_function(LV2UI_Controller controller, uint32_t port,
	uint32_t size, uint32_t protocol, const void *buffer)
{
	mod_t *mod = controller;
	sp_ui_t *ui = mod->ui;
	port_t *tar = &mod->ports[port];

	// ignore output ports
	if(tar->direction != PORT_DIRECTION_INPUT)
	{
		fprintf(stderr, "_ui_write_function: UI can only write to input port\n");
		return;
	}

	// handle special meaning of protocol=0
	if(protocol == 0)
		protocol = ui->regs.port.float_protocol.urid;

	if(protocol == ui->regs.port.float_protocol.urid)
	{
		const float *val = buffer;
		size_t size = sizeof(transfer_float_t);
		transfer_float_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transfer_float_fill(&ui->regs, &ui->forge, trans, mod->uid, tar->index, val);
			_sp_ui_to_app_advance(ui, size);
		}
	}
	else if(protocol == ui->regs.port.atom_transfer.urid)
	{
		const LV2_Atom *atom = buffer;
		size_t size = sizeof(transfer_atom_t) + sizeof(LV2_Atom) + atom->size;
		transfer_atom_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transfer_atom_fill(&ui->regs, &ui->forge, trans, mod->uid, tar->index, atom);
			_sp_ui_to_app_advance(ui, size);
		}
	}
	else if(protocol == ui->regs.port.event_transfer.urid)
	{
		const LV2_Atom *atom = buffer;
		size_t size = sizeof(transfer_atom_t) + sizeof(LV2_Atom) + atom->size;
		transfer_atom_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transfer_event_fill(&ui->regs, &ui->forge, trans, mod->uid, tar->index, atom);
			_sp_ui_to_app_advance(ui, size);
		}
	}
	else if(protocol == ui->regs.port.peak_protocol.urid)
		; // makes no sense
}

static void
_eo_ui_write_function(LV2UI_Controller controller, uint32_t port,
	uint32_t size, uint32_t protocol, const void *buffer)
{
	// to rt-thread
	_ui_write_function(controller, port, size, protocol, buffer);

	// to StdUI
	_std_port_event(controller, port, size, protocol, buffer);
}

static void
_std_ui_write_function(LV2UI_Controller controller, uint32_t port,
	uint32_t size, uint32_t protocol, const void *buffer)
{
	// to rt-thread
	_ui_write_function(controller, port, size, protocol, buffer);

	// to EoUI
	_eo_port_event(controller, port, size, protocol, buffer);
}


static void
_patched_changed(void *data, Evas_Object *obj, void *event)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	port->selected = elm_check_state_get(obj);
	_patches_update(ui);
}

static void
_check_changed(void *data, Evas_Object *obj, void *event)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	float val = elm_check_state_get(obj);

	_std_ui_write_function(mod, port->index, sizeof(float),
		ui->regs.port.float_protocol.urid, &val);
}

static void
_spinner_changed(void *data, Evas_Object *obj, void *event)
{
	Elm_Object_Item *itm = event;
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	float val = elm_spinner_value_get(obj);

	_std_ui_write_function(mod, port->index, sizeof(float),
		ui->regs.port.float_protocol.urid, &val);
}

static void
_sldr_changed(void *data, Evas_Object *obj, void *event)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	float val = elm_slider_value_get(obj);

	_std_ui_write_function(mod, port->index, sizeof(float),
		ui->regs.port.float_protocol.urid, &val);
}

static Evas_Object * 
_modlist_std_content_get(void *data, Evas_Object *obj, const char *part)
{
	if(!data)
		return NULL;
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;
	
	if(strcmp(part, "elm.swallow.content"))
		return NULL;
	
	Evas_Object *lay = elm_layout_add(obj);
	elm_layout_file_set(lay, "/usr/local/share/synthpod/synthpod.edj",
		"/synthpod/modlist/port");
	evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(lay);

	Evas_Object *patched = elm_check_add(lay);
	evas_object_smart_callback_add(patched, "changed", _patched_changed, port);
	evas_object_show(patched);
	elm_layout_content_set(lay, "elm.swallow.icon", patched);
	
	Evas_Object *dir = edje_object_add(evas_object_evas_get(lay));
	edje_object_file_set(dir, "/usr/local/share/synthpod/synthpod.edj",
		"/synthpod/patcher/port");
	char col [7];
	sprintf(col, "col,%02i", mod->col);
	edje_object_signal_emit(dir, col, PATCHER_UI);
	evas_object_show(dir);
	if(port->direction == PORT_DIRECTION_OUTPUT)
	{
		edje_object_signal_emit(dir, "source", PATCHER_UI);
		elm_layout_content_set(lay, "elm.swallow.source", dir);
	}
	else
	{
		edje_object_signal_emit(dir, "sink", PATCHER_UI);
		elm_layout_content_set(lay, "elm.swallow.sink", dir);
	}

	const char *type_str = NULL;
	const LilvNode *name_node = lilv_port_get_name(mod->plug, port->tar);
	type_str = lilv_node_as_string(name_node);
	elm_layout_text_set(lay, "elm.text", type_str);

	Evas_Object *child = NULL;
	if(port->type == PORT_TYPE_CONTROL)
	{
		int integer = lilv_port_has_property(mod->plug, port->tar, ui->regs.port.integer.node);
		int toggled = lilv_port_has_property(mod->plug, port->tar, ui->regs.port.toggled.node);
		float step_val = integer ? 1.f : (port->max - port->min) / 1000;
		float val = port->dflt;

		if(toggled)
		{
			Evas_Object *check = elm_check_add(lay);
			elm_check_state_set(check, val > 0.f ? EINA_TRUE : EINA_FALSE);
			elm_object_style_set(check, "toggle");
			evas_object_smart_callback_add(check, "changed", _check_changed, port);

			child = check;
		}
		else if(port->points)
		{
			Evas_Object *spin = elm_spinner_add(lay);
			elm_spinner_min_max_set(spin, port->min, port->max);
			elm_spinner_value_set(spin, val);
			elm_spinner_step_set(spin, 1);
			elm_spinner_editable_set(spin, EINA_FALSE);
			elm_spinner_wrap_set(spin, EINA_FALSE);
			elm_spinner_base_set(spin, 0);
			elm_spinner_round_set(spin, 1);
			elm_object_style_set(spin, "vertical");
			LILV_FOREACH(scale_points, itr, port->points)
			{
				const LilvScalePoint *point = lilv_scale_points_get(port->points, itr);
				const LilvNode *label_node = lilv_scale_point_get_label(point);
				const LilvNode *val_node = lilv_scale_point_get_value(point);

				elm_spinner_special_value_add(spin,
					lilv_node_as_float(val_node), lilv_node_as_string(label_node));
			}
			evas_object_smart_callback_add(spin, "changed", _spinner_changed, port);

			child = spin;
		}
		else // integer or float
		{
			Evas_Object *sldr = elm_slider_add(lay);
			elm_slider_horizontal_set(sldr, EINA_TRUE);
			elm_slider_unit_format_set(sldr, integer ? "%.0f" : "%.4f");
			elm_slider_min_max_set(sldr, port->min, port->max);
			elm_slider_value_set(sldr, val);
			elm_slider_step_set(sldr, step_val);
			evas_object_smart_callback_add(sldr, "changed", _sldr_changed, port);

			child = sldr;
		}
	}
	else if(port->type == PORT_TYPE_AUDIO
		|| port->type == PORT_TYPE_CV)
	{
		Evas_Object *prog = elm_progressbar_add(lay);
		elm_progressbar_horizontal_set(prog, EINA_TRUE);
		elm_progressbar_unit_format_set(prog, NULL);
		elm_progressbar_value_set(prog, 0.f);

		child = prog;
	}
	else if(port->type == PORT_TYPE_ATOM)
	{
		Evas_Object *lbl = elm_label_add(lay);
		elm_object_text_set(lbl, "Atom Port");

		child = lbl;
	}

	if(child)
	{
		elm_object_disabled_set(child, port->direction == PORT_DIRECTION_OUTPUT);
		evas_object_show(child);
		elm_layout_content_set(lay, "elm.swallow.content", child);
	}

	if(port->selected)
		elm_check_state_set(patched, EINA_TRUE);

	// subscribe to port
	const uint32_t i = port->index;
	if(port->type == PORT_TYPE_CONTROL)
		_port_subscribe(mod, i, ui->regs.port.float_protocol.urid, NULL);
	else if(port->type == PORT_TYPE_AUDIO)
		_port_subscribe(mod, i, ui->regs.port.peak_protocol.urid, NULL);
	else if(port->type == PORT_TYPE_CV)
		_port_subscribe(mod, i, ui->regs.port.peak_protocol.urid, NULL);
	/*
	else if(port->type == PORT_TYPE_ATOM)
	{
		if(port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE)
			_port_subscribe(mod, i, ui->regs.port.event_transfer.urid, NULL);
		else
			_port_subscribe(mod, i, ui->regs.port.atom_transfer.urid, NULL);
	}
	*/
	_ui_update_request(mod, port->index);

	port->std.widget = child;
	return lay;
}

static void
_modlist_std_del(void *data, Evas_Object *obj)
{
	if(!data) // empty element
		return;
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	port->std.widget = NULL;

	// unsubscribe from port
	const uint32_t i = port->index;
	if(port->type == PORT_TYPE_CONTROL)
		_port_unsubscribe(mod, i, ui->regs.port.float_protocol.urid, NULL);
	else if(port->type == PORT_TYPE_AUDIO)
		_port_unsubscribe(mod, i, ui->regs.port.peak_protocol.urid, NULL);
	else if(port->type == PORT_TYPE_CV)
		_port_unsubscribe(mod, i, ui->regs.port.peak_protocol.urid, NULL);
	else if(port->type == PORT_TYPE_ATOM)
	{
		if(port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE)
			_port_unsubscribe(mod, i, ui->regs.port.event_transfer.urid, NULL);
		else
			_port_unsubscribe(mod, i, ui->regs.port.atom_transfer.urid, NULL);
	}
}

static void
_modlist_del(void *data, Evas_Object *obj)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	// nothing
}

static char *
_modgrid_label_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;
	const LilvPlugin *plug = mod->plug;
	
	if(!strcmp(part, "elm.text"))
	{
		LilvNode *name_node = lilv_plugin_get_name(plug);
		const char *name_str = lilv_node_as_string(name_node);
		lilv_node_free(name_node);

		return strdup(name_str);
	}

	return NULL;
}

static Evas_Object *
_modgrid_content_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	if(!strcmp(part, "elm.swallow.icon"))
	{
		if(mod->eo.ui)
		{
			const LilvNode *plugin_uri = lilv_plugin_get_uri(mod->plug);
			const char *plugin_string = lilv_node_as_string(plugin_uri);

			//printf("has Eo UI\n");
			const LilvNode *ui_uri = lilv_ui_get_uri(mod->eo.ui);
			const LilvNode *bundle_uri = lilv_ui_get_bundle_uri(mod->eo.ui);
			const LilvNode *binary_uri = lilv_ui_get_binary_uri(mod->eo.ui);

			const char *ui_string = lilv_node_as_string(ui_uri);
			const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));
			const char *binary_path = lilv_uri_to_path(lilv_node_as_string(binary_uri));

			//printf("ui_string: %s\n", ui_string);
			//printf("bundle_path: %s\n", bundle_path);
			//printf("binary_path: %s\n", binary_path);

			uv_dlopen(binary_path, &mod->eo.lib); //TODO check
			
			LV2UI_DescriptorFunction ui_descfunc = NULL;
			uv_dlsym(&mod->eo.lib, "lv2ui_descriptor", (void **)&ui_descfunc);

			if(ui_descfunc)
			{
				mod->eo.descriptor = NULL;
				mod->eo.widget = NULL;

				for(int i=0; 1; i++)
				{
					const LV2UI_Descriptor *ui_desc = ui_descfunc(i);
					if(!ui_desc) // end
						break;
					else if(!strcmp(ui_desc->URI, ui_string))
					{
						mod->eo.descriptor = ui_desc;
						break;
					}
				}
			
				// get UI extension data
				if(mod->eo.descriptor && mod->eo.descriptor->extension_data)
				{
					mod->eo.idle_interface = mod->eo.descriptor->extension_data(
						LV2_UI__idleInterface);
				}

				// instantiate UI
				if(mod->eo.descriptor && mod->eo.descriptor->instantiate)
				{
					mod->eo.handle = mod->eo.descriptor->instantiate(
						mod->eo.descriptor,
						plugin_string,
						bundle_path,
						_eo_ui_write_function,
						mod,
						(void **)&(mod->eo.widget),
						mod->features);
				}

				// subscribe automatically to all non-atom ports by default
				for(int i=0; i<mod->num_ports; i++)
				{
					port_t *port = &mod->ports[i];

					if(port->type == PORT_TYPE_CONTROL)
					{
						_port_subscribe(mod, i, ui->regs.port.float_protocol.urid, NULL);
						// initialize EoUI
						float val = port->dflt;
						_eo_port_event(mod, i, sizeof(float), ui->regs.port.float_protocol.urid, &val);
						_ui_update_request(mod, port->index);
					}
					else if(port->type == PORT_TYPE_AUDIO)
						_port_subscribe(mod, i, ui->regs.port.peak_protocol.urid, NULL);
					else if(port->type == PORT_TYPE_CV)
						_port_subscribe(mod, i, ui->regs.port.peak_protocol.urid, NULL);
				}

				// subscribe manually for port notifications
				const LilvNode *plug_uri_node = lilv_plugin_get_uri(mod->plug);
				LilvNode *lv2_symbol = lilv_new_uri(ui->world, LV2_CORE__symbol);
				LilvNode *lv2_index = lilv_new_uri(ui->world, LV2_CORE__index);
				LilvNode *ui_plugin = lilv_new_uri(ui->world, LV2_UI__plugin);
				LilvNode *ui_prot = lilv_new_uri(ui->world, LV2_UI_PREFIX"protocol");

				LilvNodes *notifs = lilv_world_find_nodes(ui->world,
					lilv_ui_get_uri(mod->eo.ui), ui->regs.port.notification.node, NULL);
				LILV_FOREACH(nodes, n, notifs)
				{
					const LilvNode *notif = lilv_nodes_get(notifs, n);
					const LilvNode *sym = lilv_world_get(ui->world, notif, lv2_symbol, NULL);
					const LilvNode *ind = lilv_world_get(ui->world, notif, lv2_index, NULL);
					const LilvNode *plug = lilv_world_get(ui->world, notif, ui_plugin, NULL);
					const LilvNode *prot = lilv_world_get(ui->world, notif, ui_prot, NULL);

					if(plug && !lilv_node_equals(plug, plug_uri_node))
						continue; // notification not for this plugin 

					uint32_t index = LV2UI_INVALID_PORT_INDEX;
					if(ind)
					{
						index = lilv_node_as_int(ind);
					}
					else if(sym)
					{
						const LilvPort *port = lilv_plugin_get_port_by_symbol(mod->plug, sym);
						index = lilv_port_get_index(mod->plug, port);
					}

					if(index != LV2UI_INVALID_PORT_INDEX)
					{
						if(lilv_node_equals(prot, ui->regs.port.float_protocol.node))
							_port_subscribe(mod, index, ui->regs.port.float_protocol.urid, NULL);
						else if(lilv_node_equals(prot, ui->regs.port.peak_protocol.node))
							_port_subscribe(mod, index, ui->regs.port.peak_protocol.urid, NULL);
						else if(lilv_node_equals(prot, ui->regs.port.atom_transfer.node))
							_port_subscribe(mod, index, ui->regs.port.atom_transfer.urid, NULL);
						else if(lilv_node_equals(prot, ui->regs.port.event_transfer.node))
							_port_subscribe(mod, index, ui->regs.port.event_transfer.urid, NULL);
						else
							; //TODO protocol not supported

						printf("port has notification for: %s %s %u %u %u\n",
							lilv_node_as_string(sym),
							lilv_node_as_uri(prot),
							index,
							ui->regs.port.atom_transfer.urid,
							ui->regs.port.event_transfer.urid);
					}
				}
				lilv_nodes_free(notifs);
				lilv_node_free(lv2_symbol);
				lilv_node_free(lv2_index);
				lilv_node_free(ui_plugin);
				lilv_node_free(ui_prot);

				return mod->eo.widget;
			}
		}
	}
	else if(!strcmp(part, "elm.swallow.end"))
	{
		/* FIXME
		Evas_Object *bg = elm_bg_add(obj);
		elm_bg_color_set(bg, mod->col[0], mod->col[1], mod->col[2]);
		evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(bg, 1.f, EVAS_HINT_FILL);
		evas_object_size_hint_min_set(bg, 8, 64);
		evas_object_size_hint_max_set(bg, 8, 64);
		evas_object_show(bg);

		return bg;
		*/
		return NULL;
	}
	
	return NULL;
}

static void
_modgrid_del(void *data, Evas_Object *obj)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	//TODO this is futile, as module has already been removed in app
	// unsubscribe from all ports
	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_unsubscribe(mod, i, ui->regs.port.float_protocol.urid, NULL);
		else if(port->type == PORT_TYPE_AUDIO)
			_port_unsubscribe(mod, i, ui->regs.port.peak_protocol.urid, NULL);
		else if(port->type == PORT_TYPE_CV)
			_port_unsubscribe(mod, i, ui->regs.port.peak_protocol.urid, NULL);
		else if(port->type == PORT_TYPE_ATOM)
		{
			if(port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) 
				_port_unsubscribe(mod, i, ui->regs.port.event_transfer.urid, NULL);
			else
				_port_unsubscribe(mod, i, ui->regs.port.atom_transfer.urid, NULL);
		}
	}

	// cleanup EoUI
	if(mod->eo.ui)
	{
		if(  mod->eo.descriptor
			&& mod->eo.descriptor->cleanup
			&& mod->eo.handle)
		{
			mod->eo.descriptor->cleanup(mod->eo.handle);
		}

		uv_dlclose(&mod->eo.lib);
	}
	
	// clear parameters
	mod->eo.descriptor = NULL;
	mod->eo.handle = NULL;
	mod->eo.widget = NULL;
}

static void
_matrix_connect_request(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	patcher_event_t *ev = event_info;
	patcher_event_t *source = &ev[0];
	patcher_event_t *sink = &ev[1];
	port_t *source_port = source->ptr;
	port_t *sink_port = sink->ptr;

	/*
	printf("_matrix_connect_request: %p (%i) %p (%i)\n",
		source->ptr, source->index,
		sink->ptr, sink->index);
	*/

	size_t size = sizeof(transmit_port_connected_t);
	transmit_port_connected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_connected_fill(&ui->regs, &ui->forge, trans, size,
			source_port->mod->uid, source_port->index,
			sink_port->mod->uid, sink_port->index, 1);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_matrix_disconnect_request(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	patcher_event_t *ev = event_info;
	patcher_event_t *source = &ev[0];
	patcher_event_t *sink = &ev[1];
	port_t *source_port = source->ptr;
	port_t *sink_port = sink->ptr;

	/*
	printf("_matrix_disconnect_request: %p (%i) %p (%i)\n",
		source->ptr, source->index,
		sink->ptr, sink->index);
	*/

	size_t size = sizeof(transmit_port_connected_t);
	transmit_port_connected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_connected_fill(&ui->regs, &ui->forge, trans, size,
			source_port->mod->uid, source_port->index,
			sink_port->mod->uid, sink_port->index, 0);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_matrix_realize_request(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	patcher_event_t *ev = event_info;
	patcher_event_t *source = &ev[0];
	patcher_event_t *sink = &ev[1];
	port_t *source_port = source->ptr;
	port_t *sink_port = sink->ptr;

	/*
	printf("_matrix_realize_request: %p (%i) %p (%i)\n",
		source->ptr, source->index,
		sink->ptr, sink->index);
	*/
	
	size_t size = sizeof(transmit_port_connected_t);
	transmit_port_connected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_connected_fill(&ui->regs, &ui->forge, trans, size,
			source_port->mod->uid, source_port->index,
			sink_port->mod->uid, sink_port->index, -1);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_resize(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	Evas_Coord w, h;
	evas_object_geometry_get(obj, NULL, NULL, &w, &h);

	evas_object_resize(ui->plugpane, w, h);
}

sp_ui_t *
sp_ui_new(Evas_Object *win, sp_ui_driver_t *driver, void *data)
{
	if(!driver || !data)
		return NULL;

	sp_ui_t *ui = calloc(1, sizeof(sp_ui_t));
	if(!ui)
		return NULL;

	ui->driver = driver;
	ui->data = data;
	ui->win = win;
	
	lv2_atom_forge_init(&ui->forge, ui->driver->map);

	ui->world = lilv_world_new();
	lilv_world_load_all(ui->world);
	ui->plugs = lilv_world_get_all_plugins(ui->world);

	sp_regs_init(&ui->regs, ui->world, driver->map);
	
	ui->plugpane = elm_panes_add(ui->win);
	elm_panes_horizontal_set(ui->plugpane, EINA_FALSE);
	elm_panes_content_right_size_set(ui->plugpane, 0.25);
	evas_object_size_hint_weight_set(ui->plugpane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->plugpane, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->plugpane);

	_resize(ui, NULL, ui->win, NULL);
	evas_object_event_callback_add(ui->win, EVAS_CALLBACK_RESIZE, _resize, ui);

	ui->pluglist = elm_genlist_add(ui->plugpane);
	evas_object_smart_callback_add(ui->pluglist, "activated",
		_pluglist_activated, ui);
	evas_object_smart_callback_add(ui->pluglist, "expand,request",
		_list_expand_request, ui);
	evas_object_smart_callback_add(ui->pluglist, "contract,request",
		_list_contract_request, ui);
	//evas_object_smart_callback_add(ui->pluglist, "expanded",
	//	_pluglist_expanded, ui);
	//evas_object_smart_callback_add(ui->pluglist, "contracted",
	//	_pluglist_contracted, ui);
	evas_object_data_set(ui->pluglist, "ui", ui);
	evas_object_size_hint_weight_set(ui->pluglist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->pluglist, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->pluglist);
	elm_object_part_content_set(ui->plugpane, "right", ui->pluglist);

	ui->plugitc = elm_genlist_item_class_new();
	//ui->plugitc->item_style = "double_label";
	ui->plugitc->item_style = "default";
	ui->plugitc->func.text_get = _pluglist_label_get;
	ui->plugitc->func.content_get = NULL;
	ui->plugitc->func.state_get = NULL;
	ui->plugitc->func.del = NULL;

	LILV_FOREACH(plugins, itr, ui->plugs)
	{
		const LilvPlugin *plug = lilv_plugins_get(ui->plugs, itr);
		elm_genlist_item_append(ui->pluglist, ui->plugitc, plug, NULL,
			ELM_GENLIST_ITEM_NONE, NULL, NULL);
	}

	ui->modpane = elm_panes_add(ui->plugpane);
	elm_panes_horizontal_set(ui->modpane, EINA_FALSE);
	elm_panes_content_left_size_set(ui->modpane, 0.3);
	evas_object_size_hint_weight_set(ui->modpane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->modpane, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->modpane);
	elm_object_part_content_set(ui->plugpane, "left", ui->modpane);
	
	ui->patchpane = elm_panes_add(ui->modpane);
	elm_panes_horizontal_set(ui->patchpane, EINA_TRUE);
	elm_panes_content_left_size_set(ui->patchpane, 0.8);
	evas_object_size_hint_weight_set(ui->patchpane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->patchpane, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->patchpane);
	elm_object_part_content_set(ui->modpane, "right", ui->patchpane);

	ui->patchbox = elm_box_add(ui->patchpane);
	elm_box_horizontal_set(ui->patchbox, EINA_TRUE);
	elm_box_homogeneous_set(ui->patchbox, EINA_FALSE);
	elm_box_padding_set(ui->patchbox, 10, 10);
	evas_object_size_hint_weight_set(ui->patchbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->patchbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->patchbox);
	elm_object_part_content_set(ui->patchpane, "right", ui->patchbox);

	for(int t=0; t<PORT_TYPE_NUM; t++)
	{
		Evas_Object *matrix = patcher_object_add(ui->patchbox);
		evas_object_smart_callback_add(matrix, "connect,request",
			_matrix_connect_request, ui);
		evas_object_smart_callback_add(matrix, "disconnect,request",
			_matrix_disconnect_request, ui);
		evas_object_smart_callback_add(matrix, "realize,request",
			_matrix_realize_request, ui);
		evas_object_size_hint_weight_set(matrix, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(matrix, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(matrix);
		elm_box_pack_end(ui->patchbox, matrix);
		ui->matrix[t] = matrix;
	}

	ui->modlist = elm_genlist_add(ui->modpane);
	elm_genlist_select_mode_set(ui->modlist, ELM_OBJECT_SELECT_MODE_NONE);
	//elm_genlist_reorder_mode_set(ui->modlist, EINA_TRUE);
	evas_object_smart_callback_add(ui->modlist, "expand,request",
		_list_expand_request, ui);
	evas_object_smart_callback_add(ui->modlist, "contract,request",
		_list_contract_request, ui);
	evas_object_smart_callback_add(ui->modlist, "expanded",
		_modlist_expanded, ui);
	evas_object_smart_callback_add(ui->modlist, "contracted",
		_modlist_contracted, ui);
	evas_object_data_set(ui->modlist, "ui", ui);
	evas_object_size_hint_weight_set(ui->modlist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->modlist, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->modlist);
	elm_object_part_content_set(ui->modpane, "left", ui->modlist);
	
	ui->moditc = elm_genlist_item_class_new();
	//ui->moditc->item_style = "double_label";
	ui->moditc->item_style = "full";
	ui->moditc->func.text_get = _modlist_label_get;
	ui->moditc->func.content_get = _modlist_content_get;
	ui->moditc->func.state_get = NULL;
	ui->moditc->func.del = _modlist_del;

	ui->stditc = elm_genlist_item_class_new();
	ui->stditc->item_style = "full";
	ui->stditc->func.text_get = NULL;
	ui->stditc->func.content_get = _modlist_std_content_get;
	ui->stditc->func.state_get = NULL;
	ui->stditc->func.del = _modlist_std_del;

	ui->modgrid = elm_gengrid_add(ui->patchpane);
	elm_gengrid_select_mode_set(ui->modgrid, ELM_OBJECT_SELECT_MODE_NONE);
	elm_gengrid_reorder_mode_set(ui->modgrid, EINA_TRUE);
	elm_gengrid_item_size_set(ui->modgrid, 400, 400);
	evas_object_data_set(ui->modgrid, "ui", ui);
	evas_object_size_hint_weight_set(ui->modgrid, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->modgrid, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->modgrid);
	elm_object_part_content_set(ui->patchpane, "left", ui->modgrid);

	ui->griditc = elm_gengrid_item_class_new();
	ui->griditc->item_style = "default";
	ui->griditc->func.text_get = _modgrid_label_get;
	ui->griditc->func.content_get = _modgrid_content_get;
	ui->griditc->func.state_get = NULL;
	ui->griditc->func.del = _modgrid_del;

	return ui;
}

Evas_Object *
sp_ui_widget_get(sp_ui_t *ui)
{
	return ui->plugpane;
}

static inline mod_t *
_sp_ui_mod_get(sp_ui_t *ui, u_id_t uid)
{
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(ui->modlist);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		mod_t *mod = elm_object_item_data_get(itm);
		if(mod && (mod->uid == uid))
			return mod;
	}

	return NULL;
}

static inline port_t *
_sp_ui_port_get(sp_ui_t *ui, u_id_t uid, uint32_t index)
{
	mod_t *mod = _sp_ui_mod_get(ui, uid);
	if(mod && (index < mod->num_ports) )
		return &mod->ports[index];
	
	return NULL;
}

void
sp_ui_from_app(sp_ui_t *ui, const LV2_Atom *atom)
{
	const transmit_t *transmit = (const transmit_t *)atom;
	LV2_URID protocol = transmit->protocol.body;

	if(protocol == ui->regs.synthpod.module_add.urid)
	{
		const transmit_module_add_t *trans = (const transmit_module_add_t *)atom;

		mod_t *mod = _sp_ui_mod_add(ui, trans->uri_str, trans->uid.body);
		if(mod)
		{
			if(mod->system.source || mod->system.sink)
			{
				mod->std.itm = elm_genlist_item_append(ui->modlist, ui->moditc, mod,
					NULL, ELM_GENLIST_ITEM_TREE, NULL, NULL);

				if(mod->system.sink)
					ui->sink_itm = mod->std.itm;
			}
			else
			{
				mod->std.itm = elm_genlist_item_insert_before(ui->modlist, ui->moditc, mod,
					NULL, ui->sink_itm, ELM_GENLIST_ITEM_TREE, NULL, NULL);
			}
		
			if(mod->eo.ui) // has EoUI
			{
				mod->eo.itm = elm_gengrid_item_append(ui->modgrid, ui->griditc, mod,
					NULL, NULL);
			}
		}
	}
	else if(protocol == ui->regs.synthpod.module_del.urid)
	{
		const transmit_module_del_t *trans = (const transmit_module_del_t *)atom;
		mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);

		// remove StdUI list item
		elm_genlist_item_expanded_set(mod->std.itm, EINA_FALSE);
		elm_object_item_del(mod->std.itm);
		mod->std.itm = NULL;

		// remove EoUI grid item, if present
		if(mod->eo.itm)
		{
			elm_object_item_del(mod->eo.itm);
			mod->eo.itm = NULL;
		}
		
		_sp_ui_mod_del(ui, mod);
	
		_patches_update(ui);
	}
	else if(protocol == ui->regs.synthpod.port_connected.urid)
	{
		const transmit_port_connected_t *trans = (const transmit_port_connected_t *)atom;
		port_t *src = _sp_ui_port_get(ui, trans->src_uid.body, trans->src_port.body);
		port_t *snk = _sp_ui_port_get(ui, trans->snk_uid.body, trans->snk_port.body);

		Evas_Object *matrix = ui->matrix[src->type];
		patcher_object_connected_set(matrix, src, snk,
			trans->state.body ? EINA_TRUE : EINA_FALSE);
	}
	else if(protocol == ui->regs.port.float_protocol.urid)
	{
		const transfer_float_t *trans = (const transfer_float_t *)atom;
		uint32_t port_index = trans->transfer.port.body;
		float value = trans->value.body;

		mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
		_eo_port_event(mod, port_index, sizeof(float), protocol, &value);
		_std_port_event(mod, port_index, sizeof(float), protocol, &value);
	}
	else if(protocol == ui->regs.port.peak_protocol.urid)
	{
		const transfer_peak_t *trans = (const transfer_peak_t *)atom;
		uint32_t port_index = trans->transfer.port.body;
		LV2UI_Peak_Data data = {
			.period_start = trans->period_start.body,
			.period_size = trans->period_size.body,
			.peak = trans->peak.body
		};

		mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
		_eo_port_event(mod, port_index, sizeof(LV2UI_Peak_Data), protocol, &data);
		_std_port_event(mod, port_index, sizeof(LV2UI_Peak_Data), protocol, &data);
	}
	else if(protocol == ui->regs.port.atom_transfer.urid)
	{
		const transfer_atom_t *trans = (const transfer_atom_t *)atom;
		uint32_t port_index = trans->transfer.port.body;
		const LV2_Atom *atom = trans->atom;
		uint32_t size = sizeof(LV2_Atom) + atom->size;

		mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
		_eo_port_event(mod, port_index, size, protocol, atom);
		_std_port_event(mod, port_index, size, protocol, &atom);
	}
	else if(protocol == ui->regs.port.event_transfer.urid)
	{
		const transfer_atom_t *trans = (const transfer_atom_t *)atom;
		uint32_t port_index = trans->transfer.port.body;
		const LV2_Atom *atom = trans->atom;
		uint32_t size = sizeof(LV2_Atom) + atom->size;

		mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
		_eo_port_event(mod, port_index, size, protocol, atom);
		_std_port_event(mod, port_index, size, protocol, &atom);
	}
}

void
sp_ui_resize(sp_ui_t *ui, int w, int h)
{
	evas_object_resize(ui->plugpane, w, h);
}

void
sp_ui_iterate(sp_ui_t *ui)
{
	ecore_main_loop_iterate();
}

void
sp_ui_run(sp_ui_t *ui)
{
	elm_run();
}

void
sp_ui_free(sp_ui_t *ui)
{
	if(!ui)
		return;

	elm_gengrid_clear(ui->modgrid);
	evas_object_del(ui->modgrid);

	elm_genlist_clear(ui->modlist);
	evas_object_del(ui->modlist);

	elm_genlist_clear(ui->pluglist);
	evas_object_del(ui->pluglist);

	elm_box_clear(ui->patchbox);
	evas_object_del(ui->patchbox);

	evas_object_del(ui->patchpane);
	evas_object_del(ui->modpane);
	evas_object_event_callback_del(ui->win, EVAS_CALLBACK_RESIZE, _resize);
	evas_object_del(ui->plugpane);
	
	elm_genlist_item_class_free(ui->plugitc);
	elm_gengrid_item_class_free(ui->griditc);
	elm_genlist_item_class_free(ui->moditc);
	elm_genlist_item_class_free(ui->stditc);
	
	sp_regs_deinit(&ui->regs);

	lilv_world_free(ui->world);

	free(ui);
}
