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

#if defined(SANDBOX_LIB)
static void
_ext_ui_subscribe_function(LV2UI_Controller controller, uint32_t port,
	uint32_t protocol, bool state)
{
	mod_t *mod = controller;

	_port_subscription_set(mod, port, protocol, state);
}

static Eina_Bool
_sbox_ui_recv(void *data, Ecore_Fd_Handler *fd_handler)
{
	sandbox_master_t *sb = data;

	sandbox_master_recv(sb);

	return ECORE_CALLBACK_RENEW;
}

static void
_sbox_ui_hide(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;
	mod_ui_t *mod_ui = mod->mod_ui;

	if(mod_ui->sbox.del)
	{
		ecore_event_handler_del(mod_ui->sbox.del);
		mod_ui->sbox.del = NULL;
	}

	if(mod_ui->sbox.fd)
	{
		ecore_main_fd_handler_del(mod_ui->sbox.fd);
		mod_ui->sbox.fd = NULL;
	}

	if(mod_ui->sbox.exe)
	{
		ecore_exe_interrupt(mod_ui->sbox.exe);
		mod_ui->sbox.exe = NULL;
	}

	if(mod_ui->sbox.sb)
	{
		sandbox_master_free(mod_ui->sbox.sb);
		mod_ui->sbox.sb = NULL;
	}

	/* FIXME
	if(ecore_file_exists(&mod_ui->sbox.socket_path[6]))
		ecore_file_remove(&mod_ui->sbox.socket_path[6]);
	*/

	// unsubscribe all ports
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 0);
	}

	// unsubscribe from notifications
	_mod_subscription_set(mod, mod_ui->ui, 0);

	mod->mod_ui = NULL;
	_mod_visible_set(mod, 0, 0);
}

static Eina_Bool
_sbox_ui_quit(void *data, int type, void *event)
{
	Ecore_Exe_Event_Del *ev = event;
	mod_t *mod = data;
	mod_ui_t *mod_ui = mod->mod_ui;

	if(mod_ui && (ev->exe == mod_ui->sbox.exe) )
	{
		_sbox_ui_hide(mod);
		return ECORE_CALLBACK_CANCEL;
	}

	return ECORE_CALLBACK_PASS_ON;
}

static void
_sbox_ui_show(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;
	mod_ui_t *mod_ui = mod->mod_ui;
	const char *executable;

//#define VALGRIND
//#define GDB

#ifdef VALGRIND
#	define DBG_PRE "valgrind --leak-check=full --show-leak-kinds=all --show-reachable=no --show-possibly-lost=no "
#else
#	ifdef GDB
#		define DBG_PRE "gdb -ex 'run' --args "
#	else
#		define DBG_PRE
#	endif
#endif

	switch(mod_ui->type)
	{
#if defined(SANDBOX_X11)
		case MOD_UI_TYPE_SANDBOX_X11:
			executable = DBG_PRE"synthpod_sandbox_x11";
			break;
#endif
#if defined(SANDBOX_GTK2)
		case MOD_UI_TYPE_SANDBOX_GTK2:
			executable = DBG_PRE"synthpod_sandbox_gtk2";
			break;
#endif
#if defined(SANDBOX_GTK3)
		case MOD_UI_TYPE_SANDBOX_GTK3:
			executable = DBG_PRE"synthpod_sandbox_gtk3";
			break;
#endif
#if defined(SANDBOX_QT4)
		case MOD_UI_TYPE_SANDBOX_QT4:
			executable = DBG_PRE"synthpod_sandbox_qt4";
			break;
#endif
#if defined(SANDBOX_QT5)
		case MOD_UI_TYPE_SANDBOX_QT5:
			executable = DBG_PRE"synthpod_sandbox_qt5";
			break;
#endif
#if defined(SANDBOX_EFL)
		case MOD_UI_TYPE_SANDBOX_EFL:
			executable = DBG_PRE"synthpod_sandbox_efl";
			break;
#endif
#if defined(SANDBOX_SHOW)
		case MOD_UI_TYPE_SANDBOX_SHOW:
			executable = DBG_PRE"synthpod_sandbox_show";
			break;
#endif
#if defined(SANDBOX_KX)
		case MOD_UI_TYPE_SANDBOX_KX:
			executable = DBG_PRE"synthpod_sandbox_kx";
			break;
#endif
		default:
			executable = NULL;
			break;
	}

#undef DBG_PRE

	if(!executable)
		return;

	const char *plugin_uri = lilv_node_as_uri(lilv_plugin_get_uri(mod->plug));
	const LilvNode *bundle_uri = lilv_plugin_get_bundle_uri(mod->plug);
#if defined(LILV_0_22)
	char *bundle_path = lilv_file_uri_parse(lilv_node_as_string(bundle_uri), NULL);
#else
	const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));
#endif
	const char *ui_uri = lilv_node_as_uri(lilv_ui_get_uri(mod_ui->ui));
	strcpy(mod_ui->sbox.socket_path, "ipc:///tmp/synthpod_XXXXXX");
	int _fd = mkstemp(&mod_ui->sbox.socket_path[6]); //TODO check
	if(_fd)
		close(_fd);

	mod_ui->sbox.driver.socket_path = mod_ui->sbox.socket_path;
	mod_ui->sbox.driver.map = ui->driver->map;
	mod_ui->sbox.driver.unmap = ui->driver->unmap;
	mod_ui->sbox.driver.recv_cb = _ext_ui_write_function;
	mod_ui->sbox.driver.subscribe_cb = _ext_ui_subscribe_function;

	mod_ui->sbox.sb = sandbox_master_new(&mod_ui->sbox.driver, mod);
	if(!mod_ui->sbox.sb)
		fprintf(stderr, "failed to initialize sandbox master\n");
	int fd;
	sandbox_master_fd_get(mod_ui->sbox.sb, &fd); //FIXME check

	char *cmd = NULL;
	asprintf(&cmd, "%s -p '%s' -b '%s' -u '%s' -s '%s' -w '%s' -f %f", //FIXME makes update rate configurable
		executable, plugin_uri, bundle_path, ui_uri, mod_ui->sbox.socket_path, mod->name, ui->update_rate); //FIXME check
	//printf("cmd: %s\n", cmd);
#if defined(LILV_0_22)
	lilv_free(bundle_path);
#endif

	mod_ui->sbox.exe = ecore_exe_run(cmd, mod_ui); //FIXME check
	free(cmd);
	mod_ui->sbox.fd= ecore_main_fd_handler_add(fd, ECORE_FD_READ,
		_sbox_ui_recv, mod_ui->sbox.sb, NULL, NULL);
	mod_ui->sbox.del = ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _sbox_ui_quit, mod);

	_mod_visible_set(mod, 1, mod_ui->urid);

	// subscribe to ports
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];
		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
	}

	// subscribe to notifications
	_mod_subscription_set(mod, mod_ui->ui, 1);
}

static inline void
_sbox_ui_flush(void *data)
{
	mod_t *mod = data;
	mod_ui_t *mod_ui = mod->mod_ui;

	const bool more = sandbox_master_flush(mod_ui->sbox.sb);
	if(more)
	{
		//fprintf(stderr, "_sbox_ui_flush there is more\n");
		ecore_job_add(_sbox_ui_flush, mod); // schedule flush
	}
}

static void
_sbox_ui_port_event(mod_t *mod, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_ui_t *mod_ui = mod->mod_ui;

	const bool more = sandbox_master_send(mod_ui->sbox.sb, index, size, protocol, buf);
	if(!more)
	{
		//fprintf(stderr, "_sbox_ui_port_event there is more\n");
		ecore_job_add(_sbox_ui_flush, mod); // schedule flush
	}
}

const mod_ui_driver_t sbox_ui_driver = {
	.show = _sbox_ui_show,
	.hide = _sbox_ui_hide,
	.port_event = _sbox_ui_port_event,
};
#endif
