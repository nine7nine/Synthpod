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
	app->uris.input = lilv_new_uri(app->world, LV2_CORE__InputPort);
	app->uris.output = lilv_new_uri(app->world, LV2_CORE__OutputPort);
	app->uris.midi = lilv_new_uri(app->world, LV2_MIDI__MidiEvent);
	app->uris.osc = lilv_new_uri(app->world,
		"http://opensoundcontrol.org#OscEvent");
	
	app->urids.audio = ext_urid_map(app->ext_urid, LV2_CORE__AudioPort);
	app->urids.control = ext_urid_map(app->ext_urid, LV2_CORE__ControlPort);
	app->urids.cv = ext_urid_map(app->ext_urid, LV2_CORE__CVPort);
	app->urids.atom = ext_urid_map(app->ext_urid, LV2_ATOM__AtomPort);
	app->urids.input = ext_urid_map(app->ext_urid, LV2_CORE__InputPort);
	app->urids.output = ext_urid_map(app->ext_urid, LV2_CORE__OutputPort);
	app->urids.midi = ext_urid_map(app->ext_urid, LV2_MIDI__MidiEvent);
	app->urids.osc = ext_urid_map(app->ext_urid,
		"http://opensoundcontrol.org#OscEvent");

	app->mods = NULL;

	// populate features list
	app->feature_list[0].URI = LV2_URID__map;
	app->feature_list[0].data = ext_urid_map_get(app->ext_urid);
	
	app->feature_list[1].URI = LV2_URID__unmap;
	app->feature_list[1].data = ext_urid_unmap_get(app->ext_urid);
	
	for(int i=0; i<APP_NUM_FEATURES; i++)
		app->features[i] = &app->feature_list[i];
	app->features[APP_NUM_FEATURES] = NULL; // sentinel

	app->sample_rate = 48000; //TODO
	app->period_size = 64; //TODO
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

	lilv_node_free(app->uris.input);
	lilv_node_free(app->uris.output);

	lilv_node_free(app->uris.midi);
	lilv_node_free(app->uris.osc);

	ext_urid_free(app->ext_urid);

	lilv_world_free(app->world);

	free(app);
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

	mod_t *mod;
	EINA_INLIST_FOREACH(app->mods, mod)
	{
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

		// set output buffer capacity
		LV2_Atom_Sequence *seq = mod->bufs[1];
		seq->atom.size = app->seq_size;

		// run plugin
		lilv_instance_run(mod->inst, app->period_size);

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
	}
}

mod_t *
app_mod_add(app_t *app, const char *uri)
{
	LilvNode *uri_node = lilv_new_uri(app->world, uri);
	const LilvPlugin *plug = lilv_plugins_get_by_uri(app->plugs, uri_node);
	lilv_node_free(uri_node);
			
	if(!plug || !lilv_plugin_verify(plug))
		return NULL;

	mod_t *mod = calloc(1, sizeof(mod_t));

	mod->app = app;
	mod->plug = plug;
	mod->num_ports = lilv_plugin_get_num_ports(plug);
	mod->inst = lilv_plugin_instantiate(plug, app->sample_rate, app->features);
	lilv_instance_activate(mod->inst);
	
	mod->bufs = calloc(mod->num_ports, sizeof(void *));
	for(uint32_t i=0; i<mod->num_ports; i++)
	{
		size_t size = 0;
		const LilvPort *port = lilv_plugin_get_port_by_index(plug, i);

		if(  lilv_port_is_a(plug, port, app->uris.audio)
			|| lilv_port_is_a(plug, port, app->uris.cv) )
		{
			size = app->period_size * sizeof(float);
		}
		else if(lilv_port_is_a(plug, port, app->uris.control))
		{
			size = sizeof(float);
		}
		else if(lilv_port_is_a(plug, port, app->uris.atom))
		{
			if(lilv_port_supports_event(plug, port, app->uris.midi))
				; //TODO
			else if(lilv_port_supports_event(plug, port, app->uris.osc))
				; //TODO

			size = app->seq_size;
		}
		else
			; //TODO report error

		// allocate 8-byte aligned buffer
		posix_memalign(&mod->bufs[i], 8, size);
		memset(mod->bufs[i], 0x0, size);

		// set port buffer
		lilv_instance_connect_port(mod->inst, i, mod->bufs[i]);
	}

	lv2_atom_forge_init(&mod->forge, ext_urid_map_get(app->ext_urid));

	mod->varchunk = varchunk_new(8192);

	app->mods = eina_inlist_append(app->mods, EINA_INLIST_GET(mod));

	return mod;
}

void
app_mod_del(app_t *app, mod_t *mod)
{
	app->mods = eina_inlist_remove(app->mods, EINA_INLIST_GET(mod));

	varchunk_free(mod->varchunk);

	lilv_instance_deactivate(mod->inst);
	lilv_instance_free(mod->inst);

	for(uint32_t i=0; i<mod->num_ports; i++)
		free(mod->bufs[i]);
	free(mod->bufs);

	free(mod);
}
