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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <app.h>
#include <patcher.h>

// include lv2 core header
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>

// include lv2 extension headers
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

typedef enum _job_type_t job_type_t;
typedef struct _job_t job_t;
typedef struct _ui_write_t ui_write_t;

enum  _job_type_t {
	JOB_TYPE_MODULE_ADD,
	JOB_TYPE_MODULE_DEL,
	JOB_TYPE_CONN_ADD,
	JOB_TYPE_CONN_DEL
};

struct _job_t {
	job_type_t type;
	union {
		mod_t *mod; // pointer to elsewhere
		conn_t conn;
	} payload;
};

struct _ui_write_t {
	uint32_t size;
	uint32_t protocol;
	uint32_t port;
};

#define JOB_SIZE ( sizeof(job_t) )

#define UI_WRITE_SIZE ( sizeof(ui_write_t) )
#define UI_WRITE_PADDED ( (UI_WRITE_SIZE + 7U) & (~7U) )

// rt-thread
static void
_pacemaker_cb(uv_timer_t *pacemaker)
{
	app_t *app = pacemaker->data;

	// handle jobs
	{
		const void *ptr;
		size_t toread;
		while( (ptr = varchunk_read_request(app->rt.to, &toread)) )
		{
			const job_t *job = ptr;

			switch(job->type)
			{
				case JOB_TYPE_MODULE_ADD: // inject module
				{
					mod_t *mod = job->payload.mod;
					app->mods = eina_inlist_append(app->mods, EINA_INLIST_GET(mod));
					break;
				}
				case JOB_TYPE_MODULE_DEL:
				{
					// never called
					break;
				}
				case JOB_TYPE_CONN_ADD: // inject connection
				{
					const conn_t *conn = &job->payload.conn;
					// rewire source port output buffer to sink input buffer
					lilv_instance_connect_port(
						conn->sink->mod->inst,
						conn->sink->index,
						conn->source->buf);

					break;
				}
				case JOB_TYPE_CONN_DEL: // eject connection
				{
					const conn_t *conn = &job->payload.conn;
					// rewire source port output buffer to itself 
					lilv_instance_connect_port(
						conn->sink->mod->inst,
						conn->sink->index,
						conn->sink->buf);

					break;
				}
			}

			varchunk_read_advance(app->rt.to);
		}
	}

	//TODO FIXME
	// clear atom inputs
	mod_t *mod;
	EINA_INLIST_FOREACH(app->mods, mod)
	{
		if(mod->dead)
			continue; // ignore dead mods

		for(int i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];

			if(  (port->type == PORT_TYPE_ATOM)
				&& (port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) )
			{
				LV2_Atom_Sequence *seq = port->buf;

				seq->atom.type = app->regs.port.sequence.urid;
				seq->atom.size = port->direction == PORT_DIRECTION_INPUT
					? sizeof(LV2_Atom_Sequence_Body) // empty sequence
					: app->seq_size; // capacity
			}
		}
	}

	Eina_Inlist *l;
	EINA_INLIST_FOREACH_SAFE(app->mods, l, mod)
	{
		if(mod->dead) // handle dead modules
		{
			// eject module
			app->mods = eina_inlist_remove(app->mods, EINA_INLIST_GET(mod));

			void *ptr;
			if( (ptr = varchunk_write_request(app->rt.from, JOB_SIZE)) )
			{
				job_t *job = ptr;

				job->type = JOB_TYPE_MODULE_DEL;
				job->payload.mod = mod;

				varchunk_write_advance(app->rt.from, JOB_SIZE);
			}

			continue; // skip dead modules
		}

		// handle work
		if(mod->worker.iface && mod->worker.from)
		{
			const void *ptr;
			size_t toread;
			while( (ptr = varchunk_read_request(mod->worker.from, &toread)) )
			{
				if(mod->worker.iface->work_response)
					mod->worker.iface->work_response(mod->handle, toread, ptr);

				varchunk_read_advance(mod->worker.from);
			}
			if(mod->worker.iface->end_run)
				mod->worker.iface->end_run(mod->handle);
		}

		// handle ui pre
		if(mod->ui.from)
		{
			const void *ptr;
			size_t toread;
			while( (ptr = varchunk_read_request(mod->ui.from, &toread)) )
			{
				const ui_write_t *ui_write = ptr;
				const void *body = ptr + UI_WRITE_PADDED;
				port_t *port = &mod->ports[ui_write->port];
				void *buf = port->buf;

				if(ui_write->protocol == app->regs.port.float_protocol.urid)
				{
					const float *val = body;
					*(float *)buf = *val;
					port->last = *val;
				}
				else if(ui_write->protocol == app->regs.port.atom_transfer.urid)
				{
					const LV2_Atom *atom = body;
					memcpy(buf, atom, sizeof(LV2_Atom) + atom->size);
				}
				else if(ui_write->protocol == app->regs.port.event_transfer.urid)
				{
					const LV2_Atom *atom = body;
					LV2_Atom_Sequence *seq = buf;

					// find last event in sequence
					LV2_Atom_Event *last = NULL;
					LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
						last = ev;

					void *ptr;
					if(last)
					{
						ptr = last;
						ptr += sizeof(LV2_Atom_Event) + last->body.size;
					}
					else
						ptr = LV2_ATOM_CONTENTS(LV2_Atom_Sequence, seq);

					// append event at end of sequence
					// TODO check for buffer overflow
					LV2_Atom_Event *new_last = ptr;
					new_last->time.frames = last ? last->time.frames : 0;
					memcpy(&new_last->body, atom, sizeof(LV2_Atom) + atom->size);
					seq->atom.size += sizeof(LV2_Atom_Event) + ((atom->size + 7U) & (~7U));
				}
				else
					; //ignore, protocol not supported

				varchunk_read_advance(mod->ui.from);
			}
		}

		// run plugin
		lilv_instance_run(mod->inst, app->period_size);
		
		// handle ui post
		if(mod->ui.to)
		{
			for(int i=0; i<mod->num_ports; i++)
			{
				port_t *port = &mod->ports[i];

				if(port->protocol == 0) // no notification/subscription
					continue;

				if(port->protocol == app->regs.port.float_protocol.urid)
				{
					float val = *(float *)port->buf;
					if(val != port->last)
					{
						// update last value
						port->last = val;

						// transfer single float
						void *ptr;
						size_t request = UI_WRITE_PADDED + sizeof(float);
						if( (ptr = varchunk_write_request(mod->ui.to, request)) )
						{
							ui_write_t *ui_write = ptr;
							ui_write->size = sizeof(float);
							ui_write->protocol = port->protocol;
							ui_write->port = i;
							ptr += UI_WRITE_PADDED;

							*(float *)ptr = val;
							varchunk_write_advance(mod->ui.to, request);
						}
						else
							; //TODO
					}
				}
				else if(port->protocol == app->regs.port.peak_protocol.urid)
				{
					const float *buf = (const float *)port->buf;

					// find peak value in current period
					float peak = 0.f;
					for(int j=0; j<app->period_size; j++)
					{
						float val = fabs(buf[j]);
						if(val > peak)
							peak = val;
					}

					port->period_cnt++;

					if(  (peak != port->last) //TODO make below two configurable
						&& (fabs(peak - port->last) > 0.001) // ignore smaller changes
						&& ((port->period_cnt & 0x1f) == 0x00) ) // only update every 32 samples
					{
						printf("peak different: %i %i\n", port->last == 0.f, peak == 0.f);

						// update last value
						port->last = peak;

						void *ptr;
						size_t request = UI_WRITE_PADDED + sizeof(LV2UI_Peak_Data);
						if( (ptr = varchunk_write_request(mod->ui.to, request)) )
						{
							ui_write_t *ui_write = ptr;
							ui_write->size = sizeof(LV2UI_Peak_Data);
							ui_write->protocol = port->protocol;
							ui_write->port = i;
							ptr += UI_WRITE_PADDED;

							LV2UI_Peak_Data *peak_data = ptr;
							peak_data->period_start = port->period_cnt;
							peak_data->period_size = app->period_size;
							peak_data->peak = peak;

							varchunk_write_advance(mod->ui.to, request);
						}
						else
							; //TODO
					}
				}
				else if(port->protocol == app->regs.port.event_transfer.urid)
				{
					const LV2_Atom_Sequence *seq = port->buf;
					if(seq->atom.size <= sizeof(LV2_Atom_Sequence_Body)) // empty seq
						continue;

					// transfer each atom of sequence separately
					LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
					{
						void *ptr;
						const LV2_Atom *atom = &ev->body;
						size_t request = UI_WRITE_PADDED + sizeof(LV2_Atom) + atom->size;
						if( (ptr = varchunk_write_request(mod->ui.to, request)) )
						{
							ui_write_t *ui_write = ptr;
							ui_write->size = sizeof(LV2_Atom) + atom->size;
							ui_write->protocol = port->protocol;
							ui_write->port = i;
							ptr += UI_WRITE_PADDED;

							memcpy(ptr, atom, sizeof(LV2_Atom) + atom->size);
							varchunk_write_advance(mod->ui.to, request);
						}
						else
							; //TODO
					}
				}
				else if(port->protocol == app->regs.port.atom_transfer.urid)
				{
					const LV2_Atom *atom = port->buf;
					if(atom->size == 0) // empty atom
						continue;
					
					void *ptr;
					size_t request = UI_WRITE_PADDED + sizeof(LV2_Atom) + atom->size;
					if( (ptr = varchunk_write_request(mod->ui.to, request)) )
					{
						ui_write_t *ui_write = ptr;
						ui_write->size = sizeof(LV2_Atom) + atom->size;
						ui_write->protocol = port->protocol;
						ui_write->port = i;
						ptr += UI_WRITE_PADDED;

						memcpy(ptr, atom, sizeof(LV2_Atom) + atom->size);
						varchunk_write_advance(mod->ui.to, request);
					}
					else
						; //TODO
				}
			}
		}
	}
}

// rt-thread
static LV2_Worker_Status
_schedule_work(LV2_Worker_Schedule_Handle handle, uint32_t size, const void *data)
{
	mod_t *mod = handle;

	void *buf;
	if( (buf = varchunk_write_request(mod->worker.to, size)) )
	{
		// copy data to varchunk buffer
		memcpy(buf, data, size);
		varchunk_write_advance(mod->worker.to, size);

		// wake up worker thread
		uv_async_send(&mod->worker.async);

		return LV2_WORKER_SUCCESS;
	}

	return LV2_WORKER_ERR_NO_SPACE;
}

// non-rt ui-thread
static void
_delete_request(void *data, Evas_Object *obj, void *event)
{
	elm_exit();
}

// non-rt ui-thread
static Eina_Bool
_rt_animator(void *data)
{
	app_t *app = data;

	const void *ptr;
	size_t toread;
	while( (ptr = varchunk_read_request(app->rt.from, &toread)) )
	{
		const job_t *job = ptr;

		if(job->type == JOB_TYPE_MODULE_DEL)
		{
			mod_t *mod = job->payload.mod;

			app_mod_del(app, mod);
		}
		else if(job->type == JOB_TYPE_CONN_DEL)
		{
			const conn_t *conn = &job->payload.conn;
			//TODO
		}

		varchunk_read_advance(app->rt.from);
	}

	return EINA_TRUE;
}

// non-rt ui-thread
static Eina_Bool
_idle_animator(void *data)
{
	mod_t *mod = data;

	// call idle callback
	mod->ui.eo.idle_interface->idle(mod->ui.eo.handle);

	return EINA_TRUE;
}

// non-rt ui-thread
static Eina_Bool
_port_event_animator(void *data)
{
	mod_t *mod = data;
		
	// handle pending port notifications
	const void *ptr;
	size_t toread;
	while( (ptr = varchunk_read_request(mod->ui.to, &toread)) )
	{
		const ui_write_t *ui_write = ptr;
		const void *buf = ptr + UI_WRITE_PADDED;

		// update EoUI, if present
		if(  mod->ui.eo.ui
			&& mod->ui.eo.descriptor
			&& mod->ui.eo.descriptor->port_event
			&& mod->ui.eo.handle) //TODO simplify check
		{
			mod->ui.eo.descriptor->port_event(mod->ui.eo.handle,
				ui_write->port, ui_write->size, ui_write->protocol, buf);
		}

		// update StdUI (descriptor is always present, thus no check)
		mod->ui.std.descriptor.port_event(mod,
			ui_write->port, ui_write->size, ui_write->protocol, buf);

		varchunk_read_advance(mod->ui.to);
	}

	return EINA_TRUE;
}

// non-rt ui-thread
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

// non-rt ui-thread
static void
_pluglist_activated(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	app_t *app = data;
	const LilvPlugin *plug = elm_object_item_data_get(itm);;
		
	const LilvNode *uri_node = lilv_plugin_get_uri(plug);
	const char *uri_str = lilv_node_as_string(uri_node);

	mod_t *mod = app_mod_add(app, uri_str);

	if(mod)
	{
		mod->ui.std.itm = elm_genlist_item_append(app->ui.modlist, app->ui.moditc, mod, NULL,
			ELM_GENLIST_ITEM_TREE, NULL, NULL);
	
		if(mod->ui.eo.ui) // has EoUI
		{
			mod->ui.eo.itm = elm_gengrid_item_append(app->ui.modgrid, app->ui.griditc, mod,
				NULL, NULL);
		}
	}
}

// non-rt ui-thread
static void
_list_expand_request(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	app_t *app = data;

	Eina_Bool selected = elm_genlist_item_selected_get(itm);
	elm_genlist_item_expanded_set(itm, EINA_TRUE);
	elm_genlist_item_selected_set(itm, !selected); // preserve selection
}

// non-rt ui-thread
static void
_list_contract_request(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	app_t *app = data;

	Eina_Bool selected = elm_genlist_item_selected_get(itm);
	elm_genlist_item_expanded_set(itm, EINA_FALSE);
	elm_genlist_item_selected_set(itm, !selected); // preserve selection
}

// non-rt ui-thread
static void
_std_port_event(LV2UI_Handle ui, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = ui;
	app_t *app = mod->app;
	port_t *port = &mod->ports[index];

	//printf("_std_port_event: %u %u %u\n", index, size, protocol);

	if(protocol == 0)
		protocol = app->regs.port.float_protocol.urid;

	// check for subscription AND matching protocol
	if(protocol != port->protocol)
		return;

	// check for expanded list
	if(!elm_genlist_item_expanded_get(mod->ui.std.itm))
		return;

	// check for realized port widget
	if(!port->std.widget)
		return;

	if(protocol == app->regs.port.float_protocol.urid)
	{
		const float val = *(float *)buf;
		int toggled = lilv_port_has_property(mod->plug, port->tar, app->regs.port.toggled.node);

		if(toggled)
			elm_check_state_set(port->std.widget, val > 0.f ? EINA_TRUE : EINA_FALSE);
		else if(port->points)
		{
			int cnt = elm_segment_control_item_count_get(port->std.widget);
			for(int i=0; i<cnt; i++)
			{
				Elm_Object_Item *itm = elm_segment_control_item_get(port->std.widget, i);
				const LilvScalePoint *point = elm_object_item_data_get(itm);
				const LilvNode *val_node = lilv_scale_point_get_value(point);
				const float value = lilv_node_as_float(val_node);

				if(val == value)
				{
					elm_segment_control_item_selected_set(itm, EINA_TRUE);
					break;
				}
			}
		}
		else // integer or float
			elm_slider_value_set(port->std.widget, val);
	}
	else if(protocol == app->regs.port.peak_protocol.urid)
	{
		const LV2UI_Peak_Data *peak_data = buf;
		printf("peak: %f\n", peak_data->peak);
		elm_progressbar_value_set(port->std.widget, peak_data->peak);
	}
	else
		; //TODO atom, sequence
}

// non-rt ui-thread
static void
_eo_port_event(LV2UI_Handle ui, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = ui;
	app_t *app = mod->app;

	//printf("_eo_port_event: %u %u %u\n", index, size, protocol);

	if(  mod->ui.eo.ui
		&& mod->ui.eo.descriptor
		&& mod->ui.eo.descriptor->port_event
		&& mod->ui.eo.handle)
	{
		mod->ui.eo.descriptor->port_event(mod->ui.eo.handle,
			index, size, protocol, buf);
	}
}

// non-rt ui-thread
static void
_modlist_expanded(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	mod_t *mod = elm_object_item_data_get(itm);
	app_t *app = data;

	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		// only add control, audio, cv ports
		if(port->type == PORT_TYPE_ATOM)
			continue;

		Elm_Object_Item *elmnt;
		elmnt = elm_genlist_item_append(app->ui.modlist, app->ui.stditc, port, itm,
			ELM_GENLIST_ITEM_NONE, NULL, NULL);
		elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_NONE);
	}
}

// non-rt ui-thread
static void
_modlist_contracted(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	mod_t *mod = elm_object_item_data_get(itm);
	app_t *app = data;

	// clear items
	elm_genlist_item_subitems_clear(itm);
}

// non-rt ui-thread
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

// non-rt ui-thread
static void
_modlist_icon_clicked(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	app_t *app = mod->app;

	// remove StdUI list item
	elm_genlist_item_expanded_set(mod->ui.std.itm, EINA_FALSE);
	elm_object_item_del(mod->ui.std.itm);
	mod->ui.std.itm = NULL;

	// remove EoUI grid item, if present
	if(mod->ui.eo.itm)
	{
		elm_object_item_del(mod->ui.eo.itm);
		mod->ui.eo.itm = NULL;
	}
}

// non-rt ui-thread
static void
_patches_update(app_t *app)
{
	int count [PORT_DIRECTION_NUM][PORT_TYPE_NUM];
	// clear counters
	memset(&count, 0, PORT_DIRECTION_NUM*PORT_TYPE_NUM*sizeof(int));

	// count input|output ports per type
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(app->ui.modlist);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != app->ui.moditc)
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
		patcher_object_dimension_set(app->ui.matrix[t], 
			count[PORT_DIRECTION_OUTPUT][t], // sources
			count[PORT_DIRECTION_INPUT][t]); // sinks
	}

	// clear counters
	memset(&count, 0, PORT_DIRECTION_NUM*PORT_TYPE_NUM*sizeof(int));

	// populate patchers
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(app->ui.modlist);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != app->ui.moditc)
			continue; // ignore port items

		mod_t *mod = elm_object_item_data_get(itm);
		if(!mod->selected)
			continue; // ignore unselected mods

		for(int i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];
			if(!port->selected)
				continue; // ignore unselected ports
			
			//patcher_object_state_set(app->ui.matrix[port->type], 0, 0, EINA_TRUE);
			if(port->direction == PORT_DIRECTION_OUTPUT) // source
			{
				patcher_object_source_data_set(app->ui.matrix[port->type],
					count[port->direction][port->type], port);
			}
			else // sink
			{
				patcher_object_sink_data_set(app->ui.matrix[port->type],
					count[port->direction][port->type], port);
			}
		
			printf("%p\n", port);
			count[port->direction][port->type] += 1;
		}
	}
}

// non-rt ui-thread
static void
_modlist_check_changed(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	app_t *app = mod->app;

	mod->selected = elm_check_state_get(obj);
	printf("_modlist_check_changed: %i\n", mod->selected);
	_patches_update(app);
}

// non-rt ui-thread
static Evas_Object *
_modlist_content_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;

	if(!strcmp(part, "elm.swallow.icon"))
	{
		Evas_Object *check = elm_check_add(obj);
		elm_check_state_set(check, mod->selected);
		evas_object_smart_callback_add(check, "changed", _modlist_check_changed, mod);
		evas_object_show(check);

		return check;
	}
	else if(!strcmp(part, "elm.swallow.end"))
	{
		Evas_Object *icon = elm_icon_add(obj);
		elm_icon_standard_set(icon, "close");
		evas_object_smart_callback_add(icon, "clicked", _modlist_icon_clicked, mod);
		evas_object_size_hint_max_set(icon, 16, 16);
		evas_object_show(icon);

		return icon;
	}
	else
		return NULL;
}

static inline int
_match_port_protocol(port_t *port, uint32_t protocol, uint32_t size)
{
	mod_t *mod = port->mod;
	app_t *app = mod->app;

	if(  (protocol == app->regs.port.float_protocol.urid)
		&& (port->type == PORT_TYPE_CONTROL)
		&& (size == sizeof(float)) )
	{
		return 1;
	}
	else if ( (protocol == app->regs.port.peak_protocol.urid)
		&& ((port->type == PORT_TYPE_AUDIO) || (port->type == PORT_TYPE_CV)) )
	{
		return 1;
	}
	else if( (protocol == app->regs.port.atom_transfer.urid)
				&& (port->type == PORT_TYPE_ATOM) )
	{
		return 1;
	}
	else if( (protocol == app->regs.port.event_transfer.urid)
				&& (port->type == PORT_TYPE_ATOM)
				&& (port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) )
	{
		return 1;
	}

	return 0;
}

// non-rt ui-thread
static void
_ui_write_function(LV2UI_Controller controller, uint32_t port,
	uint32_t size, uint32_t protocol, const void *buffer)
{
	mod_t *mod = controller;
	app_t *app = mod->app;
	port_t *tar = &mod->ports[port];

	// ignore output ports
	if(tar->direction != PORT_DIRECTION_INPUT)
	{
		fprintf(stderr, "_ui_write_function: UI can only write to input port\n");
		return;
	}

	// handle special meaning of protocol=0
	if(protocol == 0)
		protocol = app->regs.port.float_protocol.urid;

	// check for matching protocol <-> port type
	if(!_match_port_protocol(tar, protocol, size))
	{
		fprintf(stderr, "_ui_write_function: port type - protocol mismatch\n");
		return;
	}

	ui_write_t header = {
		.size = size,
		.protocol = protocol,
		.port = port
	};

	const size_t padded = UI_WRITE_PADDED + size;

	void *ptr;
	if( (ptr = varchunk_write_request(mod->ui.from, padded)) )
	{
		memcpy(ptr, &header, UI_WRITE_SIZE);
		memcpy(ptr + UI_WRITE_PADDED, buffer, size);
		varchunk_write_advance(mod->ui.from, padded);
	}
	else
		fprintf(stderr, "_ui_write_function: buffer overflow\n");
}

// non-rt ui-thread
static void
_eo_ui_write_function(LV2UI_Controller controller, uint32_t port,
	uint32_t size, uint32_t protocol, const void *buffer)
{
	// to rt-thread
	_ui_write_function(controller, port, size, protocol, buffer);

	// to StdUI
	_std_port_event(controller, port, size, protocol, buffer);
}

// non-rt ui-thread
static void
_std_ui_write_function(LV2UI_Controller controller, uint32_t port,
	uint32_t size, uint32_t protocol, const void *buffer)
{
	// to rt-thread
	_ui_write_function(controller, port, size, protocol, buffer);

	// to EoUI
	_eo_port_event(controller, port, size, protocol, buffer);
}

// non-rt ui-thread
static uint32_t
_port_subscribe(LV2UI_Feature_Handle handle, uint32_t index, uint32_t protocol,
	const LV2_Feature *const *features) //TODO what are the features for?
{
	mod_t *mod = handle;
	app_t *app = mod->app;
			
	if(protocol == 0)
		protocol = app->regs.port.float_protocol.urid;
	
	if(index < mod->num_ports)
	{
		port_t *port = &mod->ports[index];
	
		if(  (port->protocol == 0) // not already subscribed
			&& _match_port_protocol(port, protocol, sizeof(float)) ) // matching protocols?
		{
			port->protocol = protocol; // atomic instruction!
			return 0; // success
		}
	}

	return 1; // fail
}

// non-rt ui-thread
static uint32_t
_port_unsubscribe(LV2UI_Feature_Handle handle, uint32_t index, uint32_t protocol,
	const LV2_Feature *const *features) //TODO what are the features for?
{
	mod_t *mod = handle;
	app_t *app = mod->app;

	if(protocol == 0)
		protocol = app->regs.port.float_protocol.urid;

	if(index < mod->num_ports)
	{
		port_t *port = &mod->ports[index];

		if(port->protocol == protocol) // matching protocols?
		{
			port->protocol = 0; // atomic instruction! 
			return 0; // success
		}
	}

	return 1; // fail
}

// non-rt ui-thread
static void
_patched_changed(void *data, Evas_Object *obj, void *event)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	app_t *app = mod->app;

	port->selected = elm_check_state_get(obj);
	_patches_update(app);
}

// non-rt ui-thread
static void
_check_changed(void *data, Evas_Object *obj, void *event)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	app_t *app = mod->app;

	float val = elm_check_state_get(obj);

	_std_ui_write_function(mod, port->index, sizeof(float),
		app->regs.port.float_protocol.urid, &val);
}

// non-rt ui-thread
static void
_segment_control_changed(void *data, Evas_Object *obj, void *event)
{
	Elm_Object_Item *itm = event;
	port_t *port = data;
	mod_t *mod = port->mod;
	app_t *app = mod->app;

	const LilvScalePoint *point = elm_object_item_data_get(itm);
	const LilvNode *val_node = lilv_scale_point_get_value(point);
	const float val = lilv_node_as_float(val_node);

	_std_ui_write_function(mod, port->index, sizeof(float),
		app->regs.port.float_protocol.urid, &val);
}

// non-rt ui-thread
static void
_sldr_changed(void *data, Evas_Object *obj, void *event)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	app_t *app = mod->app;

	float val = elm_slider_value_get(obj);

	_std_ui_write_function(mod, port->index, sizeof(float),
		app->regs.port.float_protocol.urid, &val);
}

// non-rt ui-thread
static Evas_Object * 
_modlist_std_content_get(void *data, Evas_Object *obj, const char *part)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	app_t *app = mod->app;
	
	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	const char *type_str = NULL;
	const LilvNode *name_node = lilv_port_get_name(mod->plug, port->tar);
	type_str = lilv_node_as_string(name_node);

	Evas_Object *hbox = elm_box_add(obj);
	elm_box_horizontal_set(hbox, EINA_TRUE);
	elm_box_homogeneous_set(hbox, EINA_FALSE);
	elm_box_padding_set(hbox, 0, 0);
	evas_object_size_hint_weight_set(hbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(hbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(hbox);
	
	Evas_Object *patched = elm_check_add(hbox);
	evas_object_smart_callback_add(patched, "changed", _patched_changed, port);
	evas_object_show(patched);
	elm_box_pack_end(hbox, patched);

	Evas_Object *child = NULL;
	if(port->type == PORT_TYPE_CONTROL)
	{
		int integer = lilv_port_has_property(mod->plug, port->tar, app->regs.port.integer.node);
		int toggled = lilv_port_has_property(mod->plug, port->tar, app->regs.port.toggled.node);
		float step_val = integer ? 1.f : (port->max - port->min) / 1000;

		if(toggled)
		{
			Evas_Object *check = elm_check_add(hbox);
			elm_check_state_set(check, port->dflt > 0.f ? EINA_TRUE : EINA_FALSE);
			elm_object_style_set(check, "toggle");
			evas_object_smart_callback_add(check, "changed", _check_changed, port);

			child = check;
			elm_object_text_set(child, type_str);
		}
		else if(port->points)
		{
			Evas_Object *lbl = elm_label_add(hbox);
			elm_object_text_set(lbl, type_str);
			evas_object_show(lbl);
			elm_box_pack_end(hbox, lbl);

			Evas_Object *seg = elm_segment_control_add(hbox);
			LILV_FOREACH(scale_points, itr, port->points)
			{
				const LilvScalePoint *point = lilv_scale_points_get(port->points, itr);
				const LilvNode *label_node = lilv_scale_point_get_label(point);
				const char *label_str = lilv_node_as_string(label_node);
				Elm_Object_Item *elmnt = elm_segment_control_item_add(seg, NULL, label_str);
				elm_object_item_data_set(elmnt, (void *)point);
			}
			evas_object_smart_callback_add(seg, "changed", _segment_control_changed, port);

			child = seg;
		}
		else // integer or float
		{
			Evas_Object *sldr = elm_slider_add(hbox);
			elm_slider_horizontal_set(sldr, EINA_TRUE);
			elm_slider_unit_format_set(sldr, integer ? "%.0f" : "%.4f");
			elm_slider_min_max_set(sldr, port->min, port->max);
			elm_slider_value_set(sldr, port->dflt);
			elm_slider_step_set(sldr, step_val);
			evas_object_smart_callback_add(sldr, "changed", _sldr_changed, port);

			child = sldr;
			elm_object_text_set(child, type_str);
		}
	}
	else if(port->type == PORT_TYPE_AUDIO
		|| port->type == PORT_TYPE_CV)
	{
		Evas_Object *prog = elm_progressbar_add(hbox);
		elm_progressbar_horizontal_set(prog, EINA_TRUE);
		elm_progressbar_unit_format_set(prog, NULL);
		elm_progressbar_value_set(prog, 0.f);

		child = prog;
		elm_object_text_set(child, type_str);
	}
	
	if(port->selected)
		elm_check_state_set(patched, EINA_TRUE);

	if(child)
	{
		elm_object_disabled_set(child, port->direction == PORT_DIRECTION_OUTPUT);
		evas_object_size_hint_weight_set(child, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(child, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(child);
		elm_box_pack_end(hbox, child);
	}

	// subscribe to port
	const uint32_t i = port->index;
	if(port->type == PORT_TYPE_CONTROL)
		_port_subscribe(mod, i, app->regs.port.float_protocol.urid, NULL);
	else if(port->type == PORT_TYPE_AUDIO)
		_port_subscribe(mod, i, app->regs.port.peak_protocol.urid, NULL);
	else if(port->type == PORT_TYPE_CV)
		_port_subscribe(mod, i, app->regs.port.peak_protocol.urid, NULL);
	else if(port->type == PORT_TYPE_ATOM)
	{
		if(port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE)
			_port_subscribe(mod, i, app->regs.port.event_transfer.urid, NULL);
		else
			_port_subscribe(mod, i, app->regs.port.atom_transfer.urid, NULL);
	}

	port->std.widget = child;
	return hbox;
}

// non-rt ui-thread
static void
_modlist_std_del(void *data, Evas_Object *obj)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	app_t *app = mod->app;

	port->std.widget = NULL;
	
	// unsubscribe from port
	const uint32_t i = port->index;
	if(port->type == PORT_TYPE_CONTROL)
		_port_unsubscribe(mod, i, app->regs.port.float_protocol.urid, NULL);
	else if(port->type == PORT_TYPE_AUDIO)
		_port_unsubscribe(mod, i, app->regs.port.peak_protocol.urid, NULL);
	else if(port->type == PORT_TYPE_CV)
		_port_unsubscribe(mod, i, app->regs.port.peak_protocol.urid, NULL);
	else if(port->type == PORT_TYPE_ATOM)
	{
		if(port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE)
			_port_unsubscribe(mod, i, app->regs.port.event_transfer.urid, NULL);
		else
			_port_unsubscribe(mod, i, app->regs.port.atom_transfer.urid, NULL);
	}
}

// non-rt ui-thread
static void
_modlist_del(void *data, Evas_Object *obj)
{
	mod_t *mod = data;

	mod->dead = 1; // atomic instruction!
}

// non-rt ui-thread
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

// non-rt ui-thread
static Evas_Object *
_modgrid_content_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;
	app_t *app = mod->app;

	if(strcmp(part, "elm.swallow.icon"))
		return NULL;

	if(mod->ui.eo.ui)
	{
		const LilvNode *plugin_uri = lilv_plugin_get_uri(mod->plug);
		const char *plugin_string = lilv_node_as_string(plugin_uri);

		//printf("has Eo UI\n");
		const LilvNode *ui_uri = lilv_ui_get_uri(mod->ui.eo.ui);
		const LilvNode *bundle_uri = lilv_ui_get_bundle_uri(mod->ui.eo.ui);
		const LilvNode *binary_uri = lilv_ui_get_binary_uri(mod->ui.eo.ui);

		const char *ui_string = lilv_node_as_string(ui_uri);
		const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));
		const char *binary_path = lilv_uri_to_path(lilv_node_as_string(binary_uri));

		//printf("ui_string: %s\n", ui_string);
		//printf("bundle_path: %s\n", bundle_path);
		//printf("binary_path: %s\n", binary_path);

		uv_dlopen(binary_path, &mod->ui.eo.lib); //TODO check
		
		LV2UI_DescriptorFunction ui_descfunc = NULL;
		uv_dlsym(&mod->ui.eo.lib, "lv2ui_descriptor", (void **)&ui_descfunc);

		if(ui_descfunc)
		{
			mod->ui.eo.descriptor = NULL;
			mod->ui.eo.widget = NULL;

			for(int i=0; 1; i++)
			{
				const LV2UI_Descriptor *ui_desc = ui_descfunc(i);
				if(!ui_desc) // end
					break;
				else if(!strcmp(ui_desc->URI, ui_string))
				{
					mod->ui.eo.descriptor = ui_desc;
					break;
				}
			}
		
			// get UI extension data
			if(mod->ui.eo.descriptor && mod->ui.eo.descriptor->extension_data)
			{
				mod->ui.eo.idle_interface = mod->ui.eo.descriptor->extension_data(
					LV2_UI__idleInterface);
			}

			// instantiate UI
			if(mod->ui.eo.descriptor && mod->ui.eo.descriptor->instantiate)
			{
				mod->ui.eo.handle = mod->ui.eo.descriptor->instantiate(
					mod->ui.eo.descriptor,
					plugin_string,
					bundle_path,
					_eo_ui_write_function,
					mod,
					(void **)&(mod->ui.eo.widget),
					mod->ui_features);
			}

	/* FIXME
	LilvNodes* notes               = lilv_world_find_nodes(
		lworld, lilv_ui_get_uri(ui), ui_portNotification, NULL);
	LILV_FOREACH(nodes, n, notes) {
		const LilvNode* note = lilv_nodes_get(notes, n);
		const LilvNode* sym  = lilv_world_get(lworld, note, lv2_symbol, NULL);
		const LilvNode* plug = lilv_world_get(lworld, note, ui_plugin, NULL);
		if (plug && !lilv_node_is_uri(plug)) {
			world->log().error(fmt("%1% UI has non-URI ui:plugin\n")
			                   % block->plugin()->uri().c_str());
			continue;
		}
		if (plug && strcmp(lilv_node_as_uri(plug), block->plugin()->uri().c_str())) {
			continue;  // Notification is for another plugin
		}
		if (sym) {
			uint32_t index = lv2_ui_port_index(ret.get(), lilv_node_as_string(sym));
			if (index != LV2UI_INVALID_PORT_INDEX) {
				lv2_ui_subscribe(ret.get(), index, 0, NULL);
				ret->_subscribed_ports.insert(index);
			}
		}
	}
	lilv_nodes_free(notes);
	*/

			// subscribe to all ports
			for(int i=0; i<mod->num_ports; i++)
			{
				port_t *port = &mod->ports[i];

				if(port->type == PORT_TYPE_CONTROL)
				{
					_port_subscribe(mod, i, app->regs.port.float_protocol.urid, NULL);
					// initialize StdUI and EoUI
					_eo_port_event(mod, i, sizeof(float), app->regs.port.float_protocol.urid, &port->dflt);
					_std_port_event(mod, i, sizeof(float), app->regs.port.float_protocol.urid, &port->dflt);
				}
				else if(port->type == PORT_TYPE_AUDIO)
					_port_subscribe(mod, i, app->regs.port.peak_protocol.urid, NULL);
				else if(port->type == PORT_TYPE_CV)
					_port_subscribe(mod, i, app->regs.port.peak_protocol.urid, NULL);
				else if(port->type == PORT_TYPE_ATOM)
				{
					if(port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE)
						_port_subscribe(mod, i, app->regs.port.event_transfer.urid, NULL);
					else
						_port_subscribe(mod, i, app->regs.port.atom_transfer.urid, NULL);
				}
			}

			// add idle animator
			if(mod->ui.eo.idle_interface)
				mod->ui.eo.idle_anim = ecore_animator_add(_idle_animator, mod);

			return mod->ui.eo.widget;
		}
	}

	return NULL;
}

// non-rt ui-thread
static void
_modgrid_del(void *data, Evas_Object *obj)
{
	mod_t *mod = data;
	app_t *app = mod->app;

	// unsubscribe from all ports
	for(int i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_unsubscribe(mod, i, app->regs.port.float_protocol.urid, NULL);
		else if(port->type == PORT_TYPE_AUDIO)
			_port_unsubscribe(mod, i, app->regs.port.peak_protocol.urid, NULL);
		else if(port->type == PORT_TYPE_CV)
			_port_unsubscribe(mod, i, app->regs.port.peak_protocol.urid, NULL);
		else if(port->type == PORT_TYPE_ATOM)
		{
			if(port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE) 
				_port_unsubscribe(mod, i, app->regs.port.event_transfer.urid, NULL);
			else
				_port_unsubscribe(mod, i, app->regs.port.atom_transfer.urid, NULL);
		}
	}

	// cleanup EoUI
	if(mod->ui.eo.ui)
	{
		if(  mod->ui.eo.descriptor
			&& mod->ui.eo.descriptor->cleanup
			&& mod->ui.eo.handle)
		{
			mod->ui.eo.descriptor->cleanup(mod->ui.eo.handle);
		}

		uv_dlclose(&mod->ui.eo.lib);
	}
	
	// del idle animator
	if(mod->ui.eo.idle_anim)
	{
		mod->ui.eo.idle_anim = ecore_animator_del(mod->ui.eo.idle_anim);
		mod->ui.eo.idle_interface = NULL;
		mod->ui.eo.idle_anim = NULL;
	}

	// clear parameters
	mod->ui.eo.descriptor = NULL;
	mod->ui.eo.handle = NULL;
	mod->ui.eo.widget = NULL;
}

static void
_matrix_connect(void *data, Evas_Object *obj, void *event_info)
{
	app_t *app = data;
	port_t **ports = event_info;
	port_t *source = ports[0];
	port_t *sink = ports[1];

	if(sink->links > 0) // TODO only one link per sink allowed for now
		return;

	printf("_matrix_connect: %p %p\n", source, sink);

	void *ptr;
	if( (ptr = varchunk_write_request(app->rt.to, JOB_SIZE)) )
	{
		job_t *job = ptr;

		job->type = JOB_TYPE_CONN_ADD;
		job->payload.conn.source = source;
		job->payload.conn.sink = sink;

		varchunk_write_advance(app->rt.to, JOB_SIZE);

		sink->links += 1;
		source->links += 1;
	}
	else
		fprintf(stderr, "rt varchunk buffer overrun");
}

static void
_matrix_disconnect(void *data, Evas_Object *obj, void *event_info)
{
	app_t *app = data;
	port_t **ports = event_info;
	port_t *source = ports[0];
	port_t *sink = ports[1];

	printf("_matrix_disconnect: %p %p\n", source, sink);

	void *ptr;
	if( (ptr = varchunk_write_request(app->rt.to, JOB_SIZE)) )
	{
		job_t *job = ptr;

		job->type = JOB_TYPE_CONN_DEL;
		job->payload.conn.source = source;
		job->payload.conn.sink = sink;

		varchunk_write_advance(app->rt.to, JOB_SIZE);
		
		sink->links -= 1;
		source->links -= 1;
	}
	else
		fprintf(stderr, "rt varchunk buffer overrun");
}

// non-rt ui-thread
app_t *
app_new()
{
	app_t *app = calloc(1, sizeof(app_t));

	app->world = lilv_world_new();
	lilv_world_load_all(app->world);
	app->plugs = lilv_world_get_all_plugins(app->world);

	app->ext_urid = ext_urid_new();

	// initialzie URI nodes
	app->regs.port.input.node = lilv_new_uri(app->world, LV2_CORE__InputPort);
	app->regs.port.output.node = lilv_new_uri(app->world, LV2_CORE__OutputPort);

	app->regs.port.control.node = lilv_new_uri(app->world, LV2_CORE__ControlPort);
	app->regs.port.audio.node = lilv_new_uri(app->world, LV2_CORE__AudioPort);
	app->regs.port.cv.node = lilv_new_uri(app->world, LV2_CORE__CVPort);
	app->regs.port.atom.node = lilv_new_uri(app->world, LV2_ATOM__AtomPort);

	app->regs.port.sequence.node = lilv_new_uri(app->world, LV2_ATOM__Sequence);
	app->regs.port.midi.node = lilv_new_uri(app->world, LV2_MIDI__MidiEvent);
	app->regs.port.osc.node = lilv_new_uri(app->world,
		"http://opensoundcontrol.org#OscEvent");
	app->regs.port.chim_event.node = lilv_new_uri(app->world,
		"http://open-music-kontrollers.ch/lv2/chimaera#Event");
	app->regs.port.chim_dump.node = lilv_new_uri(app->world,
		"http://open-music-kontrollers.ch/lv2/chimaera#Dump");

	app->regs.port.integer.node = lilv_new_uri(app->world, LV2_CORE__integer);
	app->regs.port.toggled.node = lilv_new_uri(app->world, LV2_CORE__toggled);

	app->regs.port.float_protocol.node = lilv_new_uri(app->world, LV2_UI_PREFIX"floatProtocol");
	app->regs.port.peak_protocol.node = lilv_new_uri(app->world, LV2_UI_PREFIX"peakProtocol");
	app->regs.port.atom_transfer.node = lilv_new_uri(app->world, LV2_ATOM__atomTransfer);
	app->regs.port.event_transfer.node = lilv_new_uri(app->world, LV2_ATOM__eventTransfer);
	app->regs.port.notification.node = lilv_new_uri(app->world, LV2_UI__portNotification);

	app->regs.work.schedule.node = lilv_new_uri(app->world, LV2_WORKER__schedule);

	app->regs.log.entry.node = lilv_new_uri(app->world, LV2_LOG__Entry);
	app->regs.log.error.node = lilv_new_uri(app->world, LV2_LOG__Error);
	app->regs.log.note.node = lilv_new_uri(app->world, LV2_LOG__Note);
	app->regs.log.trace.node = lilv_new_uri(app->world, LV2_LOG__Trace);
	app->regs.log.warning.node = lilv_new_uri(app->world, LV2_LOG__Warning);

	app->regs.ui.eo.node = lilv_new_uri(app->world, LV2_UI__EoUI);

	// initialize URIDS
	app->regs.port.input.urid = ext_urid_map(app->ext_urid, LV2_CORE__InputPort);
	app->regs.port.output.urid = ext_urid_map(app->ext_urid, LV2_CORE__OutputPort);

	app->regs.port.control.urid = ext_urid_map(app->ext_urid, LV2_CORE__ControlPort);
	app->regs.port.audio.urid = ext_urid_map(app->ext_urid, LV2_CORE__AudioPort);
	app->regs.port.cv.urid = ext_urid_map(app->ext_urid, LV2_CORE__CVPort);
	app->regs.port.atom.urid = ext_urid_map(app->ext_urid, LV2_ATOM__AtomPort);

	app->regs.port.sequence.urid = ext_urid_map(app->ext_urid, LV2_ATOM__Sequence);
	app->regs.port.midi.urid = ext_urid_map(app->ext_urid, LV2_MIDI__MidiEvent);
	app->regs.port.osc.urid = ext_urid_map(app->ext_urid,
		"http://opensoundcontrol.org#OscEvent");
	app->regs.port.chim_event.urid = ext_urid_map(app->ext_urid,
		"http://open-music-kontrollers.ch/lv2/chimaera#Event");
	app->regs.port.chim_dump.urid = ext_urid_map(app->ext_urid,
		"http://open-music-kontrollers.ch/lv2/chimaera#Dump");

	app->regs.port.integer.urid = ext_urid_map(app->ext_urid, LV2_CORE__integer);
	app->regs.port.toggled.urid= ext_urid_map(app->ext_urid, LV2_CORE__toggled);

	app->regs.port.float_protocol.urid = ext_urid_map(app->ext_urid, LV2_UI_PREFIX"floatProtocol");
	app->regs.port.peak_protocol.urid = ext_urid_map(app->ext_urid, LV2_UI_PREFIX"peakProtocol");
	app->regs.port.atom_transfer.urid = ext_urid_map(app->ext_urid, LV2_ATOM__atomTransfer);
	app->regs.port.event_transfer.urid = ext_urid_map(app->ext_urid, LV2_ATOM__eventTransfer);
	app->regs.port.notification.urid = ext_urid_map(app->ext_urid, LV2_UI__portNotification);

	app->regs.work.schedule.urid = ext_urid_map(app->ext_urid, LV2_WORKER__schedule);

	app->regs.log.entry.urid = ext_urid_map(app->ext_urid, LV2_LOG__Entry);
	app->regs.log.error.urid = ext_urid_map(app->ext_urid, LV2_LOG__Error);
	app->regs.log.note.urid = ext_urid_map(app->ext_urid, LV2_LOG__Note);
	app->regs.log.trace.urid = ext_urid_map(app->ext_urid, LV2_LOG__Trace);
	app->regs.log.warning.urid = ext_urid_map(app->ext_urid, LV2_LOG__Warning);
	
	app->regs.ui.eo.urid = ext_urid_map(app->ext_urid, LV2_UI__EoUI);

	// reset module list
	app->mods = NULL;

	app->sample_rate = 32000; //TODO
	app->period_size = 32; //TODO
	app->seq_size = 0x2000; //TODO

	// init elm
	app->ui.win = elm_win_util_standard_add("synthpod", "Synthpod");
	evas_object_smart_callback_add(app->ui.win, "delete,request", _delete_request, NULL);
	evas_object_resize(app->ui.win, 800, 450);
	evas_object_show(app->ui.win);

	app->ui.plugpane = elm_panes_add(app->ui.win);
	elm_panes_horizontal_set(app->ui.plugpane, EINA_FALSE);
	elm_panes_content_right_size_set(app->ui.plugpane, 0.25);
	evas_object_size_hint_weight_set(app->ui.plugpane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(app->ui.plugpane, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(app->ui.plugpane);
	elm_win_resize_object_add(app->ui.win, app->ui.plugpane);

	app->ui.pluglist = elm_genlist_add(app->ui.plugpane);
	evas_object_smart_callback_add(app->ui.pluglist, "activated",
		_pluglist_activated, app);
	evas_object_smart_callback_add(app->ui.pluglist, "expand,request",
		_list_expand_request, app);
	evas_object_smart_callback_add(app->ui.pluglist, "contract,request",
		_list_contract_request, app);
	//evas_object_smart_callback_add(app->ui.pluglist, "expanded",
	//	_pluglist_expanded, app);
	//evas_object_smart_callback_add(app->ui.pluglist, "contracted",
	//	_pluglist_contracted, app);
	evas_object_data_set(app->ui.pluglist, "app", app);
	evas_object_size_hint_weight_set(app->ui.pluglist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(app->ui.pluglist, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(app->ui.pluglist);
	elm_object_part_content_set(app->ui.plugpane, "right", app->ui.pluglist);

	app->ui.plugitc = elm_genlist_item_class_new();
	app->ui.plugitc->item_style = "double_label";
	app->ui.plugitc->func.text_get = _pluglist_label_get;
	app->ui.plugitc->func.content_get = NULL;
	app->ui.plugitc->func.state_get = NULL;
	app->ui.plugitc->func.del = NULL;

	LILV_FOREACH(plugins, itr, app->plugs)
	{
		const LilvPlugin *plug = lilv_plugins_get(app->plugs, itr);
		elm_genlist_item_append(app->ui.pluglist, app->ui.plugitc, plug, NULL,
			ELM_GENLIST_ITEM_NONE, NULL, NULL);
	}

	app->ui.modpane = elm_panes_add(app->ui.plugpane);
	elm_panes_horizontal_set(app->ui.modpane, EINA_FALSE);
	elm_panes_content_left_size_set(app->ui.modpane, 0.3);
	evas_object_size_hint_weight_set(app->ui.modpane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(app->ui.modpane, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(app->ui.modpane);
	elm_object_part_content_set(app->ui.plugpane, "left", app->ui.modpane);
	
	app->ui.patchpane = elm_panes_add(app->ui.modpane);
	elm_panes_horizontal_set(app->ui.patchpane, EINA_TRUE);
	elm_panes_content_left_size_set(app->ui.patchpane, 0.8);
	evas_object_size_hint_weight_set(app->ui.patchpane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(app->ui.patchpane, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(app->ui.patchpane);
	elm_object_part_content_set(app->ui.modpane, "right", app->ui.patchpane);

	app->ui.patchbox = elm_box_add(app->ui.patchpane);
	elm_box_horizontal_set(app->ui.patchbox, EINA_TRUE);
	elm_box_homogeneous_set(app->ui.patchbox, EINA_FALSE);
	elm_box_padding_set(app->ui.patchbox, 10, 10);
	evas_object_size_hint_weight_set(app->ui.patchbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(app->ui.patchbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(app->ui.patchbox);
	elm_object_part_content_set(app->ui.patchpane, "right", app->ui.patchbox);

	for(int t=0; t<PORT_TYPE_NUM; t++)
	{
		Evas_Object *matrix = patcher_object_add(app->ui.patchbox);
		//patcher_object_dimension_set(matrix, 0, 0);
		evas_object_smart_callback_add(matrix, "connect", _matrix_connect, app);
		evas_object_smart_callback_add(matrix, "disconnect", _matrix_disconnect, app);
		evas_object_size_hint_weight_set(matrix, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(matrix, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(matrix);
		elm_box_pack_end(app->ui.patchbox, matrix);
		app->ui.matrix[t] = matrix;
	}

	app->ui.modlist = elm_genlist_add(app->ui.modpane);
	elm_genlist_select_mode_set(app->ui.modlist, ELM_OBJECT_SELECT_MODE_NONE);
	//elm_genlist_reorder_mode_set(app->ui.modlist, EINA_TRUE);
	evas_object_smart_callback_add(app->ui.modlist, "expand,request",
		_list_expand_request, app);
	evas_object_smart_callback_add(app->ui.modlist, "contract,request",
		_list_contract_request, app);
	evas_object_smart_callback_add(app->ui.modlist, "expanded",
		_modlist_expanded, app);
	evas_object_smart_callback_add(app->ui.modlist, "contracted",
		_modlist_contracted, app);
	evas_object_data_set(app->ui.modlist, "app", app);
	evas_object_size_hint_weight_set(app->ui.modlist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(app->ui.modlist, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(app->ui.modlist);
	elm_object_part_content_set(app->ui.modpane, "left", app->ui.modlist);
	
	app->ui.moditc = elm_genlist_item_class_new();
	app->ui.moditc->item_style = "double_label";
	app->ui.moditc->func.text_get = _modlist_label_get;
	app->ui.moditc->func.content_get = _modlist_content_get;
	app->ui.moditc->func.state_get = NULL;
	app->ui.moditc->func.del = _modlist_del;

	app->ui.stditc = elm_genlist_item_class_new();
	app->ui.stditc->item_style = "full";
	app->ui.stditc->func.text_get = NULL;
	app->ui.stditc->func.content_get = _modlist_std_content_get;
	app->ui.stditc->func.state_get = NULL;
	app->ui.stditc->func.del = _modlist_std_del;

	app->ui.modgrid = elm_gengrid_add(app->ui.patchpane);
	elm_gengrid_select_mode_set(app->ui.modgrid, ELM_OBJECT_SELECT_MODE_NONE);
	elm_gengrid_reorder_mode_set(app->ui.modgrid, EINA_TRUE);
	elm_gengrid_item_size_set(app->ui.modgrid, 400, 400);
	evas_object_data_set(app->ui.modgrid, "app", app);
	evas_object_size_hint_weight_set(app->ui.modgrid, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(app->ui.modgrid, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(app->ui.modgrid);
	elm_object_part_content_set(app->ui.patchpane, "left", app->ui.modgrid);

	app->ui.griditc = elm_gengrid_item_class_new();
	app->ui.griditc->item_style = "default";
	app->ui.griditc->func.text_get = _modgrid_label_get;
	app->ui.griditc->func.content_get = _modgrid_content_get;
	app->ui.griditc->func.state_get = NULL;
	app->ui.griditc->func.del = _modgrid_del;

	app->rt.anim = ecore_animator_add(_rt_animator, app);
	app->rt.to = varchunk_new(8192);
	app->rt.from = varchunk_new(8192);

	return app;
}

// non-rt ui-thread
void
app_free(app_t *app)
{
	ecore_animator_del(app->rt.anim);
	varchunk_free(app->rt.to);
	varchunk_free(app->rt.from);

	// free URI nodes
	lilv_node_free(app->regs.port.input.node);
	lilv_node_free(app->regs.port.output.node);

	lilv_node_free(app->regs.port.control.node);
	lilv_node_free(app->regs.port.audio.node);
	lilv_node_free(app->regs.port.cv.node);
	lilv_node_free(app->regs.port.atom.node);

	lilv_node_free(app->regs.port.sequence.node);

	lilv_node_free(app->regs.port.midi.node);
	lilv_node_free(app->regs.port.osc.node);
	lilv_node_free(app->regs.port.chim_event.node);
	lilv_node_free(app->regs.port.chim_dump.node);

	lilv_node_free(app->regs.port.integer.node);
	lilv_node_free(app->regs.port.toggled.node);

	lilv_node_free(app->regs.port.float_protocol.node);
	lilv_node_free(app->regs.port.peak_protocol.node);
	lilv_node_free(app->regs.port.atom_transfer.node);
	lilv_node_free(app->regs.port.event_transfer.node);
	lilv_node_free(app->regs.port.notification.node);

	lilv_node_free(app->regs.work.schedule.node);

	lilv_node_free(app->regs.log.entry.node);
	lilv_node_free(app->regs.log.error.node);
	lilv_node_free(app->regs.log.note.node);
	lilv_node_free(app->regs.log.trace.node);
	lilv_node_free(app->regs.log.warning.node);

	lilv_node_free(app->regs.ui.eo.node);

	ext_urid_free(app->ext_urid);

	lilv_world_free(app->world);

	// deinit elm
	evas_object_hide(app->ui.win);

	elm_gengrid_clear(app->ui.modgrid);
	evas_object_del(app->ui.modgrid);

	elm_genlist_clear(app->ui.modlist);
	evas_object_del(app->ui.modlist);

	elm_genlist_clear(app->ui.pluglist);
	evas_object_del(app->ui.pluglist);

	elm_box_clear(app->ui.patchbox);
	evas_object_del(app->ui.patchbox);

	evas_object_del(app->ui.patchpane);
	evas_object_del(app->ui.modpane);
	evas_object_del(app->ui.plugpane);
	evas_object_del(app->ui.win);
	
	elm_genlist_item_class_free(app->ui.plugitc);
	elm_gengrid_item_class_free(app->ui.griditc);
	elm_genlist_item_class_free(app->ui.moditc);
	elm_genlist_item_class_free(app->ui.stditc);

	free(app);
}

// rt-thread 
static void
_app_quit(uv_async_t *quit)
{
	app_t *app = quit->data;

	uv_close((uv_handle_t *)&app->quit, NULL);
	uv_timer_stop(&app->pacemaker);
}

// rt-thread 
static void
_app_thread(void *arg)
{
	app_t *app = arg;

	app->loop = uv_loop_new();
	
	app->quit.data = app;
	uv_async_init(app->loop, &app->quit, _app_quit);

	app->pacemaker.data = app;
	uv_timer_init(app->loop, &app->pacemaker);
	uv_timer_start(&app->pacemaker, _pacemaker_cb, 0, 1);

	uv_run(app->loop, UV_RUN_DEFAULT);
}

// non-rt ui-thread 
void
app_run(app_t *app)
{
	uv_thread_create(&app->thread, _app_thread, app);
}

// non-rt ui-thread 
void app_stop(app_t *app)
{
	uv_async_send(&app->quit);
	uv_thread_join(&app->thread);
}

// non-rt worker-thread
static void
_mod_worker_quit(uv_async_t *quit)
{
	mod_t *mod = quit->data;

	uv_close((uv_handle_t *)&mod->worker.quit, NULL);
	uv_close((uv_handle_t *)&mod->worker.async, NULL);
}

// non-rt worker-thread
static LV2_Worker_Status
_mod_worker_respond(LV2_Worker_Respond_Handle handle, uint32_t size,
	const void *data)
{
	mod_t *mod = handle;
	void *ptr;

	if( (ptr = varchunk_write_request(mod->worker.from, size)) )
	{
		memcpy(ptr, data, size);
		varchunk_write_advance(mod->worker.from, size);

		return LV2_WORKER_SUCCESS;
	}

	return LV2_WORKER_ERR_NO_SPACE;
}

// non-rt worker-thread
static void
_mod_worker_wakeup(uv_async_t *async)
{
	mod_t *mod = async->data;
	const void *ptr;
	size_t toread;
		
	while( (ptr = varchunk_read_request(mod->worker.to, &toread)) )
	{
		if(mod->worker.iface->work)
			mod->worker.iface->work(mod->handle, _mod_worker_respond, mod, toread, ptr);
		varchunk_read_advance(mod->worker.to);
	}
}

// non-rt worker-thread
static void
_mod_worker_thread(void *arg)
{
	mod_t *mod = arg;

	uv_loop_t *loop = uv_loop_new();
	
	mod->worker.quit.data = mod;
	uv_async_init(loop, &mod->worker.quit, _mod_worker_quit);

	mod->worker.async.data = mod;
	uv_async_init(loop, &mod->worker.async, _mod_worker_wakeup);

	uv_run(loop, UV_RUN_DEFAULT);
}

// non-rt ui-thread
static uint32_t
_port_index(LV2UI_Feature_Handle handle, const char *symbol)
{
	mod_t *mod = handle;
	LilvNode *symbol_uri = lilv_new_uri(mod->app->world, symbol);
	const LilvPort *port = lilv_plugin_get_port_by_symbol(mod->plug, symbol_uri);
	lilv_node_free(symbol_uri);

	return port
		? lilv_port_get_index(mod->plug, port)
		: LV2UI_INVALID_PORT_INDEX;
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_vprintf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, va_list args)
{
	mod_t *mod = handle;
	app_t *app = mod->app;
	
	if(type == app->regs.log.trace.urid)
		return 1; //TODO support logging from rt-thread

	const char *type_str = NULL;
	if(type == app->regs.log.entry.urid)
		type_str = "Entry";
	else if(type == app->regs.log.error.urid)
		type_str = "Error";
	else if(type == app->regs.log.note.urid)
		type_str = "Note";
	else if(type == app->regs.log.trace.urid)
		type_str = "Trace";
	else if(type == app->regs.log.warning.urid)
		type_str = "Wraning";

	fprintf(stderr, "[%s]", type_str); //TODO report handle 
	vfprintf(stderr, fmt, args);
	fputc('\n', stderr);

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

// non-rt ui-thread
mod_t *
app_mod_add(app_t *app, const char *uri)
{
	LilvNode *uri_node = lilv_new_uri(app->world, uri);
	const LilvPlugin *plug = lilv_plugins_get_by_uri(app->plugs, uri_node);
	lilv_node_free(uri_node);

	const LilvNode *plugin_uri = lilv_plugin_get_uri(plug);
	const char *plugin_string = lilv_node_as_string(plugin_uri);
	//printf("plugin_string: %s\n", plugin_string);
			
	if(!plug || !lilv_plugin_verify(plug))
		return NULL;

	mod_t *mod = calloc(1, sizeof(mod_t));

	// populate worker schedule
	mod->worker.schedule.handle = mod;
	mod->worker.schedule.schedule_work = _schedule_work;

	// populate port_map
	mod->ui.eo.port_map.handle = mod;
	mod->ui.eo.port_map.port_index = _port_index;

	// populate port_subscribe
	mod->ui.eo.port_subscribe.handle = mod;
	mod->ui.eo.port_subscribe.subscribe = _port_subscribe;
	mod->ui.eo.port_subscribe.unsubscribe = _port_unsubscribe;

	// populate port_event for StdUI
	mod->ui.std.descriptor.port_event = _std_port_event;

	// populate log
	mod->log.handle = mod;
	mod->log.printf = _log_printf;
	mod->log.vprintf = _log_vprintf;

	// populate feature list
	mod->feature_list[0].URI = LV2_URID__map;
	mod->feature_list[0].data = ext_urid_map_get(app->ext_urid);
	mod->feature_list[1].URI = LV2_URID__unmap;
	mod->feature_list[1].data = ext_urid_unmap_get(app->ext_urid);
	mod->feature_list[2].URI = LV2_WORKER__schedule;
	mod->feature_list[2].data = &mod->worker.schedule;
	mod->feature_list[3].URI = LV2_LOG__log;
	mod->feature_list[3].data = &mod->log;

	// populate UI feature list
	mod->ui_feature_list[0].URI = LV2_URID__map;
	mod->ui_feature_list[0].data = ext_urid_map_get(app->ext_urid);
	mod->ui_feature_list[1].URI = LV2_URID__unmap;
	mod->ui_feature_list[1].data = ext_urid_unmap_get(app->ext_urid);
	mod->ui_feature_list[2].URI = LV2_UI__parent;
	mod->ui_feature_list[2].data = app->ui.pluglist;
	mod->ui_feature_list[3].URI = LV2_UI__portMap;
	mod->ui_feature_list[3].data = &mod->ui.eo.port_map;
	mod->ui_feature_list[4].URI = LV2_UI__portSubscribe;
	mod->ui_feature_list[4].data = &mod->ui.eo.port_subscribe;
	mod->ui_feature_list[5].URI = LV2_LOG__log;
	mod->ui_feature_list[5].data = &mod->log;
	
	for(int i=0; i<NUM_FEATURES; i++)
		mod->features[i] = &mod->feature_list[i];
	mod->features[NUM_FEATURES] = NULL; // sentinel
	
	for(int i=0; i<NUM_UI_FEATURES; i++)
		mod->ui_features[i] = &mod->ui_feature_list[i];
	mod->ui_features[NUM_UI_FEATURES] = NULL; // sentinel
		
	mod->app = app;
	mod->plug = plug;
	mod->num_ports = lilv_plugin_get_num_ports(plug);
	if(lilv_plugin_has_feature(mod->plug, app->regs.work.schedule.node))
	{
		mod->worker.to = varchunk_new(8192);
		mod->worker.from = varchunk_new(8192);
		uv_thread_create(&mod->worker.thread, _mod_worker_thread, mod);
	}
	mod->inst = lilv_plugin_instantiate(plug, app->sample_rate, mod->features);
	mod->handle = lilv_instance_get_handle(mod->inst),
	mod->worker.iface = lilv_instance_get_extension_data(mod->inst,
		LV2_WORKER__interface);
	lilv_instance_activate(mod->inst);

	mod->ports = calloc(mod->num_ports, sizeof(port_t));
	for(uint32_t i=0; i<mod->num_ports; i++)
	{
		port_t *tar = &mod->ports[i];
		size_t size = 0;
		const LilvPort *port = lilv_plugin_get_port_by_index(plug, i);

		tar->mod = mod;
		tar->tar = port;
		tar->index = i;
		tar->direction = lilv_port_is_a(plug, port, app->regs.port.input.node)
			? PORT_DIRECTION_INPUT
			: PORT_DIRECTION_OUTPUT;

		if(lilv_port_is_a(plug, port, app->regs.port.audio.node))
		{
			size = app->period_size * sizeof(float);
			tar->type =  PORT_TYPE_AUDIO;
			tar->selected = 1;
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.cv.node))
		{
			size = app->period_size * sizeof(float);
			tar->type = PORT_TYPE_CV;
			tar->selected = 1;
		}
		else if(lilv_port_is_a(plug, port, app->regs.port.control.node))
		{
			size = sizeof(float);
			tar->type = PORT_TYPE_CONTROL;
			tar->protocol = app->regs.port.float_protocol.urid; //TODO remove?
			tar->points = lilv_port_get_scale_points(mod->plug, port);
			
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
		else if(lilv_port_is_a(plug, port, app->regs.port.atom.node)) 
		{
			size = app->seq_size;
			tar->type = PORT_TYPE_ATOM;
			tar->buffer_type = PORT_BUFFER_TYPE_SEQUENCE;
			//tar->buffer_type = lilv_port_is_a(plug, port, app->regs.port.sequence.node)
			//	? PORT_BUFFER_TYPE_SEQUENCE
			//	: PORT_BUFFER_TYPE_NONE; //FIXME
			tar->selected = 1;
		}
		else
			; //TODO abort

		// allocate 8-byte aligned buffer
		posix_memalign(&tar->buf, 8, size); //TODO mlock
		memset(tar->buf, 0x0, size);

		// set port buffer
		lilv_instance_connect_port(mod->inst, i, tar->buf);
	}

	//ui
	mod->all_uis = lilv_plugin_get_uis(mod->plug);
	LILV_FOREACH(uis, ptr, mod->all_uis)
	{
		const LilvUI *ui = lilv_uis_get(mod->all_uis, ptr);
		if(lilv_ui_is_a(ui, app->regs.ui.eo.node))
		{
			mod->ui.eo.ui = ui;
			break;
		}
	}
	
	mod->ui.to = varchunk_new(8192);
	mod->ui.from = varchunk_new(8192);

	// add port_event animator
	mod->ui.port_event_anim = ecore_animator_add(_port_event_animator, mod);

	lv2_atom_forge_init(&mod->forge, ext_urid_map_get(app->ext_urid));

	// inject module into rt thread
	void *ptr;
	if( (ptr = varchunk_write_request(app->rt.to, JOB_SIZE)) )
	{
		job_t *job = ptr;

		job->type = JOB_TYPE_MODULE_ADD;
		job->payload.mod = mod;

		varchunk_write_advance(app->rt.to, JOB_SIZE);
	}
	else
		fprintf(stderr, "rt varchunk buffer overrun");

	return mod;
}

// non-rt ui-thread
void
app_mod_del(app_t *app, mod_t *mod)
{
	// del port_event animator
	ecore_animator_del(mod->ui.port_event_anim);
	mod->ui.port_event_anim = NULL;

	varchunk_free(mod->ui.to);
	varchunk_free(mod->ui.from);

	if(mod->all_uis)
		lilv_uis_free(mod->all_uis);

	// deinit worker
	if(mod->worker.iface)
	{
		uv_async_send(&mod->worker.quit);
		uv_thread_join(&mod->worker.thread);
		varchunk_free(mod->worker.to);
		varchunk_free(mod->worker.from);
	}

	// deinit instance
	lilv_instance_deactivate(mod->inst);
	lilv_instance_free(mod->inst);

	// deinit ports
	for(uint32_t i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->points)
			lilv_scale_points_free(port->points);

		free(port->buf);
	}
	free(mod->ports);

	free(mod);
}
