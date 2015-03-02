#include <stdio.h>

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

	app->uris.audio_uri = lilv_new_uri(app->world, LV2_CORE__AudioPort);
	app->uris.control_uri = lilv_new_uri(app->world, LV2_CORE__ControlPort);
	app->uris.cv_uri = lilv_new_uri(app->world, LV2_CORE__CVPort);
	app->uris.atom_uri = lilv_new_uri(app->world, LV2_ATOM__AtomPort);
	app->uris.input_uri = lilv_new_uri(app->world, LV2_CORE__InputPort);
	app->uris.output_uri = lilv_new_uri(app->world, LV2_CORE__OutputPort);
	app->uris.midi_uri = lilv_new_uri(app->world, LV2_MIDI__MidiEvent);
	app->uris.osc_uri = lilv_new_uri(app->world,
		"http://opensoundcontrol.org#OscEvent");
	
	app->urids.audio_urid = ext_urid_map(app->ext_urid, LV2_CORE__AudioPort);
	app->urids.control_urid = ext_urid_map(app->ext_urid, LV2_CORE__ControlPort);
	app->urids.cv_urid = ext_urid_map(app->ext_urid, LV2_CORE__CVPort);
	app->urids.atom_urid = ext_urid_map(app->ext_urid, LV2_ATOM__AtomPort);
	app->urids.input_urid = ext_urid_map(app->ext_urid, LV2_CORE__InputPort);
	app->urids.output_urid = ext_urid_map(app->ext_urid, LV2_CORE__OutputPort);
	app->urids.midi_urid = ext_urid_map(app->ext_urid, LV2_MIDI__MidiEvent);
	app->urids.osc_urid = ext_urid_map(app->ext_urid,
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

	return app;
}

void
app_free(app_t *app)
{
	lilv_node_free(app->uris.audio_uri);
	lilv_node_free(app->uris.control_uri);
	lilv_node_free(app->uris.cv_uri);
	lilv_node_free(app->uris.atom_uri);

	lilv_node_free(app->uris.input_uri);
	lilv_node_free(app->uris.output_uri);

	lilv_node_free(app->uris.midi_uri);
	lilv_node_free(app->uris.osc_uri);

	ext_urid_free(app->ext_urid);

	lilv_world_free(app->world);

	free(app);
}

void
app_run(app_t *app)
{
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
}
