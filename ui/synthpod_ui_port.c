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

#include <synthpod_ui_private.h>

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

void
_ui_port_tooltip_add(sp_ui_t *ui, Elm_Object_Item *elmnt, port_t *port)
{
	mod_t *mod = port->mod;

	LilvNodes *nodes = lilv_port_get_value(mod->plug, port->tar,
		ui->regs.rdfs.comment.node);
	LilvNode *node = nodes
		? lilv_nodes_get_first(nodes)
		: NULL;

	if(node)
		elm_object_item_tooltip_text_set(elmnt, lilv_node_as_string(node));

	if(nodes)
		lilv_nodes_free(nodes);
}

void
_ui_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;
	mod_ui_t *mod_ui = mod->mod_ui;

	//printf("_ui_port_event: %u %u %u %u\n", mod->uid, index, size, protocol);

	_std_port_event(mod, index, size, protocol, buf);

	if(mod_ui)
		mod_ui->driver->port_event(mod, index, size, protocol, buf);
}

void
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

static void
_port_link_toggle(void *data, Evas_Object *lay, const char *emission, const char *source)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	port->selected ^= 1; // toggle
	elm_layout_signal_emit(lay, port->selected ? "link,on" : "link,off", "");

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
_port_monitor_toggle(void *data, Evas_Object *lay, const char *emission, const char *source)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	port->std.monitored ^= 1; // toggle
	elm_layout_signal_emit(lay, port->std.monitored ? "monitor,on" : "monitor,off", "");

	// subsribe or unsubscribe, depending on monitored state
	{
		int32_t i = port->index;
		int32_t state = port->std.monitored;

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, state);
		else if(port->type == PORT_TYPE_AUDIO)
			_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, state);
		else if(port->type == PORT_TYPE_CV)
			_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, state);
	}

	// signal monitored state to app
	{
		size_t size = sizeof(transmit_port_monitored_t);
		transmit_port_monitored_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transmit_port_monitored_fill(&ui->regs, &ui->forge, trans, size, mod->uid, port->index, port->std.monitored);
			_sp_ui_to_app_advance(ui, size);
		}
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
_bitmask_changed(void *data, Evas_Object *obj, void *event)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	float val = smart_bitmask_value_get(obj);

	_std_ui_write_function(mod, port->index, sizeof(float),
		ui->regs.port.float_protocol.urid, &val);
}

static void
_spinner_changed(void *data, Evas_Object *obj, void *event)
{
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
	if(port->integer)
		val = floor(val);

	_std_ui_write_function(mod, port->index, sizeof(float),
		ui->regs.port.float_protocol.urid, &val);
}

static void
_modlist_std_del(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	if(!data)
		return;

	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	port->std.widget = NULL;

	// unsubscribe from port
	if(port->std.monitored)
	{
		const uint32_t i = port->index;
		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 0);
		else if(port->type == PORT_TYPE_AUDIO)
			_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, 0);
		else if(port->type == PORT_TYPE_CV)
			_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, 0);
	}
}

static void
_table_add_icon(Evas_Object *tab, const char *path, int col)
{
	Evas_Object *ico = elm_icon_add(tab);
	if(ico)
	{
		elm_layout_file_set(ico, path, NULL);
		evas_object_size_hint_weight_set(ico, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(ico, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(ico);

		elm_table_pack(tab, ico, col, 0, 1, 1);
	}
}

static Evas_Object * 
_modlist_std_content_get(void *data, Evas_Object *obj, const char *part)
{
	if(!data) // mepty item
		return NULL;

	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	Evas_Object *lay = elm_layout_add(obj);
	if(lay)
	{
		elm_layout_file_set(lay, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/modlist/port");
		evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_event_callback_add(lay, EVAS_CALLBACK_DEL, _modlist_std_del, port);
		evas_object_show(lay);

		// link
		elm_layout_signal_callback_add(lay, "link,toggle", "", _port_link_toggle, port);
		elm_layout_signal_emit(lay, port->selected ? "link,on" : "link,off", "");

		// monitor
		if(port->type != PORT_TYPE_ATOM)
		{
			elm_layout_signal_callback_add(lay, "monitor,toggle", "", _port_monitor_toggle, port);
			elm_layout_signal_emit(lay, port->std.monitored ? "monitor,on" : "monitor,off", "");
		}
		else
		{
			elm_layout_signal_emit(lay, "monitor,hide", "");
		}

		char col [7];
		sprintf(col, "col,%02i", mod->col);

		// source/sink
		elm_layout_signal_emit(lay, col, MODLIST_UI);
		if(port->direction == PORT_DIRECTION_OUTPUT)
		{
			elm_layout_signal_emit(lay, "source,on", "");
		}
		else
		{
			elm_layout_signal_emit(lay, "source,off", "");
		}

		LilvNode *name_node = lilv_port_get_name(mod->plug, port->tar);
		if(name_node)
		{
			const char *type_str = lilv_node_as_string(name_node);
			elm_layout_text_set(lay, "elm.text", type_str);
			lilv_node_free(name_node);
		}

		Evas_Object *child = NULL;
		if(port->type == PORT_TYPE_CONTROL)
		{
			if(port->toggled)
			{
				Evas_Object *check = smart_toggle_add(evas_object_evas_get(lay));
				if(check)
				{
					smart_toggle_color_set(check, mod->col);
					smart_toggle_disabled_set(check, port->direction == PORT_DIRECTION_OUTPUT);
					if(port->direction == PORT_DIRECTION_INPUT)
						evas_object_smart_callback_add(check, "changed", _check_changed, port);
					evas_object_smart_callback_add(check, "cat,in", _smart_mouse_in, mod);
					evas_object_smart_callback_add(check, "cat,out", _smart_mouse_out, mod);
				}

				child = check;
			}
			if(port->is_bitmask)
			{
				Evas_Object *bitmask = smart_bitmask_add(evas_object_evas_get(lay));
				if(bitmask)
				{
					smart_bitmask_color_set(bitmask, mod->col);
					smart_bitmask_disabled_set(bitmask, port->direction == PORT_DIRECTION_OUTPUT);
					const int nbits = log2(port->max + 1);
					smart_bitmask_bits_set(bitmask, nbits);
					if(port->direction == PORT_DIRECTION_INPUT)
						evas_object_smart_callback_add(bitmask, "changed", _bitmask_changed, port);
					evas_object_smart_callback_add(bitmask, "cat,in", _smart_mouse_in, mod);
					evas_object_smart_callback_add(bitmask, "cat,out", _smart_mouse_out, mod);
				}

				child = bitmask;
			}
			else if(port->points)
			{
				Evas_Object *spin = smart_spinner_add(evas_object_evas_get(lay));
				if(spin)
				{
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
					evas_object_smart_callback_add(spin, "cat,in", _smart_mouse_in, mod);
					evas_object_smart_callback_add(spin, "cat,out", _smart_mouse_out, mod);
				}

				child = spin;
			}
			else // integer or float
			{
				Evas_Object *sldr = smart_slider_add(evas_object_evas_get(lay));
				if(sldr)
				{
					smart_slider_range_set(sldr, port->min, port->max, port->dflt);
					smart_slider_color_set(sldr, mod->col);
					smart_slider_integer_set(sldr, port->integer);
					smart_slider_logarithmic_set(sldr, port->logarithmic);
					smart_slider_format_set(sldr, port->integer ? "%.0f %s" : "%.4f %s"); //TODO handle MIDI notes

					if(port->unit)
					{
						if(port->unit == ui->regs.units.midiController.urid)
						{
							smart_slider_lookup_set(sldr, _midi_controller_lookup);
						}
						else if(port->unit == ui->regs.units.midiNote.urid)
						{
							smart_slider_lookup_set(sldr, _midi_note_lookup);
						}
						else // fallback
						{
							const char *uri = ui->driver->unmap->unmap(ui->driver->unmap->handle, port->unit);
							LilvNode *unit = uri ? lilv_new_uri(ui->world, uri) : NULL;
							if(unit)
							{
								LilvNode *symbol = lilv_world_get(ui->world, unit, ui->regs.units.symbol.node, NULL);
								if(symbol)
								{
									smart_slider_unit_set(sldr, lilv_node_as_string(symbol));
									lilv_node_free(symbol);
								}
								
								lilv_node_free(unit);
							}
						}
					}
					smart_slider_disabled_set(sldr, port->direction == PORT_DIRECTION_OUTPUT);
					if(port->direction == PORT_DIRECTION_INPUT)
						evas_object_smart_callback_add(sldr, "changed", _sldr_changed, port);
					evas_object_smart_callback_add(sldr, "cat,in", _smart_mouse_in, mod);
					evas_object_smart_callback_add(sldr, "cat,out", _smart_mouse_out, mod);
				}

				child = sldr;
			}
		}
		else if(port->type == PORT_TYPE_AUDIO
			|| port->type == PORT_TYPE_CV)
		{
			Evas_Object *sldr = smart_meter_add(evas_object_evas_get(lay));
			if(sldr)
				smart_meter_color_set(sldr, mod->col);

			child = sldr;
		}
		else if(port->type == PORT_TYPE_ATOM)
		{
			Evas_Object *tab = elm_table_add(lay);
			if(tab)
			{
				elm_table_homogeneous_set(tab, EINA_TRUE);

				if(port->atom_type & PORT_ATOM_TYPE_MIDI)
					_table_add_icon(tab, SYNTHPOD_DATA_DIR"/midi.png", 0);
				if(port->atom_type & PORT_ATOM_TYPE_OSC)
					_table_add_icon(tab, SYNTHPOD_DATA_DIR"/osc.png", 1);
				if(port->atom_type & PORT_ATOM_TYPE_TIME)
					_table_add_icon(tab, SYNTHPOD_DATA_DIR"/time.png", 2);
				if(port->atom_type & PORT_ATOM_TYPE_PATCH)
					_table_add_icon(tab, SYNTHPOD_DATA_DIR"/patch.png", 3);
				if(port->atom_type & PORT_ATOM_TYPE_XPRESS)
					_table_add_icon(tab, SYNTHPOD_DATA_DIR"/xpress.png", 4);

				_table_add_icon(tab, SYNTHPOD_DATA_DIR"/atom_inverted.png", 6);
			}

			child = tab;
		}

		if(child)
		{
			evas_object_show(child);
			elm_layout_content_set(lay, "elm.swallow.content", child);
		}

		if(port->std.monitored)
		{
			// subscribe to port
			const uint32_t i = port->index;
			if(port->type == PORT_TYPE_CONTROL)
				_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
			else if(port->type == PORT_TYPE_AUDIO)
				_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, 1);
			else if(port->type == PORT_TYPE_CV)
				_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, 1);
		}

		port->std.widget = child;
	} // lay

	return lay;
}

void
_port_itc_add(sp_ui_t *ui)
{
	ui->stditc = elm_genlist_item_class_new();
	if(ui->stditc)
	{
		ui->stditc->item_style = "full";
		ui->stditc->func.text_get = NULL;
		ui->stditc->func.content_get = _modlist_std_content_get;
		ui->stditc->func.state_get = NULL;
		ui->stditc->func.del = NULL;
	}
}
