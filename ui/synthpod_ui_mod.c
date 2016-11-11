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

#define ANSI_COLOR_BOLD    "\x1b[1m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

// non-rt || rt with LV2_LOG__Trace
static int
_log_vprintf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, va_list args)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;

	char prefix [32]; //TODO how big?
	char buf [1024]; //TODO how big?

	snprintf(prefix, 32, "("ANSI_COLOR_CYAN"UI"ANSI_COLOR_RESET")  {"ANSI_COLOR_BOLD"%i"ANSI_COLOR_RESET"} ", mod->uid);
	vsnprintf(buf, 1024, fmt, args);

	char *pch = strtok(buf, "\n");
	while(pch)
	{
		if(ui->driver->log)
			ui->driver->log->printf(ui->driver->log->handle, type, "%s%s\n", prefix, pch);
		pch = strtok(NULL, "\n");
	}

	return 0;
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_printf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(handle, type, fmt, args);
  va_end(args);

	return ret;
}

static int
_sp_ui_next_col(sp_ui_t *ui)
{
	int col = 0;
	int count = INT_MAX;
	for(int i=1; i<ui->colors_max; i++)
	{
		if(ui->colors_vec[i] < count)
		{
			count = ui->colors_vec[i];
			col = i;
		}
	}

	ui->colors_vec[col] += 1;
	return col;
}

static int
_bank_cmp(const void *data1, const void *data2)
{
	const LilvNode *node1 = data1;
	const LilvNode *node2 = data2;
	if(!node1 || !node2)
		return 1;

	return lilv_node_equals(node1, node2)
		? 0
		: -1;
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

	_port_subscription_set(mod, index, protocol, 1);

	return 0;
}

static uint32_t
_port_unsubscribe(LV2UI_Feature_Handle handle, uint32_t index, uint32_t protocol,
	const LV2_Feature *const *features)
{
	mod_t *mod = handle;

	_port_subscription_set(mod, index, protocol, 0);

	return 0;
}

static void * //XXX check with _ui_write_function
_zero_writer_request(Zero_Writer_Handle handle, uint32_t port, uint32_t size,
	uint32_t protocol)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;
	port_t *tar = &mod->ports[port];

	//printf("_zero_writer_request: %u\n", size);

	// ignore output ports
	if(tar->direction != PORT_DIRECTION_INPUT)
	{
		fprintf(stderr, "_zero_writer_request: UI can only write to input port\n");
		return NULL;
	}

	// float protocol not supported by zero_writer
	assert( (protocol == ui->regs.port.atom_transfer.urid)
		|| (protocol == ui->regs.port.event_transfer.urid) );

	if(protocol == ui->regs.port.atom_transfer.urid)
	{
		size_t len = sizeof(transfer_atom_t) + lv2_atom_pad_size(size);
		transfer_atom_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			return _sp_transfer_atom_fill(&ui->regs, &ui->forge, trans, mod->uid,
				tar->index, size, NULL);
		}
	}
	else if(protocol == ui->regs.port.event_transfer.urid)
	{
		size_t len = sizeof(transfer_atom_t) + lv2_atom_pad_size(size);
		transfer_atom_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			return _sp_transfer_event_fill(&ui->regs, &ui->forge, trans, mod->uid,
				tar->index, size, NULL);
		}
	}

	return NULL; // protocol not supported 
}

static void // XXX check with _ui_write_function
_zero_writer_advance(Zero_Writer_Handle handle, uint32_t written)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;

	//printf("_zero_writer_advance: %u\n", written);

	size_t len = sizeof(transfer_atom_t) + lv2_atom_pad_size(written);
	_sp_ui_to_app_advance(ui, len);
}

static inline void
_ui_mod_selected_request(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	// request module selected state
	size_t size = sizeof(transmit_module_selected_t);
	transmit_module_selected_t *trans0 = _sp_ui_to_app_request(ui, size);
	if(trans0)
	{
		_sp_transmit_module_selected_fill(&ui->regs, &ui->forge, trans0, size, mod->uid, -1);
		_sp_ui_to_app_advance(ui, size);
	}

	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		// request port selected state
		{
			size = sizeof(transmit_port_selected_t);
			transmit_port_selected_t *trans1 = _sp_ui_to_app_request(ui, size);
			if(trans1)
			{
				_sp_transmit_port_selected_fill(&ui->regs, &ui->forge, trans1, size, mod->uid, port->index, -1);
				_sp_ui_to_app_advance(ui, size);
			}
		}

		// request port monitored state
		{
			size = sizeof(transmit_port_monitored_t);
			transmit_port_monitored_t *trans2 = _sp_ui_to_app_request(ui, size);
			if(trans2)
			{
				_sp_transmit_port_monitored_fill(&ui->regs, &ui->forge, trans2, size, mod->uid, port->index, -1);
				_sp_ui_to_app_advance(ui, size);
			}
		}
	}
}

static inline void
_ui_mod_visible_request(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	// request module visible state
	size_t size = sizeof(transmit_module_visible_t);
	transmit_module_visible_t *trans0 = _sp_ui_to_app_request(ui, size);
	if(trans0)
	{
		_sp_transmit_module_visible_fill(&ui->regs, &ui->forge, trans0, size, mod->uid, -1, 0);
		_sp_ui_to_app_advance(ui, size);
	}
}

static inline void
_ui_mod_disabled_request(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	// request module disabled state
	size_t size = sizeof(transmit_module_disabled_t);
	transmit_module_disabled_t *trans0 = _sp_ui_to_app_request(ui, size);
	if(trans0)
	{
		_sp_transmit_module_disabled_fill(&ui->regs, &ui->forge, trans0, size, mod->uid, -1);
		_sp_ui_to_app_advance(ui, size);
	}
}

static inline void
_ui_mod_embedded_request(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	// request module embedded state
	size_t size = sizeof(transmit_module_embedded_t);
	transmit_module_embedded_t *trans0 = _sp_ui_to_app_request(ui, size);
	if(trans0)
	{
		_sp_transmit_module_embedded_fill(&ui->regs, &ui->forge, trans0, size, mod->uid, -1);
		_sp_ui_to_app_advance(ui, size);
	}
}

void
_mod_visible_set(mod_t *mod, int state, LV2_URID urid)
{
	sp_ui_t *ui = mod->ui;

	// set module visible state
	const size_t size = sizeof(transmit_module_visible_t);
	transmit_module_visible_t *trans1 = _sp_ui_to_app_request(ui, size);
	if(trans1)
	{
		_sp_transmit_module_visible_fill(&ui->regs, &ui->forge, trans1, size, mod->uid, state, urid);
		_sp_ui_to_app_advance(ui, size);
	}

	// refresh modlist item
	if(mod->std.elmnt)
		elm_genlist_item_update(mod->std.elmnt);
}

static inline void
_sp_ui_mod_port_add(sp_ui_t *ui, mod_t *mod, uint32_t i, port_t *tar, const LilvPort *port)
{
	// discover port groups
	tar->group = lilv_port_get(mod->plug, port, ui->regs.group.group.node);

	tar->mod = mod;
	tar->tar = port;
	tar->index = i;
	tar->direction = lilv_port_is_a(mod->plug, port, ui->regs.port.input.node)
		? PORT_DIRECTION_INPUT
		: PORT_DIRECTION_OUTPUT;
			
	// discover port designation
	tar->designation = PORT_DESIGNATION_ALL; // default
	LilvNode *port_designation= lilv_port_get(mod->plug, port, ui->regs.core.designation.node);
	if(port_designation)
	{
		if(lilv_node_equals(port_designation, ui->regs.group.left.node))
			tar->designation = PORT_DESIGNATION_LEFT;
		else if(lilv_node_equals(port_designation, ui->regs.group.right.node))
			tar->designation = PORT_DESIGNATION_RIGHT;
		else if(lilv_node_equals(port_designation, ui->regs.group.center.node))
			tar->designation = PORT_DESIGNATION_CENTER;
		else if(lilv_node_equals(port_designation, ui->regs.group.side.node))
			tar->designation = PORT_DESIGNATION_SIDE;
		else if(lilv_node_equals(port_designation, ui->regs.group.center_left.node))
			tar->designation = PORT_DESIGNATION_CENTER_LEFT;
		else if(lilv_node_equals(port_designation, ui->regs.group.center_right.node))
			tar->designation = PORT_DESIGNATION_CENTER_RIGHT;
		else if(lilv_node_equals(port_designation, ui->regs.group.side_left.node))
			tar->designation = PORT_DESIGNATION_SIDE_LEFT;
		else if(lilv_node_equals(port_designation, ui->regs.group.side_right.node))
			tar->designation = PORT_DESIGNATION_SIDE_RIGHT;
		else if(lilv_node_equals(port_designation, ui->regs.group.rear_left.node))
			tar->designation = PORT_DESIGNATION_REAR_LEFT;
		else if(lilv_node_equals(port_designation, ui->regs.group.rear_right.node))
			tar->designation = PORT_DESIGNATION_REAR_RIGHT;
		else if(lilv_node_equals(port_designation, ui->regs.group.rear_center.node))
			tar->designation = PORT_DESIGNATION_REAR_CENTER;
		else if(lilv_node_equals(port_designation, ui->regs.group.low_frequency_effects.node))
			tar->designation = PORT_DESIGNATION_LOW_FREQUENCY_EFFECTS;

		lilv_node_free(port_designation);
	}

	if(lilv_port_is_a(mod->plug, port, ui->regs.port.audio.node))
	{
		tar->type =  PORT_TYPE_AUDIO;
	}
	else if(lilv_port_is_a(mod->plug, port, ui->regs.port.cv.node))
	{
		tar->type = PORT_TYPE_CV;
	}
	else if(lilv_port_is_a(mod->plug, port, ui->regs.port.control.node))
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

		tar->integer = lilv_port_has_property(mod->plug, tar->tar, ui->regs.port.integer.node);
		tar->toggled = lilv_port_has_property(mod->plug, tar->tar, ui->regs.port.toggled.node);
		tar->is_bitmask = lilv_port_has_property(mod->plug, tar->tar, ui->regs.port.is_bitmask.node);
		tar->logarithmic = lilv_port_has_property(mod->plug, tar->tar, ui->regs.port.logarithmic.node);
		int enumeration = lilv_port_has_property(mod->plug, port, ui->regs.port.enumeration.node);
		tar->points = enumeration
			? lilv_port_get_scale_points(mod->plug, port)
			: NULL;

		// force positive logarithmic range
		if(tar->logarithmic)
		{
			if(tar->min <= 0.f)
				tar->min = FLT_MIN; // smallest positive normalized float
		}

		// force max > min
		if(tar->max <= tar->min)
			tar->max = tar->min + FLT_MIN;

		// force min <= dflt <= max
		if(tar->dflt < tar->min)
			tar->dflt = tar->min;
		if(tar->dflt > tar->max)
			tar->dflt = tar->max;
	}
	else if(lilv_port_is_a(mod->plug, port, ui->regs.port.atom.node)) 
	{
		tar->type = PORT_TYPE_ATOM;
		tar->buffer_type = PORT_BUFFER_TYPE_SEQUENCE;
		//tar->buffer_type = lilv_port_is_a(mod->plug, port, ui->regs.port.sequence.node)
		//	? PORT_BUFFER_TYPE_SEQUENCE
		//	: PORT_BUFFER_TYPE_NONE; //TODO

		// does this port support patch:Message?
		tar->patchable = lilv_port_supports_event(mod->plug, port, ui->regs.patch.message.node);

		tar->atom_type = 0;
		if(lilv_port_supports_event(mod->plug, port, ui->regs.port.midi.node))
			tar->atom_type |= PORT_ATOM_TYPE_MIDI;
		if(lilv_port_supports_event(mod->plug, port, ui->regs.port.osc_event.node))
			tar->atom_type |= PORT_ATOM_TYPE_OSC;
		if(lilv_port_supports_event(mod->plug, port, ui->regs.port.time_position.node))
			tar->atom_type |= PORT_ATOM_TYPE_TIME;
		if(lilv_port_supports_event(mod->plug, port, ui->regs.patch.message.node))
			tar->atom_type |= PORT_ATOM_TYPE_PATCH;
		if(lilv_port_supports_event(mod->plug, port, ui->regs.xpress.message.node))
			tar->atom_type |= PORT_ATOM_TYPE_XPRESS;
	}

	// get port unit
	LilvNode *unit = lilv_port_get(mod->plug, tar->tar, ui->regs.units.unit.node);
	if(unit)
	{
		const char *unit_uri = lilv_node_as_uri(unit);
		if(unit_uri)
			tar->unit = ui->driver->map->map(ui->driver->map->handle, unit_uri);
		lilv_node_free(unit);
	}
}

static inline property_t *
_sp_ui_mod_static_prop_add(sp_ui_t *ui, mod_t *mod, const LilvNode *writable, int editable)
{
	property_t *prop = calloc(1, sizeof(property_t));
	if(!prop)
		return NULL;

	const char *writable_str = lilv_node_as_uri(writable);

	prop->mod = mod;
	prop->editable = editable;
	prop->is_bitmask = false;
	prop->label = NULL;
	prop->comment = NULL;
	prop->tar_urid = ui->driver->map->map(ui->driver->map->handle, writable_str);
	prop->type_urid = 0; // invalid type
	prop->minimum = 0.f; // not yet known
	prop->maximum = 1.f; // not yet known
	prop->unit = 0; // not yet known
	
	// get lv2:parameterProperty
	LilvNodes *paramprops = lilv_world_find_nodes(ui->world, writable,
		ui->regs.parameter.property.node, NULL);
	if(paramprops)
	{
		LILV_FOREACH(nodes, itr, paramprops)
		{
			const LilvNode *node = lilv_nodes_get(paramprops, itr);
			if(lilv_node_equals(node, ui->regs.port.is_bitmask.node))
			{
				prop->is_bitmask = true;
				lilv_nodes_free(paramprops);
			}
		}
	}

	// get rdfs:label
	LilvNode *label = lilv_world_get(ui->world, writable,
		ui->regs.rdfs.label.node, NULL);
	if(label)
	{
		const char *label_str = lilv_node_as_string(label);

		if(label_str)
			prop->label = strdup(label_str);

		lilv_node_free(label);
	}

	// get rdfs:comment
	LilvNode *comment = lilv_world_get(ui->world, writable,
		ui->regs.rdfs.comment.node, NULL);
	if(comment)
	{
		const char *comment_str = lilv_node_as_string(comment);

		if(comment_str)
			prop->comment = strdup(comment_str);

		lilv_node_free(comment);
	}

	// get type of patch:writable
	LilvNode *type = lilv_world_get(ui->world, writable,
		ui->regs.rdfs.range.node, NULL);
	if(type)
	{
		const char *type_str = lilv_node_as_string(type);

		//printf("with type: %s\n", type_str);
		prop->type_urid = ui->driver->map->map(ui->driver->map->handle, type_str);

		lilv_node_free(type);
	}

	// get lv2:minimum
	LilvNode *minimum = lilv_world_get(ui->world, writable,
		ui->regs.core.minimum.node, NULL);
	if(minimum)
	{
		prop->minimum = lilv_node_as_float(minimum);

		lilv_node_free(minimum);
	}

	// get lv2:maximum
	LilvNode *maximum = lilv_world_get(ui->world, writable,
		ui->regs.core.maximum.node, NULL);
	if(maximum)
	{
		prop->maximum = lilv_node_as_float(maximum);

		lilv_node_free(maximum);
	}

	// get units:unit
	LilvNode *unit = lilv_world_get(ui->world, writable,
		ui->regs.units.unit.node, NULL);
	if(unit)
	{
		const char *unit_uri = lilv_node_as_uri(unit);
		if(unit_uri)
			prop->unit = ui->driver->map->map(ui->driver->map->handle, unit_uri);
		lilv_node_free(unit);
	}
	
	LilvNodes *spoints = lilv_world_find_nodes(ui->world, writable,
		ui->regs.core.scale_point.node, NULL);
	if(spoints)
	{
		LILV_FOREACH(nodes, n, spoints)
		{
			const LilvNode *point = lilv_nodes_get(spoints, n);
			LilvNode *point_label = lilv_world_get(ui->world, point,
				ui->regs.rdfs.label.node, NULL);
			LilvNode *point_value = lilv_world_get(ui->world, point,
				ui->regs.rdf.value.node, NULL);

			if(point_label && point_value)
			{
				point_t *p = calloc(1, sizeof(point_t));
				p->label = strdup(lilv_node_as_string(point_label));
				if(prop->type_urid == ui->forge.Int)
				{
					p->d = calloc(1, sizeof(double));
					*p->d = lilv_node_as_float(point_value);
				}
				else if(prop->type_urid == ui->forge.Float)
				{
					p->d = calloc(1, sizeof(double));
					*p->d = lilv_node_as_float(point_value);
				}
				else if(prop->type_urid == ui->forge.Long)
				{
					p->d = calloc(1, sizeof(double));
					*p->d = lilv_node_as_float(point_value);
				}
				else if(prop->type_urid == ui->forge.Double)
				{
					p->d = calloc(1, sizeof(double));
					*p->d = lilv_node_as_float(point_value);
				}
				//FIXME do other types
				else if(prop->type_urid == ui->forge.String)
				{
					p->s = strdup(lilv_node_as_string(point_value));
				}

				prop->scale_points = eina_list_append(prop->scale_points, p);

				if(prop->std.elmnt)
					elm_genlist_item_update(prop->std.elmnt);
			}

			if(point_label)
				lilv_node_free(point_label);
			if(point_value)
				lilv_node_free(point_value);
		}
			
		lilv_nodes_free(spoints);
	}

	return prop;
}

static char *
_mod_get_name(mod_t *mod)
{
	const LilvPlugin *plug = mod->plug;
	if(plug)
	{
		LilvNode *name_node = lilv_plugin_get_name(plug);
		if(name_node)
		{
			const char *name_str = lilv_node_as_string(name_node);

			char *dup = NULL;
			if(name_str)
				asprintf(&dup, "#%u %s", mod->uid, name_str);
			
			lilv_node_free(name_node);

			return dup; //XXX needs to be freed
		}
	}

	return NULL;
}

mod_t *
_sp_ui_mod_add(sp_ui_t *ui, const char *uri, u_id_t uid)
{
	LilvNode *uri_node = lilv_new_uri(ui->world, uri);
	if(!uri_node)
		return NULL;

	const LilvPlugin *plug = lilv_plugins_get_by_uri(ui->plugs, uri_node);
	lilv_node_free(uri_node);
	if(!plug)
		return NULL;

	const LilvNode *plugin_uri = lilv_plugin_get_uri(plug);
	const char *plugin_string = NULL;
	if(plugin_uri)
		plugin_string = lilv_node_as_string(plugin_uri);

	if(!lilv_plugin_verify(plug))
		return NULL;

	mod_t *mod = calloc(1, sizeof(mod_t));
	if(!mod)
		return NULL;

	mod->ui = ui;
	mod->uid = uid;
	mod->plug = plug;
	mod->num_ports = lilv_plugin_get_num_ports(plug);
	mod->subject = ui->driver->map->map(ui->driver->map->handle, plugin_string);

	mod->name = _mod_get_name(mod);

	// populate port_map
	mod->port_map.handle = mod;
	mod->port_map.port_index = _port_index;

	// populate log
	mod->log.handle = mod;
	mod->log.printf = _log_printf;
	mod->log.vprintf = _log_vprintf;

	// populate port_subscribe
	mod->port_subscribe.handle = mod;
	mod->port_subscribe.subscribe = _port_subscribe;
	mod->port_subscribe.unsubscribe = _port_unsubscribe;

	// populate zero-writer
	mod->zero_writer.handle = mod;
	mod->zero_writer.request = _zero_writer_request;
	mod->zero_writer.advance = _zero_writer_advance;

	// populate external_ui_host
	mod->kx.host.ui_closed = _kx_ui_closed;
	mod->kx.host.plugin_human_id = mod->name;

	// populate port_event for StdUI
	mod->std.descriptor.port_event = _std_port_event;

	// populate options
	mod->opts.options[0].context = LV2_OPTIONS_INSTANCE;
	mod->opts.options[0].subject = 0;
	mod->opts.options[0].key = ui->regs.ui.window_title.urid;
	mod->opts.options[0].size = strlen(mod->name) + 1;
	mod->opts.options[0].type = ui->forge.String;
	mod->opts.options[0].value = mod->name;

	mod->opts.options[1].context = LV2_OPTIONS_INSTANCE;
	mod->opts.options[1].subject = 0;
	mod->opts.options[1].key = ui->regs.param.sample_rate.urid;
	mod->opts.options[1].size = sizeof(float);
	mod->opts.options[1].type = ui->forge.Float;
	mod->opts.options[1].value = &ui->driver->sample_rate;

	mod->opts.options[2].key = 0; // sentinel
	mod->opts.options[2].value = NULL; // sentinel

	// populate UI feature list
	int nfeatures = 0;
	mod->feature_list[nfeatures].URI = LV2_URID__map;
	mod->feature_list[nfeatures++].data = ui->driver->map;

	mod->feature_list[nfeatures].URI = LV2_URID__unmap;
	mod->feature_list[nfeatures++].data = ui->driver->unmap;

	//XXX do NOT put ANY feature BEFORE LV2_UI__parent

	mod->feature_list[nfeatures].URI = LV2_UI__parent;
	mod->feature_list[nfeatures++].data = NULL; // will be filled in before instantiation

	mod->feature_list[nfeatures].URI = XPRESS_VOICE_MAP;
	mod->feature_list[nfeatures++].data = ui->driver->xmap;

	mod->feature_list[nfeatures].URI = LV2_LOG__log;
	mod->feature_list[nfeatures++].data = &mod->log;

	mod->feature_list[nfeatures].URI = LV2_UI__portMap;
	mod->feature_list[nfeatures++].data = &mod->port_map;

	mod->feature_list[nfeatures].URI = LV2_UI__portSubscribe;
	mod->feature_list[nfeatures++].data = &mod->port_subscribe;

	mod->feature_list[nfeatures].URI = LV2_UI__idleInterface;
	mod->feature_list[nfeatures++].data = NULL;

	mod->feature_list[nfeatures].URI = LV2_EXTERNAL_UI__Host;
	mod->feature_list[nfeatures++].data = &mod->kx.host;

	mod->feature_list[nfeatures].URI = LV2_EXTERNAL_UI__Widget;
	mod->feature_list[nfeatures++].data = NULL;

	mod->feature_list[nfeatures].URI = LV2_EXTERNAL_UI_DEPRECATED_URI;
	mod->feature_list[nfeatures++].data = &mod->kx.host;

	mod->feature_list[nfeatures].URI = LV2_OPTIONS__options;
	mod->feature_list[nfeatures++].data = mod->opts.options;

	//FIXME do we want to support this? it's marked as DEPRECATED in LV2 spec
	{
		mod->feature_list[nfeatures].URI = LV2_UI_PREFIX"makeSONameResident";
		mod->feature_list[nfeatures++].data = NULL;
	}
	{
		mod->feature_list[nfeatures].URI = LV2_UI_PREFIX"makeResident";
		mod->feature_list[nfeatures++].data = NULL;
	}

	mod->feature_list[nfeatures].URI = SYNTHPOD_WORLD;
	mod->feature_list[nfeatures++].data = ui->world;

	mod->feature_list[nfeatures].URI = ZERO_WRITER__schedule;
	mod->feature_list[nfeatures++].data = &mod->zero_writer;

	mod->feature_list[nfeatures].URI = LV2_URI_MAP_URI;
	mod->feature_list[nfeatures++].data = &ui->uri_to_id;

	assert(nfeatures <= NUM_UI_FEATURES);

	for(int i=0; i<nfeatures; i++)
		mod->features[i] = &mod->feature_list[i];
	mod->features[nfeatures] = NULL; // sentinel

	// discover system modules
	if(!strcmp(uri, SYNTHPOD_PREFIX"source"))
		mod->system.source = 1;
	else if(!strcmp(uri, SYNTHPOD_PREFIX"sink"))
		mod->system.sink = 1;

	mod->ports = calloc(mod->num_ports, sizeof(port_t));
	if(mod->ports)
	{
		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *tar = &mod->ports[i];
			const LilvPort *port = lilv_plugin_get_port_by_index(plug, i);

			if(port)
				_sp_ui_mod_port_add(ui, mod, i, tar, port);
		}
	}

	// look for patch:writable's
	mod->writs = lilv_world_find_nodes(ui->world,
		plugin_uri, ui->regs.patch.writable.node, NULL);
	if(mod->writs)
	{
		LILV_FOREACH(nodes, i, mod->writs)
		{
			const LilvNode *writable = lilv_nodes_get(mod->writs, i);
			property_t *prop = _sp_ui_mod_static_prop_add(ui, mod, writable, 1);

			if(prop)
				mod->static_properties = eina_list_sorted_insert(mod->static_properties, _urid_cmp, prop);
		}
	}

	// look for patch:readable's
	mod->reads = lilv_world_find_nodes(ui->world,
		plugin_uri, ui->regs.patch.readable.node, NULL);
	if(mod->reads)
	{
		LILV_FOREACH(nodes, i, mod->reads)
		{
			const LilvNode *readable = lilv_nodes_get(mod->reads, i);
			property_t *prop = _sp_ui_mod_static_prop_add(ui, mod, readable, 0);

			if(prop)
				mod->static_properties = eina_list_sorted_insert(mod->static_properties, _urid_cmp, prop);
		}
	}

	// ui
	mod->all_uis = lilv_plugin_get_uis(mod->plug);
	if(mod->all_uis)
	{
		LILV_FOREACH(uis, ptr, mod->all_uis)
		{
			const LilvUI *lui = lilv_uis_get(mod->all_uis, ptr);
			if(!lui)
				continue;
			const LilvNode *ui_uri_node = lilv_ui_get_uri(lui);
			if(!ui_uri_node)
				continue;
			if(!strcmp(SYNTHPOD_PREFIX"root_3_eo", lilv_node_as_uri(ui_uri_node))) //FIXME
				continue;
			
			// nedded if ui ttl referenced via rdfs#seeAlso
			lilv_world_load_resource(ui->world, ui_uri_node); //TODO unload

			// check for missing features
			int missing_required_feature = 0;
			LilvNodes *required_features = lilv_world_find_nodes(ui->world,
				ui_uri_node, ui->regs.core.required_feature.node, NULL);
			if(required_features)
			{
				LILV_FOREACH(nodes, i, required_features)
				{
					const LilvNode* required_feature = lilv_nodes_get(required_features, i);
					const char *required_feature_uri = lilv_node_as_uri(required_feature);
					missing_required_feature = 1;

					for(int f=0; f<nfeatures; f++)
					{
						if(!strcmp(mod->feature_list[f].URI, required_feature_uri))
						{
							missing_required_feature = 0;
							break;
						}
					}

					if(missing_required_feature)
					{
						fprintf(stderr, "UI '%s' requires non-supported feature: %s\n",
							lilv_node_as_uri(ui_uri_node), required_feature_uri);
						break;
					}
				}
				lilv_nodes_free(required_features);
			}

			if(missing_required_feature)
				continue; // plugin requires a feature we do not support

			mod_ui_type_t mod_ui_type = MOD_UI_TYPE_UNSUPPORTED;
			const mod_ui_driver_t *mod_ui_driver = NULL;

#if defined(SANDBOX_LIB)
#	if defined(SANDBOX_X11)
			// test for X11UI
			if(mod_ui_type == MOD_UI_TYPE_UNSUPPORTED)
			{
				if(lilv_ui_is_a(lui, ui->regs.ui.x11.node))
				{
					//printf("has x11-ui\n");
					mod_ui_type = MOD_UI_TYPE_SANDBOX_X11;
					mod_ui_driver = &sbox_ui_driver;
				}
			}
#	endif

#	if defined(SANDBOX_GTK2)
			// test for GtkUI
			if(mod_ui_type == MOD_UI_TYPE_UNSUPPORTED)
			{
				if(lilv_ui_is_a(lui, ui->regs.ui.gtk2.node))
				{
					//printf("has gtk2-ui\n");
					mod_ui_type = MOD_UI_TYPE_SANDBOX_GTK2;
					mod_ui_driver = &sbox_ui_driver;
				}
			}
#	endif

#	if defined(SANDBOX_GTK3)
			// test for GtkUI
			if(mod_ui_type == MOD_UI_TYPE_UNSUPPORTED)
			{
				if(lilv_ui_is_a(lui, ui->regs.ui.gtk3.node))
				{
					//printf("has gtk3-ui\n");
					mod_ui_type = MOD_UI_TYPE_SANDBOX_GTK3;
					mod_ui_driver = &sbox_ui_driver;
				}
			}
#	endif

#	if defined(SANDBOX_QT4)
			// test for Qt4UI
			if(mod_ui_type == MOD_UI_TYPE_UNSUPPORTED)
			{
				if(lilv_ui_is_a(lui, ui->regs.ui.qt4.node))
				{
					//printf("has qt4-ui\n");
					mod_ui_type = MOD_UI_TYPE_SANDBOX_QT4;
					mod_ui_driver = &sbox_ui_driver;
				}
			}
#	endif

#	if defined(SANDBOX_QT5)
			// test for Qt5UI
			if(mod_ui_type == MOD_UI_TYPE_UNSUPPORTED)
			{
				if(lilv_ui_is_a(lui, ui->regs.ui.qt5.node))
				{
					//printf("has qt5-ui\n");
					mod_ui_type = MOD_UI_TYPE_SANDBOX_QT5;
					mod_ui_driver = &sbox_ui_driver;
				}
			}
#	endif
#endif

			// test for EoUI (precedes MOD_UI_TYPE_SANDBOX_EFL)
			if(mod_ui_type == MOD_UI_TYPE_UNSUPPORTED)
			{
				if(lilv_ui_is_a(lui, ui->regs.ui.eo.node))
				{
					//printf("has EoUI\n");
#if defined(SANDBOX_LIB) && defined(SANDBOX_EFL)
					mod_ui_type = MOD_UI_TYPE_SANDBOX_EFL;
					mod_ui_driver = &sbox_ui_driver;
#else
					mod_ui_type = MOD_UI_TYPE_EFL;
					mod_ui_driver = &efl_ui_driver;
#endif
				}
			}

			// test for show UI (precedes MOD_UI_TYPE_SANDBOX_SHOW)
			if(mod_ui_type == MOD_UI_TYPE_UNSUPPORTED)
			{
				//const bool has_idle_iface = lilv_world_ask(ui->world, ui_uri_node,
				//	ui->regs.core.extension_data.node, ui->regs.ui.idle_interface.node);
				const bool has_show_iface = lilv_world_ask(ui->world, ui_uri_node,
					ui->regs.core.extension_data.node, ui->regs.ui.show_interface.node);

				if(has_show_iface)
				{
					//printf("has show UI\n");
#if defined(SANDBOX_LIB) && defined(SANDBOX_SHOW)
					mod_ui_type = MOD_UI_TYPE_SANDBOX_SHOW;
					mod_ui_driver = &sbox_ui_driver;
#else
					mod_ui_type = MOD_UI_TYPE_SHOW;
					mod_ui_driver = &show_ui_driver;
#endif
				}
			}

			// test for kxstudio kx_ui (precedes MOD_UI_TYPE_SANDBOX_KX)
			if(mod_ui_type == MOD_UI_TYPE_UNSUPPORTED)
			{
				if(  lilv_ui_is_a(lui, ui->regs.ui.kx_widget.node)
					|| lilv_ui_is_a(lui, ui->regs.ui.external.node) )
				{
					//printf("has kx-ui\n");
#if defined(SANDBOX_LIB) && defined(SANDBOX_KX)
					mod_ui_type = MOD_UI_TYPE_SANDBOX_KX;
					mod_ui_driver = &sbox_ui_driver;
#else
					mod_ui_type = MOD_UI_TYPE_KX;
					mod_ui_driver = &kx_ui_driver;
#endif
				}
			}

			if(mod_ui_type == MOD_UI_TYPE_UNSUPPORTED)
				continue;

			mod_ui_t *mod_ui = calloc(1, sizeof(mod_ui_t));
			if(!mod_ui)
				continue;

			mod->mod_uis = eina_list_append(mod->mod_uis, mod_ui);
			mod_ui->mod = mod;
			mod_ui->ui = lui;
			mod_ui->urid = ui->driver->map->map(ui->driver->map->handle,
				lilv_node_as_string(lilv_ui_get_uri(lui)));
			mod_ui->type = mod_ui_type;
			mod_ui->driver = mod_ui_driver;
		}
	}

	if(mod->system.source || mod->system.sink)
		mod->col = 0; // reserved color for system ports
	else
		mod->col = _sp_ui_next_col(ui);

	// load presets
	mod->presets = lilv_plugin_get_related(mod->plug, ui->regs.pset.preset.node);

	// load resources for this module's presets
	LILV_FOREACH(nodes, itr, mod->presets)
	{
		const LilvNode *preset = lilv_nodes_get(mod->presets, itr);

		lilv_world_load_resource(ui->world, preset);
	}

	// request selected state
	_ui_mod_selected_request(mod);
	_ui_mod_visible_request(mod);
	_ui_mod_disabled_request(mod);
	_ui_mod_embedded_request(mod);

	//TODO save visibility in synthpod state?
	//if(!mod->eo.ui && mod->kx.ui)
	//	_kx_ui_show(mod);

	return mod;
}

void
_sp_ui_mod_del(sp_ui_t *ui, mod_t *mod)
{
	ui->colors_vec[mod->col] -= 1; // decrease color count

	for(unsigned p=0; p<mod->num_ports; p++)
	{
		port_t *port = &mod->ports[p];

		if(port->points)
			lilv_scale_points_free(port->points);

		if(port->group)
			lilv_node_free(port->group);
	}
	if(mod->ports)
		free(mod->ports);

	if(mod->presets)
	{
		// unload resources for this module's presets
		LILV_FOREACH(nodes, itr, mod->presets)
		{
			const LilvNode *preset = lilv_nodes_get(mod->presets, itr);

			lilv_world_unload_resource(ui->world, preset);
		}
		lilv_nodes_free(mod->presets);
	}

	mod_ui_t *mod_ui;
	EINA_LIST_FREE(mod->mod_uis, mod_ui)
	{
		free(mod_ui);
	}

	if(mod->std.elmnt == ui->sink_itm)
		ui->sink_itm = 0;

	if(mod->static_properties)
	{
		property_t *prop;
		EINA_LIST_FREE(mod->static_properties, prop)
			_property_free(prop);
	}
	if(mod->dynamic_properties)
	{
		property_t *prop;
		EINA_LIST_FREE(mod->dynamic_properties, prop)
			_property_free(prop);
	}
	if(mod->writs)
		lilv_nodes_free(mod->writs);
	if(mod->reads)
		lilv_nodes_free(mod->reads);

	if(mod->all_uis)
		lilv_uis_free(mod->all_uis);

	if(mod->name)
		free(mod->name);

	if(mod->groups)
		eina_hash_free(mod->groups);

	free(mod);
}

void
_mod_del_widgets(mod_t *mod)
{
	mod_ui_t *mod_ui = mod->mod_ui;

	if(mod_ui)
		mod_ui->driver->hide(mod);
}

void
_mod_subscription_set(mod_t *mod, const LilvUI *ui_ui, int state)
{
	sp_ui_t *ui = mod->ui;

	// subscribe manually for port notifications
	const LilvNode *plug_uri_node = lilv_plugin_get_uri(mod->plug);

	LilvNodes *notifs = lilv_world_find_nodes(ui->world,
		lilv_ui_get_uri(ui_ui), ui->regs.port.notification.node, NULL);
	LILV_FOREACH(nodes, n, notifs)
	{
		const LilvNode *notif = lilv_nodes_get(notifs, n);
		LilvNode *plug = lilv_world_get(ui->world, notif,
			ui->regs.ui.plugin.node, NULL);

		if(plug && !lilv_node_equals(plug, plug_uri_node))
		{
			lilv_node_free(plug);
			continue; // notification not for this plugin 
		}

		LilvNode *ind = lilv_world_get(ui->world, notif,
			ui->regs.core.index.node, NULL);
		LilvNode *sym = lilv_world_get(ui->world, notif,
			ui->regs.core.symbol.node, NULL);
		LilvNode *prot = lilv_world_get(ui->world, notif,
			ui->regs.ui.protocol.node, NULL);

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

			if(prot) // protocol specified
			{
				if(lilv_node_equals(prot, ui->regs.port.float_protocol.node))
					_port_subscription_set(mod, index, ui->regs.port.float_protocol.urid, state);
				else if(lilv_node_equals(prot, ui->regs.port.peak_protocol.node))
					_port_subscription_set(mod, index, ui->regs.port.peak_protocol.urid, state);
				else if(lilv_node_equals(prot, ui->regs.port.atom_transfer.node))
					_port_subscription_set(mod, index, ui->regs.port.atom_transfer.urid, state);
				else if(lilv_node_equals(prot, ui->regs.port.event_transfer.node))
					_port_subscription_set(mod, index, ui->regs.port.event_transfer.urid, state);
			}
			else // no protocol specified, we have to guess according to port type
			{
				if(port->type == PORT_TYPE_CONTROL)
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

		if(plug)
			lilv_node_free(plug);
		if(ind)
			lilv_node_free(ind);
		if(sym)
			lilv_node_free(sym);
		if(prot)
			lilv_node_free(prot);
	}
	lilv_nodes_free(notifs);
}

group_t *
_mod_group_get(mod_t *mod, const char *group_lbl, int group_type,
	LilvNode *node, Elm_Object_Item **parent, bool expand)
{
	sp_ui_t *ui = mod->ui;

	*parent = eina_hash_find(mod->groups, group_lbl);

	if(*parent)
	{
		return elm_object_item_data_get(*parent);
	}
	else
	{
		group_t *group = calloc(1, sizeof(group_t));

		if(group)
		{
			group->type = group_type;
			group->mod = mod;
			group->node = node;

			*parent = elm_genlist_item_sorted_insert(mod->std.list,
				ui->grpitc, group, NULL, ELM_GENLIST_ITEM_GROUP, _grpitc_cmp, NULL, NULL);
			elm_genlist_item_select_mode_set(*parent, ELM_OBJECT_SELECT_MODE_NONE);

			if(*parent)
			{
				eina_hash_add(mod->groups, group_lbl, *parent);

				return group;
			}

			free(group);
		}

		*parent = NULL;
	}

	return NULL;
}

void
_module_patch_get_all(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	// request all properties
	size_t len = sizeof(transfer_patch_get_t);
	for(unsigned index=0; index<mod->num_ports; index++)
	{
		port_t *port = &mod->ports[index];

		// only consider event ports which support patch:Message
		if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
			|| (port->direction != PORT_DIRECTION_INPUT)
			|| !port->patchable)
		{
			continue; // skip
		}

		transfer_patch_get_all_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			_sp_transfer_patch_get_all_fill(&ui->regs,
				&ui->forge, trans, mod->uid, index,
				mod->subject, ui->driver->xmap->new_uuid(ui->driver->xmap->handle));
			_sp_ui_to_app_advance(ui, len);
		}
	}
}
