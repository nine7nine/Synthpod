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


static void
_matrix_connect_request(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	patcher_event_t *ev = event_info;
	if(!ui || !ev)
		return;

	patcher_event_t *source = &ev[0];
	patcher_event_t *sink = &ev[1];
	if(!source || !sink)
		return;

	port_t *source_port = (port_t *)source->id;
	port_t *sink_port = (port_t *)sink->id;
	if(!source_port || !sink_port)
		return;

	size_t size = sizeof(transmit_port_connected_t);
	transmit_port_connected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_connected_fill(&ui->regs, &ui->forge, trans, size,
			source_port->mod->uid, source_port->index,
			sink_port->mod->uid, sink_port->index, 1, -999);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_matrix_disconnect_request(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	patcher_event_t *ev = event_info;
	if(!ui || !ev)
		return;

	patcher_event_t *source = &ev[0];
	patcher_event_t *sink = &ev[1];
	if(!source || !sink)
		return;

	port_t *source_port = (port_t *)source->id;
	port_t *sink_port = (port_t *)sink->id;
	if(!source_port || !sink_port)
		return;

	size_t size = sizeof(transmit_port_connected_t);
	transmit_port_connected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_connected_fill(&ui->regs, &ui->forge, trans, size,
			source_port->mod->uid, source_port->index,
			sink_port->mod->uid, sink_port->index, 0, -999);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_matrix_realize_request(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	patcher_event_t *ev = event_info;
	if(!ui || !ev)
		return;

	patcher_event_t *source = &ev[0];
	patcher_event_t *sink = &ev[1];
	if(!source || !sink)
		return;

	port_t *source_port = (port_t *)source->id;
	port_t *sink_port = (port_t *)sink->id;
	if(!source_port || !sink_port)
		return;

	size_t size = sizeof(transmit_port_connected_t);
	transmit_port_connected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_connected_fill(&ui->regs, &ui->forge, trans, size,
			source_port->mod->uid, source_port->index,
			sink_port->mod->uid, sink_port->index, -1, -999);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_matrix_zoom_changed(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	float *ev = event_info;
	if(!ui || !ev)
		return;

	ui->zoom = *ev; //TODO serialize this to state?
}

static void
_menu_matrix_del(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	ui->patchwin = NULL;
	ui->matrix_audio = NULL;
	ui->matrix_control = NULL;
	ui->matrix_cv = NULL;
	ui->matrix_event = NULL;
	ui->matrix_atom = NULL;
	ui->matrix_atom_midi = NULL;
	ui->matrix_atom_osc = NULL;
	ui->matrix_atom_time = NULL;
	ui->matrix_atom_patch = NULL;
	ui->matrix_atom_xpress = NULL;
	ui->matrix = NULL;
}

static void
_matrix_key_down(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	const Evas_Event_Key_Down *ev = event_info;

	const Eina_Bool cntrl = evas_key_modifier_is_set(ev->modifiers, "Control");
	
	if(cntrl)
	{
		if(!strcmp(ev->key, "KP_Add"))
			patcher_object_zoom_in(ui->matrix);
		else if(!strcmp(ev->key, "KP_Subtract"))
			patcher_object_zoom_out(ui->matrix);

		else if(!strcmp(ev->key, "a"))
			elm_toolbar_item_selected_set(ui->matrix_audio, EINA_TRUE);
		else if(!strcmp(ev->key, "n"))
			elm_toolbar_item_selected_set(ui->matrix_control, EINA_TRUE);
		else if(!strcmp(ev->key, "c"))
			elm_toolbar_item_selected_set(ui->matrix_cv, EINA_TRUE);
		else if(!strcmp(ev->key, "e"))
			elm_toolbar_item_selected_set(ui->matrix_event, EINA_TRUE);
		else if(!strcmp(ev->key, "l"))
			elm_toolbar_item_selected_set(ui->matrix_atom, EINA_TRUE);

		else if(!strcmp(ev->key, "m"))
			elm_toolbar_item_selected_set(ui->matrix_atom_midi, EINA_TRUE);
		else if(!strcmp(ev->key, "o"))
			elm_toolbar_item_selected_set(ui->matrix_atom_osc, EINA_TRUE);
		else if(!strcmp(ev->key, "t"))
			elm_toolbar_item_selected_set(ui->matrix_atom_time, EINA_TRUE);
		else if(!strcmp(ev->key, "p"))
			elm_toolbar_item_selected_set(ui->matrix_atom_patch, EINA_TRUE);
		else if(!strcmp(ev->key, "x"))
			elm_toolbar_item_selected_set(ui->matrix_atom_xpress, EINA_TRUE);
	}
}

static void
_patchbar_selected(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	Elm_Object_Item *itm = event_info;

	if(itm == ui->matrix_audio)
	{
		ui->matrix_type = PORT_TYPE_AUDIO;
	}
	else if(itm == ui->matrix_control)
	{
		ui->matrix_type = PORT_TYPE_CONTROL;
	}
	else if(itm == ui->matrix_cv)
	{
		ui->matrix_type = PORT_TYPE_CV;
	}
	else if(itm == ui->matrix_event)
	{
		ui->matrix_type = PORT_TYPE_EVENT;
	}
	else if(itm == ui->matrix_atom)
	{
		ui->matrix_type = PORT_TYPE_ATOM;
		ui->matrix_atom_type = PORT_ATOM_TYPE_ALL;
	}

	else if(itm == ui->matrix_atom_midi)
	{
		ui->matrix_type = PORT_TYPE_ATOM;
		ui->matrix_atom_type = PORT_ATOM_TYPE_MIDI;
	}
	else if(itm == ui->matrix_atom_osc)
	{
		ui->matrix_type = PORT_TYPE_ATOM;
		ui->matrix_atom_type = PORT_ATOM_TYPE_OSC;
	}
	else if(itm == ui->matrix_atom_time)
	{
		ui->matrix_type = PORT_TYPE_ATOM;
		ui->matrix_atom_type = PORT_ATOM_TYPE_TIME;
	}
	else if(itm == ui->matrix_atom_patch)
	{
		ui->matrix_type = PORT_TYPE_ATOM;
		ui->matrix_atom_type = PORT_ATOM_TYPE_PATCH;
	}
	else if(itm == ui->matrix_atom_xpress)
	{
		ui->matrix_type = PORT_TYPE_ATOM;
		ui->matrix_atom_type = PORT_ATOM_TYPE_XPRESS;
	}

	else
	{
		return;
	}
	
	_patches_update(ui);
}

static void
_patchbar_restore(sp_ui_t *ui)
{
	switch(ui->matrix_type)
	{
		case PORT_TYPE_AUDIO:
			elm_toolbar_item_selected_set(ui->matrix_audio, EINA_TRUE);
			break;
		case PORT_TYPE_CONTROL:
			elm_toolbar_item_selected_set(ui->matrix_control, EINA_TRUE);
			break;
		case PORT_TYPE_CV:
			elm_toolbar_item_selected_set(ui->matrix_cv, EINA_TRUE);
			break;
		case PORT_TYPE_EVENT:
			elm_toolbar_item_selected_set(ui->matrix_event, EINA_TRUE);
			break;
		case PORT_TYPE_ATOM:
			switch(ui->matrix_atom_type)
			{
				case PORT_ATOM_TYPE_ALL:
					elm_toolbar_item_selected_set(ui->matrix_atom, EINA_TRUE);
					break;
				case PORT_ATOM_TYPE_MIDI:
					elm_toolbar_item_selected_set(ui->matrix_atom_midi, EINA_TRUE);
					break;
				case PORT_ATOM_TYPE_OSC:
					elm_toolbar_item_selected_set(ui->matrix_atom_osc, EINA_TRUE);
					break;
				case PORT_ATOM_TYPE_TIME:
					elm_toolbar_item_selected_set(ui->matrix_atom_time, EINA_TRUE);
					break;
				case PORT_ATOM_TYPE_PATCH:
					elm_toolbar_item_selected_set(ui->matrix_atom_patch, EINA_TRUE);
					break;
				case PORT_ATOM_TYPE_XPRESS:
					elm_toolbar_item_selected_set(ui->matrix_atom_xpress, EINA_TRUE);
					break;
			}
			break;
		default:
			break;
	}
}

static inline Evas_Object *
_menu_matrix_new(sp_ui_t *ui)
{
	const char *title = "Matrix";
	Evas_Object *win = elm_win_add(ui->win, title, ELM_WIN_BASIC);
	if(win)
	{
		elm_win_title_set(win, title);
		elm_win_autodel_set(win, EINA_TRUE);
		evas_object_smart_callback_add(win, "delete,request", _menu_matrix_del, ui);
		evas_object_resize(win, 1280, 640);
		evas_object_show(win);

		evas_object_event_callback_add(win, EVAS_CALLBACK_KEY_DOWN, _matrix_key_down, ui);

		const Eina_Bool exclusive = EINA_FALSE;
		const Evas_Modifier_Mask ctrl_mask = evas_key_modifier_mask_get(
			evas_object_evas_get(win), "Control");
		if(!evas_object_key_grab(win, "KP_Add", ctrl_mask, 0, exclusive)) // zoom in
			fprintf(stderr, "could not grab '+' key\n");
		if(!evas_object_key_grab(win, "KP_Subtract", ctrl_mask, 0, exclusive)) // zoom out
			fprintf(stderr, "could not grab '-' key\n");
		if(!evas_object_key_grab(win, "a", ctrl_mask, 0, exclusive)) // AUDIO
			fprintf(stderr, "could not grab 'a' key\n");
		if(!evas_object_key_grab(win, "n", ctrl_mask, 0, exclusive)) // CONTROL
			fprintf(stderr, "could not grab 'n' key\n");
		if(!evas_object_key_grab(win, "c", ctrl_mask, 0, exclusive)) // CV
			fprintf(stderr, "could not grab 'c' key\n");
		if(!evas_object_key_grab(win, "e", ctrl_mask, 0, exclusive)) // EVENT
			fprintf(stderr, "could not grab 'e' key\n");
		if(!evas_object_key_grab(win, "l", ctrl_mask, 0, exclusive)) // ATOM
			fprintf(stderr, "could not grab 'l' key\n");

		if(!evas_object_key_grab(win, "m", ctrl_mask, 0, exclusive)) // MIDI
			fprintf(stderr, "could not grab 'm' key\n");
		if(!evas_object_key_grab(win, "o", ctrl_mask, 0, exclusive)) // OSC
			fprintf(stderr, "could not grab 'o' key\n");
		if(!evas_object_key_grab(win, "t", ctrl_mask, 0, exclusive)) // TIME
			fprintf(stderr, "could not grab 't' key\n");
		if(!evas_object_key_grab(win, "p", ctrl_mask, 0, exclusive)) // PATCH
			fprintf(stderr, "could not grab 'p' key\n");
		if(!evas_object_key_grab(win, "x", ctrl_mask, 0, exclusive)) // XPRESS
			fprintf(stderr, "could not grab 'x' key\n");

		Evas_Object *bg = elm_bg_add(win);
		if(bg)
		{
			elm_bg_color_set(bg, 64, 64, 64);
			evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(bg, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(bg);
			elm_win_resize_object_add(win, bg);
		} // bg

		Evas_Object *patchbox = elm_box_add(win);
		if(patchbox)
		{
			elm_box_horizontal_set(patchbox, EINA_TRUE);
			elm_box_homogeneous_set(patchbox, EINA_FALSE);
			evas_object_data_set(patchbox, "ui", ui);
			evas_object_size_hint_weight_set(patchbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(patchbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(patchbox);
			elm_win_resize_object_add(win, patchbox);

			Evas_Object *patchbar = elm_toolbar_add(patchbox);
			if(patchbar)
			{
				elm_toolbar_horizontal_set(patchbar, EINA_FALSE);
				elm_toolbar_homogeneous_set(patchbar, EINA_TRUE);
				elm_toolbar_align_set(patchbar, 0.f);
				elm_toolbar_select_mode_set(patchbar, ELM_OBJECT_SELECT_MODE_ALWAYS);
				elm_toolbar_shrink_mode_set(patchbar, ELM_TOOLBAR_SHRINK_SCROLL);
				evas_object_smart_callback_add(patchbar, "selected", _patchbar_selected, ui);
				evas_object_size_hint_weight_set(patchbar, 0.f, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(patchbar, 0.f, EVAS_HINT_FILL);
				evas_object_show(patchbar);
				elm_box_pack_end(patchbox, patchbar);

				ui->matrix_audio = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/audio.png", "Audio", NULL, NULL);
				elm_toolbar_item_selected_set(ui->matrix_audio, EINA_TRUE);
				elm_object_item_tooltip_text_set(ui->matrix_audio, "Ctrl + 'A'");

				ui->matrix_control = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/control.png", "Control", NULL, NULL);
				elm_object_item_tooltip_text_set(ui->matrix_control, "Ctrl + 'N'");

				ui->matrix_cv = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/cv.png", "CV", NULL, NULL);
				elm_object_item_tooltip_text_set(ui->matrix_cv, "Ctrl + 'C'");

				ui->matrix_event = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/event.png", "Event", NULL, NULL);
				elm_object_item_tooltip_text_set(ui->matrix_event, "Ctrl + 'E'");

				ui->matrix_atom = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/atom.png", "Atom", NULL, NULL);
				elm_object_item_tooltip_text_set(ui->matrix_atom, "Ctrl + 'L'");

				Elm_Object_Item *sep = elm_toolbar_item_append(patchbar,
					NULL, NULL, NULL, NULL);
				elm_toolbar_item_separator_set(sep, EINA_TRUE);

				ui->matrix_atom_midi = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/midi.png", "MIDI", NULL, NULL);
				elm_object_item_tooltip_text_set(ui->matrix_atom_midi, "Ctrl + 'M'");

				ui->matrix_atom_osc = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/osc.png", "OSC", NULL, NULL);
				elm_object_item_tooltip_text_set(ui->matrix_atom_osc, "Ctrl + 'O'");

				ui->matrix_atom_time = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/time.png", "Time", NULL, NULL);
				elm_object_item_tooltip_text_set(ui->matrix_atom_time, "Ctrl + 'T'");

				ui->matrix_atom_patch = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/patch.png", "Patch", NULL, NULL);
				elm_object_item_tooltip_text_set(ui->matrix_atom_patch, "Ctrl + 'P'");

				ui->matrix_atom_xpress = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/xpress.png", "XPress", NULL, NULL);
				elm_object_item_tooltip_text_set(ui->matrix_atom_xpress, "Ctrl + 'X'");

			} // patchbar

			ui->matrix = patcher_object_add(patchbox);
			if(ui->matrix)
			{
				if(ui->zoom > 0.f)
					patcher_object_zoom_set(ui->matrix, ui->zoom);
				evas_object_smart_callback_add(ui->matrix, "connect,request",
					_matrix_connect_request, ui);
				evas_object_smart_callback_add(ui->matrix, "disconnect,request",
					_matrix_disconnect_request, ui);
				evas_object_smart_callback_add(ui->matrix, "realize,request",
					_matrix_realize_request, ui);
				evas_object_smart_callback_add(ui->matrix, "zoom,changed",
					_matrix_zoom_changed, ui);
				evas_object_size_hint_weight_set(ui->matrix, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(ui->matrix, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(ui->matrix);
				elm_box_pack_start(patchbox, ui->matrix);

				_patchbar_restore(ui);
				_patches_update(ui);
			} // matrix
		} // patchbox

		return win;
	}

	return NULL;
}

void
_menu_matrix(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(!ui->patchwin)
		ui->patchwin = _menu_matrix_new(ui);
}
