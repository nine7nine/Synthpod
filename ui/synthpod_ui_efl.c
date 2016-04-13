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
_efl_ui_hide(mod_t *mod)
{
	mod_ui_t *mod_ui = mod->mod_ui;

	if(mod_ui->eo.win)
		evas_object_del(mod_ui->eo.win);
	mod_ui->handle = NULL;
	mod_ui->eo.widget = NULL;
	mod_ui->eo.win = NULL;

	if(mod_ui->lib)
	{
		eina_module_unload(mod_ui->lib);
		eina_module_free(mod_ui->lib);
		mod_ui->lib = NULL;
	}

	mod->mod_ui = NULL;
	_mod_visible_set(mod, 0, 0);
}

static void
_efl_ui_delete_request(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;

	_efl_ui_hide(mod);
}

static inline Evas_Object *
_efl_ui_create(Evas_Object *parent, mod_t *mod)
{
	sp_ui_t *ui = mod->ui;
	mod_ui_t *mod_ui = mod->mod_ui;

	if(!mod_ui->ui || !mod_ui->descriptor)
		return NULL;

	const LilvNode *plugin_uri = lilv_plugin_get_uri(mod->plug);
	const char *plugin_string = NULL;
	if(plugin_uri)
		plugin_string = lilv_node_as_string(plugin_uri);

	const LilvNode *bundle_uri = lilv_ui_get_bundle_uri(mod_ui->ui);
#if defined(LILV_0_22)
	char *bundle_path = lilv_file_uri_parse(lilv_node_as_string(bundle_uri), NULL);
#else
	const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));
#endif

	// subscribe automatically to all non-atom ports by default
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
	}

	// set subscriptions for notifications
	_mod_subscription_set(mod, mod_ui->ui, 1);

	// instantiate UI
	mod_ui->eo.widget = NULL;

	if(mod_ui->descriptor->instantiate)
	{
		mod->feature_list[2].data = parent;

		mod_ui->handle = mod_ui->descriptor->instantiate(
			mod_ui->descriptor,
			plugin_string,
			bundle_path,
			_ext_ui_write_function,
			mod,
			(void **)&(mod_ui->eo.widget),
			mod->features);

		mod->feature_list[2].data = NULL;
	}

#if defined(LILV_0_22)
	lilv_free(bundle_path);
#endif

	if(!mod_ui->handle || !mod_ui->eo.widget)
		return NULL;

	return mod_ui->eo.widget;
}

static void
_efl_ui_show(mod_t *mod)
{
	mod_ui_t *mod_ui = mod->mod_ui;
	sp_ui_t *ui = mod->ui;

	mod_ui->descriptor = _ui_dlopen(mod_ui->ui, &mod_ui->lib);
	if(!mod_ui->descriptor)
		return;

	// add fullscreen EoUI
	Evas_Object *win = elm_win_add(ui->win, mod->name, ELM_WIN_BASIC);
	if(win)
	{
		int w = 800;
		int h = 450;

		elm_win_title_set(win, mod->name);
		evas_object_smart_callback_add(win, "delete,request", _efl_ui_delete_request, mod);

		mod_ui->eo.win = win;

		Evas_Object *bg = elm_bg_add(win);
		if(bg)
		{
			elm_bg_color_set(bg, 64, 64, 64);
			evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(bg, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(bg);
			elm_win_resize_object_add(win, bg);
		} // bg

		Evas_Object *widget = _efl_ui_create(win, mod);
		if(widget)
		{
			// get size hint
			int W, H;
			evas_object_size_hint_min_get(widget, &W, &H);
			if(W != 0)
				w = W;
			if(H != 0)
				h = H;

			evas_object_size_hint_weight_set(widget, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(widget, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(widget);
			elm_win_resize_object_add(win, widget);
		} // widget

		evas_object_resize(win, w, h);
		evas_object_show(win);

		_mod_visible_set(mod, 1, mod_ui->urid);
	} // win
}

static void
_efl_ui_port_event(mod_t *mod, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_ui_t *mod_ui = mod->mod_ui;

	if(  mod_ui
		&& mod_ui->ui
		&& mod_ui->descriptor
		&& mod_ui->descriptor->port_event
		&& mod_ui->handle)
	{
		if(mod_ui->eo.win)
			mod_ui->descriptor->port_event(mod_ui->handle, index, size, protocol, buf);
	}
}

const mod_ui_driver_t efl_ui_driver = {
	.show = _efl_ui_show,
	.hide = _efl_ui_hide,
	.port_event = _efl_ui_port_event,
};
