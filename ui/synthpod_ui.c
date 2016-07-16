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

void //XXX check with _zero_writer_request/advance
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
		size_t len = sizeof(transfer_float_t);
		transfer_float_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			_sp_transfer_float_fill(&ui->regs, &ui->forge, trans, mod->uid,
				tar->index, buffer);
			_sp_ui_to_app_advance(ui, len);
		}
	}
	else if(protocol == ui->regs.port.atom_transfer.urid)
	{
		size_t len = sizeof(transfer_atom_t) + lv2_atom_pad_size(size);
		transfer_atom_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			_sp_transfer_atom_fill(&ui->regs, &ui->forge, trans, mod->uid, tar->index,
				size, buffer);
			_sp_ui_to_app_advance(ui, len);
		}
	}
	else if(protocol == ui->regs.port.event_transfer.urid)
	{
		size_t len = sizeof(transfer_atom_t) + lv2_atom_pad_size(size);
		transfer_atom_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			_sp_transfer_event_fill(&ui->regs, &ui->forge, trans, mod->uid, tar->index,
				size, buffer);
			_sp_ui_to_app_advance(ui, len);
		}
	}
}

void
_ext_ui_write_function(LV2UI_Controller controller, uint32_t index,
	uint32_t size, uint32_t protocol, const void *buffer)
{
	mod_t *mod = controller;
	sp_ui_t *ui = mod->ui;

	// to StdUI
	_std_port_event(controller, index, size, protocol, buffer);

	// to rt-thread
	const LV2_Atom_Object *obj = buffer;
	if(  lv2_atom_forge_is_object_type(&ui->forge, obj->atom.type)
		&& ( (obj->body.otype == ui->regs.patch.set.urid)
			|| (obj->body.otype == ui->regs.patch.put.urid)
			|| (obj->body.otype == ui->regs.patch.patch.urid) ) ) //TODO support more patch messages
	{
		assert(lv2_atom_pad_size(size) == size);
	
		// as we need to append a property to the object, we use
		// _sp_ui_to_app_request/advance directly here,
		// instead of indirectly via _ui_write_function

		// append patch:destination for feedback message handling
		const size_t nsize = size + 24;
		const size_t len = sizeof(transfer_atom_t) + nsize;
		transfer_atom_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			LV2_Atom_Object *clone= (LV2_Atom_Object *)_sp_transfer_event_fill(
				&ui->regs, &ui->forge, trans, mod->uid, index, nsize, NULL);

			memcpy(clone, obj, size); // clone original atom patch message
			clone->atom.size += 24; // increase atom object size

			// now fill destination property
			LV2_Atom_Property_Body *prop = (void *)clone + size;
			prop->key = ui->regs.patch.destination.urid;
			prop->context = 0;
			prop->value.size = sizeof(LV2_URID);
			prop->value.type = ui->forge.URID;
			LV2_URID *prop_val = LV2_ATOM_BODY(&prop->value);
			*prop_val = ui->regs.core.plugin.urid;

			_sp_ui_to_app_advance(ui, len); // finalize
		}
	}
	else // no feedback block flag needed
		_ui_write_function(controller, index, size, protocol, buffer);
}

const LV2UI_Descriptor *
_ui_dlopen(const LilvUI *ui, Eina_Module **lib)
{
	const LilvNode *ui_uri = lilv_ui_get_uri(ui);
	const LilvNode *binary_uri = lilv_ui_get_binary_uri(ui);
	if(!ui_uri || !binary_uri)
		return NULL;

	const char *ui_string = lilv_node_as_string(ui_uri);
#if defined(LILV_0_22)
	char *binary_path = lilv_file_uri_parse(lilv_node_as_string(binary_uri), NULL);
#else
	const char *binary_path = lilv_uri_to_path(lilv_node_as_string(binary_uri));
#endif
	if(!ui_string || !binary_path)
		return NULL;

	*lib = eina_module_new(binary_path);

#if defined(LILV_0_22)
	lilv_free(binary_path);
#endif

	if(!*lib)
		return NULL;

	if(!eina_module_load(*lib))
	{
		eina_module_free(*lib);
		*lib = NULL;

		return NULL;
	}

	LV2UI_DescriptorFunction ui_descfunc = NULL;
	ui_descfunc = eina_module_symbol_get(*lib, "lv2ui_descriptor");

	if(!ui_descfunc)
		goto fail;

	// search for a matching UI
	for(int i=0; 1; i++)
	{
		const LV2UI_Descriptor *ui_desc = ui_descfunc(i);

		if(!ui_desc) // end of UI list
			break;
		else if(!strcmp(ui_desc->URI, ui_string))
			return ui_desc; // matching UI found
	}

fail:
	eina_module_unload(*lib);
	eina_module_free(*lib);
	*lib = NULL;

	return NULL;
}

void
_smart_mouse_in(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	if(mod->std.list)
	{
		elm_scroller_movement_block_set(mod->std.list,
			ELM_SCROLLER_MOVEMENT_BLOCK_HORIZONTAL | ELM_SCROLLER_MOVEMENT_BLOCK_VERTICAL);
	}

	if(ui->modgrid)
	{
		elm_scroller_movement_block_set(ui->modgrid,
			ELM_SCROLLER_MOVEMENT_BLOCK_HORIZONTAL | ELM_SCROLLER_MOVEMENT_BLOCK_VERTICAL);
	}
}

void
_smart_mouse_out(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	if(mod->std.list)
		elm_scroller_movement_block_set(mod->std.list, ELM_SCROLLER_MOVEMENT_NO_BLOCK);

	if(ui->modgrid)
		elm_scroller_movement_block_set(ui->modgrid, ELM_SCROLLER_MOVEMENT_NO_BLOCK);
}

Evas_Object *
sp_ui_widget_get(sp_ui_t *ui)
{
	return ui->vbox;
}

static uint32_t
_uri_to_id(LV2_URI_Map_Callback_Data handle, const char *_, const char *uri)
{
	sp_ui_t *ui = handle;

	LV2_URID_Map *map = ui->driver->map;

	return map->map(map->handle, uri);
}

static void
_panes_changed(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	ui->nleft = elm_panes_content_left_size_get(ui->mainpane);

	// notify app
	const size_t size = sizeof(transmit_pane_left_t);
	transmit_pane_left_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_pane_left_fill(&ui->regs, &ui->forge, trans, size, ui->nleft);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_columns_changed(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	ui->ncols = elm_spinner_value_get(obj);
	_modgrid_item_size_update(ui);

	// notify app
	const size_t size = sizeof(transmit_grid_cols_t);
	transmit_grid_cols_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_grid_cols_fill(&ui->regs, &ui->forge, trans, size, ui->ncols);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_rows_changed(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	ui->nrows = elm_spinner_value_get(obj);
	_modgrid_item_size_update(ui);

	// notify app
	const size_t size = sizeof(transmit_grid_rows_t);
	transmit_grid_rows_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_grid_rows_fill(&ui->regs, &ui->forge, trans, size, ui->nrows);
		_sp_ui_to_app_advance(ui, size);
	}
}

sp_ui_t *
sp_ui_new(Evas_Object *win, const LilvWorld *world, sp_ui_driver_t *driver,
	void *data, int show_splash)
{
	if(!driver || !data)
		return NULL;

	if(  !driver->map || !driver->unmap
		|| !driver->to_app_request || !driver->to_app_advance)
		return NULL;

#if defined(ELM_1_10)
	elm_config_focus_autoscroll_mode_set(ELM_FOCUS_AUTOSCROLL_MODE_NONE);
	elm_config_focus_move_policy_set(ELM_FOCUS_MOVE_POLICY_CLICK);
	elm_config_first_item_focus_on_first_focusin_set(EINA_TRUE);
#endif

	// register theme (if not already registered by its parent)
	Elm_Theme *default_theme = elm_theme_default_get();
	if(default_theme)
	{
		const Eina_List *exts = elm_theme_extension_list_get(default_theme);

		const char *ext;
		Eina_List *l;
		bool already_registered = false;
		EINA_LIST_FOREACH((Eina_List *)exts, l, ext)
			already_registered = already_registered || !strcmp(ext, SYNTHPOD_DATA_DIR"/synthpod.edj");

		if(!already_registered)
			elm_theme_extension_add(default_theme, SYNTHPOD_DATA_DIR"/synthpod.edj");
	}

	sp_ui_t *ui = calloc(1, sizeof(sp_ui_t));
	if(!ui)
		return NULL;

	ui->ncols = 3;
	ui->nrows = 2;
	ui->nleft = 0.2;

	ui->win = win;
	ui->driver = driver;
	ui->data = data;

	ui->zoom = 0.f;
	ui->matrix_type = PORT_TYPE_AUDIO;

	lv2_atom_forge_init(&ui->forge, ui->driver->map);

	if(world)
	{
		ui->world = (LilvWorld *)world;
		ui->embedded = 1;
	}
	else
	{
		ui->world = lilv_world_new();
		if(!ui->world)
		{
			free(ui);
			return NULL;
		}
		LilvNode *node_false = lilv_new_bool(ui->world, false);
		if(node_false)
		{
			lilv_world_set_option(ui->world, LILV_OPTION_DYN_MANIFEST, node_false);
			lilv_node_free(node_false);
		}
		lilv_world_load_all(ui->world);
		LilvNode *synthpod_bundle = lilv_new_file_uri(ui->world, NULL, SYNTHPOD_BUNDLE_DIR"/");
		if(synthpod_bundle)
		{
			lilv_world_load_bundle(ui->world, synthpod_bundle);
			lilv_node_free(synthpod_bundle);
		}
	}

	if(ui->win)
	{
		_pluglist_itc_add(ui);
		_property_itc_add(ui);
		_group_itc_add(ui);
		_modlist_itc_add(ui);
		_port_itc_add(ui);
		_modgrid_itc_add(ui);
		_presetlist_itc_add(ui);

		ui->vbox = elm_box_add(ui->win);
		if(ui->vbox)
		{
			elm_box_homogeneous_set(ui->vbox, EINA_FALSE);
			elm_box_padding_set(ui->vbox, 0, 0);
			evas_object_size_hint_weight_set(ui->vbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(ui->vbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(ui->vbox);

			// get theme data items
			Evas_Object *theme = elm_layout_add(ui->vbox);
			if(theme)
			{
				elm_layout_file_set(theme, SYNTHPOD_DATA_DIR"/synthpod.edj",
					"/synthpod/theme");

				const char *colors_max = elm_layout_data_get(theme, "colors_max");
				ui->colors_max = colors_max ? atoi(colors_max) : 20;
				ui->colors_vec = calloc(ui->colors_max, sizeof(int));

				evas_object_del(theme);
			}
			else
			{
				ui->colors_max = 20;
			}

			evas_object_event_callback_add(ui->win, EVAS_CALLBACK_KEY_DOWN, _theme_key_down, ui);

			const Eina_Bool exclusive = EINA_FALSE;
			const Evas_Modifier_Mask ctrl_mask = evas_key_modifier_mask_get(
				evas_object_evas_get(ui->win), "Control");
			const Evas_Modifier_Mask shift_mask = evas_key_modifier_mask_get(
				evas_object_evas_get(ui->win), "Shift");
			// new
			if(!evas_object_key_grab(ui->win, "n", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'n' key\n");
			// open
			if(!evas_object_key_grab(ui->win, "o", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'o' key\n");
			// save and save-as
			if(!evas_object_key_grab(ui->win, "s", ctrl_mask | shift_mask, 0, exclusive))
				fprintf(stderr, "could not grab 's' key\n");
			// import
			if(!evas_object_key_grab(ui->win, "i", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'i' key\n");
			// export
			if(!evas_object_key_grab(ui->win, "e", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'e' key\n");
			// quit
			if(!evas_object_key_grab(ui->win, "q", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'q' key\n");
			// about
			if(!evas_object_key_grab(ui->win, "h", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'h' key\n");
			// matrix
			if(!evas_object_key_grab(ui->win, "m", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'm' key\n");
			// plugin
			if(!evas_object_key_grab(ui->win, "p", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'p' key\n");
			// preset 
			if(!evas_object_key_grab(ui->win, "r", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'r' key\n");

			ui->uimenu = elm_menu_add(ui->win);
			if(ui->uimenu)
			{
				elm_menu_parent_set(ui->uimenu, ui->win);
			}

			_menu_add(ui);

			ui->popup = elm_popup_add(ui->vbox);
			if(ui->popup)
			{
				elm_popup_allow_events_set(ui->popup, EINA_TRUE);
				if(show_splash)
					evas_object_show(ui->popup);

				Evas_Object *hbox = elm_box_add(ui->popup);
				if(hbox)
				{
					elm_box_horizontal_set(hbox, EINA_TRUE);
					elm_box_homogeneous_set(hbox, EINA_FALSE);
					elm_box_padding_set(hbox, 10, 0);
					evas_object_show(hbox);
					elm_object_content_set(ui->popup, hbox);

					Evas_Object *icon = elm_icon_add(hbox);
					if(icon)
					{
						elm_image_file_set(icon, SYNTHPOD_DATA_DIR"/synthpod.edj",
							"/omk/logo");
						evas_object_size_hint_min_set(icon, 128, 128);
						evas_object_size_hint_max_set(icon, 256, 256);
						evas_object_size_hint_aspect_set(icon, EVAS_ASPECT_CONTROL_BOTH, 1, 1);
						evas_object_show(icon);
						elm_box_pack_end(hbox, icon);
					}

					Evas_Object *label = elm_label_add(hbox);
					if(label)
					{
						elm_object_text_set(label,
							"<color=#b00 shadow_color=#fff font_size=20>"
							"Synthpod - Plugin Container"
							"</color></br><align=left>"
							"Version "SYNTHPOD_VERSION"</br></br>"
							"Copyright (c) 2015-2016 Hanspeter Portner</br></br>"
							"This is free and libre software</br>"
							"Released under Artistic License 2.0</br>"
							"By Open Music Kontrollers</br></br>"
							"<color=#bbb>"
							"http://open-music-kontrollers.ch/lv2/synthpod</br>"
							"dev@open-music-kontrollers.ch"
							"</color></align>");

						evas_object_show(label);
						elm_box_pack_end(hbox, label);
					}
				}
			}

			ui->message = elm_label_add(ui->vbox);
			if(ui->message)
			{
				elm_object_text_set(ui->message, "Idle");
				evas_object_show(ui->message);
			}

			ui->feedback = elm_notify_add(ui->vbox);
			if(ui->feedback)
			{
				elm_notify_timeout_set(ui->feedback, 1.f);
				elm_notify_allow_events_set(ui->feedback, EINA_TRUE);
				elm_notify_align_set(ui->feedback, 0.0, 1.f);
				evas_object_size_hint_weight_set(ui->feedback, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				if(ui->message)
					elm_object_content_set(ui->feedback, ui->message);
			}

			ui->mainpane = elm_panes_add(ui->vbox);
			if(ui->mainpane)
			{
				elm_panes_horizontal_set(ui->mainpane, EINA_FALSE);
				elm_panes_content_left_size_set(ui->mainpane, ui->nleft);
				evas_object_smart_callback_add(ui->mainpane, "unpress", _panes_changed, ui);
				evas_object_size_hint_weight_set(ui->mainpane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(ui->mainpane, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(ui->mainpane);
				elm_box_pack_end(ui->vbox, ui->mainpane);

				ui->modlist = elm_genlist_add(ui->mainpane);
				if(ui->modlist)
				{
					elm_genlist_homogeneous_set(ui->modlist, EINA_TRUE); // needef for lazy-loading
					elm_genlist_mode_set(ui->modlist, ELM_LIST_LIMIT);
					elm_genlist_block_count_set(ui->modlist, 64); // needef for lazy-loading
					//elm_genlist_select_mode_set(ui->modlist, ELM_OBJECT_SELECT_MODE_NONE);
					elm_genlist_reorder_mode_set(ui->modlist, EINA_TRUE);
					_modlist_set_callbacks(ui);
					evas_object_data_set(ui->modlist, "ui", ui);
					evas_object_size_hint_weight_set(ui->modlist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
					evas_object_size_hint_align_set(ui->modlist, EVAS_HINT_FILL, EVAS_HINT_FILL);
					evas_object_show(ui->modlist);
					elm_object_part_content_set(ui->mainpane, "left", ui->modlist);
				} // modlist

				ui->modgrid = elm_gengrid_add(ui->mainpane);
				if(ui->modgrid)
				{
					elm_gengrid_select_mode_set(ui->modgrid, ELM_OBJECT_SELECT_MODE_NONE);
					elm_gengrid_reorder_mode_set(ui->modgrid, EINA_TRUE);
					elm_gengrid_horizontal_set(ui->modgrid, EINA_TRUE);
					elm_scroller_policy_set(ui->modgrid, ELM_SCROLLER_POLICY_AUTO, ELM_SCROLLER_POLICY_OFF);
					elm_scroller_single_direction_set(ui->modgrid, ELM_SCROLLER_SINGLE_DIRECTION_HARD);
					elm_gengrid_item_size_set(ui->modgrid, 200, 200);
					_modgrid_set_callbacks(ui);
					evas_object_data_set(ui->modgrid, "ui", ui);
					evas_object_size_hint_weight_set(ui->modgrid, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
					evas_object_size_hint_align_set(ui->modgrid, EVAS_HINT_FILL, EVAS_HINT_FILL);
					evas_object_show(ui->modgrid);
					elm_object_part_content_set(ui->mainpane, "right", ui->modgrid);
				} // modgrid
			} // mainpane
		
			Evas_Object *hbox = elm_box_add(ui->vbox);
			if(hbox)
			{
				elm_box_horizontal_set(hbox, EINA_TRUE);
				elm_box_homogeneous_set(hbox, EINA_FALSE);
				evas_object_size_hint_weight_set(hbox, EVAS_HINT_EXPAND, 0.f);
				evas_object_size_hint_align_set(hbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(hbox);
				elm_box_pack_end(ui->vbox, hbox);

				ui->statusline = elm_label_add(hbox);
				if(ui->statusline)
				{
					elm_object_text_set(ui->statusline, "");
					evas_object_size_hint_weight_set(ui->statusline, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
					evas_object_size_hint_align_set(ui->statusline, EVAS_HINT_FILL, EVAS_HINT_FILL);
					evas_object_show(ui->statusline);
					elm_box_pack_end(hbox, ui->statusline);
				} // statusline

				ui->spin_cols = elm_spinner_add(hbox);
				if(ui->spin_cols)
				{
					elm_spinner_min_max_set(ui->spin_cols, 1, 8);
					elm_spinner_value_set(ui->spin_cols, ui->ncols);
					elm_spinner_step_set(ui->spin_cols, 1);
					elm_spinner_wrap_set(ui->spin_cols, EINA_FALSE);
					elm_spinner_label_format_set(ui->spin_cols, "%0.f col");
					evas_object_size_hint_weight_set(ui->spin_cols, 0.f, EVAS_HINT_EXPAND);
					evas_object_size_hint_align_set(ui->spin_cols, 0.f, EVAS_HINT_FILL);
					evas_object_smart_callback_add(ui->spin_cols, "changed", _columns_changed, ui);
					evas_object_show(ui->spin_cols);
					elm_box_pack_end(hbox, ui->spin_cols);
				}

				ui->spin_rows = elm_spinner_add(hbox);
				if(ui->spin_rows)
				{
					elm_spinner_min_max_set(ui->spin_rows, 1, 4);
					elm_spinner_value_set(ui->spin_rows, ui->nrows);
					elm_spinner_step_set(ui->spin_rows, 1);
					elm_spinner_wrap_set(ui->spin_rows, EINA_FALSE);
					elm_spinner_label_format_set(ui->spin_rows, "%0.f row");
					evas_object_size_hint_weight_set(ui->spin_rows, 0.f, EVAS_HINT_EXPAND);
					evas_object_size_hint_align_set(ui->spin_rows, 0.f, EVAS_HINT_FILL);
					evas_object_smart_callback_add(ui->spin_rows, "changed", _rows_changed, ui);
					evas_object_show(ui->spin_rows);
					elm_box_pack_end(hbox, ui->spin_rows);
				}
			}
		} // theme
	}

	// initialzie registry
	sp_regs_init(&ui->regs, ui->world, ui->driver->map);

	sp_ui_from_app_fill(ui);

	// walk plugin directories
	ui->plugs = lilv_world_get_all_plugins(ui->world);

	// populate uri_to_id
	ui->uri_to_id.callback_data = ui;
	ui->uri_to_id.uri_to_id = _uri_to_id;

	return ui;
}

void
sp_ui_resize(sp_ui_t *ui, int w, int h)
{
	if(!ui)
		return;

	if(ui->vbox)
		evas_object_resize(ui->vbox, w, h);
}

void
sp_ui_iterate(sp_ui_t *ui)
{
	ecore_main_loop_iterate();
}

void
sp_ui_refresh(sp_ui_t *ui)
{
	if(!ui)
		return;

	/*
	ui->dirty = 1; // disable ui -> app communication
	elm_genlist_clear(ui->modlist);
	ui->dirty = 0; // enable ui -> app communication
	*/

	_modlist_refresh(ui);
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

	if(ui->colors_vec)
		free(ui->colors_vec);

	if(ui->bundle_path)
		free(ui->bundle_path);

	evas_object_event_callback_del(ui->win, EVAS_CALLBACK_KEY_DOWN, _theme_key_down);

	if(ui->plugitc)
		elm_genlist_item_class_free(ui->plugitc);
	if(ui->listitc)
		elm_genlist_item_class_free(ui->listitc);
	if(ui->moditc)
		elm_genlist_item_class_free(ui->moditc);
	if(ui->stditc)
		elm_genlist_item_class_free(ui->stditc);
	if(ui->griditc)
		elm_gengrid_item_class_free(ui->griditc);
	if(ui->propitc)
		elm_genlist_item_class_free(ui->propitc);
	if(ui->grpitc)
		elm_genlist_item_class_free(ui->grpitc);
	if(ui->presetitc)
		elm_genlist_item_class_free(ui->presetitc);
	if(ui->bankitc)
		elm_genlist_item_class_free(ui->bankitc);

	sp_regs_deinit(&ui->regs);

	if(!ui->embedded)
		lilv_world_free(ui->world);

	free(ui);
}

void
sp_ui_del(sp_ui_t *ui, bool delete_self)
{
	if(ui->modgrid)
	{
		elm_gengrid_clear(ui->modgrid);
		evas_object_del(ui->modgrid);
	}

	if(ui->modlist)
	{
		elm_genlist_clear(ui->modlist);
		evas_object_del(ui->modlist);
	}

	if(ui->plugwin)
		evas_object_del(ui->plugwin);

	if(ui->patchwin)
		evas_object_del(ui->patchwin);

	if(ui->mainpane)
		evas_object_del(ui->mainpane);
	if(ui->popup)
		evas_object_del(ui->popup);
	if(ui->feedback)
		evas_object_del(ui->feedback);
	if(ui->message)
		evas_object_del(ui->message);
	if(ui->vbox)
	{
		elm_box_clear(ui->vbox);
		if(delete_self)
			evas_object_del(ui->vbox);
	}
}

void
sp_ui_bundle_load(sp_ui_t *ui, const char *bundle_path, int update_path)
{
	if(!ui || !bundle_path)
		return;

	// update internal bundle_path for one-click-save
	if(update_path)
	{
		if(ui->bundle_path)
			free(ui->bundle_path);
		ui->bundle_path = strdup(bundle_path);
	}

	// signal to app
	size_t size = sizeof(transmit_bundle_load_t)
		+ lv2_atom_pad_size(strlen(bundle_path) + 1);
	transmit_bundle_load_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_bundle_load_fill(&ui->regs, &ui->forge, trans, size,
			-1, bundle_path);
		_sp_ui_to_app_advance(ui, size);
	}
}

void
sp_ui_bundle_new(sp_ui_t *ui)
{
	if(!ui)
		return;

	_modlist_clear(ui, false, true); // do not clear system ports
}

void
sp_ui_bundle_save(sp_ui_t *ui, const char *bundle_path, int update_path)
{
	if(!ui || !bundle_path)
		return;

	// update internal bundle_path for one-click-save
	if(update_path)
	{
		if(ui->bundle_path)
			free(ui->bundle_path);
		ui->bundle_path = strdup(bundle_path);
	}

	// signal to app
	size_t size = sizeof(transmit_bundle_save_t)
		+ lv2_atom_pad_size(strlen(bundle_path) + 1);
	transmit_bundle_save_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_bundle_save_fill(&ui->regs, &ui->forge, trans, size,
			-1, bundle_path);
		_sp_ui_to_app_advance(ui, size);
	}
}
