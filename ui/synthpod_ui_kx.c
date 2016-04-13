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
_kx_ui_cleanup(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;
	mod_ui_t *mod_ui = mod->mod_ui;

	// stop animator
	if(mod_ui->kx.anim)
	{
		ecore_animator_del(mod_ui->kx.anim);
		mod_ui->kx.anim = NULL;
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
	mod_ui->kx.widget = NULL;
	mod_ui->kx.dead = 0;

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
_kx_ui_animator(void *data)
{
	mod_t *mod = data;
	mod_ui_t *mod_ui = mod->mod_ui;

	LV2_EXTERNAL_UI_RUN(mod_ui->kx.widget);

	if(mod_ui->kx.dead)
	{
		_kx_ui_cleanup(mod);

		return EINA_FALSE; // stop animator
	}

	return EINA_TRUE; // retrigger animator
}
 
// plugin ui has been closed manually
void
_kx_ui_closed(LV2UI_Controller controller)
{
	mod_t *mod = controller;
	mod_ui_t *mod_ui = mod->mod_ui;

	if(!mod_ui || !mod_ui->ui)
		return;

	// mark for cleanup
	mod_ui->kx.dead = 1;
}

static void
_kx_ui_show(mod_t *mod)
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
	mod_ui->handle = mod_ui->descriptor->instantiate(
		mod_ui->descriptor,
		plugin_string,
		bundle_path,
		_ext_ui_write_function,
		mod,
		(void **)&mod_ui->kx.widget,
		mod->features);

#if defined(LILV_0_22)
	lilv_free(bundle_path);
#endif

	if(!mod_ui->handle)
		return;

	// show UI
	LV2_EXTERNAL_UI_SHOW(mod_ui->kx.widget);

	// start animator
	mod_ui->kx.anim = ecore_animator_add(_kx_ui_animator, mod);

	_mod_visible_set(mod, 1, mod_ui->urid);
}

static void
_kx_ui_hide(mod_t *mod)
{
	mod_ui_t *mod_ui = mod->mod_ui;

	// hide UI
	if(mod_ui->kx.anim) // UI is running
		LV2_EXTERNAL_UI_HIDE(mod_ui->kx.widget);

	// cleanup
	_kx_ui_cleanup(mod);
}

static void
_kx_ui_port_event(mod_t *mod, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_ui_t *mod_ui = mod->mod_ui;
	sp_ui_t *ui = mod->ui;

	//printf("_kx_port_event: %u %u %u\n", index, size, protocol);

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

const mod_ui_driver_t kx_ui_driver = {
	.show = _kx_ui_show,
	.hide = _kx_ui_hide,
	.port_event = _kx_ui_port_event,
};
