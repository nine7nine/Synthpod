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

#include <inttypes.h>

#include <synthpod_app_private.h>
#include <synthpod_patcher.h>

#include <osc.lv2/util.h>

#if !defined(USE_DYNAMIC_PARALLELIZER)
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
#endif

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

				connectable_t *conn = _sp_app_port_connectable(port_sink);
				if(conn)
				{
					for(int s=0; s<conn->num_sources; s++)
					{
						source_t *source =  &conn->sources[s];

						if(source->port->mod == mod_source)
						{
							is_connected = true;
							break;
						}
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
		sp_app_log_trace(app, "%s: %u, %u\n",
			app->driver->unmap->unmap(app->driver->unmap->handle, mod->plug_urid),
			mod->dsp_client.num_sources, mod->dsp_client.num_sinks);
	}
	sp_app_log_trace(app, "\n");
	*/

#if !defined(USE_DYNAMIC_PARALLELIZER)
	_dsp_master_concurrent(app);

	// to nk
	LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
	if(answer)
	{
		const int32_t cpus_used = (app->dsp_master.concurrent > app->dsp_master.num_slaves + 1)
			? app->dsp_master.num_slaves + 1
			: app->dsp_master.concurrent;

		LV2_Atom_Forge_Ref ref = synthpod_patcher_set(
			&app->regs, &app->forge, 0, 0, app->regs.synthpod.cpus_used.urid,
			sizeof(int32_t), app->forge.Int, &cpus_used); //TODO subj, seqn
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
#endif
}

bool
_sp_app_port_connected(port_t *src_port, port_t *snk_port, float gain)
{
	connectable_t *conn = _sp_app_port_connectable(snk_port);
	if(conn)
	{
		for(int s = 0; s < conn->num_sources; s++)
		{
			source_t *src = &conn->sources[s];

			if(src->port == src_port)
			{
				src->gain = gain;
				return true;
			}
		}
	}

	return false;
}

int
_sp_app_port_connect(sp_app_t *app, port_t *src_port, port_t *snk_port, float gain)
{
	if(_sp_app_port_connected(src_port, snk_port, gain))
		return 0;

	connectable_t *conn = _sp_app_port_connectable(snk_port);
	if(!conn)
	{
		sp_app_log_trace(app, "%s: invalid connectable\n", __func__);
		return 0;
	}

	if(conn->num_sources >= MAX_SOURCES)
	{
		sp_app_log_trace(app, "%s: too many sources\n", __func__);
		return 0;
	}

	source_t *source = &conn->sources[conn->num_sources];
	source->port = src_port;;
	source->gain = gain;
	conn->num_sources += 1;

	// only audio port connections need to be ramped to be clickless
	if(snk_port->type == PORT_TYPE_AUDIO)
	{
		source->ramp.samples = app->ramp_samples;
		source->ramp.state = RAMP_STATE_UP;
		source->ramp.value = 0.f;
	}

	_dsp_master_reorder(app);
	return 1;
}

void
_sp_app_port_disconnect(sp_app_t *app, port_t *src_port, port_t *snk_port)
{
	connectable_t *conn = _sp_app_port_connectable(snk_port);
	if(!conn)
		return;

	// update sources list 
	bool connected = false;
	for(int i=0, j=0; i<conn->num_sources; i++)
	{
		if(conn->sources[i].port == src_port)
		{
			connected = true;
			continue;
		}

		conn->sources[j++].port = conn->sources[i].port;
	}

	if(!connected)
		return;

	conn->num_sources -= 1;

	_dsp_master_reorder(app);
}

int
_sp_app_port_disconnect_request(sp_app_t *app, port_t *src_port, port_t *snk_port,
	ramp_state_t ramp_state)
{
	if(  (src_port->direction == PORT_DIRECTION_OUTPUT)
		&& (snk_port->direction == PORT_DIRECTION_INPUT) )
	{
		source_t *source = NULL;

		connectable_t *conn = _sp_app_port_connectable(snk_port);
		if(conn)
		{
			// find connection
			for(int i=0; i<conn->num_sources; i++)
			{
				if(conn->sources[i].port == src_port)
				{
					source = &conn->sources[i];
					break;
				}
			}
		}

		if(source)
		{
			if(src_port->type == PORT_TYPE_AUDIO)
			{
				// only audio output ports need to be ramped to be clickless

				if(source->ramp.state == RAMP_STATE_NONE)
				{
					source->ramp.samples = app->ramp_samples;
					source->ramp.state = ramp_state;
					source->ramp.value = 1.f;
				}

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
		source_t *source = NULL;

		connectable_t *conn = _sp_app_port_connectable(snk_port);
		if(conn)
		{
			// find connection
			for(int i=0; i<conn->num_sources; i++)
			{
				if(conn->sources[i].port == src_port)
				{
					source = &conn->sources[i];
					break;
				}
			}
		}

		if(source)
		{
			if(src_port->type == PORT_TYPE_AUDIO)
			{
				// only audio output ports need to be ramped to be clickless
				source->ramp.samples = app->ramp_samples;
				source->ramp.state = RAMP_STATE_UP;
				source->ramp.value = 0.f;

				return 1; // needs ramping
			}
		}
	}

	return 0; // not connected
}

connectable_t *
_sp_app_port_connectable(port_t *port)
{
	switch(port->type)
	{
		case PORT_TYPE_AUDIO:
			return &port->audio.connectable;
		case PORT_TYPE_CV:
			return &port->cv.connectable;
		case PORT_TYPE_ATOM:
			return &port->atom.connectable;
		default:
			break;
	}

	return NULL;
}

int
_sp_app_port_silence_request(sp_app_t *app, port_t *src_port, port_t *snk_port,
	ramp_state_t ramp_state)
{
	if(  (src_port->direction == PORT_DIRECTION_OUTPUT)
		&& (snk_port->direction == PORT_DIRECTION_INPUT) )
	{
		source_t *source = NULL;

		connectable_t *conn = _sp_app_port_connectable(snk_port);
		if(conn)
		{
			// find connection
			for(int i=0; i<conn->num_sources; i++)
			{
				if(conn->sources[i].port == src_port)
				{
					source = &conn->sources[i];
					break;
				}
			}
		}

		if(source)
		{
			if(src_port->type == PORT_TYPE_AUDIO)
			{
				// only audio output ports need to be ramped to be clickless
				source->ramp.samples = app->ramp_samples;
				source->ramp.state = ramp_state;
				source->ramp.value = 1.f;

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
			source->port->mod->delete_request = true; // mark module for removal
			_sp_app_port_disconnect(app, source->port, port);
		}
		else if(source->ramp.state == RAMP_STATE_DOWN_DRAIN)
		{
			// fully silenced, continue with preset loading

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
			// nothing
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
_port_audio_multiplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	float *val = PORT_BASE_ALIGNED(port);
	memset(val, 0, nsamples * sizeof(float)); // init

	connectable_t *conn = &port->audio.connectable;
	for(int s=0; s<conn->num_sources; s++)
	{
		source_t *source = &conn->sources[s];

		// ramp audio output ports
		if(source->ramp.state != RAMP_STATE_NONE)
		{
			const float *src = PORT_BASE_ALIGNED(source->port);
			const float gain = source->gain * source->ramp.value;

			if(gain == 1.f)
			{
				for(uint32_t j=0; j<nsamples; j++)
					val[j] += src[j];
			}
			else // gain != 1.f
			{
				for(uint32_t j=0; j<nsamples; j++)
					val[j] += src[j] * gain;
			}

			_update_ramp(app, source, port, nsamples);
		}
		else // RAMP_STATE_NONE
		{
			const float *src = PORT_BASE_ALIGNED(source->port);
			const float gain = source->gain;
			if(gain == 1.f)
			{
				for(uint32_t j=0; j<nsamples; j++)
					val[j] += src[j];
			}
			else // gain != 1.f
			{
				for(uint32_t j=0; j<nsamples; j++)
					val[j] += src[j] * gain;
			}
		}
	}
}

__realtime static inline void
_port_cv_multiplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	float *val = PORT_BASE_ALIGNED(port);
	memset(val, 0, nsamples * sizeof(float)); // init

	connectable_t *conn = &port->cv.connectable;
	for(int s=0; s<conn->num_sources; s++)
	{
		source_t *source = &conn->sources[s];

		const float *src = PORT_BASE_ALIGNED(source->port);
		for(uint32_t j=0; j<nsamples; j++)
			val[j] += src[j];
	}
}

__realtime static inline int
_sp_app_automate(sp_app_t *app, mod_t *mod, auto_t *automation, double value,
	int64_t frames, bool pre)
{
	int do_route = 0;

	if(automation->logarithmic)
	{
		value = log(value + 1.0) / M_LN2;
	}

	// linear mapping from MIDI to automation value
	double f64 = value * automation->mul + automation->add;

	// clip automation value to destination range
	if(f64 < automation->c)
		f64 = automation->c;
	else if(f64 > automation->d)
		f64 = automation->d;

	port_t *port = &mod->ports[automation->index];
	if(port->type == PORT_TYPE_CONTROL)
	{
		if(pre)
		{
			control_port_t *control = &port->control;

			float *buf = PORT_BASE_ALIGNED(port);
			*buf = control->is_integer
				? floor(f64)
				: f64;
		}
	}
	else if( (port->type == PORT_TYPE_ATOM) && automation->property )
	{
		if(!pre)
		{
			LV2_Atom_Sequence *control = PORT_BASE_ALIGNED(port);
			LV2_Atom_Event *dst = lv2_atom_sequence_end(&control->body, control->atom.size);
			LV2_Atom_Forge_Frame obj_frame;

			lv2_atom_forge_set_buffer(&app->forge, (uint8_t *)dst, PORT_SIZE(port) - control->atom.size - sizeof(LV2_Atom));

			LV2_Atom_Forge_Ref ref;
			ref = lv2_atom_forge_frame_time(&app->forge, frames)
				&& lv2_atom_forge_object(&app->forge, &obj_frame, 0, app->regs.patch.set.urid)
				&& lv2_atom_forge_key(&app->forge, app->regs.patch.property.urid)
				&& lv2_atom_forge_urid(&app->forge, automation->property)
				&& lv2_atom_forge_key(&app->forge, app->regs.patch.value.urid);
			if(ref)
			{
				if(automation->range == app->forge.Bool)
				{
					ref = lv2_atom_forge_bool(&app->forge, f64 != 0.0);
				}
				else if(automation->range == app->forge.Int)
				{
					ref = lv2_atom_forge_int(&app->forge, floor(f64));
				}
				else if(automation->range == app->forge.Long)
				{
					ref = lv2_atom_forge_long(&app->forge, floor(f64));
				}
				else if(automation->range == app->forge.Float)
				{
					ref = lv2_atom_forge_float(&app->forge, f64);
				}
				else if(automation->range == app->forge.Double)
				{
					ref = lv2_atom_forge_double(&app->forge, f64);
				}
				//FIXME support more types

				if(ref)
					lv2_atom_forge_pop(&app->forge, &obj_frame);

				control->atom.size += sizeof(LV2_Atom_Event) + dst->body.size;
			}
		}

		do_route += 1;
	}
	else if(port->type == PORT_TYPE_CV)
	{
		//FIXME does it make sense to make this automatable?
	}

	return do_route;
}

__realtime static inline int
_sp_app_automate_event(sp_app_t *app, mod_t *mod, const LV2_Atom_Event *ev,
	bool pre)
{
	const int64_t frames = ev->time.frames;
	const LV2_Atom *atom = &ev->body;
	const LV2_Atom_Object *obj = (const LV2_Atom_Object *)atom;

	int do_route = 0;

	//printf("got automation\n");
	if(  (atom->type == app->regs.port.midi.urid)
		&& (atom->size == 3) ) // we're only interested in controller events
	{
		const uint8_t *msg = LV2_ATOM_BODY_CONST(atom);
		const uint8_t cmd = msg[0] & 0xf0;

		if(cmd == 0xb0) // Controller
		{
			const uint8_t channel = msg[0] & 0x0f;
			const uint8_t controller = msg[1];
			const uint8_t val = msg[2];

			// iterate over automations
			for(unsigned i = 0; i < MAX_AUTOMATIONS; i++)
			{
				auto_t *automation = &mod->automations[i];

				if(  (automation->type == AUTO_TYPE_MIDI)
					&& automation->snk_enabled )
				{
					midi_auto_t *mauto = &automation->midi;

					if(pre && automation->learning)
					{
						if( (mauto->channel == -1) && (mauto->controller == -1) )
						{
							mauto->channel = channel;
							mauto->controller = controller;

							automation->a = val;
							automation->b = val;
							_automation_refresh_mul_add(automation);

							automation->sync = true;
						}
						else
						{
							bool needs_refresh = false;

							if(val < automation->a)
							{
								automation->a = val;
								needs_refresh = true;
							}
							else if(val > automation->b)
							{
								automation->b = val;
								needs_refresh = true;
							}

							if(needs_refresh)
							{
								_automation_refresh_mul_add(automation);
							}

							automation->sync = true;
						}
					}

					if(  ( (mauto->channel == -1) || (mauto->channel == channel) )
						&& ( (mauto->controller == -1) || (mauto->controller == controller) ) )
					{
						do_route += _sp_app_automate(app, mod, automation, msg[2], frames, pre);
					}
				}
			}
		}
	}
	else if(lv2_osc_is_message_type(&app->osc_urid, obj->body.otype)) //FIXME also consider bundles
	{
		const LV2_Atom_String *osc_path = NULL;
		const LV2_Atom_Tuple *osc_args = NULL;

		if(lv2_osc_message_get(&app->osc_urid, obj, &osc_path, &osc_args))
		{
			const char *path = LV2_ATOM_BODY_CONST(osc_path);
			double val = 0.0;

			LV2_ATOM_TUPLE_FOREACH(osc_args, item)
			{
				switch(lv2_osc_argument_type(&app->osc_urid, item))
				{
					case LV2_OSC_FALSE:
					case LV2_OSC_NIL:
					{
						val = 0.0;
					} break;
					case LV2_OSC_TRUE:
					{
						val = 1.0;
					} break;
					case LV2_OSC_IMPULSE:
					{
						val = HUGE_VAL;
					} break;
					case LV2_OSC_INT32:
					{
						int32_t i32;
						lv2_osc_int32_get(&app->osc_urid, item, &i32);
						val = i32;
					} break;
					case LV2_OSC_INT64:
					{
						int64_t i64;
						lv2_osc_int64_get(&app->osc_urid, item, &i64);
						val = i64;
					} break;
					case LV2_OSC_FLOAT:
					{
						float f32;
						lv2_osc_float_get(&app->osc_urid, item, &f32);
						val = f32;
					} break;
					case LV2_OSC_DOUBLE:
					{
						double f64;
						lv2_osc_double_get(&app->osc_urid, item, &f64);
						val = f64;
					} break;

					case LV2_OSC_SYMBOL:
					case LV2_OSC_BLOB:
					case LV2_OSC_CHAR:
					case LV2_OSC_STRING:
					case LV2_OSC_MIDI:
					case LV2_OSC_RGBA:
					case LV2_OSC_TIMETAG:
					{
						//FIXME handle other types, especially string, blob, symbol
					}	break;
				}
			}

			// iterate over automations
			for(unsigned i = 0; i < MAX_AUTOMATIONS; i++)
			{
				auto_t *automation = &mod->automations[i];

				if(  (automation->type == AUTO_TYPE_OSC)
					&& automation->snk_enabled )
				{
					osc_auto_t *oauto = &automation->osc;

					if(pre && automation->learning)
					{
						if(oauto->path[0] == '\0')
						{
							strncpy(oauto->path, path, sizeof(oauto->path));

							automation->a = val;
							automation->b = val;
							_automation_refresh_mul_add(automation);

							automation->sync = true;
						}
						else
						{
							bool needs_refresh = false;

							if(val < automation->a)
							{
								automation->a = val;
								needs_refresh = true;
							}
							else if(val > automation->b)
							{
								automation->b = val;
								needs_refresh = true;
							}

							if(needs_refresh)
							{
								_automation_refresh_mul_add(automation);
							}

							automation->sync = true;
						}
					}

					if( (oauto->path[0] == '\0') || !strncmp(oauto->path, path, sizeof(oauto->path)) )
					{
						do_route += _sp_app_automate(app, mod, automation, val, frames, pre);
					}
				}
			}
		}
	}
	//FIXME handle other events
	
	return do_route;
}

__realtime static inline void
_port_seq_multiplex(sp_app_t *app, port_t *port, uint32_t nsamples)
{
	mod_t *mod = port->mod;
	const unsigned p = mod->num_ports - 2;
	port_t *auto_port = &mod->ports[p];

	// create forge to append to sequence (may contain events from UI)
	const uint32_t capacity = PORT_SIZE(port);
	LV2_Atom_Sequence *dst = PORT_BASE_ALIGNED(port);

	connectable_t *conn = &port->atom.connectable;
	const LV2_Atom_Sequence *seq [MAX_SOURCES];
	const LV2_Atom_Event *itr [MAX_SOURCES];
	for(int s=0; s<conn->num_sources; s++)
	{
		seq[s] = PORT_BASE_ALIGNED(conn->sources[s].port);
		itr[s] = lv2_atom_sequence_begin(&seq[s]->body);
	}

	int num_sources = conn->num_sources;

	// if destination port is patchable, also read events from automation input
	if(port->atom.patchable && (port != auto_port) )
	{
		seq[num_sources] = PORT_BASE_ALIGNED(auto_port);
		itr[num_sources] = lv2_atom_sequence_begin(&seq[num_sources]->body);

		num_sources++;
	}

	while(true)
	{
		int nxt = -1;
		int64_t frames = nsamples;

		// search for next event in timeline accross source ports
		for(int s=0; s<num_sources; s++)
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
			const LV2_Atom_Event *ev = itr[nxt];

			if(nxt == conn->num_sources) // event from automation port
			{
				_sp_app_automate_event(app, mod, ev, false);
			}
			else
			{
				int do_route = 0;

				if(port == auto_port)
				{
					// directly apply control automation, only route param automation
					do_route += _sp_app_automate_event(app, mod, ev, true);
				}
				else
				{
					do_route += 1;
				}

				if(do_route)
				{
					LV2_Atom_Event *ev2 = lv2_atom_sequence_append_event(dst, capacity, ev);
					if(!ev2)
					{
						sp_app_log_trace(app, "%s: failed to append\n", __func__);
					}
				}
			}

			// advance iterator
			itr[nxt] = lv2_atom_sequence_next(ev);
		}
		else
			break; // no more events to process
	};
}

__realtime static LV2_Atom_Forge_Ref
_patch_notification_internal(sp_app_t *app, port_t *source_port,
	uint32_t size, LV2_URID type, const void *body)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.sink_module.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&app->forge, source_port->mod->urn);

	if(ref)
		ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.sink_symbol.urid);
	if(ref)
		ref = lv2_atom_forge_string(&app->forge, source_port->symbol, strlen(source_port->symbol));

	if(ref)
		ref = lv2_atom_forge_key(&app->forge, app->regs.rdf.value.urid);
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
				/*FIXME
				const LV2_Atom_URID *destination = NULL;
				lv2_atom_object_get(obj, app->regs.patch.destination.urid, &destination, NULL);
				if(destination && (destination->atom.type == app->forge.URID)
						&& (destination->body == app->regs.core.plugin.urid) )
					continue; // ignore feedback messages
				*/

				if(  (obj->body.otype == app->regs.patch.set.urid)
					|| (obj->body.otype == app->regs.patch.get.urid)
					|| (obj->body.otype == app->regs.patch.put.urid)
					|| (obj->body.otype == app->regs.patch.patch.urid)
					|| (obj->body.otype == app->regs.patch.insert.urid)
					|| (obj->body.otype == app->regs.patch.move.urid)
					|| (obj->body.otype == app->regs.patch.copy.urid)
					|| (obj->body.otype == app->regs.patch.delete.urid)
					|| (obj->body.otype == app->regs.patch.error.urid)
					|| (obj->body.otype == app->regs.patch.ack.urid) ) //TODO support more patch messages
				{
					// for nk
					_patch_notification_add(app, port, app->regs.port.event_transfer.urid,
						obj->atom.size, obj->atom.type, &obj->body);
				}
			}
		}
	}
}

const port_driver_t control_port_driver = {
	.multiplex = NULL, // unsupported
	.transfer = _port_float_protocol_update,
	.sparse_update = true
};

const port_driver_t audio_port_driver = {
	.multiplex = _port_audio_multiplex,
	.transfer = _port_peak_protocol_update,
	.sparse_update = true
};

const port_driver_t cv_port_driver = {
	.multiplex = _port_cv_multiplex,
	.transfer = _port_peak_protocol_update,
	.sparse_update = true
};

//FIXME actually use this
const port_driver_t atom_port_driver = {
	.multiplex = NULL, // unsupported
	.transfer = _port_atom_transfer_update,
	.sparse_update = false
};

const port_driver_t seq_port_driver = {
	.multiplex = _port_seq_multiplex,
	.transfer = _port_event_transfer_update,
	.sparse_update = false
};
