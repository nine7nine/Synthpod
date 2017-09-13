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
		{
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

									if(ref)
										ref = lv2_atom_forge_key(&app->forge, app->regs.param.gain.urid);
									if(ref)
										ref = lv2_atom_forge_float(&app->forge, source->gain);
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
		else if(prop == app->regs.synthpod.node_list.urid)
		{
			LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
			if(answer)
			{
				LV2_Atom_Forge_Frame frame [3];
				LV2_Atom_Forge_Ref ref = synthpod_patcher_set_object(
					&app->regs, &app->forge, &frame[0], subj, sn, prop);
				if(ref)
					ref = lv2_atom_forge_tuple(&app->forge, &frame[1]);
				for(unsigned m1 = 0; m1 < app->num_mods; m1++)
				{
					mod_t *snk_mod = app->mods[m1];

					for(unsigned m2=0; m2<app->num_mods; m2++)
					{
						mod_t *src_mod = app->mods[m2];
						bool mods_are_connected = false;
						float x = 0.f;
						float y = 0.f;

						for(unsigned p=0; p<snk_mod->num_ports; p++)
						{
							port_t *port = &snk_mod->ports[p];

							connectable_t *conn = _sp_app_port_connectable(port);
							if(conn)
							{
								for(int j=0; j<conn->num_sources; j++)
								{
									source_t *source = &conn->sources[j];
									port_t *src_port = source->port;

									if(src_port->mod == src_mod)
									{
										mods_are_connected = true;
										x = source->pos.x;
										y = source->pos.y;
										break;
									}
								}
							}

							if(mods_are_connected)
								break;
						}

						if(mods_are_connected)
						{
							if(ref)
								ref = lv2_atom_forge_object(&app->forge, &frame[2], 0, 0);
							{
								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.source_module.urid);
								if(ref)
									ref = lv2_atom_forge_urid(&app->forge, src_mod->urn);

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.sink_module.urid);
								if(ref)
									ref = lv2_atom_forge_urid(&app->forge, snk_mod->urn);

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.node_position_x.urid);
								if(ref)
									ref = lv2_atom_forge_float(&app->forge, x);

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.node_position_y.urid);
								if(ref)
									ref = lv2_atom_forge_float(&app->forge, y);
							}
							if(ref)
								lv2_atom_forge_pop(&app->forge, &frame[2]);
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
									ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.source_min.urid);
								if(ref)
									ref = lv2_atom_forge_double(&app->forge, automation->a);

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.source_max.urid);
								if(ref)
									ref = lv2_atom_forge_double(&app->forge, automation->b);

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.sink_min.urid);
								if(ref)
									ref = lv2_atom_forge_double(&app->forge, automation->c);

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.sink_max.urid);
								if(ref)
									ref = lv2_atom_forge_double(&app->forge, automation->d);
							}
							if(ref)
								lv2_atom_forge_pop(&app->forge, &frame[2]);
						}
						else if(automation->type == AUTO_TYPE_OSC)
						{
							osc_auto_t *oauto = &automation->osc;

							if(ref)
								ref = lv2_atom_forge_object(&app->forge, &frame[2], 0, app->regs.osc.message.urid);
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
									ref = lv2_atom_forge_key(&app->forge, app->regs.osc.path.urid);
								if(ref)
									ref = lv2_atom_forge_string(&app->forge, oauto->path, strlen(oauto->path));

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.source_min.urid);
								if(ref)
									ref = lv2_atom_forge_double(&app->forge, automation->a);

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.source_max.urid);
								if(ref)
									ref = lv2_atom_forge_double(&app->forge, automation->b);

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.sink_min.urid);
								if(ref)
									ref = lv2_atom_forge_double(&app->forge, automation->c);

								if(ref)
									ref = lv2_atom_forge_key(&app->forge, app->regs.synthpod.sink_max.urid);
								if(ref)
									ref = lv2_atom_forge_double(&app->forge, automation->d);
							}
							if(ref)
								lv2_atom_forge_pop(&app->forge, &frame[2]);
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
		else if(prop == app->regs.synthpod.cpus_available.urid)
		{
			LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
			if(answer)
			{
				const int32_t cpus_available = app->dsp_master.num_slaves + 1;

				LV2_Atom_Forge_Ref ref = synthpod_patcher_set(
					&app->regs, &app->forge, subj, sn, prop,
					sizeof(int32_t), app->forge.Int, &cpus_available);
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
		else if(prop == app->regs.synthpod.cpus_used.urid)
		{
			LV2_Atom *answer = _sp_app_to_ui_request_atom(app);
			if(answer)
			{
				const int32_t cpus_used = (app->dsp_master.concurrent > app->dsp_master.num_slaves + 1)
					? app->dsp_master.num_slaves + 1
					: app->dsp_master.concurrent;

				LV2_Atom_Forge_Ref ref = synthpod_patcher_set(
					&app->regs, &app->forge, subj, sn, prop,
					sizeof(int32_t), app->forge.Int, &cpus_used);
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
					else
					{
						sp_app_log_trace(app, "%s: buffer request failed\n", __func__);
					}
				}
				else if(app->block_state == BLOCKING_STATE_BLOCK)
				{
					if(app->silence_state == SILENCING_STATE_BLOCK)
						return false; // not fully silenced yet, wait

					// send request to worker thread
					const LV2_URID pset_urn = ((const LV2_Atom_URID *)value)->body;
					size_t size = sizeof(job_t);
					job_t *job = _sp_app_to_worker_request(app, size);
					if(job)
					{
						app->block_state = BLOCKING_STATE_WAIT; // wait for job
						mod->bypassed = mod->needs_bypassing;

						job->request = JOB_TYPE_REQUEST_PRESET_LOAD;
						job->mod = mod;
						job->urn = pset_urn;
						_sp_app_to_worker_advance(app, size);

						return true; // advance
					}
					else
					{
						sp_app_log_trace(app, "%s: buffer request failed\n", __func__);
					}
				}
			}
			else if( (prop == app->regs.idisp.surface.urid)
				&& (value->type == app->forge.Bool) )
			{
				mod->idisp.subscribed = ((const LV2_Atom_Bool *)value)->body;

				if(mod->idisp.iface && mod->idisp.subscribed)
				{
					_sp_app_mod_queue_draw(mod); // trigger update
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
			else
			{
				sp_app_log_trace(app, "%s: buffer request failed\n", __func__);
			}
		}
		else if(app->block_state == BLOCKING_STATE_BLOCK)
		{
			// send request to worker thread
			size_t size = sizeof(job_t);
			job_t *job = _sp_app_to_worker_request(app, size);
			if(job)
			{
				app->block_state = BLOCKING_STATE_WAIT; // wait for job

				job->request = JOB_TYPE_REQUEST_BUNDLE_SAVE;
				job->status = -1; // TODO for what for?
				job->urn = dest;
				_sp_app_to_worker_advance(app, size);

				return true; // advance
			}
			else
			{
				sp_app_log_trace(app, "%s: buffer request failed\n", __func__);
			}
		}
	}
	else if(subj && !dest) // copy bundle from subj
	{
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
			else
			{
				sp_app_log_trace(app, "%s: buffer request failed\n", __func__);
			}
		}
		else if(app->block_state == BLOCKING_STATE_BLOCK)
		{
			//FIXME ramp up system outputs

			// send request to worker thread
			job_t *job = _sp_app_to_worker_request(app, sizeof(job_t));
			if(job)
			{
				app->block_state = BLOCKING_STATE_WAIT; // wait for job
				app->load_bundle = true; // for sp_app_bypassed

				job->request = JOB_TYPE_REQUEST_BUNDLE_LOAD;
				job->status = -1; // TODO for what for?
				job->urn = subj;
				_sp_app_to_worker_advance(app, sizeof(job_t));

				return true; // advance
			}
			else
			{
				sp_app_log_trace(app, "%s: buffer request failed\n", __func__);
			}
		}
	}
	else if(subj && dest) // copy preset to dest
	{
		mod_t *mod = _mod_find_by_urn(app, subj);

		if(app->block_state == BLOCKING_STATE_RUN)
		{
			// send request to worker thread
			job_t *job = _sp_app_to_worker_request(app, sizeof(job_t));
			if(job)
			{
				app->block_state = BLOCKING_STATE_DRAIN; // wait for drain

				job->request = JOB_TYPE_REQUEST_DRAIN;
				job->status = 0;
				_sp_app_to_worker_advance(app, sizeof(job_t));
			}
			else
			{
				sp_app_log_trace(app, "%s: buffer request failed\n", __func__);
			}
		}
		else if(app->block_state == BLOCKING_STATE_BLOCK)
		{
			// send request to worker thread
			job_t *job = _sp_app_to_worker_request(app, sizeof(job_t));
			if(job)
			{
				app->block_state = BLOCKING_STATE_WAIT; // wait for job

				job->request = JOB_TYPE_REQUEST_PRESET_SAVE;
				job->mod = mod;
				job->urn = dest;
				_sp_app_to_worker_advance(app, sizeof(job_t));

				return true; // advance
			}
			else
			{
				sp_app_log_trace(app, "%s: buffer request failed\n", __func__);
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
	const LV2_Atom_Float *link_gain = NULL;

	lv2_atom_object_get(obj,
		app->regs.synthpod.source_module.urid, &src_module,
		app->regs.synthpod.source_symbol.urid, &src_symbol,
		app->regs.synthpod.sink_module.urid, &snk_module,
		app->regs.synthpod.sink_symbol.urid, &snk_symbol,
		app->regs.param.gain.urid, &link_gain,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const char *src_sym = src_symbol
		? LV2_ATOM_BODY_CONST(src_symbol) : NULL;
	const LV2_URID snk_urn = snk_module
		? snk_module->body : 0;
	const char *snk_sym = snk_symbol
		? LV2_ATOM_BODY_CONST(snk_symbol) : NULL;
	const float gain = link_gain
		? link_gain->body : 1.f;

	if(src_urn && src_sym && snk_urn && snk_sym)
	{
		port_t *src_port = _port_find_by_symbol(app, src_urn, src_sym);
		port_t *snk_port = _port_find_by_symbol(app, snk_urn, snk_sym);

		if(src_port && snk_port)
		{
			const int32_t state = _sp_app_port_connect(app, src_port, snk_port, gain);
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

__realtime void
_node_list_add(sp_app_t *app, const LV2_Atom_Object *obj)
{
	//printf("got patch:add for nodeList:\n");

	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom_URID *snk_module = NULL;
	const LV2_Atom_Float *pos_x = NULL;
	const LV2_Atom_Float *pos_y = NULL;

	lv2_atom_object_get(obj,
		app->regs.synthpod.source_module.urid, &src_module,
		app->regs.synthpod.sink_module.urid, &snk_module,
		app->regs.synthpod.node_position_x.urid, &pos_x,
		app->regs.synthpod.node_position_y.urid, &pos_y,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const LV2_URID snk_urn = snk_module
		? snk_module->body : 0;
	const float x = pos_x 
		? pos_x->body : 0.f;
	const float y = pos_y 
		? pos_y->body : 0.f;

	if(src_urn && snk_urn)
	{
		mod_t *src_mod = _mod_find_by_urn(app, src_urn);
		mod_t *snk_mod = _mod_find_by_urn(app, snk_urn);

		if(src_mod && snk_mod)
		{
			for(unsigned p=0; p<snk_mod->num_ports; p++)
			{
				port_t *port = &snk_mod->ports[p];

				connectable_t *conn = _sp_app_port_connectable(port);
				if(conn)
				{
					for(int j=0; j<conn->num_sources; j++)
					{
						source_t *source = &conn->sources[j];
						port_t *source_port = source->port;

						if(source_port->mod == src_mod)
						{
							source->pos.x = x;
							source->pos.y = y;
						}
					}
				}
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
					sp_app_log_trace(app, "%s: failed to append\n", __func__);
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

__realtime void
_automation_list_add(sp_app_t *app, const LV2_Atom_Object *obj)
{
	//printf("got patch:add for automationList:\n");

	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom *src_symbol = NULL;
	const LV2_Atom_URID *src_property = NULL;
	const LV2_Atom_URID *src_range = NULL;
	const LV2_Atom_Int *src_channel = NULL;
	const LV2_Atom_Int *src_controller = NULL;
	const LV2_Atom_String *src_path = NULL;
	const LV2_Atom_Double *src_min = NULL;
	const LV2_Atom_Double *src_max = NULL;
	const LV2_Atom_Double *snk_min = NULL;
	const LV2_Atom_Double *snk_max = NULL;

	lv2_atom_object_get(obj,
		app->regs.synthpod.sink_module.urid, &src_module,
		app->regs.synthpod.sink_symbol.urid, &src_symbol,
		app->regs.patch.property.urid, &src_property,
		app->regs.rdfs.range.urid, &src_range,
		app->regs.midi.channel.urid, &src_channel,
		app->regs.midi.controller_number.urid, &src_controller,
		app->regs.osc.path.urid, &src_path,
		app->regs.synthpod.source_min.urid, &src_min,
		app->regs.synthpod.source_max.urid, &src_max,
		app->regs.synthpod.sink_min.urid, &snk_min,
		app->regs.synthpod.sink_max.urid, &snk_max,
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
				automation->index = port->index;
				automation->property = src_prop;
				automation->range = src_ran;

				automation->a = src_min ? src_min->body : 0.0;
				automation->b = src_max ? src_max->body : 0.0;
				automation->c = snk_min ? snk_min->body : 0.0;
				automation->d = snk_max ? snk_max->body : 0.0;

				const double div = automation->b - automation->a;
				automation->mul = div
					? (automation->d - automation->c) / div
					: 0.0;
				automation->add = div
					? (automation->c*automation->b - automation->a*automation->d) / div
					: 0.0;

				if(obj->body.otype == app->regs.midi.Controller.urid)
				{
					automation->type = AUTO_TYPE_MIDI;
					automation->midi.channel = src_channel ? src_channel->body : -1;
					automation->midi.controller = src_controller ? src_controller->body : -1;
				}
				else if(obj->body.otype == app->regs.osc.message.urid)
				{
					automation->type = AUTO_TYPE_OSC;
					if(src_path)
						strncpy(automation->osc.path, LV2_ATOM_BODY_CONST(src_path), 256);
					else
						automation->osc.path[0] = '\0';
				}

				break;
			}
		}
	}
}


__realtime static void
_mod_list_add(sp_app_t *app, const LV2_Atom_URID *urid)
{
	//printf("got patch:add for moduleList: %s\n", uri);

	// send request to worker thread
	const size_t size = sizeof(job_t);
	job_t *job = _sp_app_to_worker_request(app, size);
	if(job)
	{
		job->request = JOB_TYPE_REQUEST_MODULE_ADD;
		job->status = 0;
		job->urn = urid->body;
		_sp_app_to_worker_advance(app, size);
	}
	else
	{
		sp_app_log_trace(app, "%s: buffer request failed\n", __func__);
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
			else if(  (prop->key == app->regs.synthpod.node_list.urid)
				&& (prop->value.type == app->forge.Object) )
			{
				//FIXME never reached
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
			else if(  (prop->key == app->regs.synthpod.node_list.urid)
				&& (prop->value.type == app->forge.Object) )
			{
				_node_list_add(app, (const LV2_Atom_Object *)&prop->value);
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
		else
			sp_app_log_trace(app, "%s: unknown object type\n", __func__);
	}
	else
		sp_app_log_trace(app, "%s: not an atom object\n", __func__);

	return advance_ui[app->block_state];
}
