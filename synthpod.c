// include std headers
#include <stdio.h>

// include libuv header
#include <uv.h>

// include eina header
#include <Eina.h>

// include lilv header
#include <lilv/lilv.h>

// include lv2 core header
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>

// include lv2 extension headers
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>

#include <ext_urid.h>

int
main(int argc, char **argv)
{
	LilvWorld *world = NULL;
	const LilvPlugins *plugs = NULL;
	ext_urid_t *ext_urid = NULL;
	
	eina_init();

	world = lilv_world_new();
	lilv_world_load_all(world);
	plugs = lilv_world_get_all_plugins(world);

	ext_urid = ext_urid_new();
	ext_urid_map(ext_urid, LV2_ATOM__Object);

	LILV_FOREACH(plugins, itr, plugs)
	{
		const LilvPlugin *plug = lilv_plugins_get(plugs, itr);

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
			//const LilvNode *port_node = lilv_port_get_node(plug, port);
			const LilvNode *port_symbol_node = lilv_port_get_symbol(plug, port);
			const char *port_symbol_str = lilv_node_as_string(port_symbol_node);
			printf("\t\t%u: %s\n", i, port_symbol_str);
		}
	}

	const char *uri = ext_urid_unmap(ext_urid, 1);
	printf("%u %s\n", 1, uri);
	
	printf("%s %u\n",
		LV2_ATOM__Object,
		ext_urid_map(ext_urid, LV2_ATOM__Object));
	printf("%s %u\n",
		LV2_ATOM__Tuple,
		ext_urid_map(ext_urid, LV2_ATOM__Tuple));
	printf("%s %u\n",
		LV2_ATOM__Vector,
		ext_urid_map(ext_urid, LV2_ATOM__Vector));

	ext_urid_free(ext_urid);

	lilv_world_free(world);
	
	eina_shutdown();

	return 0;
}
