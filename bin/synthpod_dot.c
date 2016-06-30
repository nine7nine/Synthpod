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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include <synthpod_app.h>

#include <sratom/sratom.h>
#include <symap.h>

#define CUINT8(str) ((const uint8_t *)(str))

typedef struct _prog_t prog_t;

struct _prog_t {
	Symap *symap;
	Sratom *sratom;
	LV2_URID_Map map;
	LV2_URID_Unmap unmap;
};

static uint32_t
_map(void *data, const char *uri)
{
	prog_t *prog = data;

	return symap_map(prog->symap, uri);
}

static const char *
_unmap(void *data, uint32_t urid)
{
	prog_t *prog = data;

	return symap_unmap(prog->symap, urid);
}

static inline void
_prog_init(prog_t *prog)
{
	prog->symap = symap_new();

	prog->map.handle = prog;
	prog->map.map = _map;

	prog->unmap.handle = prog;
	prog->unmap.unmap = _unmap;

	prog->sratom = sratom_new(&prog->map);
}

static inline void
_prog_deinit(prog_t *prog)
{
	sratom_free(prog->sratom);
	symap_free(prog->symap);
}

//FIXME is duplicate code from <synthpod_app_state.c>
static inline LV2_Atom_Object *
_deserialize_from_turtle(Sratom *sratom, LV2_URID_Unmap *unmap, const char *path)
{
	LV2_Atom_Object *obj = NULL;

	FILE *f = fopen(path, "rb");
	if(f)
	{
		fseek(f, 0, SEEK_END);
		long fsize = ftell(f);
		fseek(f, 0, SEEK_SET);

		char *ttl = malloc(fsize + 1);
		if(ttl)
		{
			if(fread(ttl, fsize, 1, f) == 1)
			{
				ttl[fsize] = 0;

				const char* base_uri = "file:///tmp/base/";

				SerdNode s = serd_node_from_string(SERD_URI, CUINT8(""));
				SerdNode p = serd_node_from_string(SERD_URI, CUINT8(LV2_STATE__state));
				obj = (LV2_Atom_Object *)sratom_from_turtle(sratom, base_uri, &s, &p, ttl);
			}

			free(ttl);
		}
	}

	return obj;
}

static inline void
_prog_run(prog_t *prog, const char *path)
{
	const LV2_URID synthpod__graph = prog->map.map(prog->map.handle, SYNTHPOD_PREFIX"graph");
	const LV2_URID lv2__index = prog->map.map(prog->map.handle, LV2_CORE__index);
	const LV2_URID lv2__symbol = prog->map.map(prog->map.handle, LV2_CORE__symbol);
	const LV2_URID lv2__port = prog->map.map(prog->map.handle, LV2_CORE__port);
	const LV2_URID lv2__Port = prog->map.map(prog->map.handle, LV2_CORE__Port);

	printf(
		"digraph G {\n"
		"  graph [\n"
		"    rankdir=TD,\n"
		"    compound=true,\n"
		"    overlap=scale,\n"
		"    remincross=true\n"
		"  ];");

	LV2_Atom_Object *obj = _deserialize_from_turtle(prog->sratom, &prog->unmap, path);
	if(obj)
	{
		const LV2_Atom_Tuple *graph = NULL;
		lv2_atom_object_get(obj, synthpod__graph, &graph, NULL);
		if(graph)
		{
			/* FIXME cluster connections are only valid for fdp
			printf("  ");
			bool once = true;
			LV2_ATOM_TUPLE_FOREACH(graph, iter)
			{
				const LV2_Atom_Object *mod_obj = (const LV2_Atom_Object *)iter;

				const LV2_Atom_Int *index = NULL;
				lv2_atom_object_get(mod_obj, lv2__index, &index, NULL);

				const char *mod_uri_str = prog->unmap.unmap(prog->unmap.handle, mod_obj->body.otype);

				if(!index || !mod_uri_str)
					continue;

				if(once)
					once = false;
				else
					printf(" -> ");

				printf("cluster_%i", index->body);
			}
			printf(";\n");
			*/

			LV2_ATOM_TUPLE_FOREACH(graph, iter)
			{
				const LV2_Atom_Object *mod_obj = (const LV2_Atom_Object *)iter;

				const LV2_Atom_Int *index = NULL;
				lv2_atom_object_get(mod_obj, lv2__index, &index, NULL);

				const char *mod_uri_str = prog->unmap.unmap(prog->unmap.handle, mod_obj->body.otype);

				if(!index || !mod_uri_str)
					continue;

				printf(
					"  subgraph cluster_%i {\n"
					"  graph [\n"
					"    style=filled,\n"
					"    label=\"(#%i) %s\",\n"
					"    rankdir=LR,\n"
					"    compound=true,\n"
					"    overlap=scale,\n"
					"    remincross=true\n"
					"  ];\n",
					index->body, index->body, mod_uri_str);

				bool first = true;
				LV2_ATOM_OBJECT_FOREACH(mod_obj, item)
				{
					const LV2_Atom_Object *port_obj = (const LV2_Atom_Object *)&item->value;

					if(  (item->key != lv2__port)
						|| (port_obj->body.otype != lv2__Port) )
						continue;

					const LV2_Atom_String *port_symbol = NULL;
					lv2_atom_object_get(port_obj, lv2__symbol, &port_symbol, NULL);

					if(!port_symbol)
						continue;

					printf("    _%i_%s [shape=plaintext, label=\"%s\"];\n",
						index->body, LV2_ATOM_BODY_CONST(port_symbol), LV2_ATOM_BODY_CONST(port_symbol));
				}
				printf("  }\n");

				LV2_ATOM_OBJECT_FOREACH(mod_obj, item)
				{
					const LV2_Atom_Object *port_obj = (const LV2_Atom_Object *)&item->value;

					if(  (item->key != lv2__port)
						|| (port_obj->body.otype != lv2__Port) )
						continue;

					const LV2_Atom_String *port_symbol = NULL;
					lv2_atom_object_get(port_obj, lv2__symbol, &port_symbol, NULL);

					if(!port_symbol)
						continue;

					LV2_ATOM_OBJECT_FOREACH(port_obj, sub)
					{
						const LV2_Atom_Object *source_obj = (const LV2_Atom_Object *)&sub->value;

						if(  (sub->key != lv2__port)
							|| (source_obj->body.otype != lv2__Port) )
							continue;

						const LV2_Atom_String *source_symbol = NULL;
						const LV2_Atom_Int *source_index = NULL;
						lv2_atom_object_get(source_obj, lv2__symbol, &source_symbol, lv2__index, &source_index, NULL);

						if(!source_symbol || !source_index)
							continue;

						printf("  _%i_%s -> _%i_%s [color=red];\n",
							source_index->body, LV2_ATOM_BODY_CONST(source_symbol),
							index->body, LV2_ATOM_BODY_CONST(port_symbol));
					}
				}
			}
		}
		free(obj);
	}

	printf("}\n");
}

int
main(int argc, char **argv)
{
	static prog_t prog;

	_prog_init(&prog);

	if(argc > 1)
	{
		_prog_run(&prog, argv[1]);
	}

	_prog_deinit(&prog);

	return 0;
}
