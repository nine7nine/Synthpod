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

#define PORT_SIZE(PORT) ((PORT)->size)

__non_realtime static int
_from_ui_cmp(const void *itm1, const void *itm2)
{
	const from_ui_t *from_ui1 = itm1;
	const from_ui_t *from_ui2 = itm2;

	return _signum(from_ui1->protocol, from_ui2->protocol);
}

static inline const from_ui_t *
_from_ui_bsearch(uint32_t p, from_ui_t *a, unsigned n)
{
	unsigned start = 0;
	unsigned end = n;

	while(start < end)
	{
		const unsigned mid = start + (end - start)/2;
		const from_ui_t *dst = &a[mid];

		if(p < dst->protocol)
			end = mid;
		else if(p > dst->protocol)
			start = mid + 1;
		else
			return dst;
	}

	return NULL;
}

__realtime static bool
_sp_app_from_ui_float_protocol(sp_app_t *app, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);
	const transfer_float_t *trans = (const transfer_float_t *)atom;

	port_t *port = _sp_app_port_get(app, trans->transfer.uid.body, trans->transfer.port.body);
	if(!port) // port not found
		return advance_ui[app->block_state];

	// set port value
	void *buf = PORT_BASE_ALIGNED(port);
	*(float *)buf = trans->value.body;
	port->last = trans->value.body;

	_sp_app_port_control_stash(port);

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
	LV2_Atom_Sequence *seq = PORT_BUF_ALIGNED(port);

	// find last event
	LV2_Atom_Event *last = NULL;
	LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
		last = ev;

	LV2_Atom_Event *ev = _lv2_atom_sequence_append_atom(seq, capacity,
		last ? last->time.frames : 0, trans->atom);
	(void)ev; //TODO check

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

		// disconnect sources
		for(int s=0; s<port->num_sources; s++)
		{
			_sp_app_port_disconnect_request(app,
				port->sources[s].port, port, RAMP_STATE_DOWN);
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
			mod->bypassed = true;

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

	float *buf_ptr = PORT_BASE_ALIGNED(port);
	port->last = *buf_ptr - 0.1; // will force notification

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

void
sp_app_from_ui_fill(sp_app_t *app)
{
	unsigned ptr = 0;
	from_ui_t *from_uis = app->from_uis;

	from_uis[ptr].protocol = app->regs.port.float_protocol.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_float_protocol;

	from_uis[ptr].protocol = app->regs.port.atom_transfer.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_atom_transfer;

	from_uis[ptr].protocol = app->regs.port.event_transfer.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_event_transfer;

	from_uis[ptr].protocol = app->regs.synthpod.module_list.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_module_list;

	from_uis[ptr].protocol = app->regs.synthpod.module_supported.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_module_supported;

	from_uis[ptr].protocol = app->regs.synthpod.module_add.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_module_add;

	from_uis[ptr].protocol = app->regs.synthpod.module_del.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_module_del;

	from_uis[ptr].protocol = app->regs.synthpod.module_move.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_module_move;

	from_uis[ptr].protocol = app->regs.synthpod.module_preset_load.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_module_preset_load;

	from_uis[ptr].protocol = app->regs.synthpod.module_preset_save.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_module_preset_save;

	from_uis[ptr].protocol = app->regs.synthpod.module_selected.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_module_selected;

	from_uis[ptr].protocol = app->regs.synthpod.module_visible.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_module_visible;

	from_uis[ptr].protocol = app->regs.synthpod.module_disabled.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_module_disabled;

	from_uis[ptr].protocol = app->regs.synthpod.module_embedded.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_module_embedded;

	from_uis[ptr].protocol = app->regs.synthpod.port_connected.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_port_connected;

	from_uis[ptr].protocol = app->regs.synthpod.port_subscribed.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_port_subscribed;

	from_uis[ptr].protocol = app->regs.synthpod.port_refresh.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_port_refresh;

	from_uis[ptr].protocol = app->regs.synthpod.port_selected.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_port_selected;

	from_uis[ptr].protocol = app->regs.synthpod.port_monitored.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_port_monitored;

	from_uis[ptr].protocol = app->regs.synthpod.bundle_load.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_bundle_load;

	from_uis[ptr].protocol = app->regs.synthpod.bundle_save.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_bundle_save;

	from_uis[ptr].protocol = app->regs.synthpod.grid_cols.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_grid_cols;

	from_uis[ptr].protocol = app->regs.synthpod.grid_rows.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_grid_rows;

	from_uis[ptr].protocol = app->regs.synthpod.pane_left.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_pane_left;

	from_uis[ptr].protocol = app->regs.synthpod.quit.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_quit;

	from_uis[ptr].protocol = app->regs.synthpod.path_get.urid;
	from_uis[ptr++].cb = _sp_app_from_ui_path_get;

	assert(ptr == FROM_UI_NUM);
	// sort according to URID
	qsort(from_uis, FROM_UI_NUM, sizeof(from_ui_t), _from_ui_cmp);
}

bool
sp_app_from_ui(sp_app_t *app, const LV2_Atom *atom)
{
	if(!advance_ui[app->block_state])
		return false; // we are draining or waiting

	atom = ASSUME_ALIGNED(atom);
	const transmit_t *transmit = (const transmit_t *)atom;

	// check for atom object type
	if(!lv2_atom_forge_is_object_type(&app->forge, transmit->obj.atom.type))
		return advance_ui[app->block_state];

	// what we want to search for
	const uint32_t protocol = transmit->obj.body.otype;

	// search for corresponding callback
	const from_ui_t *from_ui = _from_ui_bsearch(protocol, app->from_uis, FROM_UI_NUM);

	// run callback if found
	if(from_ui)
		return from_ui->cb(app, atom);

	return advance_ui[app->block_state];
}
