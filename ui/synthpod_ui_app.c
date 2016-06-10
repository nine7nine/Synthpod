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

static int
_from_app_cmp(const void *itm1, const void *itm2)
{
	const from_app_t *from_app1 = itm1;
	const from_app_t *from_app2 = itm2;

	return _signum(from_app1->protocol, from_app2->protocol);
}

static inline const from_app_t *
_from_app_bsearch(uint32_t p, from_app_t *a, unsigned n)
{
	unsigned start = 0;
	unsigned end = n;

	while(start < end)
	{
		const unsigned mid = start + (end - start)/2;
		const from_app_t *dst = &a[mid];

		if(p < dst->protocol)
			end = mid;
		else if(p > dst->protocol)
			start = mid + 1;
		else
			return dst;
	}

	return NULL;
}

static mod_t *
_sp_ui_mod_get(sp_ui_t *ui, u_id_t uid)
{
	if(!ui || !ui->modlist)
		return NULL;

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

static port_t *
_sp_ui_port_get(sp_ui_t *ui, u_id_t uid, uint32_t index)
{
	mod_t *mod = _sp_ui_mod_get(ui, uid);
	if(mod && (index < mod->num_ports) )
		return &mod->ports[index];

	return NULL;
}

static void
_sp_ui_from_app_module_supported(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_supported_t *trans = (const transmit_module_supported_t *)atom;

	if(trans->state.body == 0) // not supported -> disable it in pluglist
	{
		if(ui->pluglist)
		{
			for(Elm_Object_Item *elmnt = elm_genlist_first_item_get(ui->pluglist);
				elmnt;
				elmnt = elm_genlist_item_next_get(elmnt))
			{
				plug_info_t *info = elm_object_item_data_get(elmnt);
				if(info && info->plug)
				{
					const LilvNode *plug_uri = lilv_plugin_get_uri(info->plug);
					if(plug_uri && !strcmp(lilv_node_as_uri(plug_uri), trans->uri_str))
					{
						elm_object_item_disabled_set(elmnt, EINA_TRUE);
						break;
					}
				}
			}
		}
	}
}

static void
_sp_ui_from_app_module_add(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_add_t *trans = (const transmit_module_add_t *)atom;

	mod_t *mod = _sp_ui_mod_add(ui, trans->uri_str, trans->uid.body);
	if(!mod)
		return; //TODO report

	if(mod->system.source || mod->system.sink || !ui->sink_itm)
	{
		if(ui->modlist)
		{
			mod->std.elmnt = elm_genlist_item_append(ui->modlist, ui->listitc, mod,
				NULL, ELM_GENLIST_ITEM_NONE, NULL, NULL);
		}

		if(mod->system.sink)
			ui->sink_itm = mod->std.elmnt;
	}
	else // no sink and no source
	{
		if(ui->modlist)
		{
			mod->std.elmnt = elm_genlist_item_insert_before(ui->modlist, ui->listitc, mod,
				NULL, ui->sink_itm, ELM_GENLIST_ITEM_NONE, NULL, NULL);
		}
	}
}

static void
_sp_ui_from_app_module_del(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_del_t *trans = (const transmit_module_del_t *)atom;
	mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
	if(!mod)
		return;

	mod_ui_t *mod_ui = mod->mod_ui;

	if(mod_ui)
		_mod_del_widgets(mod);

	// remove StdUI list item
	if(mod->std.list)
	{
		elm_genlist_clear(mod->std.list);
		evas_object_del(mod->std.list);
		mod->std.list = NULL;
	}
	if(mod->std.grid)
	{
		elm_object_item_del(mod->std.grid);
	}
	if(mod->std.elmnt)
	{
		elm_object_item_del(mod->std.elmnt);
		mod->std.elmnt = NULL;
	}

	_patches_update(ui);
}

static void
_sp_ui_from_app_module_preset_load(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_preset_load_t *trans = (const transmit_module_preset_load_t *)atom;
	mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
	if(!mod)
		return;

	// refresh state
	_module_patch_get_all(mod);
}

static void
_sp_ui_from_app_module_preset_save(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_preset_save_t *trans = (const transmit_module_preset_save_t *)atom;
	mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
	if(!mod)
		return;

	// reload presets for this module
	mod->presets = _preset_reload(ui->world, &ui->regs, mod->plug, mod->presets,
		trans->label_str);
}

static void
_sp_ui_from_app_module_selected(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_selected_t *trans = (const transmit_module_selected_t *)atom;
	mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
	if(!mod)
		return;

	if(mod->selected != trans->state.body)
	{
		mod->selected = trans->state.body;
		if(mod->std.elmnt)
			elm_genlist_item_update(mod->std.elmnt);

		_patches_update(ui);
	}
}

static void
_sp_ui_from_app_module_visible(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_visible_t *trans = (const transmit_module_visible_t *)atom;
	mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
	if(!mod)
		return;

	if(trans->state.body == 1)
	{
		Eina_List *l;
		mod_ui_t *mod_ui;
		EINA_LIST_FOREACH(mod->mod_uis, l, mod_ui)
		{
			if(mod_ui->urid == trans->urid.body)
			{
				_mod_ui_toggle_raw(mod, mod_ui);
				break;
			}
		}
	}
}

static void
_sp_ui_from_app_module_profiling(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_profiling_t *trans = (const transmit_module_profiling_t *)atom;

	mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
	if(!mod)
		return;

	//printf("%u: %2.1f%% %2.1f%% %2.1f%%\n",
	//	mod->uid, trans->min.body, trans->avg.body, trans->max.body);

	if(mod->std.frame)
	{
		char dsp [128]; //TODO size?
		snprintf(dsp, 128, "%s | %.1f%%", mod->name, trans->avg.body);
		elm_object_text_set(mod->std.frame, dsp);
	}
}

static void
_sp_ui_from_app_dsp_profiling(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_dsp_profiling_t *trans = (const transmit_dsp_profiling_t *)atom;

	if(ui->statusline)
	{
		char dsp [80];
		snprintf(dsp, 80, "<font=Mono align=left>DSP | min: %4.1f%% | avg: %4.1f%% | max: %4.1f%% | ovh: %4.1f%% |</font>",
			trans->min.body,
			trans->avg.body,
			trans->max.body,
			trans->ovh.body);
		elm_object_text_set(ui->statusline, dsp);
	}
}

static void
_sp_ui_from_app_module_embedded(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_embedded_t *trans = (const transmit_module_embedded_t *)atom;
	mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
	if(!mod)
		return;

	if(mod->std.grid && !trans->state.body)
		elm_object_item_del(mod->std.grid);
	else if(!mod->std.grid && trans->state.body)
	{
		mod->std.grid = elm_gengrid_item_append(ui->modgrid, ui->griditc, mod,
			NULL, NULL);

		// refresh modlist item
		if(mod->std.elmnt)
			elm_genlist_item_update(mod->std.elmnt);
	}
}

static void
_sp_ui_from_app_port_connected(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_port_connected_t *trans = (const transmit_port_connected_t *)atom;
	port_t *src = _sp_ui_port_get(ui, trans->src_uid.body, trans->src_port.body);
	port_t *snk = _sp_ui_port_get(ui, trans->snk_uid.body, trans->snk_port.body);
	if(!src || !snk)
		return;

	if(ui->matrix && (src->type == ui->matrix_type))
	{
		patcher_object_connected_set(ui->matrix, (intptr_t)src, (intptr_t)snk,
			trans->state.body ? EINA_TRUE : EINA_FALSE,
			trans->indirect.body);
	}
}

static void
_sp_ui_from_app_float_protocol(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transfer_float_t *trans = (const transfer_float_t *)atom;
	uint32_t port_index = trans->transfer.port.body;
	const float value = trans->value.body;
	mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
	if(!mod)
		return;

	_ui_port_event(mod, port_index, sizeof(float),
		ui->regs.port.float_protocol.urid, &value);
}

static void
_sp_ui_from_app_peak_protocol(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

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

	_ui_port_event(mod, port_index, sizeof(LV2UI_Peak_Data),
		ui->regs.port.peak_protocol.urid, &data);
}

static void
_sp_ui_from_app_atom_transfer(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transfer_atom_t *trans = (const transfer_atom_t *)atom;
	uint32_t port_index = trans->transfer.port.body;
	const LV2_Atom *subatom = trans->atom;
	uint32_t size = sizeof(LV2_Atom) + subatom->size;
	mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
	if(!mod)
		return;

	_ui_port_event(mod, port_index, size,
		ui->regs.port.atom_transfer.urid, subatom);
}

static void
_sp_ui_from_app_event_transfer(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transfer_atom_t *trans = (const transfer_atom_t *)atom;
	uint32_t port_index = trans->transfer.port.body;
	const LV2_Atom *subatom = trans->atom;
	uint32_t size = sizeof(LV2_Atom) + subatom->size;
	mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
	if(!mod)
		return;

	_ui_port_event(mod, port_index, size,
		ui->regs.port.event_transfer.urid, subatom);
}

static void
_sp_ui_from_app_port_selected(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_port_selected_t *trans = (const transmit_port_selected_t *)atom;
	port_t *port = _sp_ui_port_get(ui, trans->uid.body, trans->port.body);
	if(!port)
		return;

	if(port->selected != trans->state.body)
	{
		port->selected = trans->state.body;

		// FIXME update port itm
		//mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
		/* FIXME
		if(mod && mod->std.elmnt)
			elm_genlist_item_update(mod->std.elmnt);
		*/

		_patches_update(ui);
	}
}

static void
_sp_ui_from_app_port_monitored(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_port_monitored_t *trans = (const transmit_port_monitored_t *)atom;
	port_t *port = _sp_ui_port_get(ui, trans->uid.body, trans->port.body);
	if(!port)
		return;

	if(port->std.monitored != trans->state.body)
	{
		port->std.monitored = trans->state.body;

		// FIXME update port itm
		//mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
		/* FIXME
		if(mod && mod->std.elmnt)
			elm_genlist_item_update(mod->std.elmnt);
		*/
	}
}

static void
_sp_ui_from_app_module_list(sp_ui_t *ui, const LV2_Atom *atom)
{
	if(ui->modlist)
	{
		ui->dirty = 1; // disable ui -> app communication
		elm_genlist_clear(ui->modlist);
		ui->dirty = 0; // enable ui -> app communication

		_modlist_refresh(ui);
	}
}

static void
_sp_ui_from_app_bundle_load(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_bundle_load_t *trans = (const transmit_bundle_load_t *)atom;

	if(ui->driver->opened)
		ui->driver->opened(ui->data, trans->status.body);

	if(ui->popup && evas_object_visible_get(ui->popup))
	{
		elm_popup_timeout_set(ui->popup, 1.f);
		evas_object_show(ui->popup);
	}
}

static void
_sp_ui_from_app_bundle_save(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_bundle_save_t *trans = (const transmit_bundle_save_t *)atom;

	if(ui->driver->saved)
		ui->driver->saved(ui->data, trans->status.body);
}

static void
_sp_ui_from_app_grid_cols(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_grid_cols_t *trans = (const transmit_grid_cols_t *)atom;

	ui->ncols = trans->cols.body;
	elm_spinner_value_set(ui->spin_cols, ui->ncols);
	_modgrid_item_size_update(ui);
}

static void
_sp_ui_from_app_grid_rows(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_grid_rows_t *trans = (const transmit_grid_rows_t *)atom;

	ui->nrows = trans->rows.body;
	elm_spinner_value_set(ui->spin_rows, ui->nrows);
	_modgrid_item_size_update(ui);
}

static void
_sp_ui_from_app_pane_left(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_pane_left_t *trans = (const transmit_pane_left_t *)atom;

	ui->nleft = trans->left.body;
	elm_panes_content_left_size_set(ui->mainpane, ui->nleft);
}

void
sp_ui_from_app_fill(sp_ui_t *ui)
{
	unsigned ptr = 0;
	from_app_t *from_apps = ui->from_apps;

	from_apps[ptr].protocol = ui->regs.synthpod.module_supported.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_module_supported;

	from_apps[ptr].protocol = ui->regs.synthpod.module_add.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_module_add;

	from_apps[ptr].protocol = ui->regs.synthpod.module_del.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_module_del;

	from_apps[ptr].protocol = ui->regs.synthpod.module_preset_load.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_module_preset_load;

	from_apps[ptr].protocol = ui->regs.synthpod.module_preset_save.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_module_preset_save;

	from_apps[ptr].protocol = ui->regs.synthpod.module_selected.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_module_selected;

	from_apps[ptr].protocol = ui->regs.synthpod.module_visible.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_module_visible;

	from_apps[ptr].protocol = ui->regs.synthpod.module_profiling.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_module_profiling;

	from_apps[ptr].protocol = ui->regs.synthpod.module_embedded.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_module_embedded;

	from_apps[ptr].protocol = ui->regs.synthpod.port_connected.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_port_connected;

	from_apps[ptr].protocol = ui->regs.port.float_protocol.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_float_protocol;

	from_apps[ptr].protocol = ui->regs.port.peak_protocol.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_peak_protocol;

	from_apps[ptr].protocol = ui->regs.port.atom_transfer.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_atom_transfer;

	from_apps[ptr].protocol = ui->regs.port.event_transfer.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_event_transfer;

	from_apps[ptr].protocol = ui->regs.synthpod.port_selected.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_port_selected;

	from_apps[ptr].protocol = ui->regs.synthpod.port_monitored.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_port_monitored;

	from_apps[ptr].protocol = ui->regs.synthpod.module_list.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_module_list;

	from_apps[ptr].protocol = ui->regs.synthpod.bundle_load.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_bundle_load;

	from_apps[ptr].protocol = ui->regs.synthpod.bundle_save.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_bundle_save;

	from_apps[ptr].protocol = ui->regs.synthpod.dsp_profiling.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_dsp_profiling;

	from_apps[ptr].protocol = ui->regs.synthpod.grid_cols.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_grid_cols;

	from_apps[ptr].protocol = ui->regs.synthpod.grid_rows.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_grid_rows;

	from_apps[ptr].protocol = ui->regs.synthpod.pane_left.urid;
	from_apps[ptr++].cb = _sp_ui_from_app_pane_left;

	assert(ptr == FROM_APP_NUM);
	// sort according to URID
	qsort(from_apps, FROM_APP_NUM, sizeof(from_app_t), _from_app_cmp);
}

void
sp_ui_from_app(sp_ui_t *ui, const LV2_Atom *atom)
{
	if(!ui || !atom)
		return;

	atom = ASSUME_ALIGNED(atom);
	const transmit_t *transmit = (const transmit_t *)atom;

	// check for atom object type
	if(!lv2_atom_forge_is_object_type(&ui->forge, transmit->obj.atom.type))
		return;

	// what we want to search for
	const uint32_t protocol = transmit->obj.body.otype;

	// search for corresponding callback
	const from_app_t *from_app = _from_app_bsearch(protocol, ui->from_apps, FROM_APP_NUM);

	// run callback if found
	if(from_app)
		from_app->cb(ui, atom);
}
