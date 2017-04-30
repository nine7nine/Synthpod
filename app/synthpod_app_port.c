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

#define PORT_SIZE(PORT) ((PORT)->size)

__realtime static inline void
_dsp_master_concurrent(sp_app_t *app)
{
	dsp_master_t *dsp_master = &app->dsp_master;

	dsp_master->concurrent = 0;

	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];
		dsp_client_t *dsp_client = &mod->dsp_client;

		dsp_client->count = dsp_client->num_sources;
		dsp_client->mark = 0;
	}

	while(true)
	{
		bool done = true;
		unsigned sum = 0;

		for(unsigned m=0; m<app->num_mods; m++)
		{
			mod_t *mod = app->mods[m];
			dsp_client_t *dsp_client = &mod->dsp_client;

			const int count = dsp_client->count;
			if(count == 0)
				sum += 1;
			else if(count > 0)
				done = false;
		}

		//printf("sum: %u, concurrent: %u\n", sum, dsp_master->concurrent);
		if(sum > dsp_master->concurrent)
			dsp_master->concurrent = sum;

		if(done)
			break;

		for(unsigned m=0; m<app->num_mods; m++)
		{
			mod_t *mod = app->mods[m];
			dsp_client_t *dsp_client = &mod->dsp_client;

			if(dsp_client->count == 0)
			{
				dsp_client->mark += 1;

				for(unsigned j=0; j<dsp_client->num_sinks; j++)
				{
					dsp_client_t *sink = dsp_client->sinks[j];

					sink->mark += 1;
				}
			}
		}

		for(unsigned m=0; m<app->num_mods; m++)
		{
			mod_t *mod = app->mods[m];
			dsp_client_t *dsp_client = &mod->dsp_client;

			if(dsp_client->mark > 0)
			{
				dsp_client->count -= dsp_client->mark;
				dsp_client->mark = 0;
			}
		}
	}

	//printf("concurrent: %u\n", dsp_master->concurrent);
}

__realtime void
_dsp_master_reorder(sp_app_t *app)
{
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];
		dsp_client_t *dsp_client = &mod->dsp_client;

		dsp_client->num_sinks = 0;
		dsp_client->num_sources = 0;
	}

	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod_sink = app->mods[m];

		for(unsigned n=0; n<m; n++)
		{
			mod_t *mod_source = app->mods[n];
			bool is_connected = false;

			for(unsigned p=0; p<mod_sink->num_ports; p++)
			{
				port_t *port_sink = &mod_sink->ports[p];

				for(int s=0; s<port_sink->num_sources; s++)
				{
					source_t *source =  &port_sink->sources[s];

					if(source->port->mod == mod_source)
					{
						is_connected = true;
						break;
					}
				}

				if(is_connected)
				{
					break;
				}
			}

			if(is_connected)
			{
				mod_source->dsp_client.sinks[mod_source->dsp_client.num_sinks] = &mod_sink->dsp_client;
				mod_source->dsp_client.num_sinks += 1;
				mod_sink->dsp_client.num_sources += 1;

				//printf("%u -> %u\n", mod_source->uid, mod_sink->uid);
			}
		}
	}

	/*
	for(unsigned m=0; m<app->num_mods; m++)
	{
		mod_t *mod = app->mods[m];
		printf("%u: %u, %u\n", mod->uid, mod->dsp_client.num_sources, mod->dsp_client.num_sinks);
	}
	*/

	_dsp_master_concurrent(app);
}

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
	_dsp_master_reorder(app);
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
	_dsp_master_reorder(app);
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
		else if(source->ramp.state == RAMP_STATE_DOWN_DISABLE)
		{
			source->port->mod->disabled = true; // disable module in graph
			source->ramp.value = 0.f;
			return; // stay in RAMP_STATE_DOWN_DISABLE
			//FIXME make this more efficient, e.g. without multiplexing while disabled
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

__realtime void
_sp_app_port_control_stash(port_t *port)
{
	control_port_t *control = &port->control;
	void *buf = PORT_BASE_ALIGNED(port);

	if(_sp_app_port_try_lock(control))
	{
		control->stash = *(float *)buf;

		_sp_app_port_unlock(control);
	}
	else
	{
		control->stashing = true;
	}
}

__realtime static inline void
_port_control_simplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	float *dst = PORT_BASE_ALIGNED(port);

	port_t *src_port = port->sources[0].port;

	// normalize
	const float norm = (*dst - src_port->control.min) * src_port->control.range_1;
	*dst = port->control.min + norm * port->control.range; //TODO handle exponential ranges

	_sp_app_port_control_stash(port);
}

__realtime static inline void
_port_control_multiplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	float *dst = PORT_BASE_ALIGNED(port);
	*dst = 0; // init

	for(int s=0; s<port->num_sources; s++)
	{
		port_t *src_port = port->sources[s].port;

		const float *src = PORT_BASE_ALIGNED(src_port);

		// normalize
		const float norm = (*src - src_port->control.min) * src_port->control.range_1;
		*dst += port->control.min + norm * port->control.range; //TODO handle exponential ranges
	}

	_sp_app_port_control_stash(port);
}

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

__realtime static inline void
_port_seq_multiplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	// create forge to append to sequence (may contain events from UI)
	const uint32_t capacity = PORT_SIZE(port);
	LV2_Atom_Sequence *dst = PORT_BASE_ALIGNED(port);

	const LV2_Atom_Sequence *seq [port->num_sources];
	const LV2_Atom_Event *itr [port->num_sources];
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
			LV2_Atom_Event *ev = lv2_atom_sequence_append_event(dst, capacity, itr[nxt]);
			(void)ev; //TODO check

			// advance iterator
			itr[nxt] = lv2_atom_sequence_next(itr[nxt]);
		}
		else
			break; // no more events to process
	};
}

__realtime static inline void
_port_ev_multiplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	//const LV2_Atom_Sequence *seq = PORT_BUF_ALIGNED(port);

	//FIXME FIXME FIXME actually implement me FIXME FIXME FIXME
}

__realtime static inline void
_port_seq_simplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	const LV2_Atom_Sequence *seq = PORT_BUF_ALIGNED(port);
	// move messages from UI to default buffer

	if(seq->atom.size > sizeof(LV2_Atom_Sequence_Body)) // has messages from UI
	{
		//printf("adding UI event\n");

		port_t *src_port = port->sources[0].port;
		const uint32_t capacity = PORT_SIZE(src_port);
		LV2_Atom_Sequence *dst = PORT_BUF_ALIGNED(src_port);

		LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
		{
			LV2_Atom_Event *ev2 = lv2_atom_sequence_append_event(dst, capacity, ev);
			if(ev2)
				ev2->time.frames = nsamples - 1;
		}
	}
}

__realtime static LV2_Atom_Forge_Ref
_patch_notification_internal(sp_app_t *app, port_t *source_port,
	uint32_t size, LV2_URID type, const void *body)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.notification_module.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&app->forge, source_port->mod->urn);

	if(ref)
		ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.notification_symbol.urid);
	if(ref)
		ref = lv2_atom_forge_string(&app->forge, source_port->symbol, strlen(source_port->symbol));

	if(ref)
		ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.notification_value.urid);
	if(ref)
		ref = lv2_atom_forge_atom(&app->forge, size, type);
	if(ref)
		ref = lv2_atom_forge_write(&app->forge, body, size);

	return ref;
}

__realtime static void
_patch_notification_add(sp_app_t *app, port_t *source_port,
	LV2_URID proto, uint32_t size, LV2_URID type, const void *body)
{
	LV2_Atom_Forge_Frame frame [3];

	LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
	if(answer)
	{
		if(synthpod_patcher_add_object(&app->regs, &app->forge, &frame[0],
				0, 0, app->regs.synthpod.notification_list.urid) //TODO subject
			&& lv2_atom_forge_object(&app->forge, &frame[2], 0, proto)
			&& _patch_notification_internal(app, source_port, size, type, body) )
		{
			synthpod_patcher_pop(&app->forge, frame, 3);
			_sp_app_to_ui_advance_atom(app, answer);
		}
	}
}

__realtime static inline void
_port_float_protocol_update(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	bool needs_update = false;
	float new_val = 0.f;

	const float *val = PORT_BASE_ALIGNED(port);
	new_val = *val;
	needs_update = new_val != port->control.last;

	if(needs_update) // update last value
		port->control.last = new_val;

	if(needs_update)
	{
		size_t size = sizeof(transfer_float_t);
		transfer_float_t *trans = _sp_app_to_ui_request(app, size);
		if(trans)
		{
			_sp_transfer_float_fill(&app->regs, &app->forge, trans, port->mod->uid, port->index, &new_val);
			_sp_app_to_ui_advance(app, size);
		}

		// for nk
		_patch_notification_add(app, port, app->regs.port.float_protocol.urid,
			sizeof(float), app->forge.Float, &new_val);
	}
}

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

	if(fabs(peak - port->audio.last) >= 1e-3) //TODO make this configurable
	{
		// update last value
		port->audio.last = peak;

		const LV2UI_Peak_Data data = {
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

		// for nk
		const struct {
			LV2_Atom_Tuple header;
			LV2_Atom_Int period_start;
				int32_t space_1;
			LV2_Atom_Int period_size;
				int32_t space_2;
			LV2_Atom_Float peak;
				int32_t space_3;
		} tup = {
			.header = {
				.atom = {
					.size = 3*sizeof(LV2_Atom_Long),
					.type = app->forge.Tuple
				}
			},
			.period_start = {
				.atom = {
					.size = sizeof(int32_t),
					.type = app->forge.Int
				},
				.body = data.period_start
			},
			.period_size = {
				.atom = {
					.size = sizeof(int32_t),
					.type = app->forge.Int
				},
				.body = data.period_size
			},
			.peak = {
				.atom = {
					.size = sizeof(float),
					.type = app->forge.Float
				},
				.body = data.peak
			}
		};
		_patch_notification_add(app, port, app->regs.port.peak_protocol.urid,
			tup.header.atom.size, tup.header.atom.type, &tup.period_start);
	}
}

__realtime static inline void
_port_atom_transfer_update(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	const LV2_Atom *atom = PORT_BASE_ALIGNED(port);

	if(atom->size == 0) // empty atom
		return;
	else if( (port->atom.buffer_type == PORT_BUFFER_TYPE_SEQUENCE)
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

	// for nk
	_patch_notification_add(app, port, app->regs.port.atom_transfer.urid,
		atom->size, atom->type, LV2_ATOM_BODY_CONST(atom));
}

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

			// for nk
			_patch_notification_add(app, port, app->regs.port.event_transfer.urid,
				atom->size, atom->type, LV2_ATOM_BODY_CONST(atom));
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
					|| (obj->body.otype == app->regs.patch.patch.urid)
					|| (obj->body.otype == app->regs.patch.error.urid)
					|| (obj->body.otype == app->regs.patch.ack.urid) ) //TODO support more patch messages
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

					// for nk
					_patch_notification_add(app, port, app->regs.port.event_transfer.urid,
						obj->atom.size, obj->atom.type, &obj->body);
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
