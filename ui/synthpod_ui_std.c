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

int
_stditc_cmp(const void *data1, const void *data2)
{
	const Elm_Object_Item *itm1 = data1;
	const Elm_Object_Item *itm2 = data2;
	if(!itm1 || !itm2)
		return 1;

	port_t *port1 = elm_object_item_data_get(itm1);
	port_t *port2 = elm_object_item_data_get(itm2);
	if(!port1 || !port2)
		return 1;

	// compare port indeces
	return port1->index < port2->index
		? -1
		: (port1->index > port2->index
			? 1
			: 0);
}

void
_std_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;
	port_t *port = &mod->ports[index]; //FIXME handle patch:Response

	//printf("_std_port_event: %u %u %u\n", index, size, protocol);

	if(protocol == 0)
		protocol = ui->regs.port.float_protocol.urid;

	if(protocol == ui->regs.port.float_protocol.urid)
	{
		float val = *(float *)buf;

		// we should not set a value lower/higher than min/max for widgets
		//FIXME should be done by smart_*_value_set
		if(val < port->min)
			val = port->min;
		if(val > port->max)
			val = port->max;

		if(port->std.widget)
		{
			if(port->toggled)
				smart_toggle_value_set(port->std.widget, floor(val));
			else if(port->is_bitmask)
				smart_bitmask_value_set(port->std.widget, floor(val));
			else if(port->points)
				smart_spinner_value_set(port->std.widget, val);
			else // integer or float
				smart_slider_value_set(port->std.widget, val);
		}
	}
	else if(protocol == ui->regs.port.peak_protocol.urid)
	{
		const LV2UI_Peak_Data *peak_data = buf;
		//TODO smooth/filter signal?
		port->peak = peak_data->peak;

		smart_meter_value_set(port->std.widget, port->peak);
	}
	else if(protocol == ui->regs.port.event_transfer.urid)
	{
		const LV2_Atom_Object *obj = buf;

		if(lv2_atom_forge_is_object_type(&ui->forge, obj->atom.type))
		{
			const LV2_Atom_URID *destination = NULL;
			lv2_atom_object_get(obj, ui->regs.patch.destination.urid, &destination, NULL);
			if(destination && (destination->atom.type == ui->forge.URID)
					&& (destination->body == ui->regs.core.plugin.urid) )
				return; // ignore feedback messages

			// check for patch:Set
			if(obj->body.otype == ui->regs.patch.set.urid)
			{
				const LV2_Atom_URID *subject = NULL;
				const LV2_Atom_URID *property = NULL;
				const LV2_Atom *value = NULL;

				LV2_Atom_Object_Query q[] = {
					{ ui->regs.patch.subject.urid, (const LV2_Atom **)&subject },
					{ ui->regs.patch.property.urid, (const LV2_Atom **)&property },
					{ ui->regs.patch.value.urid, &value },
					{ 0, NULL }
				};
				lv2_atom_object_query(obj, q);

				bool subject_match = subject && (subject->atom.type == ui->forge.URID)
					? subject->body == mod->subject
					: true;

				if(subject_match && property && (property->atom.type == ui->forge.URID) && value)
					_mod_set_property(mod, property->body, value);
			}
			// check for patch:Put
			else if(obj->body.otype == ui->regs.patch.put.urid)
			{
				const LV2_Atom_URID *subject = NULL;
				const LV2_Atom_Object *body = NULL;

				LV2_Atom_Object_Query q[] = {
					{ ui->regs.patch.subject.urid, (const LV2_Atom **)&subject },
					{ ui->regs.patch.body.urid, (const LV2_Atom **)&body },
					{ 0, NULL }
				};
				lv2_atom_object_query(obj, q);

				bool subject_match = subject && (subject->atom.type == ui->forge.URID)
					? subject->body == mod->subject
					: true;

				if(subject_match && body && lv2_atom_forge_is_object_type(&ui->forge, body->atom.type))
				{
					LV2_ATOM_OBJECT_FOREACH(body, prop)
					{
						_mod_set_property(mod, prop->key, &prop->value);
					}
				}
			}
			// check for patch:Patch
			else if(obj->body.otype == ui->regs.patch.patch.urid)
			{
				const LV2_Atom_URID *subject = NULL;
				const LV2_Atom_Object *add = NULL;
				const LV2_Atom_Object *remove = NULL;

				LV2_Atom_Object_Query q[] = {
					{ ui->regs.patch.subject.urid, (const LV2_Atom **)&subject },
					{ ui->regs.patch.add.urid, (const LV2_Atom **)&add },
					{ ui->regs.patch.remove.urid, (const LV2_Atom **)&remove },
					{ 0, NULL }
				};
				lv2_atom_object_query(obj, q);

				if(  (!subject || (subject->atom.type == ui->forge.URID))
					&& add && lv2_atom_forge_is_object_type(&ui->forge, add->atom.type)
					&& remove && lv2_atom_forge_is_object_type(&ui->forge, remove->atom.type))
				{
					Elm_Object_Item *parent;
					const char *group_lbl = "*Properties*";
					group_t *group = _mod_group_get(mod, group_lbl, GROUP_TYPE_PROPERTY, NULL, &parent, true);

					LV2_ATOM_OBJECT_FOREACH(remove, atom_prop)
					{
						if(atom_prop->key == ui->regs.patch.readable.urid)
						{
							if(subject && (subject->body != mod->subject))
								continue; // ignore alien patch events

							const LV2_URID tar_urid = ((const LV2_Atom_URID *)&atom_prop->value)->body;
							if(tar_urid == ui->regs.patch.wildcard.urid)
							{
								// delete all readable dynamic properties of this module
								Eina_List *l1, *l2;
								property_t *prop;
								EINA_LIST_FOREACH_SAFE(mod->dynamic_properties, l1, l2, prop)
								{
									if(prop->editable)
										continue; // skip writable

									_property_remove(mod, group, prop);
									_property_free(prop);
								}
							}
							else // !wildcard
							{
								property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

								if(prop)
								{
									_property_remove(mod, group, prop);
									_property_free(prop);
								}
							}
						}
						else if(atom_prop->key == ui->regs.patch.writable.urid)
						{
							if(subject && (subject->body != mod->subject))
								continue; // ignore alien patch events

							const LV2_URID tar_urid = ((const LV2_Atom_URID *)&atom_prop->value)->body;
							if(tar_urid == ui->regs.patch.wildcard.urid)
							{
								// delete all readable dynamic properties of this module
								Eina_List *l1, *l2;
								property_t *prop;
								EINA_LIST_FOREACH_SAFE(mod->dynamic_properties, l1, l2, prop)
								{
									if(!prop->editable)
										continue; // skip readable

									_property_remove(mod, group, prop);
									_property_free(prop);
								}
							}
							else // !wildcard
							{
								property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

								if(prop)
								{
									_property_remove(mod, group, prop);
									_property_free(prop);
								}
							}
						}
						else if(atom_prop->key == ui->regs.rdfs.label.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop && prop->label)
							{
								free(prop->label);
								prop->label = NULL;
							}
						}
						else if(atom_prop->key == ui->regs.rdfs.comment.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop && prop->comment)
							{
								free(prop->comment);
								prop->comment = NULL;
							}
						}
						else if(atom_prop->key == ui->regs.rdfs.range.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
								prop->type_urid = 0;
						}
						else if(atom_prop->key == ui->regs.core.minimum.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
								prop->minimum = 0.f;
						}
						else if(atom_prop->key == ui->regs.core.maximum.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
								prop->maximum = 1.f;
						}
						else if(atom_prop->key == ui->regs.units.unit.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
								prop->unit = 0;
						}
						else if(atom_prop->key == ui->regs.core.scale_point.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								point_t *p;
								EINA_LIST_FREE(prop->scale_points, p)
								{
									free(p->label);
									free(p->s);
									free(p);
								}
							}
						}
					}

					LV2_ATOM_OBJECT_FOREACH(add, atom_prop)
					{
						if(atom_prop->key == ui->regs.patch.readable.urid)
						{
							if(subject && (subject->body != mod->subject))
								continue; // ignore alien patch events

							property_t *prop = calloc(1, sizeof(property_t));
							if(prop)
							{
								prop->mod = mod;
								prop->editable = 0;
								prop->is_bitmask = false;
								prop->tar_urid = ((const LV2_Atom_URID *)&atom_prop->value)->body;
								prop->label = NULL; // not yet known
								prop->comment = NULL; // not yet known
								prop->type_urid = 0; // not yet known
								prop->minimum = 0.f; // not yet known
								prop->maximum = 1.f; // not yet known
								prop->unit = 0; // not yet known

								mod->dynamic_properties = eina_list_sorted_insert(mod->dynamic_properties, _urid_cmp, prop);

								// append property to corresponding group
								if(group)
									group->children = eina_list_append(group->children, prop);

								// append property to UI
								if(parent && mod->std.list) //TODO remove duplicate code
								{
									Elm_Object_Item *elmnt = elm_genlist_item_sorted_insert(mod->std.list,
										ui->propitc, prop, parent, ELM_GENLIST_ITEM_NONE, _propitc_cmp,
										NULL, NULL);
									if(elmnt)
									{
										int select_mode = ELM_OBJECT_SELECT_MODE_NONE;
										elm_genlist_item_select_mode_set(elmnt, select_mode);
										_ui_property_tooltip_add(ui, elmnt, prop);
										prop->std.elmnt = elmnt;
									}
								}
							}
						}
						else if(atom_prop->key == ui->regs.patch.writable.urid)
						{
							if(subject && (subject->body != mod->subject))
								continue; // ignore alien patch events

							property_t *prop = calloc(1, sizeof(property_t));
							if(prop)
							{
								prop->mod = mod;
								prop->editable = 1;
								prop->is_bitmask = false;
								prop->tar_urid = ((const LV2_Atom_URID *)&atom_prop->value)->body;
								prop->label = NULL; // not yet known
								prop->comment = NULL; // not yet known
								prop->type_urid = 0; // not yet known
								prop->minimum = 0.f; // not yet known
								prop->maximum = 1.f; // not yet known
								prop->unit = 0; // not yet known

								mod->dynamic_properties = eina_list_sorted_insert(mod->dynamic_properties, _urid_cmp, prop);

								// append property to corresponding group
								if(group)
									group->children = eina_list_append(group->children, prop);

								// append property to UI
								if(parent && mod->std.list) //TODO remove duplicate code
								{
									Elm_Object_Item *elmnt = elm_genlist_item_sorted_insert(mod->std.list,
										ui->propitc, prop, parent, ELM_GENLIST_ITEM_NONE, _propitc_cmp,
										NULL, NULL);
									if(elmnt)
									{
										int select_mode = (prop->type_urid == ui->forge.String)
											|| (prop->type_urid == ui->forge.URI)
												? ELM_OBJECT_SELECT_MODE_DEFAULT
												: ELM_OBJECT_SELECT_MODE_NONE;
										elm_genlist_item_select_mode_set(elmnt, select_mode);
										_ui_property_tooltip_add(ui, elmnt, prop);
										prop->std.elmnt = elmnt;
									}
								}
							}
						}
						else if(atom_prop->key == ui->regs.rdfs.label.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								prop->label = strndup(LV2_ATOM_BODY_CONST(&atom_prop->value), atom_prop->value.size);
								if(prop->std.elmnt)
									elm_genlist_item_update(prop->std.elmnt);
							}
						}
						else if(atom_prop->key == ui->regs.rdfs.comment.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								prop->comment = strndup(LV2_ATOM_BODY_CONST(&atom_prop->value), atom_prop->value.size);
								if(prop->std.elmnt)
								{
									_ui_property_tooltip_add(ui, prop->std.elmnt, prop);
									elm_genlist_item_update(prop->std.elmnt);
								}
							}
						}
						else if(atom_prop->key == ui->regs.rdfs.range.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								prop->type_urid = ((const LV2_Atom_URID *)&atom_prop->value)->body;
								if(prop->std.elmnt)
									elm_genlist_item_update(prop->std.elmnt);
							}
						}
						else if(atom_prop->key == ui->regs.core.minimum.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								if(atom_prop->value.type == ui->forge.Int)
									prop->minimum = ((const LV2_Atom_Int *)&atom_prop->value)->body;
								else if(atom_prop->value.type == ui->forge.Long)
									prop->minimum = ((const LV2_Atom_Long *)&atom_prop->value)->body;
								else if(atom_prop->value.type == ui->forge.Float)
									prop->minimum = ((const LV2_Atom_Float *)&atom_prop->value)->body;
								else if(atom_prop->value.type == ui->forge.Double)
									prop->minimum = ((const LV2_Atom_Double *)&atom_prop->value)->body;

								if(prop->std.elmnt)
									elm_genlist_item_update(prop->std.elmnt);
							}
						}
						else if(atom_prop->key == ui->regs.core.maximum.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								if(atom_prop->value.type == ui->forge.Int)
									prop->maximum = ((const LV2_Atom_Int *)&atom_prop->value)->body;
								else if(atom_prop->value.type == ui->forge.Long)
									prop->maximum = ((const LV2_Atom_Long *)&atom_prop->value)->body;
								else if(atom_prop->value.type == ui->forge.Float)
									prop->maximum = ((const LV2_Atom_Float *)&atom_prop->value)->body;
								else if(atom_prop->value.type == ui->forge.Double)
									prop->maximum = ((const LV2_Atom_Double *)&atom_prop->value)->body;

								if(prop->std.elmnt)
									elm_genlist_item_update(prop->std.elmnt);
							}
						}
						else if(atom_prop->key == ui->regs.units.unit.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								if(atom_prop->value.type == ui->forge.URID)
									prop->unit = ((const LV2_Atom_URID *)&atom_prop->value)->body;

								if(prop->std.elmnt)
									elm_genlist_item_update(prop->std.elmnt);
							}
						}
						else if(atom_prop->key == ui->regs.core.scale_point.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								const LV2_Atom_Object *point_obj = (const LV2_Atom_Object *)&atom_prop->value;

								const LV2_Atom_String *point_label = NULL;
								const LV2_Atom *point_value = NULL;

								LV2_Atom_Object_Query point_q[] = {
									{ ui->regs.rdfs.label.urid, (const LV2_Atom **)&point_label },
									{ ui->regs.rdf.value.urid, (const LV2_Atom **)&point_value },
									{ 0, NULL }
								};
								lv2_atom_object_query(point_obj, point_q);

								if(point_label && point_value)
								{
									point_t *p = calloc(1, sizeof(point_t));
									p->label = strndup(LV2_ATOM_BODY_CONST(point_label), point_label->atom.size);
									if(point_value->type == ui->forge.Int)
									{
										p->d = calloc(1, sizeof(double));
										*p->d = ((const LV2_Atom_Int *)point_value)->body;
									}
									else if(point_value->type == ui->forge.Float)
									{
										p->d = calloc(1, sizeof(double));
										*p->d = ((const LV2_Atom_Float *)point_value)->body;
									}
									else if(point_value->type == ui->forge.Long)
									{
										p->d = calloc(1, sizeof(double));
										*p->d = ((const LV2_Atom_Long *)point_value)->body;
									}
									else if(point_value->type == ui->forge.Double)
									{
										p->d = calloc(1, sizeof(double));
										*p->d = ((const LV2_Atom_Double *)point_value)->body;
									}
									//FIXME do other types
									else if(point_value->type == ui->forge.String)
									{
										p->s = strndup(LV2_ATOM_BODY_CONST(point_value), point_value->size);
									}

									prop->scale_points = eina_list_append(prop->scale_points, p);

									if(prop->std.elmnt)
										elm_genlist_item_update(prop->std.elmnt);
								}
							}
						}
					}
				}
				else
					fprintf(stderr, "patch:Patch one of patch:subject, patch:add, patch:add missing\n");
			}
		}
	}
	else
		fprintf(stderr, "unknown protocol\n");
}

void
_std_ui_write_function(LV2UI_Controller controller, uint32_t index,
	uint32_t size, uint32_t protocol, const void *buf)
{
	mod_t *mod = controller;
	mod_ui_t *mod_ui = mod->mod_ui;

	// to rt-thread
	_ui_write_function(controller, index, size, protocol, buf);

	if(mod_ui)
		mod_ui->driver->port_event(mod, index, size, protocol, buf);
}
