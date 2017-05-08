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

#include <synthpod_app_private.h>
#include <synthpod_patcher.h>

#if 0 // is deprecated
__realtime static bool
_sp_app_from_ui_float_protocol(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);
	const transfer_float_t *trans = (const transfer_float_t *)atom;

	port_t *port = _sp_app_port_get(app, trans->transfer.uid.body, trans->transfer.port.body);
	if(!port) // port not found
		return advance_ui[app->block_state];

	// set port value
	float *buf = PORT_BASE_ALIGNED(port);
	if(port->type == PORT_TYPE_CONTROL)
	{
		*buf = trans->value.body;
		port->control.last = trans->value.body;
		_sp_app_port_control_stash(port);
	}
	else if(port->type == PORT_TYPE_CV)
	{
		for(unsigned i = 0; i < app->driver->max_block_size; i++)
			buf[i] = trans->value.body;
		port->cv.last = fabs(trans->value.body);
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_atom_transfer(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);
	const transfer_atom_t *trans = (const transfer_atom_t *)atom;

	port_t *port = _sp_app_port_get(app, trans->transfer.uid.body, trans->transfer.port.body);
	if(!port) // port not found
		return advance_ui[app->block_state];

	// set port value
	void *buf = PORT_BASE_ALIGNED(port);
	memcpy(buf, trans->atom, sizeof(LV2_Atom) + trans->atom->size);

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_event_transfer(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transfer_atom_t *trans = (const transfer_atom_t *)atom;

	port_t *port = _sp_app_port_get(app, trans->transfer.uid.body, trans->transfer.port.body);
	if(!port) // port not found
		return advance_ui[app->block_state];

	// messages from UI are ALWAYS appended to default port buffer, no matter
	// how many sources the port may have
	const uint32_t capacity = PORT_SIZE(port);
	LV2_Atom_Sequence *seq = PORT_BASE_ALIGNED(port);

	// find last event
	LV2_Atom_Event *last = NULL;
	LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
		last = ev;

	const LV2_Atom_Event *dummy = (const void *)atom- offsetof(LV2_Atom_Event, body);
	LV2_Atom_Event *ev = lv2_atom_sequence_append_event(seq, capacity, dummy);
	if(ev)
		ev->time.frames = 0;
	else
		printf("_sp_app_from_ui_event_transfer: failed to append\n");

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_module_list(sp_app_t *app, const LV2_Atom *atom)
{
	// iterate over existing modules and send module_add_t
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		//signal to UI
		size_t size = sizeof(transmit_module_add_t)
			+ lv2_atom_pad_size(strlen(mod->uri_str) + 1);
		transmit_module_add_t *trans = _sp_app_to_ui_request(app, size);
		if(trans)
		{
			_sp_transmit_module_add_fill(&app->regs, &app->forge, trans, size,
				mod->uid, mod->uri_str);
			_sp_app_to_ui_advance(app, size);
		}
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_module_supported(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_supported_t *module_supported = (const transmit_module_supported_t *)atom;

	if(module_supported->state.body == -1) // query
	{
		// send request to worker thread
		size_t size = sizeof(job_t) + module_supported->uri.atom.size;
		job_t *job = _sp_app_to_worker_request(app, size);
		if(job)
		{
			job->request = JOB_TYPE_REQUEST_MODULE_SUPPORTED;
			memcpy(job->uri, module_supported->uri_str, module_supported->uri.atom.size);
			_sp_app_to_worker_advance(app, size);
		}
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_module_add(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_add_t *module_add = (const transmit_module_add_t *)atom;

	// send request to worker thread
	size_t size = sizeof(job_t) + module_add->uri.atom.size;
	job_t *job = _sp_app_to_worker_request(app, size);
	if(job)
	{
		job->request = JOB_TYPE_REQUEST_MODULE_ADD;
		memcpy(job->uri, module_add->uri_str, module_add->uri.atom.size);
		_sp_app_to_worker_advance(app, size);
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_module_del(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_del_t *module_del = (const transmit_module_del_t *)atom;

	// search mod according to its UUID
	mod_t *mod = _sp_app_mod_get(app, module_del->uid.body);
	if(!mod) // mod not found
		return advance_ui[app->block_state];

	int needs_ramping = 0;
	for(unsigned p1=0; p1<mod->num_ports; p1++)
	{
		port_t *port = &mod->ports[p1];

		connectable_t *conn = _sp_app_port_connectable(port);
		if(conn)
		{
			// disconnect sources
			for(int s=0; s<conn->num_sources; s++)
			{
				_sp_app_port_disconnect_request(app,
					conn->sources[s].port, port, RAMP_STATE_DOWN);
			}
		}

		// disconnect sinks
		for(unsigned m=0; m<app->num_mods; m++)
			for(unsigned p2=0; p2<app->mods[m]->num_ports; p2++)
			{
				needs_ramping += _sp_app_port_disconnect_request(app,
					port, &app->mods[m]->ports[p2], RAMP_STATE_DOWN_DEL);
			}
	}
	if(needs_ramping == 0)
		_sp_app_mod_eject(app, mod);

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_module_move(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_move_t *move = (const transmit_module_move_t *)atom;

	mod_t *mod = _sp_app_mod_get(app, move->uid.body);
	mod_t *prev = _sp_app_mod_get(app, move->prev.body);
	if(!mod || !prev)
		return advance_ui[app->block_state];

	uint32_t mod_idx;
	for(mod_idx=0; mod_idx<app->num_mods; mod_idx++)
		if(app->mods[mod_idx] == mod)
			break;

	uint32_t prev_idx;
	for(prev_idx=0; prev_idx<app->num_mods; prev_idx++)
		if(app->mods[prev_idx] == prev)
			break;

	if(mod_idx < prev_idx)
	{
		// forward loop
		for(unsigned i=mod_idx, j=i; i<app->num_mods; i++)
		{
			if(app->mods[i] == mod)
				continue;

			app->mods[j++] = app->mods[i];

			if(app->mods[i] == prev)
				app->mods[j++] = mod;
		}
	}
	else // mod_idx > prev_idx
	{
		// reverse loop
		for(int i=app->num_mods-1, j=i; i>=0; i--)
		{
			if(app->mods[i] == mod)
				continue;
			else if(app->mods[i] == prev)
				app->mods[j--] = mod;

			app->mods[j--] = app->mods[i];
		}
	}

	//TODO signal to ui

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_module_preset_load(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_preset_load_t *pset = (const transmit_module_preset_load_t *)atom;

	mod_t *mod = _sp_app_mod_get(app, pset->uid.body);
	if(!mod)
		return advance_ui[app->block_state];

	assert( (app->block_state == BLOCKING_STATE_RUN)
		|| (app->block_state == BLOCKING_STATE_BLOCK) );
	if(app->block_state == BLOCKING_STATE_RUN)
	{
		const bool needs_ramping = _mod_needs_ramping(mod, RAMP_STATE_DOWN_DRAIN, true);
		app->silence_state = !needs_ramping
			? SILENCING_STATE_RUN
			: SILENCING_STATE_BLOCK;

		// send request to worker thread
		size_t size = sizeof(job_t);
		job_t *job = _sp_app_to_worker_request(app, size);
		if(job)
		{
			app->block_state = BLOCKING_STATE_DRAIN; // wait for drain

			job->request = JOB_TYPE_REQUEST_DRAIN;
			job->status = 0;
			_sp_app_to_worker_advance(app, size);
		}
	}
	else if(app->block_state == BLOCKING_STATE_BLOCK)
	{
		if(app->silence_state == SILENCING_STATE_BLOCK)
			return false; // not fully silenced yet, wait

		// send request to worker thread
		size_t size = sizeof(job_t) + pset->uri.atom.size;
		job_t *job = _sp_app_to_worker_request(app, size);
		if(job)
		{
			app->block_state = BLOCKING_STATE_WAIT; // wait for job
			mod->bypassed = mod->needs_bypassing;

			job->request = JOB_TYPE_REQUEST_PRESET_LOAD;
			job->mod = mod;
			memcpy(job->uri, pset->uri_str, pset->uri.atom.size);
			_sp_app_to_worker_advance(app, size);

			return true; // advance
		}
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_module_preset_save(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_preset_save_t *pset = (const transmit_module_preset_save_t *)atom;

	mod_t *mod = _sp_app_mod_get(app, pset->uid.body);
	if(!mod)
		return advance_ui[app->block_state];

	assert( (app->block_state == BLOCKING_STATE_RUN)
		|| (app->block_state == BLOCKING_STATE_BLOCK) );
	if(app->block_state == BLOCKING_STATE_RUN)
	{
		// send request to worker thread
		size_t size = sizeof(job_t);
		job_t *job = _sp_app_to_worker_request(app, size);
		if(job)
		{
			app->block_state = BLOCKING_STATE_DRAIN; // wait for drain

			job->request = JOB_TYPE_REQUEST_DRAIN;
			job->status = 0;
			_sp_app_to_worker_advance(app, size);
		}
	}
	else if(app->block_state == BLOCKING_STATE_BLOCK)
	{
		// send request to worker thread
		size_t size = sizeof(job_t) + pset->label.atom.size;
		job_t *job = _sp_app_to_worker_request(app, size);
		if(job)
		{
			app->block_state = BLOCKING_STATE_WAIT; // wait for job

			job->request = JOB_TYPE_REQUEST_PRESET_SAVE;
			job->mod = mod;
			memcpy(job->uri, pset->label_str, pset->label.atom.size);
			_sp_app_to_worker_advance(app, size);

			return true; // advance
		}
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_module_selected(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_selected_t *select = (const transmit_module_selected_t *)atom;

	mod_t *mod = _sp_app_mod_get(app, select->uid.body);
	if(!mod)
		return advance_ui[app->block_state];

	switch(select->state.body)
	{
		case -1: // query
		{
			// signal ui
			size_t size = sizeof(transmit_module_selected_t);
			transmit_module_selected_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transmit_module_selected_fill(&app->regs, &app->forge, trans, size,
					mod->uid, mod->selected);
				_sp_app_to_ui_advance(app, size);
			}
			break;
		}
		case 0: // deselect
			mod->selected = false;
			break;
		case 1: // select
			mod->selected = true;
			break;
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_module_visible(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_visible_t *visible = (const transmit_module_visible_t *)atom;

	mod_t *mod = _sp_app_mod_get(app, visible->uid.body);
	if(!mod)
		return advance_ui[app->block_state];

	switch(visible->state.body)
	{
		case -1: // query
		{
			// signal ui
			size_t size = sizeof(transmit_module_visible_t);
			transmit_module_visible_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transmit_module_visible_fill(&app->regs, &app->forge, trans, size,
					mod->uid, mod->visible ? 1 : 0, mod->visible);
				_sp_app_to_ui_advance(app, size);
			}
			break;
		}
		case 0: // deselect
			mod->visible = 0;
			break;
		case 1: // select
			mod->visible = visible->urid.body;
			break;
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_module_disabled(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_disabled_t *disabled = (const transmit_module_disabled_t *)atom;

	mod_t *mod = _sp_app_mod_get(app, disabled->uid.body);
	if(!mod)
		return advance_ui[app->block_state];

	switch(disabled->state.body)
	{
		case -1: // query
		{
			// signal ui
			size_t size = sizeof(transmit_module_disabled_t);
			transmit_module_disabled_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transmit_module_disabled_fill(&app->regs, &app->forge, trans, size,
					mod->uid, mod->disabled);
				_sp_app_to_ui_advance(app, size);
			}
			break;
		}
		case 0: // deselect
		{
			mod->disabled = false;
			_mod_needs_ramping(mod, RAMP_STATE_UP, false);
			break;
		}
		case 1: // select
		{
			const bool needs_ramping = _mod_needs_ramping(mod, RAMP_STATE_DOWN_DISABLE, true);
			if(!needs_ramping) // disable it right now
				mod->disabled = true;
			break;
		}
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_module_embedded(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_embedded_t *embedded = (const transmit_module_embedded_t *)atom;

	mod_t *mod = _sp_app_mod_get(app, embedded->uid.body);
	if(!mod)
		return advance_ui[app->block_state];

	switch(embedded->state.body)
	{
		case -1: // query
		{
			// signal ui
			size_t size = sizeof(transmit_module_embedded_t);
			transmit_module_embedded_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transmit_module_embedded_fill(&app->regs, &app->forge, trans, size,
					mod->uid, mod->embedded);
				_sp_app_to_ui_advance(app, size);
			}
			break;
		}
		case 0: // deselect
			mod->embedded = false;
			break;
		case 1: // select
			mod->embedded = true;
			break;
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_port_connected(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_port_connected_t *conn = (const transmit_port_connected_t *)atom;

	port_t *src_port = _sp_app_port_get(app, conn->src_uid.body, conn->src_port.body);
	port_t *snk_port = _sp_app_port_get(app, conn->snk_uid.body, conn->snk_port.body);
	if(!src_port || !snk_port)
		return advance_ui[app->block_state];

	int32_t state = 0;
	switch(conn->state.body)
	{
		case -1: // query
		{
			if(_sp_app_port_connected(src_port, snk_port))
				state = 1;
			break;
		}
		case 0: // disconnect
		{
			_sp_app_port_disconnect_request(app, src_port, snk_port, RAMP_STATE_DOWN);
			state = 0;
			break;
		}
		case 1: // connect
		{
			state = _sp_app_port_connect(app, src_port, snk_port);
			break;
		}
	}

	int32_t indirect = 0; // aka direct
	if(src_port->mod == snk_port->mod)
	{
		indirect = -1; // feedback
	}
	else
	{
		for(unsigned m=0; m<app->num_mods; m++)
		{
			if(app->mods[m] == src_port->mod)
			{
				indirect = 0;
				break;
			}
			else if(app->mods[m] == snk_port->mod)
			{
				indirect = 1;
				break;
			}
		}
	}

	// signal to ui
	size_t size = sizeof(transmit_port_connected_t);
	transmit_port_connected_t *trans = _sp_app_to_ui_request(app, size);
	if(trans)
	{
		_sp_transmit_port_connected_fill(&app->regs, &app->forge, trans, size,
			src_port->mod->uid, src_port->index,
			snk_port->mod->uid, snk_port->index, state, indirect);
		_sp_app_to_ui_advance(app, size);
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_port_subscribed(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_port_subscribed_t *subscribe = (const transmit_port_subscribed_t *)atom;

	port_t *port = _sp_app_port_get(app, subscribe->uid.body, subscribe->port.body);
	if(!port)
		return advance_ui[app->block_state];

	if(subscribe->state.body) // subscribe
	{
		port->protocol = subscribe->prot.body;
		port->subscriptions += 1;
	}
	else // unsubscribe
	{
		if(port->subscriptions > 0)
			port->subscriptions -= 1;
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_port_refresh(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_port_refresh_t *refresh = (const transmit_port_refresh_t *)atom;

	port_t *port = _sp_app_port_get(app, refresh->uid.body, refresh->port.body);
	if(!port)
		return advance_ui[app->block_state];

	if(port->type == PORT_TYPE_CONTROL)
	{
		float *buf_ptr = PORT_BASE_ALIGNED(port);
		port->control.last = *buf_ptr - 0.1; // will force notification
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_port_selected(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_port_selected_t *select = (const transmit_port_selected_t *)atom;

	port_t *port = _sp_app_port_get(app, select->uid.body, select->port.body);
	if(!port)
		return advance_ui[app->block_state];

	switch(select->state.body)
	{
		case -1: // query
		{
			// signal ui
			size_t size = sizeof(transmit_port_selected_t);
			transmit_port_selected_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transmit_port_selected_fill(&app->regs, &app->forge, trans, size,
					port->mod->uid, port->index, port->selected);
				_sp_app_to_ui_advance(app, size);
			}
			break;
		}
		case 0: // deselect
			port->selected = 0;
			break;
		case 1: // select
			port->selected = 1;
			break;
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_port_monitored(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_port_monitored_t *monitor = (const transmit_port_monitored_t *)atom;

	port_t *port = _sp_app_port_get(app, monitor->uid.body, monitor->port.body);
	if(!port)
		return advance_ui[app->block_state];

	switch(monitor->state.body)
	{
		case -1: // query
		{
			// signal ui
			size_t size = sizeof(transmit_port_monitored_t);
			transmit_port_monitored_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transmit_port_monitored_fill(&app->regs, &app->forge, trans, size,
					port->mod->uid, port->index, port->monitored);
				_sp_app_to_ui_advance(app, size);
			}
			break;
		}
		case 0: // unmonitor
			port->monitored = 0;
			break;
		case 1: // monitor
			port->monitored = 1;
			break;
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_bundle_load(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_bundle_load_t *load = (const transmit_bundle_load_t *)atom;
	if(!load->path.atom.size)
		return advance_ui[app->block_state];

	assert( (app->block_state == BLOCKING_STATE_RUN)
		|| (app->block_state == BLOCKING_STATE_BLOCK) );
	if(app->block_state == BLOCKING_STATE_RUN)
	{
		//FIXME ramp down system outputs

		// send request to worker thread
		size_t size = sizeof(job_t);
		job_t *job = _sp_app_to_worker_request(app, size);
		if(job)
		{
			app->block_state = BLOCKING_STATE_DRAIN; // wait for drain

			job->request = JOB_TYPE_REQUEST_DRAIN;
			job->status = 0;
			_sp_app_to_worker_advance(app, size);
		}
	}
	else if(app->block_state == BLOCKING_STATE_BLOCK)
	{
		//FIXME ramp up system outputs

		// send request to worker thread
		size_t size = sizeof(job_t) + load->path.atom.size;
		job_t *job = _sp_app_to_worker_request(app, size);
		if(job)
		{
			app->block_state = BLOCKING_STATE_WAIT; // wait for job
			app->load_bundle = true; // for sp_app_bypassed

			job->request = JOB_TYPE_REQUEST_BUNDLE_LOAD;
			job->status = load->status.body;
			memcpy(job->uri, load->path_str, load->path.atom.size);
			_sp_app_to_worker_advance(app, size);

			return true; // advance
		}
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_bundle_save(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_bundle_save_t *save = (const transmit_bundle_save_t *)atom;
	if(!save->path.atom.size)
		return advance_ui[app->block_state];

	assert( (app->block_state == BLOCKING_STATE_RUN)
		|| (app->block_state == BLOCKING_STATE_BLOCK) );
	if(app->block_state == BLOCKING_STATE_RUN)
	{
		// send request to worker thread
		size_t size = sizeof(job_t);
		job_t *job = _sp_app_to_worker_request(app, size);
		if(job)
		{
			app->block_state = BLOCKING_STATE_DRAIN; // wait for drain

			job->request = JOB_TYPE_REQUEST_DRAIN;
			job->status = 0;
			_sp_app_to_worker_advance(app, size);
		}
	}
	else if(app->block_state == BLOCKING_STATE_BLOCK)
	{
		// send request to worker thread
		size_t size = sizeof(job_t) + save->path.atom.size;
		job_t *job = _sp_app_to_worker_request(app, size);
		if(job)
		{
			app->block_state = BLOCKING_STATE_WAIT; // wait for job

			job->request = JOB_TYPE_REQUEST_BUNDLE_SAVE;
			job->status = save->status.body;
			memcpy(job->uri, save->path_str, save->path.atom.size);
			_sp_app_to_worker_advance(app, size);

			return true; // advance
		}
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_grid_cols(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_grid_cols_t *grid = (const transmit_grid_cols_t *)atom;

	if(grid->cols.body == -1)
	{
		// signal ui
		const size_t size = sizeof(transmit_grid_cols_t);
		transmit_grid_cols_t *trans = _sp_app_to_ui_request(app, size);
		if(trans)
		{
			_sp_transmit_grid_cols_fill(&app->regs, &app->forge, trans, size,
				app->ncols);	
			_sp_app_to_ui_advance(app, size);
		}
	}
	else if(grid->cols.body > 0)
	{
		app->ncols = grid->cols.body;
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_grid_rows(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_grid_rows_t *grid = (const transmit_grid_rows_t *)atom;

	if(grid->rows.body == -1)
	{
		// signal ui
		const size_t size = sizeof(transmit_grid_rows_t);
		transmit_grid_rows_t *trans = _sp_app_to_ui_request(app, size);
		if(trans)
		{
			_sp_transmit_grid_rows_fill(&app->regs, &app->forge, trans, size,
				app->nrows);	
			_sp_app_to_ui_advance(app, size);
		}
	}
	else if(grid->rows.body > 0)
	{
		app->nrows = grid->rows.body;
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_pane_left(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_pane_left_t *pane = (const transmit_pane_left_t *)atom;

	if(pane->left.body == -1.f)
	{
		// signal ui
		const size_t size = sizeof(transmit_pane_left_t);
		transmit_pane_left_t *trans = _sp_app_to_ui_request(app, size);
		if(trans)
		{
			_sp_transmit_pane_left_fill(&app->regs, &app->forge, trans, size,
				app->nleft);	
			_sp_app_to_ui_advance(app, size);
		}
	}
	else if( (pane->left.body >= 0.f) && (pane->left.body <= 1.f) )
	{
		app->nleft = pane->left.body;
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_quit(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_quit_t *quit = (const transmit_quit_t *)atom;

	if(app->driver->close_request)
		app->driver->close_request(app->data);

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_path_get(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_path_get_t *path_get= (const transmit_path_get_t *)atom;

	//FIXME not rt-safe
	const size_t size = sizeof(transmit_path_get_t) + strlen(app->bundle_path) + 1;
	transmit_path_get_t *trans = _sp_app_to_ui_request(app, size);
	if(trans)
	{
		_sp_transmit_path_get_fill(&app->regs, &app->forge, trans, size, app->bundle_path);
		_sp_app_to_ui_advance(app, size);
	}

	return advance_ui[app->block_state];
}
#endif

static inline bool
_mod_needs_ramping(mod_t *mod, ramp_state_t state, bool silencing)
{
	sp_app_t *app = mod->app;

	// ramping
	int needs_ramping = 0;
	for(unsigned p1=0; p1<mod->num_ports; p1++)
	{
		port_t *port = &mod->ports[p1];

		// silence sources
		/* TODO is this needed?
		for(int s=0; s<port->num_sources; s++)
		{
			_sp_app_port_silence_request(app,
				port->sources[s].port, port, state);
		}
		*/

		// silence sinks
		for(unsigned m=0; m<app->num_mods; m++)
			for(unsigned p2=0; p2<app->mods[m]->num_ports; p2++)
			{
				if(silencing)
				{
					needs_ramping += _sp_app_port_silence_request(app,
						port, &app->mods[m]->ports[p2], state);
				}
				else
				{
					needs_ramping += _sp_app_port_desilence(app,
						port, &app->mods[m]->ports[p2]);
				}
			}
	}

	return needs_ramping > 0;
}

//FIXME move into another file
__realtime static mod_t *
_mod_find_by_urn(sp_app_t *app, LV2_URID urn)
{
	for(unsigned m = 0; m < app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];

		if(mod->urn == urn)
			return mod;
	}

	return NULL;
}

//FIXME move into another file
__realtime static port_t *
_port_find_by_symbol(sp_app_t *app, LV2_URID urn, const char *symbol)
{
	mod_t *mod = _mod_find_by_urn(app, urn);
	if(mod)
	{
		for(unsigned p = 0; p < mod->num_ports; p++)
		{
			port_t *port = &mod->ports[p];

			if(!strcmp(port->symbol, symbol))
				return port;
		}
	}

	return NULL;
}

__realtime void
_sp_app_ui_set_modlist(sp_app_t *app, LV2_URID subj, int32_t seqn)
{
	LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
	if(answer)
	{
		LV2_Atom_Forge_Frame frame [2];
		LV2_Atom_Forge_Ref ref = synthpod_patcher_set_object(
			&app->regs, &app->forge, &frame[0], subj, seqn, app->regs.synthpod.module_list.urid);
		if(ref)
			ref = lv2_atom_forge_tuple(&app->forge, &frame[1]);
		for(unsigned m = 0; m < app->num_mods; m++)
		{
			mod_t *mod = app->mods[m];

			if(ref)
				ref = lv2_atom_forge_urid(&app->forge, mod->urn);
		}
		if(ref)
		{
			synthpod_patcher_pop(&app->forge, frame, 2);
			_sp_app_to_ui_advance_atom(app, answer);
		}
		else
		{
			_sp_app_to_ui_overflow(app);
		}
	}
	else
	{
		_sp_app_to_ui_overflow(app);
	}
}

__realtime static bool
_sp_app_from_ui_patch_get(sp_app_t *app, const LV2_Atom *atom)
{
	const LV2_Atom_Object *obj = ASSUME_ALIGNED(atom);

	const LV2_Atom_URID *subject = NULL;
	const LV2_Atom_Int *seqn = NULL;
	const LV2_Atom_URID *property = NULL;

	lv2_atom_object_get(obj,
		app->regs.patch.subject.urid, &subject,
		app->regs.patch.sequence_number.urid, &seqn,
		app->regs.patch.property.urid, &property,
		0);

	const LV2_URID subj = subject && (subject->atom.type == app->forge.URID)
		? subject->body : 0;
	const int32_t sn = seqn && (seqn->atom.type == app->forge.Int)
		? seqn->body : 0;
	const LV2_URID prop = property && (property->atom.type == app->forge.URID)
		? property->body : 0;

	//printf("got patch:Get for <%s>\n", app->driver->unmap->unmap(app->driver->unmap->handle, subj));

	if(!subj && prop) //FIXME
	{
		//printf("\tpatch:property <%s>\n", app->driver->unmap->unmap(app->driver->unmap->handle, prop));

		if(prop == app->regs.synthpod.module_list.urid)
		{
			_sp_app_ui_set_modlist(app, subj, sn);
		}
		else if(prop == app->regs.synthpod.connection_list.urid)
		{
			LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
			if(answer)
			{
				LV2_Atom_Forge_Frame frame [3];
				LV2_Atom_Forge_Ref ref = synthpod_patcher_set_object(
					&app->regs, &app->forge, &frame[0], subj, sn, prop);
				if(ref)
					ref = lv2_atom_forge_tuple(&app->forge, &frame[1]);
				for(unsigned m = 0; m < app->num_mods; m++)
				{
					mod_t *mod = app->mods[m];

					for(unsigned p = 0; p < mod->num_ports; p++)
					{
						port_t *port = &mod->ports[p];

						connectable_t *conn = _sp_app_port_connectable(port);
						if(conn)
						{
							for(int s = 0; s < conn->num_sources; s++)
							{
								source_t *source = &conn->sources[s];

								if(ref)
									ref = lv2_atom_forge_object(&app->forge, &frame[2], 0, 0);
								{
									if(ref)
										ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.source_module.urid);
									if(ref)
										ref = lv2_atom_forge_urid(&app->forge, source->port->mod->urn);

									if(ref)
										ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.source_symbol.urid);
									if(ref)
										ref = lv2_atom_forge_string(&app->forge, source->port->symbol, strlen(source->port->symbol));

									if(ref)
										ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.sink_module.urid);
									if(ref)
										ref = lv2_atom_forge_urid(&app->forge, port->mod->urn);

									if(ref)
										ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.sink_symbol.urid);
									if(ref)
										ref = lv2_atom_forge_string(&app->forge, port->symbol, strlen(port->symbol));
								}
								if(ref)
									lv2_atom_forge_pop(&app->forge, &frame[2]);
							}
						}
					}
				}
				if(ref)
				{
					synthpod_patcher_pop(&app->forge, frame, 2);
					_sp_app_to_ui_advance_atom(app, answer);
				}
				else
				{
					_sp_app_to_ui_overflow(app);
				}
			}
			else
			{
				_sp_app_to_ui_overflow(app);
			}
		}
		else if(prop == app->regs.pset.preset.urid)
		{
			LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
			if(answer)
			{
				const LV2_URID bundle_urid = app->driver->map->map(app->driver->map->handle, app->bundle_path); //FIXME store bundle path as URID

				LV2_Atom_Forge_Ref ref = synthpod_patcher_set(
					&app->regs, &app->forge, subj, sn, prop,
					sizeof(uint32_t), app->forge.URID, &bundle_urid);
				if(ref)
				{
					_sp_app_to_ui_advance_atom(app, answer);
				}
				else
				{
					_sp_app_to_ui_overflow(app);
				}
			}
			else
			{
				_sp_app_to_ui_overflow(app);
			}
		}
		else if(prop == app->regs.synthpod.automation_list.urid)
		{
			//printf("patch:Get for spod:automationList\n");

			LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
			if(answer)
			{
				LV2_Atom_Forge_Frame frame [3];
				LV2_Atom_Forge_Ref ref = synthpod_patcher_set_object(
					&app->regs, &app->forge, &frame[0], subj, sn, prop);
				if(ref)
					ref = lv2_atom_forge_tuple(&app->forge, &frame[1]);

				for(unsigned m = 0; m < app->num_mods; m++)
				{
					mod_t *mod = app->mods[m];

					for(unsigned i = 0; i < MAX_AUTOMATIONS; i++)
					{
						auto_t *automation = &mod->automations[i];
						port_t *port = &mod->ports[automation->index];

						if(automation->type == AUTO_TYPE_MIDI)
						{
							midi_auto_t *mauto = &automation->midi;

							if(ref)
								ref = lv2_atom_forge_object(&app->forge, &frame[2], 0, app->regs.midi.Controller.urid);
							{
								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.sink_module.urid);
								if(ref)
									ref = lv2_atom_forge_urid(&app->forge, mod->urn);

								if(automation->property)
								{
									if(ref)
										ref = lv2_atom_forge_key(&app->forge, app->regs.patch.property.urid);
									if(ref)
										ref = lv2_atom_forge_urid(&app->forge, automation->property);
									if(ref)
										ref = lv2_atom_forge_key(&app->forge, app->regs.rdfs.range.urid);
									if(ref)
										ref = lv2_atom_forge_urid(&app->forge, automation->range);
								}
								else
								{
									if(ref)
										ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.sink_symbol.urid);
									if(ref)
										ref = lv2_atom_forge_string(&app->forge, port->symbol, strlen(port->symbol));
								}

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.midi.channel.urid);
								if(ref)
									ref = lv2_atom_forge_int(&app->forge, mauto->channel);

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.midi.controller_number.urid);
								if(ref)
									ref = lv2_atom_forge_int(&app->forge, mauto->controller);

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.core.minimum.urid);
								if(ref)
									ref = lv2_atom_forge_int(&app->forge, mauto->min);

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.core.maximum.urid);
								if(ref)
									ref = lv2_atom_forge_int(&app->forge, mauto->max);
							}
							if(ref)
								lv2_atom_forge_pop(&app->forge, &frame[2]);
						}
						else if(automation->type == AUTO_TYPE_OSC)
						{
							//FIXME
						}
					}
				}

				if(ref)
				{
					synthpod_patcher_pop(&app->forge, frame, 2);
					_sp_app_to_ui_advance_atom(app, answer);
				}
				else
				{
					_sp_app_to_ui_overflow(app);
				}
			}
			else
			{
				_sp_app_to_ui_overflow(app);
			}
		}
		//TODO handle more properties
	}
	else if(subj)
	{
		for(unsigned m = 0; m < app->num_mods; m++)
		{
			mod_t *mod = app->mods[m];

			if(mod->urn == subj)
			{
				LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
				if(answer)
				{
					LV2_Atom_Forge_Frame frame [2];
					LV2_Atom_Forge_Ref ref = synthpod_patcher_put_object(
						&app->regs, &app->forge, &frame[0], subj, sn);
					if(ref)
						ref = lv2_atom_forge_object(&app->forge, &frame[1], 0, 0);
					{
						if(ref)
							ref = lv2_atom_forge_key(&app->forge, app->regs.core.plugin.urid);
						if(ref)
							ref = lv2_atom_forge_urid(&app->forge, mod->plug_urid);

						if(ref)
							ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.module_position_x.urid);
						if(ref)
							ref = lv2_atom_forge_float(&app->forge, mod->pos.x);

						if(ref)
							ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.module_position_y.urid);
						if(ref)
							ref = lv2_atom_forge_float(&app->forge, mod->pos.y);
					}
					if(ref)
					{
						synthpod_patcher_pop(&app->forge, frame, 2);
						_sp_app_to_ui_advance_atom(app, answer);
					}
					else
					{
						_sp_app_to_ui_overflow(app);
					}
				}
				else
				{
					_sp_app_to_ui_overflow(app);
				}

				break; // match
			}
		}
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_patch_set(sp_app_t *app, const LV2_Atom *atom)
{
	const LV2_Atom_Object *obj = ASSUME_ALIGNED(atom);

	const LV2_Atom_URID *subject = NULL;
	const LV2_Atom_Int *seqn = NULL;
	const LV2_Atom_URID *property = NULL;
	const LV2_Atom *value = NULL;

	lv2_atom_object_get(obj,
		app->regs.patch.subject.urid, &subject,
		app->regs.patch.sequence_number.urid, &seqn,
		app->regs.patch.property.urid, &property,
		app->regs.patch.value.urid, &value,
		0);

	const LV2_URID subj = subject && (subject->atom.type == app->forge.URID)
		? subject->body : 0;
	const int32_t sn = seqn && (seqn->atom.type == app->forge.Int)
		? seqn->body : 0;
	const LV2_URID prop = property && (property->atom.type == app->forge.URID)
		? property->body : 0;

	if(subj && prop && value) // is for a module
	{
		//printf("got patch:Set: %s\n", app->driver->unmap->unmap(app->driver->unmap->handle, prop));

		mod_t *mod = _mod_find_by_urn(app, subj);
		if(mod)
		{
			if(  (prop == app->regs.synthpod.module_position_x.urid)
				&& (value->type == app->forge.Float) )
			{
				mod->pos.x = ((const LV2_Atom_Float *)value)->body;
				_sp_app_order(app);
			}
			else if( (prop == app->regs.synthpod.module_position_y.urid)
				&& (value->type == app->forge.Float) )
			{
				mod->pos.y = ((const LV2_Atom_Float *)value)->body;
				_sp_app_order(app);
			}
			else if( (prop == app->regs.pset.preset.urid)
				&& (value->type == app->forge.URID) )
			{
				if(app->block_state == BLOCKING_STATE_RUN)
				{
					const bool needs_ramping = _mod_needs_ramping(mod, RAMP_STATE_DOWN_DRAIN, true);
					app->silence_state = !needs_ramping
						? SILENCING_STATE_RUN
						: SILENCING_STATE_BLOCK;

					// send request to worker thread
					size_t size = sizeof(job_t);
					job_t *job = _sp_app_to_worker_request(app, size);
					if(job)
					{
						app->block_state = BLOCKING_STATE_DRAIN; // wait for drain

						job->request = JOB_TYPE_REQUEST_DRAIN;
						job->status = 0;
						_sp_app_to_worker_advance(app, size);
					}
				}
				else if(app->block_state == BLOCKING_STATE_BLOCK)
				{
					if(app->silence_state == SILENCING_STATE_BLOCK)
						return false; // not fully silenced yet, wait

					// send request to worker thread
					const LV2_URID pset_urid = ((const LV2_Atom_URID *)value)->body;
					const char *pset_uri = app->driver->unmap->unmap(app->driver->unmap->handle, pset_urid);
					size_t size = sizeof(job_t) + strlen(pset_uri) + 1;
					job_t *job = _sp_app_to_worker_request(app, size);
					if(job)
					{
						app->block_state = BLOCKING_STATE_WAIT; // wait for job
						mod->bypassed = mod->needs_bypassing;

						job->request = JOB_TYPE_REQUEST_PRESET_LOAD;
						job->mod = mod;
						memcpy(job->uri, pset_uri, strlen(pset_uri) + 1);
						_sp_app_to_worker_advance(app, size);

						return true; // advance
					}
				}
			}
		}

		//TODO handle more properties
	}

	return advance_ui[app->block_state];
}

__realtime static bool
_sp_app_from_ui_patch_copy(sp_app_t *app, const LV2_Atom *atom)
{
	const LV2_Atom_Object *obj = ASSUME_ALIGNED(atom);

	const LV2_Atom_URID *subject = NULL;
	const LV2_Atom_Int *seqn = NULL;
	const LV2_Atom_URID *destination = NULL;

	lv2_atom_object_get(obj,
		app->regs.patch.subject.urid, &subject,
		app->regs.patch.sequence_number.urid, &seqn,
		app->regs.patch.destination.urid, &destination,
		0);

	const LV2_URID subj = subject && (subject->atom.type == app->forge.URID)
		? subject->body : 0;
	const int32_t sn = seqn && (seqn->atom.type == app->forge.Int)
		? seqn->body : 0;
	const LV2_URID dest = destination && (destination->atom.type == app->forge.URID)
		? destination->body : 0;

	if(!subj && dest) // save bundle to dest
	{
		const char *path_str = app->driver->unmap->unmap(app->driver->unmap->handle, dest); //FIXME use urn directly

		if(app->block_state == BLOCKING_STATE_RUN)
		{
			// send request to worker thread
			size_t size = sizeof(job_t);
			job_t *job = _sp_app_to_worker_request(app, size);
			if(job)
			{
				app->block_state = BLOCKING_STATE_DRAIN; // wait for drain

				job->request = JOB_TYPE_REQUEST_DRAIN;
				job->status = 0;
				_sp_app_to_worker_advance(app, size);
			}
		}
		else if(app->block_state == BLOCKING_STATE_BLOCK)
		{
			// send request to worker thread
			size_t size = sizeof(job_t) + strlen(path_str) + 1;
			job_t *job = _sp_app_to_worker_request(app, size);
			if(job)
			{
				app->block_state = BLOCKING_STATE_WAIT; // wait for job

				job->request = JOB_TYPE_REQUEST_BUNDLE_SAVE;
				job->status = -1; //FIXME what is this for again?
				memcpy(job->uri, path_str, strlen(path_str) + 1);
				_sp_app_to_worker_advance(app, size);

				return true; // advance
			}
		}
	}
	else if(subj && !dest) // copy bundle from subj
	{
		const char *path_str = app->driver->unmap->unmap(app->driver->unmap->handle, subj); //FIXME use urn directly

		if(app->block_state == BLOCKING_STATE_RUN)
		{
			//FIXME ramp down system outputs

			// send request to worker thread
			size_t size = sizeof(job_t);
			job_t *job = _sp_app_to_worker_request(app, size);
			if(job)
			{
				app->block_state = BLOCKING_STATE_DRAIN; // wait for drain

				job->request = JOB_TYPE_REQUEST_DRAIN;
				job->status = 0;
				_sp_app_to_worker_advance(app, size);
			}
		}
		else if(app->block_state == BLOCKING_STATE_BLOCK)
		{
			//FIXME ramp up system outputs

			// send request to worker thread
			size_t size = sizeof(job_t) + strlen(path_str) + 1;
			job_t *job = _sp_app_to_worker_request(app, size);
			if(job)
			{
				app->block_state = BLOCKING_STATE_WAIT; // wait for job
				app->load_bundle = true; // for sp_app_bypassed

				job->request = JOB_TYPE_REQUEST_BUNDLE_LOAD;
				job->status = -1; //FIXME what is this for again?
				memcpy(job->uri, path_str, strlen(path_str) + 1);
				_sp_app_to_worker_advance(app, size);

				return true; // advance
			}
		}
	}

	return advance_ui[app->block_state];
}

__realtime void
_connection_list_add(sp_app_t *app, const LV2_Atom_Object *obj)
{
	//printf("got patch:add for connectionList:\n");

	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom *src_symbol = NULL;
	const LV2_Atom_URID *snk_module = NULL;
	const LV2_Atom *snk_symbol = NULL;

	lv2_atom_object_get(obj,
		app->regs.synthpod.source_module.urid, &src_module,
		app->regs.synthpod.source_symbol.urid, &src_symbol,
		app->regs.synthpod.sink_module.urid, &snk_module,
		app->regs.synthpod.sink_symbol.urid, &snk_symbol,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const char *src_sym = src_symbol
		? LV2_ATOM_BODY_CONST(src_symbol) : NULL;
	const LV2_URID snk_urn = snk_module
		? snk_module->body : 0;
	const char *snk_sym = snk_symbol
		? LV2_ATOM_BODY_CONST(snk_symbol) : NULL;

	if(src_urn && src_sym && snk_urn && snk_sym)
	{
		port_t *src_port = _port_find_by_symbol(app, src_urn, src_sym);
		port_t *snk_port = _port_find_by_symbol(app, snk_urn, snk_sym);

		if(src_port && snk_port)
		{
			const int32_t state = _sp_app_port_connect(app, src_port, snk_port);
			(void)state;

			// signal to UI
			LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
			if(answer)
			{
				LV2_Atom_Forge_Ref ref = synthpod_patcher_add_atom(&app->regs, &app->forge,
					0, 0, app->regs.synthpod.connection_list.urid, &obj->atom); //TODO subject
				if(ref)
				{
					_sp_app_to_ui_advance_atom(app, answer);
				}
				else
				{
					_sp_app_to_ui_overflow(app);
				}
			}
			else
			{
				_sp_app_to_ui_overflow(app);
			}
		}
	}
}

__realtime static void
_connection_list_rem(sp_app_t *app, const LV2_Atom_Object *obj)
{
	//printf("got patch:remove for connectionList:\n");

	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom *src_symbol = NULL;
	const LV2_Atom_URID *snk_module = NULL;
	const LV2_Atom *snk_symbol = NULL;

	lv2_atom_object_get(obj,
		app->regs.synthpod.source_module.urid, &src_module,
		app->regs.synthpod.source_symbol.urid, &src_symbol,
		app->regs.synthpod.sink_module.urid, &snk_module,
		app->regs.synthpod.sink_symbol.urid, &snk_symbol,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const char *src_sym = src_symbol
		? LV2_ATOM_BODY_CONST(src_symbol) : NULL;
	const LV2_URID snk_urn = snk_module
		? snk_module->body : 0;
	const char *snk_sym = snk_symbol
		? LV2_ATOM_BODY_CONST(snk_symbol) : NULL;

	if(src_urn && src_sym && snk_urn && snk_sym)
	{
		port_t *src_port = _port_find_by_symbol(app, src_urn, src_sym);
		port_t *snk_port = _port_find_by_symbol(app, snk_urn, snk_sym);

		if(src_port && snk_port)
		{
			const int32_t state = _sp_app_port_disconnect_request(app, src_port, snk_port, RAMP_STATE_DOWN);
			(void)state;

			// signal to UI
			LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
			if(answer)
			{
				LV2_Atom_Forge_Ref ref = synthpod_patcher_remove_atom(&app->regs, &app->forge,
					0, 0, app->regs.synthpod.connection_list.urid, &obj->atom); //TODO subject
				if(ref)
				{
					_sp_app_to_ui_advance_atom(app, answer);
				}
				else
				{
					_sp_app_to_ui_overflow(app);
				}
			}
			else
			{
				_sp_app_to_ui_overflow(app);
			}
		}
	}
}

//FIXME _subscription_list_clear, e.g. with patch:wildcard

__realtime static void
_subscription_list_add(sp_app_t *app, const LV2_Atom_Object *obj)
{
	//printf("got patch:add for subscriptionList:\n");

	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom *src_symbol = NULL;

	lv2_atom_object_get(obj,
		app->regs.synthpod.sink_module.urid, &src_module,
		app->regs.synthpod.sink_symbol.urid, &src_symbol,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const char *src_sym = src_symbol
		? LV2_ATOM_BODY_CONST(src_symbol) : NULL;

	if(src_urn && src_sym)
	{
		port_t *src_port = _port_find_by_symbol(app, src_urn, src_sym);

		if(src_port)
		{
			src_port->subscriptions += 1;

			if(src_port->type == PORT_TYPE_CONTROL)
			{
				const float *buf_ptr = PORT_BASE_ALIGNED(src_port);
				src_port->control.last = *buf_ptr - 0.1; // will force notification
			}
		}
	}
}

__realtime static void
_subscription_list_rem(sp_app_t *app, const LV2_Atom_Object *obj)
{
	//printf("got patch:remove for subscriptionList:\n");

	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom *src_symbol = NULL;

	lv2_atom_object_get(obj,
		app->regs.synthpod.sink_module.urid, &src_module,
		app->regs.synthpod.sink_symbol.urid, &src_symbol,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const char *src_sym = src_symbol
		? LV2_ATOM_BODY_CONST(src_symbol) : NULL;

	if(src_urn && src_sym)
	{
		port_t *src_port = _port_find_by_symbol(app, src_urn, src_sym);

		if(src_port)
		{
			if(src_port->subscriptions > 0)
				src_port->subscriptions -= 1;
		}
	}
}

__realtime static void
_notification_list_add(sp_app_t *app, const LV2_Atom_Object *obj)
{
	//printf("got patch:add for notificationList:\n");

	const LV2_URID src_proto = obj->body.otype;
	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom *src_symbol = NULL;
	const LV2_Atom *src_value = NULL;

	lv2_atom_object_get(obj,
		app->regs.synthpod.sink_module.urid, &src_module,
		app->regs.synthpod.sink_symbol.urid, &src_symbol,
		app->regs.rdf.value.urid, &src_value,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const char *src_sym = src_symbol
		? LV2_ATOM_BODY_CONST(src_symbol) : NULL;

	if(src_urn && src_sym && src_value)
	{
		port_t *src_port = _port_find_by_symbol(app, src_urn, src_sym);

		if(src_port)
		{
			if(  (src_proto == app->regs.port.float_protocol.urid)
				&& (src_value->type == app->forge.Float) )
			{
				const float val = ((const LV2_Atom_Float *)src_value)->body;
				float *buf_ptr = PORT_BASE_ALIGNED(src_port);

				if(src_port->type == PORT_TYPE_CONTROL)
				{
					*buf_ptr = val;
					src_port->control.last = *buf_ptr; // we don't want any notification
					_sp_app_port_control_stash(src_port);
				}
				else if(src_port->type == PORT_TYPE_CV)
				{
					for(unsigned i = 0; i < app->driver->max_block_size; i++)
					{
						buf_ptr[i] = val;
						//FIXME omit notification ?
					}
				}
			}
			else if( (src_proto == app->regs.port.event_transfer.urid)
				&& (src_port->type == PORT_TYPE_ATOM) )
			{
				//printf("got atom:eventTransfer\n");

				// messages from UI are ALWAYS appended to default port buffer, no matter
				// how many sources the port may have
				const uint32_t capacity = PORT_SIZE(src_port);
				LV2_Atom_Sequence *seq = PORT_BASE_ALIGNED(src_port);

				const LV2_Atom_Event *dummy = (const void *)src_value - offsetof(LV2_Atom_Event, body);
				LV2_Atom_Event *ev = lv2_atom_sequence_append_event(seq, capacity, dummy);
				if(ev)
					ev->time.frames = 0;
				else
					printf("_notification_list_add: failed to append\n");
			}
			else if( (src_proto == app->regs.port.atom_transfer.urid)
				&& (src_port->type == PORT_TYPE_ATOM) )
			{
				//printf("got atom:atomTransfer\n");
				LV2_Atom *atom = PORT_BASE_ALIGNED(src_port);
				//FIXME memcpy(atom, src_value, lv2_atom_total-size(src_value));
			}
		}
	}
}

__realtime static void
_automation_list_rem_internal(port_t *port, LV2_URID prop)
{
	mod_t *mod = port->mod;
	
	for(unsigned i = 0; i < MAX_AUTOMATIONS; i++)
	{
		auto_t *automation = &mod->automations[i];

		if(automation->type == AUTO_TYPE_NONE)
			continue; // ignore

		if(!prop && (automation->index == port->index))
			automation->type = AUTO_TYPE_NONE; // invalidate
		else if(prop && (automation->property == prop) )
			automation->type = AUTO_TYPE_NONE; // invalidate
	}
}

__realtime static port_t *
_automation_port_find(mod_t *mod, const char *src_sym, LV2_URID src_prop)
{
	for(unsigned p = 0; p < mod->num_ports; p++)
	{
		port_t *port = &mod->ports[p];

		if(src_sym)
		{
			if( (port->type == PORT_TYPE_CONTROL) && !strcmp(port->symbol, src_sym) )
				return port;
		}
		else if(src_prop)
		{
			if( (port->type == PORT_TYPE_ATOM) && port->atom.patchable)
				return port;
		}
	}

	return NULL;
}

__realtime static void
_automation_list_rem(sp_app_t *app, const LV2_Atom_Object *obj)
{
	//printf("got patch:remove for automationList:\n");

	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom *src_symbol = NULL;
	const LV2_Atom_URID *src_property = NULL;

	lv2_atom_object_get(obj,
		app->regs.synthpod.sink_module.urid, &src_module,
		app->regs.synthpod.sink_symbol.urid, &src_symbol,
		app->regs.patch.property.urid, &src_property,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const char *src_sym = src_symbol
		? LV2_ATOM_BODY_CONST(src_symbol) : NULL;
	const LV2_URID src_prop = src_property
		? src_property->body : 0;

	mod_t *mod = _mod_find_by_urn(app, src_urn);
	if(mod)
	{
		port_t *port = _automation_port_find(mod, src_sym, src_prop);
		if(port)
		{
			_automation_list_rem_internal(port, src_prop);
		}
	}
}

__realtime static void
_midi_automation_list_add(sp_app_t *app, const LV2_Atom_Object *obj)
{
	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom *src_symbol = NULL;
	const LV2_Atom_URID *src_property = NULL;
	const LV2_Atom_URID *src_range = NULL;
	const LV2_Atom_Int *src_channel = NULL;
	const LV2_Atom_Int *src_controller = NULL;
	const LV2_Atom_Int *src_min = NULL;
	const LV2_Atom_Int *src_max = NULL;

	lv2_atom_object_get(obj,
		app->regs.synthpod.sink_module.urid, &src_module,
		app->regs.synthpod.sink_symbol.urid, &src_symbol,
		app->regs.patch.property.urid, &src_property,
		app->regs.rdfs.range.urid, &src_range,
		app->regs.midi.channel.urid, &src_channel,
		app->regs.midi.controller_number.urid, &src_controller,
		app->regs.core.minimum.urid, &src_min,
		app->regs.core.maximum.urid, &src_max,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const char *src_sym = src_symbol
		? LV2_ATOM_BODY_CONST(src_symbol) : NULL;
	const LV2_URID src_prop = src_property
		? src_property->body : 0;
	const LV2_URID src_ran = src_range
		? src_range->body : 0;

	mod_t *mod = _mod_find_by_urn(app, src_urn);
	if(mod)
	{
		port_t *port = _automation_port_find(mod, src_sym, src_prop);
		if(port)
		{
			_automation_list_rem_internal(port, src_prop); // remove any previously registered automation

			for(unsigned i = 0; i < MAX_AUTOMATIONS; i++)
			{
				auto_t *automation = &mod->automations[i];

				if(automation->type != AUTO_TYPE_NONE)
					continue; // search empty slot

				// fill slot
				automation->type = AUTO_TYPE_MIDI;
				automation->index = port->index;
				automation->property = src_prop;
				automation->range = src_ran;

				automation->midi.channel = src_channel ? src_channel->body : -1;
				automation->midi.controller = src_controller ? src_controller->body : -1;
				automation->midi.min = src_min ? src_min->body : 0x0;
				automation->midi.max = src_max ? src_max->body : 0x7f;

				const int range = automation->midi.max - automation->midi.min;
				automation->midi.range_1 = range
					? 1.f / range
					: 0.f;

				break;
			}
		}
	}
}

__realtime void
_automation_list_add(sp_app_t *app, const LV2_Atom_Object *obj)
{
	//printf("got patch:add for automationList:\n");

	const LV2_URID src_proto = obj->body.otype;

	if(src_proto == app->regs.midi.Controller.urid)
		_midi_automation_list_add(app, obj);
	//FIXME OSC
}

__realtime static void
_mod_list_add(sp_app_t *app, const LV2_Atom_URID *urid)
{
	const char *uri = app->driver->unmap->unmap(app->driver->unmap->handle, urid->body);
	//printf("got patch:add for moduleList: %s\n", uri);

	// send request to worker thread
	const size_t uri_sz = strlen(uri) + 1;
	const size_t size = sizeof(job_t) + uri_sz;
	job_t *job = _sp_app_to_worker_request(app, size);
	if(job)
	{
		job->request = JOB_TYPE_REQUEST_MODULE_ADD;
		memcpy(job->uri, uri, uri_sz);
		_sp_app_to_worker_advance(app, size);
	}
}

__realtime static void
_mod_list_rem(sp_app_t *app, const LV2_Atom_URID *urn)
{
	const char *uri = app->driver->unmap->unmap(app->driver->unmap->handle, urn->body);
	//printf("got patch:remove for moduleList: %s\n", uri);

	// search mod according to its URN
	mod_t *mod = _mod_find_by_urn(app, urn->body);
	if(!mod) // mod not found
		return;

	int needs_ramping = 0;
	for(unsigned p1=0; p1<mod->num_ports; p1++)
	{
		port_t *port = &mod->ports[p1];

		connectable_t *conn = _sp_app_port_connectable(port);
		if(conn)
		{
			// disconnect sources
			for(int s=0; s<conn->num_sources; s++)
			{
				_sp_app_port_disconnect_request(app,
					conn->sources[s].port, port, RAMP_STATE_DOWN);
			}
		}

		// disconnect sinks
		for(unsigned m=0; m<app->num_mods; m++)
			for(unsigned p2=0; p2<app->mods[m]->num_ports; p2++)
			{
				needs_ramping += _sp_app_port_disconnect_request(app,
					port, &app->mods[m]->ports[p2], RAMP_STATE_DOWN_DEL);
			}
	}
	if(needs_ramping == 0)
		_sp_app_mod_eject(app, mod);
}

__realtime static bool
_sp_app_from_ui_patch_patch(sp_app_t *app, const LV2_Atom *atom)
{
	const LV2_Atom_Object *obj = ASSUME_ALIGNED(atom);

	const LV2_Atom_URID *subject = NULL;
	const LV2_Atom_Int *seqn = NULL;
	const LV2_Atom_Object *add = NULL;
	const LV2_Atom_Object *rem = NULL;

	lv2_atom_object_get(obj,
		app->regs.patch.subject.urid, &subject,
		app->regs.patch.sequence_number.urid, &seqn,
		app->regs.patch.add.urid, &add,
		app->regs.patch.remove.urid, &rem,
		0);

	const LV2_URID subj = subject && (subject->atom.type == app->forge.URID)
		? subject->body : 0; //FIXME check for
	const int32_t sn = seqn && (seqn->atom.type == app->forge.Int)
		? seqn->body : 0;

	//printf("got patch:Patch: %s\n", app->driver->unmap->unmap(app->driver->unmap->handle, subj));

	if(  add && (add->atom.type == app->forge.Object)
		&& rem && (rem->atom.type == app->forge.Object) )
	{
		LV2_ATOM_OBJECT_FOREACH(rem, prop)
		{
			//printf("got patch:remove: %s\n", app->driver->unmap->unmap(app->driver->unmap->handle, prop->key));

			if(  (prop->key == app->regs.synthpod.connection_list.urid)
				&& (prop->value.type == app->forge.Object) )
			{
				_connection_list_rem(app, (const LV2_Atom_Object *)&prop->value);
			}
			else if( (prop->key == app->regs.synthpod.subscription_list.urid)
				&& (prop->value.type == app->forge.Object) )
			{
				_subscription_list_rem(app, (const LV2_Atom_Object *)&prop->value);
			}
			else if( (prop->key == app->regs.synthpod.notification_list.urid)
				&& (prop->value.type == app->forge.Object) )
			{
				//FIXME never reached
			}
			else if( (prop->key == app->regs.synthpod.module_list.urid)
				&& (prop->value.type == app->forge.URID) )
			{
				_mod_list_rem(app, (const LV2_Atom_URID *)&prop->value);
			}
			else if( (prop->key == app->regs.synthpod.automation_list.urid)
				&& (prop->value.type == app->forge.Object) )
			{
				_automation_list_rem(app, (const LV2_Atom_Object *)&prop->value);
			}
		}

		LV2_ATOM_OBJECT_FOREACH(add, prop)
		{
			//printf("got patch:add: %s\n", app->driver->unmap->unmap(app->driver->unmap->handle, prop->key));

			if(  (prop->key == app->regs.synthpod.connection_list.urid)
				&& (prop->value.type == app->forge.Object) )
			{
				_connection_list_add(app, (const LV2_Atom_Object *)&prop->value);
			}
			else if(  (prop->key == app->regs.synthpod.subscription_list.urid)
				&& (prop->value.type == app->forge.Object) )
			{
				_subscription_list_add(app, (const LV2_Atom_Object *)&prop->value);
			}
			else if(  (prop->key == app->regs.synthpod.notification_list.urid)
				&& (prop->value.type == app->forge.Object) )
			{
				_notification_list_add(app, (const LV2_Atom_Object *)&prop->value);
			}
			else if( (prop->key == app->regs.synthpod.module_list.urid)
				&& (prop->value.type == app->forge.URID) )
			{
				_mod_list_add(app, (const LV2_Atom_URID *)&prop->value);
			}
			else if(  (prop->key == app->regs.synthpod.automation_list.urid)
				&& (prop->value.type == app->forge.Object) )
			{
				_automation_list_add(app, (const LV2_Atom_Object *)&prop->value);
			}
		}
	}

	return advance_ui[app->block_state];
}

bool
sp_app_from_ui(sp_app_t *app, const LV2_Atom *atom)
{
	if(!advance_ui[app->block_state])
		return false; // we are draining or waiting

	const LV2_Atom_Object *obj = ASSUME_ALIGNED(atom);
	//printf("%s\n", app->driver->unmap->unmap(app->driver->unmap->handle, obj->body.otype));

	if(lv2_atom_forge_is_object_type(&app->forge, obj->atom.type))
	{
		if(obj->body.otype == app->regs.patch.get.urid)
			return _sp_app_from_ui_patch_get(app, &obj->atom);
		else if(obj->body.otype == app->regs.patch.set.urid)
			return _sp_app_from_ui_patch_set(app, &obj->atom);
		else if(obj->body.otype == app->regs.patch.copy.urid)
			return _sp_app_from_ui_patch_copy(app, &obj->atom);
		else if(obj->body.otype == app->regs.patch.patch.urid)
			return _sp_app_from_ui_patch_patch(app, &obj->atom);
	}

	return advance_ui[app->block_state];
}
