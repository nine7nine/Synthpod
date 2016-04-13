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

void
_patches_update(sp_ui_t *ui)
{
	if(!ui->modlist)
		return;

	int count [PORT_DIRECTION_NUM][PORT_TYPE_NUM];
	// clear counters
	memset(&count, 0, PORT_DIRECTION_NUM*PORT_TYPE_NUM*sizeof(int));

	// count input|output ports per type
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(ui->modlist);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != ui->listitc)
			continue; // ignore port items

		mod_t *mod = elm_object_item_data_get(itm);
		if(!mod || !mod->selected)
			continue; // ignore unselected mods

		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];
			if(!port->selected)
				continue; // ignore unselected ports

			if(  (port->type == PORT_TYPE_ATOM)
				&& (ui->matrix_atom_type != PORT_ATOM_TYPE_ALL)
				&& !(port->atom_type & ui->matrix_atom_type))
				continue;

			count[port->direction][port->type] += 1;
		}
	}

	// set dimension of patchers
	if(ui->matrix)
	{
		patcher_object_dimension_set(ui->matrix, 
			count[PORT_DIRECTION_OUTPUT][ui->matrix_type], // sources
			count[PORT_DIRECTION_INPUT][ui->matrix_type]); // sinks
	}

	// clear counters
	memset(&count, 0, PORT_DIRECTION_NUM*PORT_TYPE_NUM*sizeof(int));

	// populate patchers
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(ui->modlist);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != ui->listitc)
			continue; // ignore port items

		mod_t *mod = elm_object_item_data_get(itm);
		if(!mod || !mod->selected)
			continue; // ignore unselected mods

		bool first_source = true;
		bool first_sink = true;
		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];
			if(!port->selected)
				continue; // ignore unselected ports
			if(port->type != ui->matrix_type)
				continue; // ignore unselected port types

			if(  (port->type == PORT_TYPE_ATOM)
				&& (ui->matrix_atom_type != PORT_ATOM_TYPE_ALL)
				&& !(port->atom_type & ui->matrix_atom_type))
				continue; // ignore unwanted atom types

			LilvNode *name_node = lilv_port_get_name(mod->plug, port->tar);
			const char *name_str = NULL;
			if(name_node)
				name_str = lilv_node_as_string(name_node);

			if(port->direction == PORT_DIRECTION_OUTPUT) // source
			{
				if(ui->matrix)
				{
					patcher_object_source_id_set(ui->matrix,
						count[port->direction][port->type], (intptr_t)port);
					patcher_object_source_color_set(ui->matrix,
						count[port->direction][port->type], mod->col);
					patcher_object_source_label_set(ui->matrix,
						count[port->direction][port->type], name_str);
					if(first_source)
					{
						patcher_object_source_group_set(ui->matrix,
							count[port->direction][port->type], mod->name);
						first_source = false;
					}
				}
			}
			else // sink
			{
				if(ui->matrix)
				{
					patcher_object_sink_id_set(ui->matrix,
						count[port->direction][port->type], (intptr_t)port);
					patcher_object_sink_color_set(ui->matrix,
						count[port->direction][port->type], mod->col);
					patcher_object_sink_label_set(ui->matrix,
						count[port->direction][port->type], name_str);
					if(first_sink)
					{
						patcher_object_sink_group_set(ui->matrix,
							count[port->direction][port->type], mod->name);
						first_sink = false;
					}
				}
			}

			if(name_node)
				lilv_node_free(name_node);

			count[port->direction][port->type] += 1;
		}
	}

	if(ui->matrix)
		patcher_object_realize(ui->matrix);
}

