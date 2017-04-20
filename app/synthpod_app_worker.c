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

static inline void *
__sp_worker_to_app_request(sp_app_t *app, size_t size)
{
	if(app->driver->to_app_request)
		return app->driver->to_app_request(size, app->data);
	else
		return NULL;
}
#define _sp_worker_to_app_request(APP, SIZE) \
	ASSUME_ALIGNED(__sp_worker_to_app_request((APP), (SIZE)))

static inline void
_sp_worker_to_app_advance(sp_app_t *app, size_t size)
{
	if(app->driver->to_app_advance)
		app->driver->to_app_advance(size, app->data);
}

bool
sp_app_from_worker(sp_app_t *app, uint32_t len, const void *data)
{
	if(!advance_work[app->block_state])
		return false; // we are blocking

	const job_t *job = ASSUME_ALIGNED(data);

	switch(job->reply)
	{
		case JOB_TYPE_REPLY_MODULE_SUPPORTED:
		{
			//signal to UI
			size_t size = sizeof(transmit_module_supported_t)
				+ lv2_atom_pad_size(strlen(job->uri) + 1);
			transmit_module_supported_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transmit_module_supported_fill(&app->regs, &app->forge, trans, size,
					job->status, job->uri);
				_sp_app_to_ui_advance(app, size);
			}

			break;
		}
		case JOB_TYPE_REPLY_MODULE_ADD:
		{
			mod_t *mod = job->mod;

			if(app->num_mods >= MAX_MODS)
				break; //TODO delete mod

			// inject module into ordered list
			app->ords[app->num_mods] = mod;

			// inject module into module graph
			app->mods[app->num_mods] = app->mods[app->num_mods-1]; // system sink
			app->mods[app->num_mods-1] = mod;
			app->num_mods += 1;

			// sort ordered list
			_sp_app_mod_qsort(app->ords, app->num_mods);

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

			//signal to NK
			LV2_Atom *answer  = _sp_app_to_ui_request(app, 1024); //FIXME
			if(answer)
			{
				lv2_atom_forge_set_buffer(&app->forge, (uint8_t *)answer, 1024);
				LV2_Atom_Forge_Ref ref = synthpod_patcher_add(&app->regs, &app->forge,
					0, 0, app->regs.synthpod.module_list.urid, //TODO subject
					sizeof(uint32_t), app->forge.URID, &mod->urn);
				if(ref)
					_sp_app_to_ui_advance(app, lv2_atom_total_size(answer));
			}

			break;
		}
		case JOB_TYPE_REPLY_MODULE_DEL:
		{
			const LV2_URID urn = job->urn;

			// signal to NK
			LV2_Atom *answer  = _sp_app_to_ui_request(app, 1024); //FIXME
			if(answer)
			{
				lv2_atom_forge_set_buffer(&app->forge, (uint8_t *)answer, 1024);
				LV2_Atom_Forge_Ref ref = synthpod_patcher_remove(&app->regs, &app->forge,
					0, 0, app->regs.synthpod.module_list.urid, //TODO subject
				 	sizeof(uint32_t), app->forge.URID, &urn);
				if(ref)
					_sp_app_to_ui_advance(app, lv2_atom_total_size(answer));
			}

			break;
		}
		case JOB_TYPE_REPLY_PRESET_LOAD:
		{
			//printf("app: preset loaded\n");
			mod_t *mod = job->mod;

			assert(app->block_state == BLOCKING_STATE_WAIT);
			app->block_state = BLOCKING_STATE_RUN; // release block
			mod->bypassed = false;

			if(app->silence_state == SILENCING_STATE_WAIT)
			{
				app->silence_state = SILENCING_STATE_RUN;

				// ramping
				for(unsigned p1=0; p1<mod->num_ports; p1++)
				{
					port_t *port = &mod->ports[p1];

					// desilence sinks
					for(unsigned m=0; m<app->num_mods; m++)
						for(unsigned p2=0; p2<app->mods[m]->num_ports; p2++)
						{
							_sp_app_port_desilence(app, port, &app->mods[m]->ports[p2]);
						}
				}
			}

			//signal to UI
			size_t size = sizeof(transmit_module_preset_load_t);
			transmit_module_preset_load_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transmit_module_preset_load_fill(&app->regs, &app->forge, trans, size,
					mod->uid, NULL);
				_sp_app_to_ui_advance(app, size);
			}

			break;
		}
		case JOB_TYPE_REPLY_PRESET_SAVE:
		{
			//printf("app: preset saved\n");

			assert(app->block_state == BLOCKING_STATE_WAIT);
			app->block_state = BLOCKING_STATE_RUN; // release block

			//FIXME signal to UI

			break;
		}
		case JOB_TYPE_REPLY_BUNDLE_LOAD:
		{
			//printf("app: bundle loaded\n");

			assert(app->block_state == BLOCKING_STATE_WAIT);
			app->block_state = BLOCKING_STATE_RUN; // releae block
			assert(app->load_bundle == true);
			app->load_bundle = false; // for sp_app_bypassed

			// signal to UI
			size_t size = sizeof(transmit_bundle_load_t)
				+ lv2_atom_pad_size(strlen(job->uri) + 1);
			transmit_bundle_load_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transmit_bundle_load_fill(&app->regs, &app->forge, trans, size,
					job->status, job->uri);
				_sp_app_to_ui_advance(app, size);
			}

			break;
		}
		case JOB_TYPE_REPLY_BUNDLE_SAVE:
		{
			//printf("app: bundle saved\n");

			assert(app->block_state == BLOCKING_STATE_WAIT);
			app->block_state = BLOCKING_STATE_RUN; // release block
			assert(app->load_bundle == false);

			// signal to UI
			size_t size = sizeof(transmit_bundle_save_t)
				+ lv2_atom_pad_size(strlen(job->uri) + 1);
			transmit_bundle_save_t *trans = _sp_app_to_ui_request(app, size);
			if(trans)
			{
				_sp_transmit_bundle_save_fill(&app->regs, &app->forge, trans, size,
					job->status, job->uri);
				_sp_app_to_ui_advance(app, size);
			}

			break;
		}
		case JOB_TYPE_REPLY_DRAIN:
		{
			assert(app->block_state == BLOCKING_STATE_DRAIN);
			app->block_state = BLOCKING_STATE_BLOCK;

			break;
		}
	}

	return advance_work[app->block_state];
}

void
sp_worker_from_app(sp_app_t *app, uint32_t len, const void *data)
{
	const job_t *job = ASSUME_ALIGNED(data);

	switch(job->request)
	{
		case JOB_TYPE_REQUEST_MODULE_SUPPORTED:
		{
			const int32_t status= _sp_app_mod_is_supported(app, job->uri) ? 1 : 0;

			// signal to app
			size_t job_size = sizeof(job_t) + strlen(job->uri) + 1;
			job_t *job1 = _sp_worker_to_app_request(app, job_size);
			if(job1)
			{
				job1->reply = JOB_TYPE_REPLY_MODULE_SUPPORTED;
				job1->status = status;
				memcpy(job1->uri, job->uri, strlen(job->uri) + 1);
				_sp_worker_to_app_advance(app, job_size);
			}
			break;
		}
		case JOB_TYPE_REQUEST_MODULE_ADD:
		{
			mod_t *mod = _sp_app_mod_add(app, job->uri, 0, 0);
			if(!mod)
				break; //TODO report

			// signal to app
			size_t job_size = sizeof(job_t);
			job_t *job1 = _sp_worker_to_app_request(app, job_size);
			if(job1)
			{
				job1->reply = JOB_TYPE_REPLY_MODULE_ADD;
				job1->mod = mod;
				_sp_worker_to_app_advance(app, job_size);
			}

			break;
		}
		case JOB_TYPE_REQUEST_MODULE_DEL:
		{
			const LV2_URID urn = job->mod->urn;
			int status = _sp_app_mod_del(app, job->mod);

			// signal to app
			size_t job_size = sizeof(job_t);
			job_t *job1 = _sp_worker_to_app_request(app, job_size);
			if(job1)
			{
				job1->reply = JOB_TYPE_REPLY_MODULE_DEL;
				job1->urn = urn;
				_sp_worker_to_app_advance(app, job_size);
			}

			break;
		}
		case JOB_TYPE_REQUEST_PRESET_LOAD:
		{
			int status = _sp_app_state_preset_load(app, job->mod, job->uri, true);
			(void)status; //FIXME check this

			// signal to app
			size_t job_size = sizeof(job_t);
			job_t *job1 = _sp_worker_to_app_request(app, job_size);
			if(job1)
			{
				job1->reply = JOB_TYPE_REPLY_PRESET_LOAD;
				job1->mod = job->mod;
				_sp_worker_to_app_advance(app, job_size);
			}

			break;
		}
		case JOB_TYPE_REQUEST_PRESET_SAVE:
		{
			int status = _sp_app_state_preset_save(app, job->mod, job->uri);
			(void)status; //FIXME check this

			// signal to app
			size_t job_size = sizeof(job_t);
			job_t *job1 = _sp_worker_to_app_request(app, job_size);
			if(job1)
			{
				job1->reply = JOB_TYPE_REPLY_PRESET_SAVE;
				job1->mod = job->mod;
				_sp_worker_to_app_advance(app, job_size);
			}

			break;
		}
		case JOB_TYPE_REQUEST_BUNDLE_LOAD:
		{
			int status = _sp_app_state_bundle_load(app, job->uri);

			// signal to app
			size_t job_size = sizeof(job_t) + strlen(job->uri) + 1;
			job_t *job1 = _sp_worker_to_app_request(app, job_size);
			if(job1)
			{
				job1->reply = JOB_TYPE_REPLY_BUNDLE_LOAD;
				job1->status = status;
				strcpy(job1->uri, job->uri);
				_sp_worker_to_app_advance(app, job_size);
			}

			break;
		}
		case JOB_TYPE_REQUEST_BUNDLE_SAVE:
		{
			int status = _sp_app_state_bundle_save(app, job->uri);

			// signal to app
			size_t job_size = sizeof(job_t) + strlen(job->uri) + 1;
			job_t *job1 = _sp_worker_to_app_request(app, job_size);
			if(job1)
			{
				job1->reply = JOB_TYPE_REPLY_BUNDLE_SAVE;
				job1->status = status;
				strcpy(job1->uri, job->uri);
				_sp_worker_to_app_advance(app, job_size);
			}

			break;
		}
		case JOB_TYPE_REQUEST_DRAIN:
		{
			// signal to app
			size_t job_size = sizeof(job_t);
			job_t *job1 = _sp_worker_to_app_request(app, job_size);
			if(job1)
			{
				job1->reply = JOB_TYPE_REPLY_DRAIN;
				job1->status = 0;
				_sp_worker_to_app_advance(app, job_size);
			}

			break;
		}
	}
}
