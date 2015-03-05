/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <app.h>

// include lv2 core header
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>

// include lv2 extension headers
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

typedef struct _ui_write_t ui_write_t;

struct _ui_write_t {
	uint32_t size;
	uint32_t protocol;
	uint32_t port;
};

#define UI_WRITE_SIZE ( sizeof(ui_write_t) )
#define UI_WRITE_PADDED ( (sizeof(ui_write_t) + 7U) & (~7U) )

// rt
static void
_pacemaker_cb(uv_timer_t *pacemaker)
{
	app_t *app = pacemaker->data;

	mod_t *mod;
	EINA_INLIST_FOREACH(app->mods, mod)
	{
		// handle work
		if(mod->worker.iface)
		{
			const void *ptr;
			size_t toread;
			while( (ptr = varchunk_read_request(mod->worker.from, &toread)) )
			{
				if(mod->worker.iface->work_response)
					mod->worker.iface->work_response(mod->handle, toread, ptr);

				varchunk_read_advance(mod->worker.from);
			}
			if(mod->worker.iface->end_run)
				mod->worker.iface->end_run(mod->handle);
		}

		// handle ui pre
		if(mod->ui.ui)
		{
			const void *ptr;
			size_t toread;
			while( (ptr = varchunk_read_request(mod->ui.from, &toread)) )
			{
				const ui_write_t *ui_write = ptr;
				const void *body = ptr + UI_WRITE_PADDED;
				port_t *port = &mod->ports[ui_write->port];
				void *buf = port->buf;

				// check for input port
				if(port->direction != app->urids.input)
					continue;

				//TODO check for matching port protocol

				if( ((ui_write->protocol == app->urids.float_protocol)
					|| (ui_write->protocol == 0))
					&& (port->type == app->urids.control)
					&& (ui_write->size == sizeof(float)) )
				{
					const float *val = body;
					*(float *)buf = *val;
				}
				else if( (ui_write->protocol == app->urids.atom_transfer)
					&& (port->type == app->urids.atom) )
				{
					const LV2_Atom *atom = body;
					memcpy(buf, atom, sizeof(LV2_Atom) + atom->size);
				}
				else if( (ui_write->protocol == app->urids.event_transfer)
					&& (port->type == app->urids.atom)
					&& (port->buffer_type == app->urids.sequence) )
				{
					const LV2_Atom *atom = body;
					LV2_Atom_Sequence *seq = buf;

					// find last event in sequence
					LV2_Atom_Event *last = NULL;
					LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
						last = ev;

					void *ptr;
					if(last)
					{
						ptr = last;
						ptr += sizeof(LV2_Atom_Event) + last->body.size;
					}
					else
						ptr = LV2_ATOM_CONTENTS(LV2_Atom_Sequence, seq);

					// append event at end of sequence
					// TODO check for buffer overflow
					LV2_Atom_Event *new_last = ptr;
					new_last->time.frames = last ? last->time.frames : 0;
					memcpy(&new_last->body, atom, sizeof(LV2_Atom) + atom->size);
					seq->atom.size += sizeof(LV2_Atom_Event) + atom->size;
				}
				else
					; //ignore, protocol not supported

				varchunk_read_advance(mod->ui.from);
			}
		}

		/*
		// fill input buffer
		LV2_Atom_Forge_Frame frame;
		lv2_atom_forge_set_buffer(&mod->forge, mod->bufs[0], app->seq_size);
		lv2_atom_forge_sequence_head(&mod->forge, &frame, 0);
		{
			uint8_t msg [] = {
				'/', 'h', 'e', 'l',			'l', 'o', 0x0, 0x0,
				',', 's', 0x0, 0x0,			'w', 'o', 'r', 'l',
				'd', 0x0, 0x0, 0x0};

			lv2_atom_forge_frame_time(&mod->forge, 0);
			lv2_atom_forge_atom(&mod->forge, sizeof(msg), app->urids.osc);
			lv2_atom_forge_raw(&mod->forge, msg, sizeof(msg));
			lv2_atom_forge_pad(&mod->forge, sizeof(msg));
		}
		lv2_atom_forge_pop(&mod->forge, &frame);
		*/

		// set output buffer capacity
		//LV2_Atom_Sequence *seq = mod->bufs[1];

		//LV2_Atom_Sequence *seq = mod->bufs[0];
		//seq->atom.size = app->seq_size;

		// run plugin
		lilv_instance_run(mod->inst, app->period_size);
		
		// handle ui post
		if(mod->ui.ui)
		{
			for(int i=0; i<mod->num_ports; i++)
			{
				port_t *port = &mod->ports[i];

				if(port->protocol == 0)
					continue;

				if( (port->type == app->urids.control)
						&& (port->protocol == app->urids.float_protocol) )
				{
					float val = *(float *)port->buf;
					if(val != port->last)
					{
						// update last value
						port->last = val;

						// transfer single float
						void *ptr;
						size_t request = UI_WRITE_PADDED + sizeof(float);
						if( (ptr = varchunk_write_request(mod->ui.to, request)) )
						{
							ui_write_t *ui_write = ptr;
							ui_write->size = sizeof(float);
							ui_write->protocol = app->urids.float_protocol;
							ui_write->port = i;
							ptr += UI_WRITE_PADDED;

							*(float *)ptr = val;
							varchunk_write_advance(mod->ui.to, request);
						}
						else
							; //TODO
					}
				}
				else if(port->type == app->urids.atom)
				{
					const LV2_Atom *atom = port->buf;
					if(atom->size == 0)
						continue;

					if( (port->buffer_type == app->urids.sequence)
						&& (port->protocol == app->urids.event_transfer) )
					{
						// transfer each atom of sequence separately
						const LV2_Atom_Sequence *seq = port->buf;
						LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
						{
							void *ptr;
							const LV2_Atom *atom = &ev->body;
							size_t request = UI_WRITE_PADDED + sizeof(LV2_Atom) + atom->size;
							if( (ptr = varchunk_write_request(mod->ui.to, request)) )
							{
								ui_write_t *ui_write = ptr;
								ui_write->size = sizeof(LV2_Atom) + atom->size;
								ui_write->protocol = app->urids.event_transfer;
								ui_write->port = i;
								ptr += UI_WRITE_PADDED;

								memcpy(ptr, atom, sizeof(LV2_Atom) + atom->size);
								varchunk_write_advance(mod->ui.to, request);
							}
							else
								; //TODO
						}
					}
					else if(port->protocol == app->urids.atom_transfer)
					{
						// transfer whole atom
						void *ptr;
						size_t request = UI_WRITE_PADDED + sizeof(LV2_Atom) + atom->size;
						if( (ptr = varchunk_write_request(mod->ui.to, request)) )
						{
							ui_write_t *ui_write = ptr;
							ui_write->size = sizeof(LV2_Atom) + atom->size;
							ui_write->protocol = app->urids.atom_transfer;
							ui_write->port = i;
							ptr += UI_WRITE_PADDED;

							memcpy(ptr, atom, sizeof(LV2_Atom) + atom->size);
							varchunk_write_advance(mod->ui.to, request);
						}
						else
							; //TODO
					}
				}

				//TODO wake up UI thread

				//void *ptr;
				//size_t written = 0;
				//if( (ptr = varchunk_write_request(mod->ui.to, written)) )
				//{
				//	//TODO
				//	varchunk_write_advance(mod->ui.to, written);
				//}
			}
		}

		/*
		// read output buffer
		LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
		{
			const LV2_Atom *atom = LV2_ATOM_BODY_CONST(ev);
			printf("%u %u\n", atom->size, atom->type);
			const uint8_t *body = LV2_ATOM_BODY_CONST(atom);
			for(int i=0; i<atom->size; i++)
				printf("\t%02x %c\n",
					body[i],
					isprint(body[i]) ? body[i] : ' ');
		}
		*/
	}
}
	
static LV2_Worker_Status
_schedule_work(LV2_Worker_Schedule_Handle handle, uint32_t size, const void *data)
{
	mod_t *mod = handle;

	void *buf;
	if( (buf = varchunk_write_request(mod->worker.to, size)) )
	{
		// copy data to varchunk buffer
		memcpy(buf, data, size);
		varchunk_write_advance(mod->worker.to, size);

		// wake up worker thread
		uv_async_send(&mod->worker.async);

		return LV2_WORKER_SUCCESS;
	}

	return LV2_WORKER_ERR_NO_SPACE;
}

app_t *
app_new()
{
	app_t *app = calloc(1, sizeof(app_t));

	app->world = lilv_world_new();
	lilv_world_load_all(app->world);
	app->plugs = lilv_world_get_all_plugins(app->world);

	app->ext_urid = ext_urid_new();

	app->uris.audio = lilv_new_uri(app->world, LV2_CORE__AudioPort);
	app->uris.control = lilv_new_uri(app->world, LV2_CORE__ControlPort);
	app->uris.cv = lilv_new_uri(app->world, LV2_CORE__CVPort);
	app->uris.atom = lilv_new_uri(app->world, LV2_ATOM__AtomPort);
	app->uris.sequence = lilv_new_uri(app->world, LV2_ATOM__Sequence);
	app->uris.input = lilv_new_uri(app->world, LV2_CORE__InputPort);
	app->uris.output = lilv_new_uri(app->world, LV2_CORE__OutputPort);
	app->uris.midi = lilv_new_uri(app->world, LV2_MIDI__MidiEvent);
	app->uris.osc = lilv_new_uri(app->world,
		"http://opensoundcontrol.org#OscEvent");
	app->uris.chim_event = lilv_new_uri(app->world,
		"http://open-music-kontrollers.ch/lv2/chimaera#Event");
	app->uris.chim_dump = lilv_new_uri(app->world,
		"http://open-music-kontrollers.ch/lv2/chimaera#Dump");
	app->uris.work_schedule = lilv_new_uri(app->world, LV2_WORKER__schedule);
	app->uris.float_protocol = lilv_new_uri(app->world, LV2_UI_PREFIX"floatProtocol");
	app->uris.peak_protocol = lilv_new_uri(app->world, LV2_UI_PREFIX"peakProtocol");
	app->uris.atom_transfer = lilv_new_uri(app->world, LV2_ATOM__atomTransfer);
	app->uris.event_transfer = lilv_new_uri(app->world, LV2_ATOM__eventTransfer);
	app->uris.eo = lilv_new_uri(app->world, LV2_UI__EoUI);
	
	app->urids.audio = ext_urid_map(app->ext_urid, LV2_CORE__AudioPort);
	app->urids.control = ext_urid_map(app->ext_urid, LV2_CORE__ControlPort);
	app->urids.cv = ext_urid_map(app->ext_urid, LV2_CORE__CVPort);
	app->urids.atom = ext_urid_map(app->ext_urid, LV2_ATOM__AtomPort);
	app->urids.sequence = ext_urid_map(app->ext_urid, LV2_ATOM__Sequence);
	app->urids.input = ext_urid_map(app->ext_urid, LV2_CORE__InputPort);
	app->urids.output = ext_urid_map(app->ext_urid, LV2_CORE__OutputPort);
	app->urids.midi = ext_urid_map(app->ext_urid, LV2_MIDI__MidiEvent);
	app->urids.osc = ext_urid_map(app->ext_urid,
		"http://opensoundcontrol.org#OscEvent");
	app->urids.chim_event = ext_urid_map(app->ext_urid,
		"http://open-music-kontrollers.ch/lv2/chimaera#Event");
	app->urids.chim_dump = ext_urid_map(app->ext_urid,
		"http://open-music-kontrollers.ch/lv2/chimaera#Dump");
	app->urids.work_schedule = ext_urid_map(app->ext_urid, LV2_WORKER__schedule);
	app->urids.float_protocol = ext_urid_map(app->ext_urid, LV2_UI_PREFIX"floatProtocol");
	app->urids.peak_protocol = ext_urid_map(app->ext_urid, LV2_UI_PREFIX"peakProtocol");
	app->urids.atom_transfer = ext_urid_map(app->ext_urid, LV2_ATOM__atomTransfer);
	app->urids.event_transfer = ext_urid_map(app->ext_urid, LV2_ATOM__eventTransfer);
	app->urids.eo = ext_urid_map(app->ext_urid, LV2_UI__EoUI);

	app->mods = NULL;

	app->sample_rate = 32000; //TODO
	app->period_size = 32; //TODO
	app->seq_size = 0x2000; //TODO

	return app;
}

void
app_free(app_t *app)
{
	lilv_node_free(app->uris.audio);
	lilv_node_free(app->uris.control);
	lilv_node_free(app->uris.cv);
	lilv_node_free(app->uris.atom);
	lilv_node_free(app->uris.sequence);

	lilv_node_free(app->uris.input);
	lilv_node_free(app->uris.output);

	lilv_node_free(app->uris.midi);
	lilv_node_free(app->uris.osc);
	
	lilv_node_free(app->uris.chim_event);
	lilv_node_free(app->uris.chim_dump);
	
	lilv_node_free(app->uris.work_schedule);

	lilv_node_free(app->uris.float_protocol);
	lilv_node_free(app->uris.peak_protocol);
	lilv_node_free(app->uris.atom_transfer);
	lilv_node_free(app->uris.event_transfer);

	lilv_node_free(app->uris.eo);

	ext_urid_free(app->ext_urid);

	lilv_world_free(app->world);

	free(app);
}

static void
_app_quit(uv_async_t *quit)
{
	app_t *app = quit->data;

	uv_close((uv_handle_t *)&app->quit, NULL);
	uv_timer_stop(&app->pacemaker);
}

static void
_app_thread(void *arg)
{
	app_t *app = arg;

	app->loop = uv_loop_new();
	
	app->quit.data = app;
	uv_async_init(app->loop, &app->quit, _app_quit);

	app->pacemaker.data = app;
	uv_timer_init(app->loop, &app->pacemaker);
	uv_timer_start(&app->pacemaker, _pacemaker_cb, 0, 1);

	uv_run(app->loop, UV_RUN_DEFAULT);
}

void
app_run(app_t *app)
{
	/*
	// test plugin discovery
	LILV_FOREACH(plugins, itr, app->plugs)
	{
		const LilvPlugin *plug = lilv_plugins_get(app->plugs, itr);

		const LilvNode *bndl_uri_node = lilv_plugin_get_bundle_uri(plug);
		const char *bndl_uri_uri = lilv_node_as_uri(bndl_uri_node);
		const char *bndl_uri_path = lilv_uri_to_path(bndl_uri_uri);

		const LilvNode *plug_uri_node = lilv_plugin_get_uri(plug);
		const char *plug_uri_uri = lilv_node_as_uri(plug_uri_node);

		uint32_t num_ports = lilv_plugin_get_num_ports(plug);
		printf("[%s]\n\t%s\n\t%s\n\t%u\n",
			lilv_plugin_verify(plug) ? "PASS" : "FAIL",
			plug_uri_uri,
			bndl_uri_path,
			num_ports);

		for(uint32_t i=0; i<num_ports; i++)
		{
			const LilvPort *port = lilv_plugin_get_port_by_index(plug, i);
			if(!lilv_port_is_a(plug, port, app->uris.atom_uri)
				|| !lilv_port_supports_event(plug, port, app->uris.osc_uri))
				continue;
			const LilvNode *port_symbol_node = lilv_port_get_symbol(plug, port);
			const char *port_symbol_str = lilv_node_as_string(port_symbol_node);
			printf("\t\t%u: %s\n", i, port_symbol_str);
		}
	}

	// test URID map/unmap
	ext_urid_map(app->ext_urid, LV2_ATOM__Object);

	const char *uri = ext_urid_unmap(app->ext_urid, 1);
	printf("%u %s\n", 1, uri);
	
	printf("%s %u\n",
		LV2_ATOM__Object,
		ext_urid_map(app->ext_urid, LV2_ATOM__Object));
	printf("%s %u\n",
		LV2_ATOM__Tuple,
		ext_urid_map(app->ext_urid, LV2_ATOM__Tuple));
	printf("%s %u\n",
		LV2_ATOM__Vector,
		ext_urid_map(app->ext_urid, LV2_ATOM__Vector));
	*/

	uv_thread_create(&app->thread, _app_thread, app);
}

void app_stop(app_t *app)
{
	uv_async_send(&app->quit);
	uv_thread_join(&app->thread);
}

static void
_mod_worker_quit(uv_async_t *quit)
{
	mod_t *mod = quit->data;

	uv_close((uv_handle_t *)&mod->worker.quit, NULL);
	uv_close((uv_handle_t *)&mod->worker.async, NULL);
}

// non-rt
static LV2_Worker_Status
_mod_worker_respond(LV2_Worker_Respond_Handle handle, uint32_t size,
	const void *data)
{
	mod_t *mod = handle;
	void *ptr;

	if( (ptr = varchunk_write_request(mod->worker.from, size)) )
	{
		memcpy(ptr, data, size);
		varchunk_write_advance(mod->worker.from, size);

		return LV2_WORKER_SUCCESS;
	}

	return LV2_WORKER_ERR_NO_SPACE;
}

// non-rt
static void
_mod_worker_wakeup(uv_async_t *async)
{
	mod_t *mod = async->data;
	const void *ptr;
	size_t toread;
		
	while( (ptr = varchunk_read_request(mod->worker.to, &toread)) )
	{
		if(mod->worker.iface->work)
			mod->worker.iface->work(mod->handle, _mod_worker_respond, mod, toread, ptr);
		varchunk_read_advance(mod->worker.to);
	}
}

// non-rt
static void
_mod_worker_thread(void *arg)
{
	mod_t *mod = arg;

	uv_loop_t *loop = uv_loop_new();
	
	mod->worker.quit.data = mod;
	uv_async_init(loop, &mod->worker.quit, _mod_worker_quit);

	mod->worker.async.data = mod;
	uv_async_init(loop, &mod->worker.async, _mod_worker_wakeup);

	uv_run(loop, UV_RUN_DEFAULT);
}

// non-rt ui-thread
static void
_ui_write_function(LV2UI_Controller controller, uint32_t port,
	uint32_t size, uint32_t protocol, const void *buffer)
{
	mod_t *mod = controller;

	ui_write_t header = {
		.size = size,
		.protocol = protocol,
		.port = port
	};

	const size_t padded = UI_WRITE_PADDED + size;

	void *ptr;
	if( (ptr = varchunk_write_request(mod->ui.from, padded)) )
	{
		memcpy(ptr, &header, UI_WRITE_SIZE);
		memcpy(ptr + UI_WRITE_PADDED, buffer, size);
		varchunk_write_advance(mod->ui.from, padded);
	}
	else
		fprintf(stderr, "_ui_write_function: buffer overflow\n");
}

mod_t *
app_mod_add(app_t *app, const char *uri)
{
	LilvNode *uri_node = lilv_new_uri(app->world, uri);
	const LilvPlugin *plug = lilv_plugins_get_by_uri(app->plugs, uri_node);
	lilv_node_free(uri_node);

	const LilvNode *plugin_uri = lilv_plugin_get_uri(plug);
	const char *plugin_string = lilv_node_as_string(plugin_uri);
	printf("plugin_string: %s\n", plugin_string);
			
	if(!plug || !lilv_plugin_verify(plug))
		return NULL;

	mod_t *mod = calloc(1, sizeof(mod_t));

	// populate worker schedule
	mod->worker.schedule.handle = mod;
	mod->worker.schedule.schedule_work = _schedule_work;

	// populate features list
	mod->feature_list[0].URI = LV2_URID__map;
	mod->feature_list[0].data = ext_urid_map_get(app->ext_urid);
	mod->ui_feature_list[0].URI = LV2_URID__map;
	mod->ui_feature_list[0].data = ext_urid_map_get(app->ext_urid);
	
	mod->feature_list[1].URI = LV2_URID__unmap;
	mod->feature_list[1].data = ext_urid_unmap_get(app->ext_urid);
	mod->ui_feature_list[1].URI = LV2_URID__unmap;
	mod->ui_feature_list[1].data = ext_urid_unmap_get(app->ext_urid);
	
	mod->feature_list[2].URI = LV2_WORKER__schedule;
	mod->feature_list[2].data = &mod->worker.schedule;

	mod->ui_feature_list[2].URI = LV2_UI__parent;
	mod->ui_feature_list[2].data = app->ui.box;
	
	for(int i=0; i<NUM_FEATURES; i++)
		mod->features[i] = &mod->feature_list[i];
	mod->features[NUM_FEATURES] = NULL; // sentinel
	
	for(int i=0; i<NUM_UI_FEATURES; i++)
		mod->ui_features[i] = &mod->ui_feature_list[i];
	mod->ui_features[NUM_UI_FEATURES] = NULL; // sentinel
		
	mod->app = app;
	mod->plug = plug;
	mod->num_ports = lilv_plugin_get_num_ports(plug);
	if(lilv_plugin_has_feature(mod->plug, app->uris.work_schedule))
	{
		mod->worker.to = varchunk_new(8192);
		mod->worker.from = varchunk_new(8192);
		uv_thread_create(&mod->worker.thread, _mod_worker_thread, mod);
	}
	mod->inst = lilv_plugin_instantiate(plug, app->sample_rate, mod->features);
	mod->handle = lilv_instance_get_handle(mod->inst),
	mod->worker.iface = lilv_instance_get_extension_data(mod->inst,
		LV2_WORKER__interface);
	lilv_instance_activate(mod->inst);

	mod->ports = calloc(mod->num_ports, sizeof(port_t));
	for(uint32_t i=0; i<mod->num_ports; i++)
	{
		port_t *tar = &mod->ports[i];
		size_t size = 0;
		const LilvPort *port = lilv_plugin_get_port_by_index(plug, i);

		if(lilv_port_is_a(plug, port, app->uris.audio))
		{
			size = app->period_size * sizeof(float);
			tar->type =  app->urids.cv;
		}
		else if(lilv_port_is_a(plug, port, app->uris.cv))
		{
			size = app->period_size * sizeof(float);
			tar->type = app->urids.cv;
		}
		else if(lilv_port_is_a(plug, port, app->uris.control))
		{
			size = sizeof(float);
			tar->type = app->urids.control;
			tar->protocol = app->urids.float_protocol;
		}
		else if(lilv_port_is_a(plug, port, app->uris.atom))
		{
			size = app->seq_size;
			tar->type = app->urids.atom;
			tar->buffer_type = lilv_port_is_a(plug, port, app->uris.sequence)
				? app->urids.sequence
				: 0;
		}
		else
			tar->type = 0; // ignored

		tar->direction = lilv_port_is_a(plug, port, app->uris.input)
			? app->urids.input
			: app->urids.output;

		// allocate 8-byte aligned buffer
		posix_memalign(&tar->buf, 8, size); //TODO mlock
		memset(tar->buf, 0x0, size);

		// set port buffer
		lilv_instance_connect_port(mod->inst, i, tar->buf);
	}

	//ui
	LilvUIs *all_uis = lilv_plugin_get_uis(mod->plug);
	LILV_FOREACH(uis, ptr, all_uis)
	{
		const LilvUI *ui = lilv_uis_get(all_uis, ptr);
		if(lilv_ui_is_a(ui, app->uris.eo))
		{
			mod->ui.ui = ui;
			break;
		}
	}
	lilv_uis_free(all_uis);

	if(mod->ui.ui)
	{
		LV2UI_DescriptorFunction ui_descfunc = NULL;
		const char *path = NULL;

		printf("has Eo UI\n");
		const LilvNode *ui_uri = lilv_ui_get_uri(mod->ui.ui);
		const LilvNode *bundle_uri = lilv_ui_get_bundle_uri(mod->ui.ui);
		const LilvNode *binary_uri = lilv_ui_get_binary_uri(mod->ui.ui);

		const char *ui_string = lilv_node_as_string(ui_uri);
		const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));
		const char *binary_path = lilv_uri_to_path(lilv_node_as_string(binary_uri));

		printf("ui_string: %s\n", ui_string);
		printf("bundle_path: %s\n", bundle_path);
		printf("binary_path: %s\n", binary_path);

		uv_dlopen(binary_path, &mod->ui.lib); //TODO check
		uv_dlsym(&mod->ui.lib, "lv2ui_descriptor", (void **)&ui_descfunc);

		if(ui_descfunc)
		{
			mod->ui.descriptor = NULL;

			for(int i=0; 1; i++)
			{
				const LV2UI_Descriptor *ui_desc = ui_descfunc(i);
				if(!ui_desc) // end
					break;
				else if(!strcmp(ui_desc->URI, ui_string))
				{
					mod->ui.descriptor = ui_desc;
					break;
				}
			}

			if(mod->ui.descriptor && mod->ui.descriptor->instantiate)
			{
				mod->ui.to = varchunk_new(8192);
				mod->ui.from = varchunk_new(8192);

				mod->ui.handle = mod->ui.descriptor->instantiate(
					mod->ui.descriptor,
					plugin_string,
					bundle_path,
					_ui_write_function,
					mod,
					(void **)&(mod->ui.widget),
					mod->ui_features);

				if(mod->ui.handle && mod->ui.widget)
				{
					elm_box_pack_end(app->ui.box, mod->ui.widget);
				}
			}
		}
	}

	lv2_atom_forge_init(&mod->forge, ext_urid_map_get(app->ext_urid));

	app->mods = eina_inlist_append(app->mods, EINA_INLIST_GET(mod));

	return mod;
}

void
app_mod_del(app_t *app, mod_t *mod)
{
	app->mods = eina_inlist_remove(app->mods, EINA_INLIST_GET(mod));

	if(mod->ui.ui)
	{
		if(mod->ui.widget)
			elm_box_unpack(app->ui.box, mod->ui.widget);

		if(mod->ui.descriptor && mod->ui.descriptor->cleanup && mod->ui.handle)
			mod->ui.descriptor->cleanup(mod->ui.handle);

		varchunk_free(mod->ui.to);
		varchunk_free(mod->ui.from);

		uv_dlclose(&mod->ui.lib);
	}

	// deinit worker
	if(mod->worker.iface)
	{
		uv_async_send(&mod->worker.quit);
		uv_thread_join(&mod->worker.thread);
		varchunk_free(mod->worker.to);
		varchunk_free(mod->worker.from);
	}

	// deinit instance
	lilv_instance_deactivate(mod->inst);
	lilv_instance_free(mod->inst);

	// deinit ports
	for(uint32_t i=0; i<mod->num_ports; i++)
		free(mod->ports[i].buf);
	free(mod->ports);

	free(mod);
}
