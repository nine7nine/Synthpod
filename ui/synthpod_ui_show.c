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
_show_ui_hide(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;
	mod_ui_t *mod_ui = mod->mod_ui;

	// stop animator
	if(mod_ui->show.anim)
	{
		ecore_animator_del(mod_ui->show.anim);
		mod_ui->show.anim = NULL;
	}

	// hide UI
	if(mod_ui->show.show_iface && mod_ui->show.show_iface->hide && mod_ui->handle)
	{
		if(mod_ui->show.show_iface->hide(mod_ui->handle))
			fprintf(stderr, "show_iface->hide failed\n");
		else
			mod_ui->show.visible = 0; // toggle visibility flag
	}

	// unsubscribe all ports
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 0);
	}

	// unsubscribe from notifications
	_mod_subscription_set(mod, mod_ui->ui, 0);

	// call cleanup 
	if(mod_ui->descriptor && mod_ui->descriptor->cleanup && mod_ui->handle)
		mod_ui->descriptor->cleanup(mod_ui->handle);
	mod_ui->handle = NULL;
	mod_ui->show.idle_iface = NULL;
	mod_ui->show.show_iface = NULL;

	if(mod_ui->lib)
	{
		eina_module_unload(mod_ui->lib);
		eina_module_free(mod_ui->lib);
		mod_ui->lib = NULL;
	}

	mod->mod_ui = NULL;
	_mod_visible_set(mod, 0, 0);
}

static Eina_Bool
_show_ui_animator(void *data)
{
	mod_t *mod = data;
	mod_ui_t *mod_ui = mod->mod_ui;

	int res = 0;
	if(mod_ui->show.idle_iface && mod_ui->show.idle_iface->idle && mod_ui->handle)
		res = mod_ui->show.idle_iface->idle(mod_ui->handle);

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
	mod_ui_t *mod_ui = mod->mod_ui;

	mod_ui->descriptor = _ui_dlopen(mod_ui->ui, &mod_ui->lib);
	if(!mod_ui->descriptor)
		return;

	const LilvNode *plugin_uri = lilv_plugin_get_uri(mod->plug);
	const char *plugin_string = lilv_node_as_string(plugin_uri);

	const LilvNode *bundle_uri = lilv_ui_get_bundle_uri(mod_ui->ui);
#if defined(LILV_0_22)
	char *bundle_path = lilv_file_uri_parse(lilv_node_as_string(bundle_uri), NULL);
#else
	const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));
#endif

	// subscribe to ports
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];
		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
	}

	// subscribe to notifications
	_mod_subscription_set(mod, mod_ui->ui, 1);

	// instantiate UI
	void *dummy;
	mod_ui->handle = mod_ui->descriptor->instantiate(
		mod_ui->descriptor,
		plugin_string,
		bundle_path,
		_ext_ui_write_function,
		mod,
		&dummy,
		mod->features);

#if defined(LILV_0_22)
	lilv_free(bundle_path);
#endif

	if(!mod_ui->handle)
		return;

	// get show iface if any
	if(mod_ui->descriptor->extension_data)
		mod_ui->show.show_iface = mod_ui->descriptor->extension_data(LV2_UI__showInterface);

	if(!mod_ui->show.show_iface)
		return;

	// show UI
	if(mod_ui->show.show_iface && mod_ui->show.show_iface->show && mod_ui->handle)
	{
		if(mod_ui->show.show_iface->show(mod_ui->handle))
			fprintf(stderr, "show_iface->show failed\n");
		else
			mod_ui->show.visible = 1; // toggle visibility flag
	}

	// get idle iface if any
	if(mod_ui->descriptor->extension_data)
		mod_ui->show.idle_iface = mod_ui->descriptor->extension_data(LV2_UI__idleInterface);

	// start animator
	if(mod_ui->show.idle_iface)
		mod_ui->show.anim = ecore_animator_add(_show_ui_animator, mod);

	_mod_visible_set(mod, 1, mod_ui->urid);
}

static void
_show_ui_port_event(mod_t *mod, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_ui_t *mod_ui = mod->mod_ui;
	sp_ui_t *ui = mod->ui;

	//printf("_show_port_event: %u %u %u\n", index, size, protocol);

	if(  mod_ui
		&& mod_ui->ui
		&& mod_ui->descriptor
		&& mod_ui->descriptor->port_event
		&& mod_ui->handle)
	{
		mod_ui->descriptor->port_event(mod_ui->handle,
			index, size, protocol, buf);
		if(protocol == ui->regs.port.float_protocol.urid)
		{
			// send it twice for plugins that expect "0" instead of float_protocol URID
			mod_ui->descriptor->port_event(mod_ui->handle,
				index, size, 0, buf);
		}
	}
}

const mod_ui_driver_t show_ui_driver = {
	.show = _show_ui_show,
	.hide = _show_ui_hide,
	.port_event = _show_ui_port_event,
};
