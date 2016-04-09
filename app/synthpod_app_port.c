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

port_t *
_sp_app_port_get(sp_app_t *app, u_id_t uid, uint32_t index)
{
	mod_t *mod = _sp_app_mod_get(app, uid);
	if(mod && (index < mod->num_ports) )
		return &mod->ports[index];
	
	return NULL;
}

bool
_sp_app_port_connected(port_t *src_port, port_t *snk_port)
{
	for(int s = 0; s < snk_port->num_sources; s++)
		if(snk_port->sources[s].port == src_port)
			return true;

	return false;
}

static inline void
_sp_app_port_rewire(port_t *snk_port)
{
	if(SINK_IS_SIMPLEX(snk_port))
	{
		// directly wire source port output buffer to sink input buffer
		snk_port->base = PORT_BUF_ALIGNED(snk_port->sources[0].port);
	}
	else
	{
		// multiplex multiple source port output buffers to sink input buffer
		snk_port->base = PORT_BUF_ALIGNED(snk_port);

		// clear audio/cv port buffers without connections
		if(SINK_IS_NILPLEX(snk_port))
		{
			if(  (snk_port->type == PORT_TYPE_AUDIO)
				|| (snk_port->type == PORT_TYPE_CV) )
			{
				memset(PORT_BASE_ALIGNED(snk_port), 0x0, snk_port->size);
			}
		}
	}

	lilv_instance_connect_port(
		snk_port->mod->inst,
		snk_port->index,
		snk_port->base);
}

int
_sp_app_port_connect(sp_app_t *app, port_t *src_port, port_t *snk_port)
{
	if(_sp_app_port_connected(src_port, snk_port))
		return 0;

	if(snk_port->num_sources >= MAX_SOURCES)
		return 0;

	source_t *conn = &snk_port->sources[snk_port->num_sources];
	conn->port = src_port;;
	snk_port->num_sources += 1;
	snk_port->num_feedbacks += src_port->mod == snk_port->mod ? 1 : 0;
	snk_port->is_ramping = src_port->type == PORT_TYPE_AUDIO;

	// only audio port connections need to be ramped to be clickless
	if(snk_port->is_ramping)
	{
		conn->ramp.samples = app->ramp_samples;
		conn->ramp.state = RAMP_STATE_UP;
		conn->ramp.value = 0.f;
	}

	_sp_app_port_rewire(snk_port);
	return 1;
}

void
_sp_app_port_disconnect(sp_app_t *app, port_t *src_port, port_t *snk_port)
{
	// update sources list 
	bool connected = false;
	for(int i=0, j=0; i<snk_port->num_sources; i++)
	{
		if(snk_port->sources[i].port == src_port)
		{
			connected = true;
			continue;
		}

		snk_port->sources[j++].port = snk_port->sources[i].port;
	}

	if(!connected)
		return;

	snk_port->num_sources -= 1;
	snk_port->num_feedbacks -= src_port->mod == snk_port->mod ? 1 : 0;
	snk_port->is_ramping = false;

	_sp_app_port_rewire(snk_port);
}

static inline void
_sp_app_port_reconnect(sp_app_t *app, port_t *src_port, port_t *snk_port, bool is_ramping)
{
	//printf("_sp_app_port_reconnect\n");	

	if(!_sp_app_port_connected(src_port, snk_port))
		return;

	snk_port->is_ramping = is_ramping;

	_sp_app_port_rewire(snk_port);
}

int
_sp_app_port_disconnect_request(sp_app_t *app, port_t *src_port, port_t *snk_port,
	ramp_state_t ramp_state)
{
	if(  (src_port->direction == PORT_DIRECTION_OUTPUT)
		&& (snk_port->direction == PORT_DIRECTION_INPUT) )
	{
		source_t *conn = NULL;
	
		// find connection
		for(int i=0; i<snk_port->num_sources; i++)
		{
			if(snk_port->sources[i].port == src_port)
			{
				conn = &snk_port->sources[i];
				break;
			}
		}

		if(conn)
		{
			if(src_port->type == PORT_TYPE_AUDIO)
			{
				_sp_app_port_reconnect(app, src_port, snk_port, true); // handles port_connect

				// only audio output ports need to be ramped to be clickless
				conn->ramp.samples = app->ramp_samples;
				conn->ramp.state = ramp_state;
				conn->ramp.value = 1.f;

				return 1; // needs ramping
			}
			else // !AUDIO
			{
				// disconnect immediately
				_sp_app_port_disconnect(app, src_port, snk_port);
			}
		}
	}

	return 0; // not connected
}

int
_sp_app_port_desilence(sp_app_t *app, port_t *src_port, port_t *snk_port)
{
	if(  (src_port->direction == PORT_DIRECTION_OUTPUT)
		&& (snk_port->direction == PORT_DIRECTION_INPUT) )
	{
		source_t *conn = NULL;
	
		// find connection
		for(int i=0; i<snk_port->num_sources; i++)
		{
			if(snk_port->sources[i].port == src_port)
			{
				conn = &snk_port->sources[i];
				break;
			}
		}

		if(conn)
		{
			if(src_port->type == PORT_TYPE_AUDIO)
			{
				//_sp_app_port_reconnect(app, src_port, snk_port, true); // handles port_connect
				// XXX we are already in multiplex mode

				// only audio output ports need to be ramped to be clickless
				conn->ramp.samples = app->ramp_samples;
				conn->ramp.state = RAMP_STATE_UP;
				conn->ramp.value = 0.f;

				return 1; // needs ramping
			}
		}
	}

	return 0; // not connected
}

int
_sp_app_port_silence_request(sp_app_t *app, port_t *src_port, port_t *snk_port,
	ramp_state_t ramp_state)
{
	if(  (src_port->direction == PORT_DIRECTION_OUTPUT)
		&& (snk_port->direction == PORT_DIRECTION_INPUT) )
	{
		source_t *conn = NULL;
	
		// find connection
		for(int i=0; i<snk_port->num_sources; i++)
		{
			if(snk_port->sources[i].port == src_port)
			{
				conn = &snk_port->sources[i];
				break;
			}
		}

		if(conn)
		{
			if(src_port->type == PORT_TYPE_AUDIO)
			{
				_sp_app_port_reconnect(app, src_port, snk_port, true); // handles port_connect

				// only audio output ports need to be ramped to be clickless
				conn->ramp.samples = app->ramp_samples;
				conn->ramp.state = ramp_state;
				conn->ramp.value = 1.f;

				return 1; // needs ramping
			}
		}
	}

	return 0; // not connected
}

static inline void
_update_ramp(sp_app_t *app, source_t *source, port_t *port, uint32_t nsamples)
{
	// update ramp properties
	source->ramp.samples -= nsamples; // update remaining samples to ramp over
	if(source->ramp.samples <= 0)
	{
		if(source->ramp.state == RAMP_STATE_DOWN)
		{
			_sp_app_port_disconnect(app, source->port, port);
		}
		else if(source->ramp.state == RAMP_STATE_DOWN_DEL)
		{
			_sp_app_port_disconnect(app, source->port, port);
			source->port->mod->delete_request = true; // mark module for removal
		}
		else if(source->ramp.state == RAMP_STATE_DOWN_DRAIN)
		{
			// fully silenced, continue with preset loading
			//_sp_app_port_reconnect(app, source->port, port, false); // handles port_connect
			// XXX stay in multiplex mode

			app->silence_state = SILENCING_STATE_WAIT;
			source->ramp.value = 0.f;
			return; // stay in RAMP_STATE_DOWN_DRAIN
		}
		else if(source->ramp.state == RAMP_STATE_UP)
		{
			_sp_app_port_reconnect(app, source->port, port, false); // handles port_connect
		}

		source->ramp.state = RAMP_STATE_NONE; // ramp is complete
	}
	else
	{
		source->ramp.value = (float)source->ramp.samples / (float)app->ramp_samples;
		if(source->ramp.state == RAMP_STATE_UP)
			source->ramp.value = 1.f - source->ramp.value;
		//printf("ramp: %u.%u %f\n", source->port->mod->uid, source->port->index, source->ramp.value);
	}
}

// rt
__realtime static inline void
_port_control_simplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	_sp_app_port_spin_lock(port); // concurrent acess from worker and rt threads

	float *dst = PORT_BASE_ALIGNED(port);

	port_t *src_port = port->sources[0].port;

	// normalize
	const float norm = (*dst - src_port->min) * src_port->range_1;
	*dst = port->min + norm * port->range; //TODO handle exponential ranges

	_sp_app_port_spin_unlock(port);
}

// rt
__realtime static inline void
_port_control_multiplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	_sp_app_port_spin_lock(port); // concurrent acess from worker and rt threads

	float *dst = PORT_BASE_ALIGNED(port);
	*dst = 0; // init

	for(int s=0; s<port->num_sources; s++)
	{
		port_t *src_port = port->sources[s].port;

		_sp_app_port_spin_lock(src_port); // concurrent acess from worker and rt threads

		const float *src = PORT_BASE_ALIGNED(src_port);

		// normalize
		const float norm = (*src - src_port->min) * src_port->range_1;
		*dst += port->min + norm * port->range; //TODO handle exponential ranges

		_sp_app_port_spin_unlock(src_port);
	}

	_sp_app_port_spin_unlock(port);
}

// rt
__realtime static inline void
_port_audio_multiplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	float *val = PORT_BASE_ALIGNED(port);
	memset(val, 0, nsamples * sizeof(float)); // init

	for(int s=0; s<port->num_sources; s++)
	{
		source_t *source = &port->sources[s];

		// ramp audio output ports
		if(source->ramp.state != RAMP_STATE_NONE)
		{
			const float *src = PORT_BASE_ALIGNED(source->port);
			const float ramp_value = source->ramp.value;
			for(uint32_t j=0; j<nsamples; j++)
				val[j] += src[j] * ramp_value;

			_update_ramp(app, source, port, nsamples);
		}
		else // RAMP_STATE_NONE
		{
			const float *src = PORT_BASE_ALIGNED(source->port);
			for(uint32_t j=0; j<nsamples; j++)
				val[j] += src[j];
		}
	}
}

// rt
__realtime static inline void
_port_audio_simplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	source_t *source = &port->sources[0];

	if(source->ramp.state != RAMP_STATE_NONE)
	{
		float *src = PORT_BASE_ALIGNED(source->port);
		const float ramp_value = source->ramp.value;
		for(uint32_t j=0; j<nsamples; j++)
			src[j] *= ramp_value;

		_update_ramp(app, source, port, nsamples);
	}
}

// rt
__realtime static inline void
_port_cv_multiplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	float *val = PORT_BASE_ALIGNED(port);
	memset(val, 0, nsamples * sizeof(float)); // init

	for(int s=0; s<port->num_sources; s++)
	{
		source_t *source = &port->sources[s];

		const float *src = PORT_BASE_ALIGNED(source->port);
		for(uint32_t j=0; j<nsamples; j++)
			val[j] += src[j];
	}
}

// rt
__realtime static inline void
_port_seq_multiplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	// create forge to append to sequence (may contain events from UI)
	LV2_Atom_Forge *forge = &app->forge;
	LV2_Atom_Forge_Frame frame;
	LV2_Atom_Forge_Ref ref;
	ref = _lv2_atom_forge_sequence_append(forge, &frame, PORT_BASE_ALIGNED(port), port->size);

	const LV2_Atom_Sequence *seq [32]; //TODO how big?
	const LV2_Atom_Event *itr [32]; //TODO how big?
	for(int s=0; s<port->num_sources; s++)
	{
		seq[s] = PORT_BASE_ALIGNED(port->sources[s].port);
		itr[s] = lv2_atom_sequence_begin(&seq[s]->body);
	}

	while(1)
	{
		int nxt = -1;
		int64_t frames = nsamples;

		// search for next event in timeline accross source ports
		for(int s=0; s<port->num_sources; s++)
		{
			if(lv2_atom_sequence_is_end(&seq[s]->body, seq[s]->atom.size, itr[s]))
				continue; // reached sequence end
			
			if(itr[s]->time.frames < frames)
			{
				frames = itr[s]->time.frames;
				nxt = s;
			}
		}

		if(nxt >= 0) // next event found
		{
			// add event to forge
			size_t len = sizeof(LV2_Atom) + itr[nxt]->body.size;
			if(ref && (forge->offset + sizeof(LV2_Atom_Sequence_Body)
				+ lv2_atom_pad_size(len) < forge->size) )
			{
				ref = lv2_atom_forge_frame_time(forge, frames);
				if(ref)
					ref = lv2_atom_forge_raw(forge, &itr[nxt]->body, len);
				if(ref)
					lv2_atom_forge_pad(forge, len);
			}

			// advance iterator
			itr[nxt] = lv2_atom_sequence_next(itr[nxt]);
		}
		else
			break; // no more events to process
	};

	if(ref)
		lv2_atom_forge_pop(forge, &frame);
}

// rt
__realtime static inline void
_port_ev_multiplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	//const LV2_Atom_Sequence *seq = PORT_BUF_ALIGNED(port);

	//FIXME FIXME FIXME actually implement me FIXME FIXME FIXME
}

// rt
__realtime static inline void
_port_seq_simplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	const LV2_Atom_Sequence *seq = PORT_BUF_ALIGNED(port);
	// move messages from UI on default buffer

	if(seq->atom.size > sizeof(LV2_Atom_Sequence_Body)) // has messages from UI
	{
		//printf("adding UI event\n");

		// create forge to append to sequence (may contain events from UI)
		LV2_Atom_Forge *forge = &app->forge;
		LV2_Atom_Forge_Frame frame;
		LV2_Atom_Forge_Ref ref;
		ref = _lv2_atom_forge_sequence_append(forge, &frame,
			PORT_BUF_ALIGNED(port->sources[0].port),
			PORT_SIZE(port->sources[0].port));

		LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
		{
			const LV2_Atom *atom = &ev->body;

			if(ref && (forge->offset + sizeof(LV2_Atom_Sequence_Body)
				+ sizeof(LV2_Atom) + lv2_atom_pad_size(atom->size) < forge->size) )
			{
				ref = lv2_atom_forge_frame_time(forge, nsamples-1);
				if(ref)
					ref = lv2_atom_forge_raw(forge, atom, sizeof(LV2_Atom) + atom->size);
				if(ref)
					lv2_atom_forge_pad(forge, atom->size);
			}
		}

		if(ref)
			lv2_atom_forge_pop(forge, &frame);
	}
}

// rt
__realtime static inline void
_port_float_protocol_update(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	_sp_app_port_spin_lock(port); // concurrent acess from worker and rt threads

	const float *val = PORT_BASE_ALIGNED(port);
	const bool needs_update = *val != port->last;

	if(needs_update) // update last value
		port->last = *val;

	_sp_app_port_spin_unlock(port);

	if(needs_update)
	{
		size_t size = sizeof(transfer_float_t);
		transfer_float_t *trans = _sp_app_to_ui_request(app, size);
		if(trans)
		{
			_sp_transfer_float_fill(&app->regs, &app->forge, trans, port->mod->uid, port->index, val);
			_sp_app_to_ui_advance(app, size);
		}
	}
}

// rt
__realtime static inline void
_port_peak_protocol_update(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	const float *vec = PORT_BASE_ALIGNED(port);

	// find peak value in current period
	float peak = 0.f;
	for(uint32_t j=0; j<nsamples; j++)
	{
		float val = fabs(vec[j]);
		if(val > peak)
			peak = val;
	}

	if(fabs(peak - port->last) >= 1e-3) //TODO make this configurable
	{
		// update last value
		port->last = peak;

		LV2UI_Peak_Data data = {
			.period_start = app->fps.period_cnt,
			.period_size = nsamples,
			.peak = peak
		};

		size_t size = sizeof(transfer_peak_t);
		transfer_peak_t *trans = _sp_app_to_ui_request(app, size);
		if(trans)
		{
			_sp_transfer_peak_fill(&app->regs, &app->forge, trans,
				port->mod->uid, port->index, &data);
			_sp_app_to_ui_advance(app, size);
		}
	}
}

// rt
__realtime static inline void
_port_atom_transfer_update(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	const LV2_Atom *atom = PORT_BASE_ALIGNED(port);

	if(atom->size == 0) // empty atom
		return;
	else if( (port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE)
			&& (atom->size == sizeof(LV2_Atom_Sequence_Body)) ) // empty atom sequence
		return;

	uint32_t atom_size = sizeof(LV2_Atom) + atom->size;
	size_t size = sizeof(transfer_atom_t) + lv2_atom_pad_size(atom_size);
	transfer_atom_t *trans = _sp_app_to_ui_request(app, size);
	if(trans)
	{
		_sp_transfer_atom_fill(&app->regs, &app->forge, trans,
			port->mod->uid, port->index, atom_size, atom);
		_sp_app_to_ui_advance(app, size);
	}
}

// rt
__realtime static inline void
_port_event_transfer_update(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	const LV2_Atom_Sequence *seq = PORT_BASE_ALIGNED(port);

	if(seq->atom.size == sizeof(LV2_Atom_Sequence_Body)) // empty seq
		return;
	
	const bool subscribed = port->subscriptions != 0;

	if(subscribed)
	{
		LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
		{
			const LV2_Atom *atom = &ev->body;

			const uint32_t atom_size = sizeof(LV2_Atom) + atom->size;
			const size_t size = sizeof(transfer_atom_t) + lv2_atom_pad_size(atom_size);
			transfer_atom_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transfer_event_fill(&app->regs, &app->forge, trans,
					port->mod->uid, port->index, atom_size, atom);
				_sp_app_to_ui_advance(app, size);
			}
		}
	}
	else // patched
	{
		LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
		{
			const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

			if(lv2_atom_forge_is_object_type(&app->forge, obj->atom.type))
			{ 
				const LV2_Atom_URID *destination = NULL;
				lv2_atom_object_get(obj, app->regs.patch.destination.urid, &destination, NULL);
				if(destination && (destination->atom.type == app->forge.URID)
						&& (destination->body == app->regs.core.plugin.urid) )
					continue; // ignore feedback messages

				if(  (obj->body.otype == app->regs.patch.set.urid)
					|| (obj->body.otype == app->regs.patch.put.urid)
					|| (obj->body.otype == app->regs.patch.patch.urid) ) //TODO support more patch messages
				{
					const uint32_t atom_size = sizeof(LV2_Atom) + obj->atom.size;
					const size_t size = sizeof(transfer_atom_t) + lv2_atom_pad_size(atom_size);
					transfer_atom_t *trans = _sp_app_to_ui_request(app, size);
					if(trans)
					{
						_sp_transfer_event_fill(&app->regs, &app->forge, trans,
							port->mod->uid, port->index, atom_size, &obj->atom);
						_sp_app_to_ui_advance(app, size);
					}
				}
			}
		}
	}
}

const port_driver_t control_port_driver = {
	.simplex = _port_control_simplex,
	.multiplex = _port_control_multiplex,
	.transfer = _port_float_protocol_update,
	.sparse_update = true
};

const port_driver_t audio_port_driver = {
	.simplex = _port_audio_simplex,
	.multiplex = _port_audio_multiplex,
	.transfer = _port_peak_protocol_update,
	.sparse_update = true
};

const port_driver_t cv_port_driver = {
	.simplex = NULL,
	.multiplex = _port_cv_multiplex,
	.transfer = _port_peak_protocol_update,
	.sparse_update = true
};

//FIXME actually use this
const port_driver_t atom_port_driver = {
	.simplex = NULL,
	.multiplex = NULL, // unsupported
	.transfer = _port_atom_transfer_update,
	.sparse_update = false
};

const port_driver_t seq_port_driver = {
	.simplex = _port_seq_simplex,
	.multiplex = _port_seq_multiplex,
	.transfer = _port_event_transfer_update,
	.sparse_update = false
};

const port_driver_t ev_port_driver = {
	.simplex = NULL,
	.multiplex = _port_ev_multiplex,
	.transfer = NULL,
	.sparse_update = false
};
