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
#include <smart_slider.h>
#include <smart_meter.h>
#include <smart_spinner.h>
#include <smart_toggle.h>
#include <lv2_external_ui.h> // kxstudio kx-ui extension

#define NUM_UI_FEATURES 12
#define MODLIST_UI "/synthpod/modlist/ui"
#define MODGRID_UI "/synthpod/modgrid/ui"

typedef struct _mod_t mod_t;
typedef struct _port_t port_t;

typedef enum _plug_info_type_t plug_info_type_t;
typedef struct _plug_info_t plug_info_t;

enum _plug_info_type_t {
	PLUG_INFO_TYPE_NAME								= 0,
	PLUG_INFO_TYPE_URI								= 1,
	PLUG_INFO_TYPE_PROJECT						= 2,
	PLUG_INFO_TYPE_BUNDLE_URI					= 3,
	PLUG_INFO_TYPE_AUTHOR_NAME				= 4,
	PLUG_INFO_TYPE_AUTHOR_EMAIL				= 5,
	PLUG_INFO_TYPE_AUTHOR_HOMEPAGE		= 6	,

	PLUG_INFO_TYPE_MAX								= 7
};

struct _plug_info_t {
	plug_info_type_t type;
	const LilvPlugin *plug;
};

struct _mod_t {
	sp_ui_t *ui;
	u_id_t uid;
	int selected;
	
	// features
	LV2_Feature feature_list [NUM_UI_FEATURES];
	const LV2_Feature *features [NUM_UI_FEATURES + 1];

	// extension data
	LV2_Extension_Data_Feature ext_data;
	
	// self
	const LilvPlugin *plug;
	LilvUIs *all_uis;
	LilvNodes *presets;

	// ports
	uint32_t num_ports;
	port_t *ports;

	// UI color
	int col;

	// LV2UI_Port_Map extention
	LV2UI_Port_Map port_map;

	// LV2UI_Port_Subscribe extension
	LV2UI_Port_Subscribe port_subscribe;

	// opts
	struct {
		LV2_Options_Option options [2];
	} opts;

	// Eo UI
	struct {
		const LilvUI *ui;
		uv_lib_t lib;
		const LV2UI_Descriptor *descriptor;

		LV2UI_Handle handle;
		Evas_Object *widget;

		struct {
			Elm_Object_Item *itm;
		} embedded;

		struct {
			Evas_Object *win;
		} full;
	} eo;

	// custom UIs via the LV2UI_{Show,Idle}_Interface extensions
	struct {
		const LilvUI *ui;
		uv_lib_t lib;

		const LV2UI_Descriptor *descriptor;
		LV2UI_Handle handle;

		int dead;
		int visible;
		const LV2UI_Idle_Interface *idle_iface;
		const LV2UI_Show_Interface *show_iface;

		Ecore_Animator *anim;
	} show;

	// kx external-ui
	struct {
		const LilvUI *ui;
		uv_lib_t lib;

		int dead;
		const LV2UI_Descriptor *descriptor;
		LV2UI_Handle handle;

		LV2_External_UI_Host host;
		LV2_External_UI_Widget *widget;

		Ecore_Animator *anim;
	} kx;

	// X11 UI
	struct {
		const LilvUI *ui;
		uv_lib_t lib;

		const LV2UI_Descriptor *descriptor;
		LV2UI_Handle handle;
		const LV2UI_Idle_Interface *idle_iface;

		LV2UI_Resize host_resize_iface;
		const LV2UI_Resize *client_resize_iface;
		Evas_Object *win;
		Ecore_X_Window xwin;
		Ecore_Animator *anim;
	} x11;

	// TODO MOD UI
	// TODO GtkUI
	// TODO Qt4UI
	// TODO Qt5UI

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
	int subscriptions;

	const LilvPort *tar;
	uint32_t index;

	port_direction_t direction; // input, output
	port_type_t type; // audio, CV, control, atom
	port_buffer_type_t buffer_type; // none, sequence

	LilvScalePoints *points;
	char *unit;

	float dflt;
	float min;
	float max;

	float peak;
			
	struct {
		Evas_Object *widget;
	} std;
};

struct _sp_ui_t {
	sp_ui_driver_t *driver;
	void *data;

	int embedded;
	LilvWorld *world;
	const LilvPlugins *plugs;

	reg_t regs;
	LV2_Atom_Forge forge;

	Evas_Object *win;
	Evas_Object *theme;

	Evas_Object *mainpane;
	Evas_Object *leftpane;
	Evas_Object *plugpane;

	Evas_Object *plugbox;
	Evas_Object *plugentry;
	Evas_Object *pluglist;

	Evas_Object *patchgrid;
	Evas_Object *matrix[PORT_TYPE_NUM];

	Evas_Object *modlist;

	Evas_Object *modgrid;

	Elm_Genlist_Item_Class *plugitc;
	Elm_Genlist_Item_Class *moditc;
	Elm_Genlist_Item_Class *stditc;
	Elm_Genlist_Item_Class *psetitc;
	Elm_Genlist_Item_Class *psetitmitc;
	Elm_Gengrid_Item_Class *griditc;
	Elm_Genlist_Item_Class *patchitc;
		
	Elm_Object_Item *sink_itm;
};

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
		float val = *(float *)buf;
		int toggled = lilv_port_has_property(mod->plug, port->tar, ui->regs.port.toggled.node);

		// we should not set a value lower/higher than min/max for widgets
		if(val < port->min)
			val = port->min;
		if(val > port->max)
			val = port->max;

		if(toggled)
			smart_toggle_value_set(port->std.widget, val);
		else if(port->points)
			smart_spinner_value_set(port->std.widget, val);
		else // integer or float
			smart_slider_value_set(port->std.widget, val);
	}
	else if(protocol == ui->regs.port.peak_protocol.urid)
	{
		const LV2UI_Peak_Data *peak_data = buf;
		if(peak_data->peak > port->peak)
			port->peak = peak_data->peak;
		else
			port->peak *= 0.8;
		if(port->peak > 0.f)
			smart_meter_value_set(port->std.widget, port->peak);
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
		if(mod->eo.full.win)
			mod->eo.descriptor->port_event(mod->eo.handle, index, size, protocol, buf);
		else if(mod->eo.embedded.itm)
			mod->eo.descriptor->port_event(mod->eo.handle, index, size, protocol, buf);
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

static inline void
_ui_port_update_request(mod_t *mod, uint32_t index)
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

static inline void
_port_subscription_set(mod_t *mod, uint32_t index, uint32_t protocol, int state)
{
	sp_ui_t *ui = mod->ui;

	if(protocol == 0)
		protocol = ui->regs.port.float_protocol.urid;

	size_t size = sizeof(transmit_port_subscribed_t);
	transmit_port_subscribed_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_subscribed_fill(&ui->regs, &ui->forge, trans, size,
			mod->uid, index, protocol, state);
		_sp_ui_to_app_advance(ui, size);
	}

	if(state == 1)
		_ui_port_update_request(mod, index);
}

static uint32_t
_port_subscribe(LV2UI_Feature_Handle handle, uint32_t index, uint32_t protocol,
	const LV2_Feature *const *features)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;

	_port_subscription_set(mod, index, protocol, 1);

	return 0;
}

static uint32_t
_port_unsubscribe(LV2UI_Feature_Handle handle, uint32_t index, uint32_t protocol,
	const LV2_Feature *const *features)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;

	_port_subscription_set(mod, index, protocol, 0);

	return 0;
}

static inline void
_ui_mod_selected_request(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	size_t size = sizeof(transmit_module_selected_t);
	transmit_module_selected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_selected_fill(&ui->regs, &ui->forge, trans, size, mod->uid, -1);
		_sp_ui_to_app_advance(ui, size);
	}

	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		size_t size = sizeof(transmit_port_selected_t);
		transmit_port_selected_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transmit_port_selected_fill(&ui->regs, &ui->forge, trans, size, mod->uid, port->index, -1);
			_sp_ui_to_app_advance(ui, size);
		}
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
		assert(size == sizeof(float));
		size_t size = sizeof(transfer_float_t);
		transfer_float_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transfer_float_fill(&ui->regs, &ui->forge, trans, mod->uid, tar->index, buffer);
			_sp_ui_to_app_advance(ui, size);
		}
	}
	else if(protocol == ui->regs.port.atom_transfer.urid)
	{
		assert(size == sizeof(LV2_Atom) + ((LV2_Atom *)buffer)->size);
		size_t len = sizeof(transfer_atom_t) + lv2_atom_pad_size(size);
		transfer_atom_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			_sp_transfer_atom_fill(&ui->regs, &ui->forge, trans, mod->uid, tar->index, buffer);
			_sp_ui_to_app_advance(ui, len);
		}
	}
	else if(protocol == ui->regs.port.event_transfer.urid)
	{
		assert(size == sizeof(LV2_Atom) + ((LV2_Atom *)buffer)->size);
		size_t len = sizeof(transfer_atom_t) + lv2_atom_pad_size(size);
		transfer_atom_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			_sp_transfer_event_fill(&ui->regs, &ui->forge, trans, mod->uid, tar->index, buffer);
			_sp_ui_to_app_advance(ui, len);
		}
	}
	else if(protocol == ui->regs.port.peak_protocol.urid)
		; // makes no sense
}

static inline void
_show_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;

	//printf("_show_port_event: %u %u %u\n", index, size, protocol);

	if(  mod->show.ui
		&& mod->show.descriptor
		&& mod->show.descriptor->port_event
		&& mod->show.handle)
	{
		mod->show.descriptor->port_event(mod->show.handle,
			index, size, protocol, buf);
		if(protocol == ui->regs.port.float_protocol.urid)
		{
			// send it twice for plugins that expect "0" instead of float_protocol URID
			mod->show.descriptor->port_event(mod->show.handle,
				index, size, 0, buf);
		}
	}
}

static inline void
_kx_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;

	//printf("_kx_port_event: %u %u %u\n", index, size, protocol);

	if(  mod->kx.ui
		&& mod->kx.descriptor
		&& mod->kx.descriptor->port_event
		&& mod->kx.handle)
	{
		mod->kx.descriptor->port_event(mod->kx.handle,
			index, size, protocol, buf);
		if(protocol == ui->regs.port.float_protocol.urid)
		{
			// send it twice for plugins that expect "0" instead of float_protocol URID
			mod->kx.descriptor->port_event(mod->kx.handle,
				index, size, 0, buf);
		}
	}
}

static inline void
_x11_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;

	//printf("_x11_port_event: %u %u %u\n", index, size, protocol);

	if(  mod->x11.ui
		&& mod->x11.descriptor
		&& mod->x11.descriptor->port_event
		&& mod->x11.handle)
	{
		mod->x11.descriptor->port_event(mod->x11.handle,
			index, size, protocol, buf);
		if(protocol == ui->regs.port.float_protocol.urid)
		{
			// send it twice for plugins that expect "0" instead of float_protocol URID
			mod->x11.descriptor->port_event(mod->x11.handle,
				index, size, 0, buf);
		}
	}
}

static inline void
_ui_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;

	//printf("_ui_port_event: %u %u %u %u\n", mod->uid, index, size, protocol);

	_std_port_event(mod, index, size, protocol, buf);

	if(mod->eo.ui && mod->eo.descriptor)
		_eo_port_event(mod, index, size, protocol, buf);
	else if(mod->show.ui && mod->show.descriptor)
		_show_port_event(mod, index, size, protocol, buf);
	else if(mod->kx.ui && mod->kx.descriptor)
		_kx_port_event(mod, index, size, protocol, buf);
	else if(mod->x11.ui && mod->x11.descriptor)
		_x11_port_event(mod, index, size, protocol, buf);
}

static void
_ext_ui_write_function(LV2UI_Controller controller, uint32_t port,
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
	mod_t *mod = controller;

	// to rt-thread
	_ui_write_function(controller, port, size, protocol, buffer);

	if(mod->eo.ui && mod->eo.descriptor)
		_eo_port_event(controller, port, size, protocol, buffer);
	else if(mod->show.ui && mod->show.descriptor)
		_show_port_event(controller, port, size, protocol, buffer);
	else if(mod->kx.ui && mod->kx.descriptor)
		_kx_port_event(controller, port, size, protocol, buffer);
	else if(mod->x11.ui && mod->x11.descriptor)
		_x11_port_event(controller, port, size, protocol, buffer);
}

static void
_mod_subscription_set(mod_t *mod, const LilvUI *ui_ui, int state)
{
	sp_ui_t *ui = mod->ui;	

	// subscribe manually for port notifications
	const LilvNode *plug_uri_node = lilv_plugin_get_uri(mod->plug);
	LilvNode *lv2_symbol = lilv_new_uri(ui->world, LV2_CORE__symbol);
	LilvNode *lv2_index = lilv_new_uri(ui->world, LV2_CORE__index);
	LilvNode *ui_plugin = lilv_new_uri(ui->world, LV2_UI__plugin);
	LilvNode *ui_prot = lilv_new_uri(ui->world, LV2_UI_PREFIX"protocol");

	LilvNodes *notifs = lilv_world_find_nodes(ui->world,
		lilv_ui_get_uri(ui_ui), ui->regs.port.notification.node, NULL);
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
			port_t *port = &mod->ports[index];

			// protocol specified
			if(lilv_node_equals(prot, ui->regs.port.float_protocol.node))
				_port_subscription_set(mod, index, ui->regs.port.float_protocol.urid, state);
			else if(lilv_node_equals(prot, ui->regs.port.peak_protocol.node))
				_port_subscription_set(mod, index, ui->regs.port.peak_protocol.urid, state);
			else if(lilv_node_equals(prot, ui->regs.port.atom_transfer.node))
				_port_subscription_set(mod, index, ui->regs.port.atom_transfer.urid, state);
			else if(lilv_node_equals(prot, ui->regs.port.event_transfer.node))
				_port_subscription_set(mod, index, ui->regs.port.event_transfer.urid, state);

			// no protocol specified, we have to guess according to port type
			else if(port->type == PORT_TYPE_CONTROL)
				_port_subscription_set(mod, index, ui->regs.port.float_protocol.urid, state);
			else if(port->type == PORT_TYPE_AUDIO)
				_port_subscription_set(mod, index, ui->regs.port.peak_protocol.urid, state);
			else if(port->type == PORT_TYPE_CV)
				_port_subscription_set(mod, index, ui->regs.port.peak_protocol.urid, state);
			else if(port->type == PORT_TYPE_ATOM)
			{
				if(port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE)
					_port_subscription_set(mod, index, ui->regs.port.event_transfer.urid, state);
				else
					_port_subscription_set(mod, index, ui->regs.port.atom_transfer.urid, state);
			}

			//TODO handle ui:notifyType

			/*	
			printf("port has notification for: %s %s %u %u %u\n",
				lilv_node_as_string(sym),
				lilv_node_as_uri(prot),
				index,
				ui->regs.port.atom_transfer.urid,
				ui->regs.port.event_transfer.urid);
			*/
		}
	}
	lilv_nodes_free(notifs);
	lilv_node_free(lv2_symbol);
	lilv_node_free(lv2_index);
	lilv_node_free(ui_plugin);
	lilv_node_free(ui_prot);
}

static void
_show_ui_hide(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	// stop animator
	if(mod->show.anim)
	{
		ecore_animator_del(mod->show.anim);
		mod->show.anim = NULL;
	}

	// hide UI
	int res = 0;
	if(mod->show.show_iface && mod->show.show_iface->hide && mod->show.handle)
		res = mod->show.show_iface->hide(mod->show.handle);
	//TODO handle res != 0

	mod->show.visible = 0; // toggle visibility flag

	// unsubscribe all ports
	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 0);
	}
	
	// unsubscribe from notifications
	_mod_subscription_set(mod, mod->show.ui, 0);

	// call cleanup 
	if(mod->show.descriptor && mod->show.descriptor->cleanup && mod->show.handle)
		mod->show.descriptor->cleanup(mod->show.handle);
	mod->show.handle = NULL;
	mod->show.idle_iface = NULL;
	mod->show.show_iface = NULL;
}

static Eina_Bool
_show_ui_animator(void *data)
{
	mod_t *mod = data;

	int res = 0;
	if(mod->show.idle_iface && mod->show.idle_iface->idle && mod->show.handle)
		res = mod->show.idle_iface->idle(mod->show.handle);

	if(res) // UI requests to be hidden
	{
		_show_ui_hide(mod);

		return EINA_FALSE; // stop animator
	}

	return EINA_TRUE; // retrigger animator
}

static void
_show_ui_show(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	const LilvNode *plugin_uri = lilv_plugin_get_uri(mod->plug);
	const char *plugin_string = lilv_node_as_string(plugin_uri);

	const LilvNode *bundle_uri = lilv_ui_get_bundle_uri(mod->show.ui);
	const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));

	if(!mod->show.descriptor)
		return;

	// subscribe to ports
	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];
		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
	}

	// subscribe to notifications
	_mod_subscription_set(mod, mod->show.ui, 1);

	// instantiate UI
	void *dummy;
	mod->show.handle = mod->show.descriptor->instantiate(
		mod->show.descriptor,
		plugin_string,
		bundle_path,
		_ext_ui_write_function,
		mod,
		&dummy,
		mod->features);

	if(!mod->show.handle)
		return;

	// get show iface if any
	if(mod->show.descriptor->extension_data)
		mod->show.show_iface = mod->show.descriptor->extension_data(LV2_UI__showInterface);

	if(!mod->show.show_iface)
		return;

	// show UI
	int res = 0;
	if(mod->show.show_iface && mod->show.show_iface->show && mod->show.handle)
		res = mod->show.show_iface->show(mod->show.handle);
	//TODO handle res != 0
		
	mod->show.visible = 1; // toggle visibility flag

	// get idle iface if any
	if(mod->show.descriptor->extension_data)
		mod->show.idle_iface = mod->show.descriptor->extension_data(LV2_UI__idleInterface);

	// start animator
	if(mod->show.idle_iface)
		mod->show.anim = ecore_animator_add(_show_ui_animator, mod);
}

static void
_kx_ui_cleanup(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	// stop animator
	if(mod->kx.anim)
	{
		ecore_animator_del(mod->kx.anim);
		mod->kx.anim = NULL;
	}

	// unsubscribe all ports
	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 0);
	}
	
	// unsubscribe from notifications
	_mod_subscription_set(mod, mod->kx.ui, 0);

	// call cleanup 
	if(mod->kx.descriptor && mod->kx.descriptor->cleanup && mod->kx.handle)
		mod->kx.descriptor->cleanup(mod->kx.handle);
	mod->kx.handle = NULL;
	mod->kx.widget = NULL;
	mod->kx.dead = 0;
}

static Eina_Bool
_kx_ui_animator(void *data)
{
	mod_t *mod = data;

	LV2_EXTERNAL_UI_RUN(mod->kx.widget);

	if(mod->kx.dead)
	{
		_kx_ui_cleanup(mod);

		return EINA_FALSE; // stop animator
	}

	return EINA_TRUE; // retrigger animator
}

static void
_kx_ui_show(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	const LilvNode *plugin_uri = lilv_plugin_get_uri(mod->plug);
	const char *plugin_string = lilv_node_as_string(plugin_uri);

	const LilvNode *bundle_uri = lilv_ui_get_bundle_uri(mod->kx.ui);
	const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));

	if(!mod->kx.descriptor)
		return;

	// subscribe to ports
	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];
		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
	}

	// subscribe to notifications
	_mod_subscription_set(mod, mod->kx.ui, 1);

	// instantiate UI
	mod->kx.handle = mod->kx.descriptor->instantiate(
		mod->kx.descriptor,
		plugin_string,
		bundle_path,
		_ext_ui_write_function,
		mod,
		(void **)&mod->kx.widget,
		mod->features);

	if(!mod->kx.handle)
		return;

	// show UI
	LV2_EXTERNAL_UI_SHOW(mod->kx.widget);

	// start animator
	mod->kx.anim = ecore_animator_add(_kx_ui_animator, mod);
}

static void
_kx_ui_hide(mod_t *mod)
{
	// hide UI
	if(mod->kx.anim) // UI is running
		LV2_EXTERNAL_UI_HIDE(mod->kx.widget);

	// cleanup
	_kx_ui_cleanup(mod);
}
 
// plugin ui has been closed manually
static void
_kx_ui_closed(LV2UI_Controller controller)
{
	mod_t *mod = controller;

	if(!mod || !mod->kx.ui)
		return;

	// mark for cleanup
	mod->kx.dead = 1;
}

static int
_x11_ui_host_resize(LV2UI_Feature_Handle handle, int w, int h)
{
	mod_t *mod = handle;

	if(mod->x11.ui && mod->x11.win)
		evas_object_resize(mod->x11.win, w, h);

	return 0;
}

static Eina_Bool
_x11_ui_animator(void *data)
{
	mod_t *mod = data;

	if(mod->x11.idle_iface && mod->x11.idle_iface->idle && mod->x11.handle)
		mod->x11.idle_iface->idle(mod->x11.handle);

	return EINA_TRUE; // retrigger animator
}

static void
_x11_ui_hide(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	// stop animator
	if(mod->x11.anim)
	{
		ecore_animator_del(mod->x11.anim);
		mod->x11.anim = NULL;
	}

	// unsubscribe all ports
	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 0);
	}
	
	// unsubscribe from notifications
	_mod_subscription_set(mod, mod->x11.ui, 0);

	// call cleanup 
	if(mod->x11.descriptor && mod->x11.descriptor->cleanup && mod->x11.handle)
		mod->x11.descriptor->cleanup(mod->x11.handle);
	mod->x11.handle = NULL;

	evas_object_del(mod->x11.win);
	mod->x11.win = NULL;
	mod->x11.xwin = 0;
	mod->x11.idle_iface = NULL;
}

static void
_x11_delete_request(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;

	_x11_ui_hide(mod);
}

static void
_x11_ui_client_resize(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;

	int w, h;
	evas_object_geometry_get(obj, NULL, NULL, &w, &h);

	printf("_x11_ui_client_resize: %i %i\n", w, h);
	mod->x11.client_resize_iface->ui_resize(mod->x11.handle, w, h);
}

static void
_x11_ui_show(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	const LilvNode *plugin_uri = lilv_plugin_get_uri(mod->plug);
	const char *plugin_string = lilv_node_as_string(plugin_uri);

	const LilvNode *bundle_uri = lilv_ui_get_bundle_uri(mod->x11.ui);
	const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));

	if(!mod->x11.descriptor)
		return;

	// subscribe to ports
	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];
		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
	}

	// subscribe to notifications
	_mod_subscription_set(mod, mod->x11.ui, 1);

	mod->x11.win = elm_win_add(ui->win, plugin_string, ELM_WIN_BASIC);
	evas_object_smart_callback_add(mod->x11.win, "delete,request", _x11_delete_request, mod);
	evas_object_resize(mod->x11.win, 400, 400);
	evas_object_show(mod->x11.win);
	mod->x11.xwin = elm_win_xwindow_get(mod->x11.win);

	void *dummy;
	mod->feature_list[2].data = (void *)((uintptr_t)mod->x11.xwin);

	// instantiate UI
	mod->x11.handle = mod->x11.descriptor->instantiate(
		mod->x11.descriptor,
		plugin_string,
		bundle_path,
		_ext_ui_write_function,
		mod,
		&dummy,
		mod->features);
	
	mod->feature_list[2].data = NULL;

	if(!mod->x11.handle)
		return;

	// get interfaces
	if(mod->x11.descriptor->extension_data)
	{
		// get idle iface
		mod->x11.idle_iface = mod->x11.descriptor->extension_data(LV2_UI__idleInterface);
		
		// get resize iface
		mod->x11.client_resize_iface = mod->x11.descriptor->extension_data(LV2_UI__resize);
		if(mod->x11.client_resize_iface)
			evas_object_event_callback_add(mod->x11.win, EVAS_CALLBACK_RESIZE, _x11_ui_client_resize, mod);
	}

	// start animator
	if(mod->x11.idle_iface)
		mod->x11.anim = ecore_animator_add(_x11_ui_animator, mod);
}

static const void *
_data_access(const char *uri)
{
	return NULL; //FIXME this should call the plugins extension data function
}
		
static const LV2UI_Descriptor *
_ui_dlopen(const LilvUI *ui, uv_lib_t *lib)
{
	const LilvNode *ui_uri = lilv_ui_get_uri(ui);
	const LilvNode *binary_uri = lilv_ui_get_binary_uri(ui);

	const char *ui_string = lilv_node_as_string(ui_uri);
	const char *binary_path = lilv_uri_to_path(lilv_node_as_string(binary_uri));

	if(uv_dlopen(binary_path, lib))
		return NULL;
	
	LV2UI_DescriptorFunction ui_descfunc = NULL;
	uv_dlsym(lib, "lv2ui_descriptor", (void **)&ui_descfunc);

	if(!ui_descfunc)
		return NULL;

	// search for a matching UI
	for(int i=0; 1; i++)
	{
		const LV2UI_Descriptor *ui_desc = ui_descfunc(i);

		if(!ui_desc) // end of UI list
			break;
		else if(!strcmp(ui_desc->URI, ui_string))
			return ui_desc; // matching UI found
	}

	return NULL;
}

static mod_t *
_sp_ui_mod_add(sp_ui_t *ui, const char *uri, u_id_t uid, void *inst)
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
	mod->port_map.handle = mod;
	mod->port_map.port_index = _port_index;

	// populate port_subscribe
	mod->port_subscribe.handle = mod;
	mod->port_subscribe.subscribe = _port_subscribe;
	mod->port_subscribe.unsubscribe = _port_unsubscribe;

	// populate external_ui_host
	mod->kx.host.ui_closed = _kx_ui_closed;
	mod->kx.host.plugin_human_id = "Synthpod"; //TODO provide something here?

	// populate extension_data
	mod->ext_data.data_access = _data_access;

	// populate port_event for StdUI
	mod->std.descriptor.port_event = _std_port_event;

	// populate x11 resize
	mod->x11.host_resize_iface.ui_resize = _x11_ui_host_resize;
	mod->x11.host_resize_iface.handle = mod;

	// populate options
	mod->opts.options[0].context = LV2_OPTIONS_INSTANCE;
	mod->opts.options[0].subject = 0;
	mod->opts.options[0].key = ui->regs.ui.window_title.urid;
	mod->opts.options[0].size = 8;
	mod->opts.options[0].type = ui->forge.String;
	mod->opts.options[0].value = "Synthpod";

	//TODO provide sample rate, buffer size, etc

	mod->opts.options[1].key = 0; // sentinel
	mod->opts.options[1].value = NULL; // sentinel

	// populate UI feature list
	mod->feature_list[0].URI = LV2_URID__map;
	mod->feature_list[0].data = ui->driver->map;
	mod->feature_list[1].URI = LV2_URID__unmap;
	mod->feature_list[1].data = ui->driver->unmap;
	mod->feature_list[2].URI = LV2_UI__parent;
	mod->feature_list[2].data = NULL; // will be filled in before instantiation
	mod->feature_list[3].URI = LV2_UI__portMap;
	mod->feature_list[3].data = &mod->port_map;
	mod->feature_list[4].URI = LV2_UI__portSubscribe;
	mod->feature_list[4].data = &mod->port_subscribe;
	mod->feature_list[5].URI = LV2_DATA_ACCESS_URI;
	mod->feature_list[5].data = &mod->ext_data;
	mod->feature_list[6].URI = LV2_INSTANCE_ACCESS_URI;
	mod->feature_list[6].data = inst;
	mod->feature_list[7].URI = LV2_UI__idleInterface; // signal support for idleInterface
	mod->feature_list[7].data = NULL;
	mod->feature_list[8].URI = LV2_EXTERNAL_UI__Host;
	mod->feature_list[8].data = &mod->kx.host;
	mod->feature_list[9].URI = LV2_UI__resize;
	mod->feature_list[9].data = &mod->x11.host_resize_iface;
	mod->feature_list[10].URI = SYNTHPOD_WORLD;
	mod->feature_list[10].data = ui->world;
	mod->feature_list[11].URI = LV2_OPTIONS__options;
	mod->feature_list[11].data = mod->opts.options;
	
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
		}
		else if(lilv_port_is_a(plug, port, ui->regs.port.cv.node))
		{
			tar->type = PORT_TYPE_CV;
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

			tar->points = lilv_port_get_scale_points(plug, port);
		}
		else if(lilv_port_is_a(plug, port, ui->regs.port.atom.node)) 
		{
			tar->type = PORT_TYPE_ATOM;
			tar->buffer_type = PORT_BUFFER_TYPE_SEQUENCE;
			//tar->buffer_type = lilv_port_is_a(plug, port, ui->regs.port.sequence.node)
			//	? PORT_BUFFER_TYPE_SEQUENCE
			//	: PORT_BUFFER_TYPE_NONE; //TODO
		}

		// get port unit
		LilvNode *unit = lilv_port_get(mod->plug, tar->tar, ui->regs.units.unit.node);
		if(unit)
		{
			LilvNode *symbol = lilv_world_get(ui->world, unit, ui->regs.units.symbol.node, NULL);
			if(symbol)
			{
				tar->unit = strdup(lilv_node_as_string(symbol));
				lilv_node_free(symbol);
			}

			lilv_node_free(unit);
		}
	}
		
	// ui
	mod->all_uis = lilv_plugin_get_uis(mod->plug);
	LILV_FOREACH(uis, ptr, mod->all_uis)
	{
		const LilvUI *lui = lilv_uis_get(mod->all_uis, ptr);
		const LilvNode *ui_uri_node = lilv_ui_get_uri(lui);

		// test for EoUI
		{
			if(lilv_ui_is_a(lui, ui->regs.ui.eo.node))
			{
				//printf("has EoUI\n");
				mod->eo.ui = lui;
			}
		}

		// test for show UI
		{
			LilvNode *extension_data = lilv_new_uri(ui->world, LV2_CORE__extensionData);
			//LilvNode *required_feature = lilv_new_uri(ui->world, LV2_CORE__requiredFeature);
			LilvNode *show_interface = lilv_new_uri(ui->world, LV2_UI__showInterface);
			LilvNode *idle_interface = lilv_new_uri(ui->world, LV2_UI__idleInterface);
			
			LilvNodes* has_idle_iface = lilv_world_find_nodes(ui->world, ui_uri_node,
				extension_data, idle_interface);
			LilvNodes* has_show_iface = lilv_world_find_nodes(ui->world, ui_uri_node,
				extension_data, show_interface);

			//if(lilv_nodes_size(has_show_iface) && lilv_nodes_size(has_idle_iface))
			if(lilv_nodes_size(has_show_iface)) // idle_iface is implicitely included
			{
				mod->show.ui = lui;
				//printf("has show UI\n");
			}

			lilv_nodes_free(has_show_iface);
			lilv_nodes_free(has_idle_iface);

			lilv_node_free(extension_data);
			//lilv_node_free(required_feature);
			lilv_node_free(show_interface);
			lilv_node_free(idle_interface);
		}

		// test for kxstudio kx_ui
		{
			LilvNode *kx_ui = lilv_new_uri(ui->world, LV2_EXTERNAL_UI__Widget);
			if(lilv_ui_is_a(lui, kx_ui))
			{
				//printf("has kx-ui\n");
				mod->kx.ui = lui;
			}
			lilv_node_free(kx_ui);
		}

		// test for X11UI
		{
			LilvNode *x11_ui = lilv_new_uri(ui->world, LV2_UI__X11UI);
			if(lilv_ui_is_a(lui, x11_ui))
			{
				//printf("has x11-ui\n");
				mod->x11.ui = lui;
			}
			lilv_node_free(x11_ui);
		}
	}

	if(mod->eo.ui)
		mod->eo.descriptor = _ui_dlopen(mod->eo.ui, &mod->eo.lib);
	else if(mod->show.ui)
		mod->show.descriptor = _ui_dlopen(mod->show.ui, &mod->show.lib);
	else if(mod->kx.ui)
		mod->kx.descriptor = _ui_dlopen(mod->kx.ui, &mod->kx.lib);
	else if(mod->x11.ui)
		mod->x11.descriptor = _ui_dlopen(mod->x11.ui, &mod->x11.lib);
	
	if(mod->system.source || mod->system.sink)
		mod->col = 0; // reserved color for system ports
	else
		mod->col = _next_color();

	// load presets
	mod->presets = lilv_plugin_get_related(mod->plug, ui->regs.pset.preset.node);

	// request selected state
	_ui_mod_selected_request(mod);

	//TODO save visibility in synthpod state?
	//if(!mod->eo.ui && mod->kx.ui)
	//	_kx_ui_show(mod);
	
	return mod;
}

void
_sp_ui_mod_del(sp_ui_t *ui, mod_t *mod)
{
	for(int p=0; p<mod->num_ports; p++)
	{
		port_t *port = &mod->ports[p];

		if(port->points)
			lilv_scale_points_free(port->points);

		if(port->unit)
			free(port->unit);
	}

	if(mod->all_uis)
		lilv_uis_free(mod->all_uis);

	if(mod->eo.ui && mod->eo.descriptor)
		uv_dlclose(&mod->eo.lib);
	else if(mod->show.ui && mod->show.descriptor)
		uv_dlclose(&mod->show.lib);
	else if(mod->kx.ui && mod->kx.descriptor)
		uv_dlclose(&mod->kx.lib);
	else if(mod->x11.ui && mod->x11.descriptor)
		uv_dlclose(&mod->x11.lib);

	mod->eo.descriptor = NULL;
	mod->show.descriptor = NULL;
	mod->kx.descriptor = NULL;
	mod->x11.descriptor = NULL;

	free(mod);
}

static char * 
_pluglist_label_get(void *data, Evas_Object *obj, const char *part)
{
	const plug_info_t *info = data;

	switch(info->type)
	{
		case PLUG_INFO_TYPE_NAME:
		{
			LilvNode *node = lilv_plugin_get_name(info->plug);
			if(node)
			{
				char *str = strdup(lilv_node_as_string(node));
				lilv_node_free(node);

				return str;
			}
			else
				return NULL;
		}
		case PLUG_INFO_TYPE_URI:
		{
			const LilvNode *node = lilv_plugin_get_uri(info->plug);
			if(node)
				return strdup(lilv_node_as_uri(node));
			else
				return NULL;
		}
		case PLUG_INFO_TYPE_BUNDLE_URI:
		{
			const LilvNode *node = lilv_plugin_get_bundle_uri(info->plug);
			if(node)
				return strdup(lilv_node_as_uri(node));
			else
				return NULL;
		}
		case PLUG_INFO_TYPE_PROJECT:
		{
			LilvNode *node = lilv_plugin_get_project(info->plug);
			if(node)
			{
				char *str = strdup(lilv_node_as_string(node));
				lilv_node_free(node);

				return str;
			}
			else
				return NULL;
		}
		case PLUG_INFO_TYPE_AUTHOR_NAME:
		{
			LilvNode *node = lilv_plugin_get_author_name(info->plug);
			if(node)
			{
				char *str = strdup(lilv_node_as_string(node));
				lilv_node_free(node);

				return str;
			}
			else
				return NULL;
		}
		case PLUG_INFO_TYPE_AUTHOR_EMAIL:
		{
			LilvNode *node = lilv_plugin_get_author_email(info->plug);
			if(node)
			{
				char *str = strdup(lilv_node_as_string(node));
				lilv_node_free(node);

				return str;
			}
			else
				return NULL;
		}
		case PLUG_INFO_TYPE_AUTHOR_HOMEPAGE:
		{
			LilvNode *node = lilv_plugin_get_author_homepage(info->plug);
			if(node)
			{
				char *str = strdup(lilv_node_as_string(node));
				lilv_node_free(node);

				return str;
			}
			else
				return NULL;
		}
		default:
			return NULL;
	}
}

static void
_pluglist_del(void *data, Evas_Object *obj)
{
	plug_info_t *info = data;

	free(info);
}

static void
_pluglist_activated(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;
	plug_info_t *info = elm_object_item_data_get(itm);
		
	const LilvNode *uri_node = lilv_plugin_get_uri(info->plug);
	const char *uri_str = lilv_node_as_string(uri_node);

	size_t size = sizeof(transmit_module_add_t) + lv2_atom_pad_size(strlen(uri_str) + 1);
	transmit_module_add_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_add_fill(&ui->regs, &ui->forge, trans, size, 0, uri_str, NULL);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_pluglist_expanded(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;
	plug_info_t *info = elm_object_item_data_get(itm);

	plug_info_t *child;
	Elm_Object_Item *elmnt;

	for(int t=1; t<PLUG_INFO_TYPE_MAX; t++)
	{
		child = calloc(1, sizeof(plug_info_t));
		child->type = t;
		child->plug = info->plug;
		elmnt = elm_genlist_item_append(ui->pluglist, ui->plugitc,
			child, itm, ELM_GENLIST_ITEM_NONE, NULL, NULL);
		elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_NONE);
	}
}

static void
_pluglist_contracted(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;

	// clear items
	elm_genlist_item_subitems_clear(itm);
}

static void
_list_expand_request(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	//Eina_Bool selected = elm_genlist_item_selected_get(itm);
	elm_genlist_item_expanded_set(itm, EINA_TRUE);
	//elm_genlist_item_selected_set(itm, !selected); // preserve selection
}

static void
_list_contract_request(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	//Eina_Bool selected = elm_genlist_item_selected_get(itm);
	elm_genlist_item_expanded_set(itm, EINA_FALSE);
	//elm_genlist_item_selected_set(itm, !selected); // preserve selection
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
		if(!ui->matrix[t])
			continue;

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
				if(ui->matrix[port->type])
				{
					patcher_object_source_data_set(ui->matrix[port->type],
						count[port->direction][port->type], port);
					patcher_object_source_color_set(ui->matrix[port->type],
						count[port->direction][port->type], mod->col);
					patcher_object_source_label_set(ui->matrix[port->type],
						count[port->direction][port->type], name_str);
				}
			}
			else // sink
			{
				if(ui->matrix[port->type])
				{
					patcher_object_sink_data_set(ui->matrix[port->type],
						count[port->direction][port->type], port);
					patcher_object_sink_color_set(ui->matrix[port->type],
						count[port->direction][port->type], mod->col);
					patcher_object_sink_label_set(ui->matrix[port->type],
						count[port->direction][port->type], name_str);
				}
			}
			
			lilv_node_free(name_node);
		
			count[port->direction][port->type] += 1;
		}
	}

	for(int t=0; t<PORT_TYPE_NUM; t++)
	{
		if(!ui->matrix[t])
			continue;

		patcher_object_realize(ui->matrix[t]);
	}
}

static void
_modlist_expanded(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	mod_t *mod = elm_object_item_data_get(itm);
	sp_ui_t *ui = data;
	Elm_Object_Item *elmnt;

	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);

	if(class == ui->moditc) // is parent module item
	{
		// port entries
		for(int i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];

			// ignore dummy system ports
			if(mod->system.source && (port->direction == PORT_DIRECTION_INPUT) )
				continue;
			if(mod->system.sink && (port->direction == PORT_DIRECTION_OUTPUT) )
				continue;

			// only add control, audio, cv ports
			elmnt = elm_genlist_item_append(ui->modlist, ui->stditc, port, itm,
				ELM_GENLIST_ITEM_NONE, NULL, NULL);
			elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_NONE);
			//elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_DEFAULT); TODO
		}

		// presets
		if(lilv_nodes_size(mod->presets))
		{
			elmnt = elm_genlist_item_append(ui->modlist, ui->psetitc, mod, itm,
				ELM_GENLIST_ITEM_TREE, NULL, NULL);
			elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_DEFAULT);
		}

		// separator
		elmnt = elm_genlist_item_append(ui->modlist, ui->stditc, NULL, itm,
			ELM_GENLIST_ITEM_NONE, NULL, NULL);
		elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_NONE);
	}
	else if(class == ui->psetitc) // is presets item
	{
		LILV_FOREACH(nodes, i, mod->presets)
		{
			const LilvNode* preset = lilv_nodes_get(mod->presets, i);

			elmnt = elm_genlist_item_append(ui->modlist, ui->psetitmitc, preset, itm,
			ELM_GENLIST_ITEM_NONE, NULL, NULL);
			elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_DEFAULT);
		}
	}
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

static void
_modlist_activated(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;
	
	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);

	if(class == ui->psetitmitc) // is presets item
	{
		// get parent item
		Elm_Object_Item *parent = elm_genlist_item_parent_get(itm);
		if(!parent)
			return;

		mod_t *mod = elm_object_item_data_get(parent);

		const LilvNode* preset = elm_object_item_data_get(itm);
		const char *label = _preset_label_get(ui->world, &ui->regs, preset);

		// signal app
		size_t size = sizeof(transmit_module_preset_t) + lv2_atom_pad_size(strlen(label) + 1);
		transmit_module_preset_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transmit_module_preset_fill(&ui->regs, &ui->forge, trans, size, mod->uid, label);
			_sp_ui_to_app_advance(ui, size);
		}

		// contract parent list item
		evas_object_smart_callback_call(obj, "contract,request", parent);
	}
	//TODO toggle checkboxes on modules and ports
}

// only called upon user interaction
static void
_modlist_moved(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	Elm_Object_Item *first = elm_genlist_first_item_get(obj);
	Elm_Object_Item *last = elm_genlist_last_item_get(obj);

	// we must not move mod to top or end of list
	if(itm == first)
	{
		// promote system source to top of list
		Elm_Object_Item *source = elm_genlist_item_next_get(itm);
		elm_genlist_item_promote(source); // does not call _modlist_moved
	}
	else if(itm == last)
	{
		// demote system sink to end of list
		Elm_Object_Item *sink = elm_genlist_item_prev_get(itm);
		elm_genlist_item_demote(sink); // does not call _modlist_moved
	}

	// get previous item
	Elm_Object_Item *prev = elm_genlist_item_prev_get(itm);
		
	mod_t *itm_mod = elm_object_item_data_get(itm);
	mod_t *prev_mod = elm_object_item_data_get(prev);
		
	// signal app
	size_t size = sizeof(transmit_module_move_t);
	transmit_module_move_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_move_fill(&ui->regs, &ui->forge, trans, size,
			itm_mod->uid, prev_mod->uid);
		_sp_ui_to_app_advance(ui, size);
	}

	_patches_update(ui);
}

static char * 
_modlist_label_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;
	const LilvPlugin *plug = mod->plug;

	if(!strcmp(part, "elm.text"))
	{
		LilvNode *name_node = lilv_plugin_get_name(plug);
		if(name_node)
		{
			const char *name_str = lilv_node_as_string(name_node);
			lilv_node_free(name_node);

			return strdup(name_str);
		}
		else
			return NULL;
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

	// close show ui
	if(mod->show.ui && mod->show.descriptor)
		_show_ui_hide(mod);
	// close kx ui
	else if(mod->kx.ui && mod->kx.descriptor)
		_kx_ui_hide(mod);
	// close x11 ui
	else if(mod->x11.ui && mod->x11.descriptor)
		_x11_ui_hide(mod);
	
	size_t size = sizeof(transmit_module_del_t);
	transmit_module_del_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_del_fill(&ui->regs, &ui->forge, trans, size, mod->uid);
		_sp_ui_to_app_advance(ui, size);
	}
}

static inline Evas_Object *
_eo_widget_create(Evas_Object *parent, mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	const LilvNode *plugin_uri = lilv_plugin_get_uri(mod->plug);
	const char *plugin_string = lilv_node_as_string(plugin_uri);

	const LilvNode *bundle_uri = lilv_ui_get_bundle_uri(mod->eo.ui);
	const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));

	if(!mod->eo.ui || !mod->eo.descriptor)
		return NULL;

	// subscribe automatically to all non-atom ports by default
	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
	}

	// set subscriptions for notifications
	_mod_subscription_set(mod, mod->eo.ui, 1);
	
	// instantiate UI
	mod->eo.widget = NULL;

	if(mod->eo.descriptor->instantiate)
	{
		mod->feature_list[2].data = parent;

		mod->eo.handle = mod->eo.descriptor->instantiate(
			mod->eo.descriptor,
			plugin_string,
			bundle_path,
			_ext_ui_write_function,
			mod,
			(void **)&(mod->eo.widget),
			mod->features);
		
		mod->feature_list[2].data = NULL;
	}

	if(!mod->eo.handle || !mod->eo.widget)
		return NULL;

	return mod->eo.widget;
}

static void
_full_delete_request(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	evas_object_del(mod->eo.full.win);
	mod->eo.handle = NULL;
	mod->eo.widget = NULL;
	mod->eo.full.win = NULL;

	// add EoUI to modgrid
	mod->eo.embedded.itm = elm_gengrid_item_append(ui->modgrid, ui->griditc, mod,
		NULL, NULL);
}

static void
_modlist_toggle_clicked(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	if(mod->eo.ui)
	{
		if(mod->eo.full.win)
		{
			// remove fullscreen EoUI
			evas_object_del(mod->eo.full.win);
			mod->eo.handle = NULL;
			mod->eo.widget = NULL;
			mod->eo.full.win = NULL;

			// add EoUI to midgrid
			mod->eo.embedded.itm = elm_gengrid_item_append(ui->modgrid, ui->griditc, mod,
				NULL, NULL);
		}
		else if(mod->eo.embedded.itm)
		{
			// remove EoUI from modgrid
			elm_object_item_del(mod->eo.embedded.itm);
	
			const LilvNode *plugin_uri = lilv_plugin_get_uri(mod->plug);
			const char *plugin_string = lilv_node_as_string(plugin_uri);

			// add fullscreen EoUI
			Evas_Object *win = elm_win_add(ui->win, plugin_string, ELM_WIN_BASIC);
			elm_win_title_set(win, plugin_string);
			evas_object_smart_callback_add(win, "delete,request", _full_delete_request, mod);
			evas_object_resize(win, 800, 450);
			evas_object_show(win);
			mod->eo.full.win = win;

			Evas_Object *bg = elm_bg_add(win);	
			elm_bg_color_set(bg, 64, 64, 64);
			evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(bg, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(bg);
			elm_win_resize_object_add(win, bg);
			
			Evas_Object *container = elm_layout_add(win);
			elm_layout_file_set(container, "/usr/local/share/synthpod/synthpod.edj",
				"/synthpod/modgrid/container");
			char col [7];
			sprintf(col, "col,%02i", mod->col);
			elm_layout_signal_emit(container, col, MODGRID_UI);
			evas_object_size_hint_weight_set(container, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(container, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(container);
			elm_win_resize_object_add(win, container);

			for(int i=0; i<10; i++)
				ecore_main_loop_iterate(); // refresh window

			Evas_Object *widget = _eo_widget_create(container, mod);
			evas_object_size_hint_weight_set(widget, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(widget, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(widget);
			elm_layout_content_set(container, "elm.swallow.content", widget);
		}
	}
	else if(mod->show.ui && mod->show.descriptor)
	{
		if(mod->show.visible)
			_show_ui_hide(mod);
		else
			_show_ui_show(mod);
	}
	else if(mod->kx.ui && mod->kx.descriptor)
	{
		if(mod->kx.widget)
			_kx_ui_hide(mod);
		else
			_kx_ui_show(mod);
	}
	else if(mod->x11.ui && mod->x11.descriptor)
	{
		if(mod->x11.win)
			_x11_ui_hide(mod);
		else
			_x11_ui_show(mod);
	}
}

static void
_modlist_check_changed(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	mod->selected = elm_check_state_get(obj);
	_patches_update(ui);

	// signal app
	size_t size = sizeof(transmit_module_selected_t);
	transmit_module_selected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_selected_fill(&ui->regs, &ui->forge, trans, size, mod->uid, mod->selected);
		_sp_ui_to_app_advance(ui, size);
	}
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
	if(name_node)
	{
		const char *name_str = lilv_node_as_string(name_node);
		lilv_node_free(name_node);
		elm_layout_text_set(lay, "elm.text", name_str);
	}
	
	char col [7];
	sprintf(col, "col,%02i", mod->col);
	elm_layout_signal_emit(lay, col, MODLIST_UI);

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

	if(mod->show.ui || mod->kx.ui || mod->eo.ui || mod->x11.ui) //TODO also check for descriptor
	{
		Evas_Object *icon = elm_icon_add(lay);
		elm_icon_standard_set(icon, "arrow_up");
		evas_object_smart_callback_add(icon, "clicked", _modlist_toggle_clicked, mod);
		evas_object_show(icon);
		elm_layout_content_set(lay, "elm.swallow.preend", icon);
	}

	return lay;
}

static void
_patched_changed(void *data, Evas_Object *obj, void *event)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	port->selected = elm_check_state_get(obj);
	_patches_update(ui);

	size_t size = sizeof(transmit_port_selected_t);
	transmit_port_selected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_selected_fill(&ui->regs, &ui->forge, trans, size, mod->uid, port->index, port->selected);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_check_changed(void *data, Evas_Object *obj, void *event)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	float val = smart_toggle_value_get(obj);

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

	float val = smart_spinner_value_get(obj);

	_std_ui_write_function(mod, port->index, sizeof(float),
		ui->regs.port.float_protocol.urid, &val);
}

static void
_sldr_changed(void *data, Evas_Object *obj, void *event)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	float val = smart_slider_value_get(obj);
	if(lilv_port_has_property(mod->plug, port->tar, ui->regs.port.integer.node))
		val = floor(val);

	_std_ui_write_function(mod, port->index, sizeof(float),
		ui->regs.port.float_protocol.urid, &val);
}

static char *
_fmt_int(double val)
{
	char str [64];
	sprintf(str, "%.0lf", floor(val));
	return strdup(str);	
}

static char *
_fmt_flt(double val)
{
	char str [64];
	sprintf(str, "%.4lf", val);
	return strdup(str);	
}

static void
_fmt_free(char *str)
{
	free(str);
}

static void
_smart_mouse_in(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	elm_scroller_movement_block_set(ui->modlist,
		ELM_SCROLLER_MOVEMENT_BLOCK_HORIZONTAL | ELM_SCROLLER_MOVEMENT_BLOCK_VERTICAL);
}

static void
_smart_mouse_out(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	elm_scroller_movement_block_set(ui->modlist, ELM_SCROLLER_MOVEMENT_NO_BLOCK);
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
		float step_val = integer
			? 1.f / (port->max - port->min)
			: 0.001; // use 1000 steps for continuous values
		float val = port->dflt;

		if(toggled)
		{
			Evas_Object *check = smart_toggle_add(evas_object_evas_get(lay));
			smart_toggle_color_set(check, mod->col);
			smart_toggle_disabled_set(check, port->direction == PORT_DIRECTION_OUTPUT);
			if(port->direction == PORT_DIRECTION_INPUT)
				evas_object_smart_callback_add(check, "changed", _check_changed, port);
			evas_object_smart_callback_add(check, "mouse,in", _smart_mouse_in, ui);
			evas_object_smart_callback_add(check, "mouse,out", _smart_mouse_out, ui);

			child = check;
		}
		else if(port->points)
		{
			Evas_Object *spin = smart_spinner_add(evas_object_evas_get(lay));
			smart_spinner_color_set(spin, mod->col);
			smart_spinner_disabled_set(spin, port->direction == PORT_DIRECTION_OUTPUT);
			LILV_FOREACH(scale_points, itr, port->points)
			{
				const LilvScalePoint *point = lilv_scale_points_get(port->points, itr);
				const LilvNode *label_node = lilv_scale_point_get_label(point);
				const LilvNode *val_node = lilv_scale_point_get_value(point);

				smart_spinner_value_add(spin,
					lilv_node_as_float(val_node), lilv_node_as_string(label_node));
			}
			if(port->direction == PORT_DIRECTION_INPUT)
				evas_object_smart_callback_add(spin, "changed", _spinner_changed, port);
			evas_object_smart_callback_add(spin, "mouse,in", _smart_mouse_in, ui);
			evas_object_smart_callback_add(spin, "mouse,out", _smart_mouse_out, ui);

			child = spin;
		}
		else // integer or float
		{
			Evas_Object *sldr = smart_slider_add(evas_object_evas_get(lay));
			smart_slider_range_set(sldr, port->min, port->max, port->dflt);
			smart_slider_color_set(sldr, mod->col);
			smart_slider_integer_set(sldr, integer);
			smart_slider_format_set(sldr, integer ? "%.0f %s" : "%.4f %s");
			if(port->unit)
				smart_slider_unit_set(sldr, port->unit);
			smart_slider_disabled_set(sldr, port->direction == PORT_DIRECTION_OUTPUT);
			if(port->direction == PORT_DIRECTION_INPUT)
				evas_object_smart_callback_add(sldr, "changed", _sldr_changed, port);
			evas_object_smart_callback_add(sldr, "mouse,in", _smart_mouse_in, ui);
			evas_object_smart_callback_add(sldr, "mouse,out", _smart_mouse_out, ui);

			child = sldr;
		}
	}
	else if(port->type == PORT_TYPE_AUDIO
		|| port->type == PORT_TYPE_CV)
	{
		Evas_Object *sldr = smart_meter_add(evas_object_evas_get(lay));
		smart_meter_color_set(sldr, mod->col);

		child = sldr;
	}
	else if(port->type == PORT_TYPE_ATOM)
	{
		Evas_Object *lbl = elm_label_add(lay);
		elm_object_text_set(lbl, "Atom Port");

		child = lbl;
	}

	if(child)
	{
		evas_object_show(child);
		elm_layout_content_set(lay, "elm.swallow.content", child);
	}

	if(port->selected)
		elm_check_state_set(patched, EINA_TRUE);

	// subscribe to port
	const uint32_t i = port->index;
	if(port->type == PORT_TYPE_CONTROL)
		_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
	else if(port->type == PORT_TYPE_AUDIO)
		_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, 1);
	else if(port->type == PORT_TYPE_CV)
		_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, 1);

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
		_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 0);
	else if(port->type == PORT_TYPE_AUDIO)
		_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, 0);
	else if(port->type == PORT_TYPE_CV)
		_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, 0);
}

static char * 
_modlist_psets_label_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;

	if(!strcmp(part, "elm.text"))
	{
		return strdup("Presets");
	}
	else
		return NULL;
}

static char * 
_modlist_pset_label_get(void *data, Evas_Object *obj, const char *part)
{
	const LilvNode* preset = data;
	sp_ui_t *ui = evas_object_data_get(obj, "ui");

	if(!strcmp(part, "elm.text"))
		return strdup(_preset_label_get(ui->world, &ui->regs, preset));
	else
		return NULL;
}

static void
_modlist_del(void *data, Evas_Object *obj)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	// close show ui
	if(mod->show.ui && mod->show.descriptor)
		_show_ui_hide(mod);
	// close kx ui
	else if(mod->kx.ui && mod->kx.descriptor)
		_kx_ui_hide(mod);
	// close x11 ui
	else if(mod->x11.ui && mod->x11.descriptor)
		_x11_ui_hide(mod);

	_sp_ui_mod_del(ui, mod);
}

static char *
_modgrid_label_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;
	const LilvPlugin *plug = mod->plug;
	
	if(!strcmp(part, "elm.text"))
	{
		LilvNode *name_node = lilv_plugin_get_name(plug);
		if(name_node)
		{
			const char *name_str = lilv_node_as_string(name_node);
			lilv_node_free(name_node);

			return strdup(name_str);
		}
		else
			return NULL;
	}

	return NULL;
}

static void
_modgrid_mouse_in(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	elm_scroller_movement_block_set(ui->modgrid,
		ELM_SCROLLER_MOVEMENT_BLOCK_HORIZONTAL | ELM_SCROLLER_MOVEMENT_BLOCK_VERTICAL);
}

static void
_modgrid_mouse_out(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	elm_scroller_movement_block_set(ui->modgrid, ELM_SCROLLER_MOVEMENT_NO_BLOCK);
}

static Evas_Object *
_modgrid_content_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	if(!strcmp(part, "elm.swallow.icon"))
	{
		Evas_Object *container = elm_layout_add(ui->modgrid);
		elm_layout_file_set(container, "/usr/local/share/synthpod/synthpod.edj",
			"/synthpod/modgrid/container");
		char col [7];
		sprintf(col, "col,%02i", mod->col);
		elm_layout_signal_emit(container, col, MODGRID_UI);
		evas_object_event_callback_add(container, EVAS_CALLBACK_MOUSE_IN, _modgrid_mouse_in, ui);
		evas_object_event_callback_add(container, EVAS_CALLBACK_MOUSE_OUT, _modgrid_mouse_out, ui);
		evas_object_size_hint_weight_set(container, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(container, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(container);

		//TODO add EVAS DEL callback
		Evas_Object *widget = _eo_widget_create(container, mod);
		evas_object_size_hint_weight_set(widget, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(widget, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(widget);
		elm_layout_content_set(container, "elm.swallow.content", widget);

		return container;
	}
	else if(!strcmp(part, "elm.swallow.end"))
	{
		// what?
	}
	
	return NULL;
}

static void
_modgrid_del(void *data, Evas_Object *obj)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	if(!mod->eo.ui)
		return;

	// unsubscribe from all ports
	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 0);
	}

	// unsubscribe from all notifications
	_mod_subscription_set(mod, mod->eo.ui, 0);

	// cleanup EoUI
	if(  mod->eo.descriptor
		&& mod->eo.descriptor->cleanup
		&& mod->eo.handle)
	{
		mod->eo.descriptor->cleanup(mod->eo.handle);
	}
	
	// clear parameters
	mod->eo.handle = NULL;
	mod->eo.widget = NULL;
	mod->eo.embedded.itm = NULL;
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
			sink_port->mod->uid, sink_port->index, 1, -1);
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
			sink_port->mod->uid, sink_port->index, 0, -1);
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
			sink_port->mod->uid, sink_port->index, -1, -1);
		_sp_ui_to_app_advance(ui, size);
	}
}

static char *
_patchgrid_label_get(void *data, Evas_Object *obj, const char *part)
{
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	Evas_Object **matrix = data;
	port_type_t type = matrix - ui->matrix;

	if(!strcmp(part, "elm.text"))
	{
		switch(type)
		{
			case PORT_TYPE_AUDIO:
				return strdup("Audio Ports");
			case PORT_TYPE_ATOM:
				return strdup("Atom Ports");
			case PORT_TYPE_CONTROL:
				return strdup("Control Ports");
			case PORT_TYPE_CV:
				return strdup("CV Ports");
			default:
				break;
		}
	}

	return NULL;
}

static void
_patchgrid_del(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	Evas_Object **matrix = data;

	*matrix = NULL;
}

static Evas_Object *
_patchgrid_content_get(void *data, Evas_Object *obj, const char *part)
{
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	Evas_Object **matrix = data;

	if(!strcmp(part, "elm.swallow.icon"))
	{
		*matrix = patcher_object_add(ui->patchgrid);
		evas_object_smart_callback_add(*matrix, "connect,request",
			_matrix_connect_request, ui);
		evas_object_smart_callback_add(*matrix, "disconnect,request",
			_matrix_disconnect_request, ui);
		evas_object_smart_callback_add(*matrix, "realize,request",
			_matrix_realize_request, ui);
		evas_object_event_callback_add(*matrix, EVAS_CALLBACK_DEL, _patchgrid_del, matrix);
		evas_object_size_hint_weight_set(*matrix, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(*matrix, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(*matrix);

		_patches_update(ui);

		return *matrix;
	}
	
	return NULL;
}

static void
_resize(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	Evas_Coord w, h;
	evas_object_geometry_get(obj, NULL, NULL, &w, &h);

	evas_object_resize(ui->theme, w, h);
}

static void
_pluglist_populate(sp_ui_t *ui, const char *match)
{
	LILV_FOREACH(plugins, itr, ui->plugs)
	{
		const LilvPlugin *plug = lilv_plugins_get(ui->plugs, itr);
		LilvNode *name_node = lilv_plugin_get_name(plug);
		if(!name_node)
			continue;

		const char *name_str = lilv_node_as_string(name_node);

		if(strcasestr(name_str, match))
		{
			plug_info_t *info = calloc(1, sizeof(plug_info_t));
			info->type = PLUG_INFO_TYPE_NAME;
			info->plug = plug;
			elm_genlist_item_append(ui->pluglist, ui->plugitc, info, NULL,
				ELM_GENLIST_ITEM_TREE, NULL, NULL);
		}
		
		lilv_node_free(name_node);
	}
}

static void
_background_loading(void *data)
{
	sp_ui_t *ui = data;

	// walk plugin directories
	ui->plugs = lilv_world_get_all_plugins(ui->world);

	// initialzie registry
	sp_regs_init(&ui->regs, ui->world, ui->driver->map);

	// fill pluglist
	_pluglist_populate(ui, ""); // populate with everything

	// request mod list
	size_t size = sizeof(transmit_module_list_t);
	transmit_module_list_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_list_fill(&ui->regs, &ui->forge, trans, size);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_plugentry_changed(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	
	const char *chunk = elm_entry_entry_get(obj);
	char *match = elm_entry_markup_to_utf8(chunk);

	elm_genlist_clear(ui->pluglist);
	_pluglist_populate(ui, match); // populate with matching plugins
	free(match);
}

sp_ui_t *
sp_ui_new(Evas_Object *win, const LilvWorld *world, sp_ui_driver_t *driver, void *data)
{
	if(!driver || !data)
		return NULL;

	elm_config_focus_autoscroll_mode_set(ELM_FOCUS_AUTOSCROLL_MODE_NONE);
	elm_config_focus_move_policy_set(ELM_FOCUS_MOVE_POLICY_CLICK);
	elm_config_first_item_focus_on_first_focusin_set(EINA_TRUE);

	sp_ui_t *ui = calloc(1, sizeof(sp_ui_t));
	if(!ui)
		return NULL;

	ui->driver = driver;
	ui->data = data;
	ui->win = win;
	
	lv2_atom_forge_init(&ui->forge, ui->driver->map);

	if(world)
	{
		ui->world = (LilvWorld *)world;
		ui->embedded = 1;
	}
	else
	{
		ui->world = lilv_world_new();
		lilv_world_load_all(ui->world);
	}

	ui->theme = elm_layout_add(win);
	elm_layout_file_set(ui->theme, "/usr/local/share/synthpod/synthpod.edj",
		"/synthpod/theme");
	evas_object_size_hint_weight_set(ui->theme, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->theme, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->theme);

	_resize(ui, NULL, ui->win, NULL);
	evas_object_event_callback_add(ui->win, EVAS_CALLBACK_RESIZE, _resize, ui);

	for(int i=0; i<10; i++)
		ecore_main_loop_iterate();
	
	ui->mainpane = elm_panes_add(ui->theme);
	elm_panes_horizontal_set(ui->mainpane, EINA_FALSE);
	elm_panes_content_left_size_set(ui->mainpane, 0.5);
	evas_object_size_hint_weight_set(ui->mainpane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->mainpane, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->mainpane);
	elm_layout_content_set(ui->theme, "elm.swallow.content", ui->mainpane);
	
	ui->leftpane = elm_panes_add(ui->theme);
	elm_panes_horizontal_set(ui->leftpane, EINA_FALSE);
	elm_panes_content_left_size_set(ui->leftpane, 0.5);
	evas_object_size_hint_weight_set(ui->leftpane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->leftpane, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->leftpane);
	elm_object_part_content_set(ui->mainpane, "left", ui->leftpane);

	ui->plugpane = elm_panes_add(ui->mainpane);
	elm_panes_horizontal_set(ui->plugpane, EINA_TRUE);
	elm_panes_content_left_size_set(ui->plugpane, 0.33);
	evas_object_size_hint_weight_set(ui->plugpane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->plugpane, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->plugpane);
	elm_object_part_content_set(ui->leftpane, "left", ui->plugpane);

	ui->plugbox = elm_box_add(ui->plugpane);
	elm_box_horizontal_set(ui->plugbox, EINA_FALSE);
	elm_box_homogeneous_set(ui->plugbox, EINA_FALSE);
	evas_object_data_set(ui->plugbox, "ui", ui);
	evas_object_size_hint_weight_set(ui->plugbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->plugbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->plugbox);
	elm_object_part_content_set(ui->plugpane, "left", ui->plugbox);
	
	ui->plugentry = elm_entry_add(ui->plugbox);
	elm_entry_entry_set(ui->plugentry, "");
	elm_entry_editable_set(ui->plugentry, EINA_TRUE);
	elm_entry_single_line_set(ui->plugentry, EINA_TRUE);
	elm_entry_scrollable_set(ui->plugentry, EINA_TRUE);
	evas_object_smart_callback_add(ui->plugentry, "changed,user", _plugentry_changed, ui);
	evas_object_data_set(ui->plugentry, "ui", ui);
	//evas_object_size_hint_weight_set(ui->plugentry, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->plugentry, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->plugentry);
	elm_box_pack_end(ui->plugbox, ui->plugentry);

	ui->pluglist = elm_genlist_add(ui->plugbox);
	//elm_genlist_homogeneous_set(ui->pluglist, EINA_TRUE); // needef for lazy-loading
	evas_object_smart_callback_add(ui->pluglist, "activated",
		_pluglist_activated, ui);
	evas_object_smart_callback_add(ui->pluglist, "expand,request",
		_list_expand_request, ui);
	evas_object_smart_callback_add(ui->pluglist, "contract,request",
		_list_contract_request, ui);
	evas_object_smart_callback_add(ui->pluglist, "expanded",
		_pluglist_expanded, ui);
	evas_object_smart_callback_add(ui->pluglist, "contracted",
		_pluglist_contracted, ui);
	evas_object_data_set(ui->pluglist, "ui", ui);
	evas_object_size_hint_weight_set(ui->pluglist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->pluglist, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->pluglist);
	elm_box_pack_end(ui->plugbox, ui->pluglist);

	ui->plugitc = elm_genlist_item_class_new();
	ui->plugitc->item_style = "default";
	ui->plugitc->func.text_get = _pluglist_label_get;
	ui->plugitc->func.content_get = NULL;
	ui->plugitc->func.state_get = NULL;
	ui->plugitc->func.del = _pluglist_del;

	ui->patchgrid = elm_gengrid_add(ui->plugpane);
	elm_gengrid_horizontal_set(ui->patchgrid, EINA_FALSE);
	elm_gengrid_select_mode_set(ui->patchgrid, ELM_OBJECT_SELECT_MODE_NONE);
	elm_gengrid_reorder_mode_set(ui->patchgrid, EINA_TRUE);
	elm_gengrid_item_size_set(ui->patchgrid, 400, 400);
	evas_object_data_set(ui->patchgrid, "ui", ui);
	evas_object_size_hint_weight_set(ui->patchgrid, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->patchgrid, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->patchgrid);
	elm_object_part_content_set(ui->plugpane, "right", ui->patchgrid);

	ui->patchitc = elm_gengrid_item_class_new();
	ui->patchitc->item_style = "default";
	ui->patchitc->func.text_get = _patchgrid_label_get;
	ui->patchitc->func.content_get = _patchgrid_content_get;
	ui->patchitc->func.state_get = NULL;
	ui->patchitc->func.del = NULL;

	for(int t=0; t<PORT_TYPE_NUM; t++)
	{
		Elm_Object_Item *itm = elm_gengrid_item_append(ui->patchgrid, ui->patchitc,
			&ui->matrix[t], NULL, NULL);
	}

	ui->modlist = elm_genlist_add(ui->leftpane);
	elm_genlist_homogeneous_set(ui->modlist, EINA_TRUE); // needef for lazy-loading
	elm_genlist_block_count_set(ui->modlist, 64); // needef for lazy-loading
	//elm_genlist_select_mode_set(ui->modlist, ELM_OBJECT_SELECT_MODE_NONE);
	elm_genlist_reorder_mode_set(ui->modlist, EINA_TRUE);
	evas_object_smart_callback_add(ui->modlist, "expand,request",
		_list_expand_request, ui);
	evas_object_smart_callback_add(ui->modlist, "contract,request",
		_list_contract_request, ui);
	evas_object_smart_callback_add(ui->modlist, "expanded",
		_modlist_expanded, ui);
	evas_object_smart_callback_add(ui->modlist, "contracted",
		_modlist_contracted, ui);
	evas_object_smart_callback_add(ui->modlist, "activated",
		_modlist_activated, ui);
	evas_object_smart_callback_add(ui->modlist, "moved",
		_modlist_moved, ui);
	evas_object_data_set(ui->modlist, "ui", ui);
	evas_object_size_hint_weight_set(ui->modlist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->modlist, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->modlist);
	elm_object_part_content_set(ui->leftpane, "right", ui->modlist);
	
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

	ui->psetitc = elm_genlist_item_class_new();
	ui->psetitc->item_style = "default";
	ui->psetitc->func.text_get = _modlist_psets_label_get;
	ui->psetitc->func.content_get = NULL;
	ui->psetitc->func.state_get = NULL;
	ui->psetitc->func.del = NULL;

	ui->psetitmitc = elm_genlist_item_class_new();
	ui->psetitmitc->item_style = "default";
	ui->psetitmitc->func.text_get = _modlist_pset_label_get;
	ui->psetitmitc->func.content_get = NULL;
	ui->psetitmitc->func.state_get = NULL;
	ui->psetitmitc->func.del = NULL;

	ui->modgrid = elm_gengrid_add(ui->mainpane);
	elm_gengrid_select_mode_set(ui->modgrid, ELM_OBJECT_SELECT_MODE_NONE);
	elm_gengrid_reorder_mode_set(ui->modgrid, EINA_TRUE);
	elm_gengrid_item_size_set(ui->modgrid, 600, 400);
	evas_object_data_set(ui->modgrid, "ui", ui);
	evas_object_size_hint_weight_set(ui->modgrid, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->modgrid, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->modgrid);
	elm_object_part_content_set(ui->mainpane, "right", ui->modgrid);

	ui->griditc = elm_gengrid_item_class_new();
	ui->griditc->item_style = "default";
	ui->griditc->func.text_get = _modgrid_label_get;
	ui->griditc->func.content_get = _modgrid_content_get;
	ui->griditc->func.state_get = NULL;
	ui->griditc->func.del = _modgrid_del;

	ecore_job_add(_background_loading, ui);

	return ui;
}

Evas_Object *
sp_ui_widget_get(sp_ui_t *ui)
{
	return ui->theme;
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
	LV2_URID protocol = transmit->prop.key;

	if(protocol == ui->regs.synthpod.module_add.urid)
	{
		const transmit_module_add_t *trans = (const transmit_module_add_t *)atom;

		mod_t *mod = _sp_ui_mod_add(ui, trans->uri_str, trans->uid.body, (void *)trans->inst.body);
		if(!mod)
			return;

		if(mod->system.source || mod->system.sink || !ui->sink_itm)
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
			mod->eo.embedded.itm = elm_gengrid_item_append(ui->modgrid, ui->griditc, mod,
				NULL, NULL);
		}
	}
	else if(protocol == ui->regs.synthpod.module_del.urid)
	{
		const transmit_module_del_t *trans = (const transmit_module_del_t *)atom;
		mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
		if(!mod)
			return;

		if(mod->eo.full.win)
		{
			// remove full EoI if present
			evas_object_del(mod->eo.full.win);
			mod->eo.handle = NULL;
			mod->eo.widget = NULL;
			mod->eo.full.win = NULL;
		}
		else if(mod->eo.embedded.itm)
		{
			// remove EoUI grid item, if present
			elm_object_item_del(mod->eo.embedded.itm);
		}

		// remove StdUI list item
		elm_genlist_item_expanded_set(mod->std.itm, EINA_FALSE);
		elm_object_item_del(mod->std.itm);
		mod->std.itm = NULL;
	
		_patches_update(ui);
	}
	else if(protocol == ui->regs.synthpod.module_selected.urid)
	{
		const transmit_module_selected_t *trans = (const transmit_module_selected_t *)atom;
		mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);

		if(!mod)
			return;
		if(mod->selected != trans->state.body)
		{
			mod->selected = trans->state.body;
			if(mod->std.itm)
				elm_genlist_item_update(mod->std.itm);
			_patches_update(ui);
		}
	}
	else if(protocol == ui->regs.synthpod.port_connected.urid)
	{
		const transmit_port_connected_t *trans = (const transmit_port_connected_t *)atom;
		port_t *src = _sp_ui_port_get(ui, trans->src_uid.body, trans->src_port.body);
		port_t *snk = _sp_ui_port_get(ui, trans->snk_uid.body, trans->snk_port.body);

		Evas_Object *matrix = ui->matrix[src->type];
		if(matrix)
		{
			patcher_object_connected_set(matrix, src, snk,
				trans->state.body ? EINA_TRUE : EINA_FALSE,
				trans->indirect.body ? EINA_TRUE : EINA_FALSE);
		}
	}
	else if(protocol == ui->regs.port.float_protocol.urid)
	{
		const transfer_float_t *trans = (const transfer_float_t *)atom;
		uint32_t port_index = trans->transfer.port.body;
		const float value = trans->value.body;

		mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
		if(!mod)
			return;
		_ui_port_event(mod, port_index, sizeof(float), protocol, &value);
	}
	else if(protocol == ui->regs.port.peak_protocol.urid)
	{
		const transfer_peak_t *trans = (const transfer_peak_t *)atom;
		uint32_t port_index = trans->transfer.port.body;
		const LV2UI_Peak_Data data = {
			.period_start = trans->period_start.body,
			.period_size = trans->period_size.body,
			.peak = trans->peak.body
		};

		mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
		if(!mod)
			return;
		_ui_port_event(mod, port_index, sizeof(LV2UI_Peak_Data), protocol, &data);
	}
	else if(protocol == ui->regs.port.atom_transfer.urid)
	{
		const transfer_atom_t *trans = (const transfer_atom_t *)atom;
		uint32_t port_index = trans->transfer.port.body;
		const LV2_Atom *subatom = trans->atom;
		uint32_t size = sizeof(LV2_Atom) + subatom->size;

		mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
		if(!mod)
			return;
		_ui_port_event(mod, port_index, size, protocol, subatom);
	}
	else if(protocol == ui->regs.port.event_transfer.urid)
	{
		const transfer_atom_t *trans = (const transfer_atom_t *)atom;
		uint32_t port_index = trans->transfer.port.body;
		const LV2_Atom *subatom = trans->atom;
		uint32_t size = sizeof(LV2_Atom) + subatom->size;

		mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
		if(!mod)
			return;
		_ui_port_event(mod, port_index, size, protocol, subatom);
	}
	else if(protocol == ui->regs.synthpod.port_selected.urid)
	{
		const transmit_port_selected_t *trans = (const transmit_port_selected_t *)atom;

		port_t *port = _sp_ui_port_get(ui, trans->uid.body, trans->port.body);
		if(!port)
			return;
		if(port->selected != trans->state.body)
		{
			port->selected = trans->state.body;
			//TODO update modlist item?
			_patches_update(ui);
		}
	}
}

void
sp_ui_resize(sp_ui_t *ui, int w, int h)
{
	evas_object_resize(ui->theme, w, h);
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

	evas_object_del(ui->plugentry);
	evas_object_del(ui->plugbox);

	elm_gengrid_clear(ui->patchgrid);
	evas_object_del(ui->patchgrid);

	evas_object_del(ui->plugpane);
	evas_object_del(ui->leftpane);
	evas_object_del(ui->mainpane);
	evas_object_del(ui->theme);
	//evas_object_event_callback_del(ui->win, EVAS_CALLBACK_RESIZE, _resize);
	
	elm_genlist_item_class_free(ui->plugitc);
	elm_gengrid_item_class_free(ui->griditc);
	elm_genlist_item_class_free(ui->moditc);
	elm_genlist_item_class_free(ui->stditc);
	elm_genlist_item_class_free(ui->psetitc);
	elm_genlist_item_class_free(ui->psetitmitc);
	elm_gengrid_item_class_free(ui->patchitc);
	
	sp_regs_deinit(&ui->regs);

	if(!ui->embedded)
		lilv_world_free(ui->world);

	free(ui);
}
