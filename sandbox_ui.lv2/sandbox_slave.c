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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <string.h>
#include <ctype.h>

#include <sandbox_slave.h>
#include <sandbox_io.h>

#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/options/options.h>
#include <lv2/lv2plug.in/ns/ext/uri-map/uri-map.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#include <lilv/lilv.h>

#define MAPPER_IMPLEMENTATION
#include <mapper.lv2/mapper.h>

struct _sandbox_slave_t {
	mapper_t *mapper;

	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	LV2_URI_Map_Feature uri_id;

	LV2_URID log_trace;
	LV2_URID log_error;
	LV2_URID log_warning;
	LV2_URID log_note;

	LV2_Log_Log log;

	LV2UI_Port_Map port_map;
	LV2UI_Port_Subscribe port_subscribe;

	LV2UI_Resize host_resize;

	LilvWorld *world;
	LilvNode *plugin_bundle_node;
	LilvNode *ui_bundle_node;
	LilvNode *plugin_node;
	LilvNode *ui_node;

	const LilvPlugin *plug;
	LilvUIs *uis;
	const LilvUI *ui;

	bool no_user_resize;
	void *lib;
	const LV2UI_Descriptor *desc;
	void *handle;

	sandbox_io_t io;

	const sandbox_slave_driver_t *driver;
	void *data;

	bool initialized;

	const char *plugin_urn;
	const char *plugin_uri;
	const char *plugin_bundle_path;
	const char *ui_uri;
	const char *ui_bundle_path;
	const char *socket_path;
	const char *window_title;
	float sample_rate;
	float update_rate;
};

#define ANSI_COLOR_BOLD    "\x1b[1m"
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

enum {
	COLOR_TRACE = 0,
	COLOR_LOG,
	COLOR_ERROR,
	COLOR_NOTE,
	COLOR_WARNING,

	COLOR_UI,
	COLOR_URN1,
	COLOR_URN2
};

static const char *prefix [2][8] = {
	[0] = {
		[COLOR_TRACE]   = "[Trace]",
		[COLOR_LOG]     = "[Log]  ",
		[COLOR_ERROR]   = "[Error]",
		[COLOR_NOTE]    = "[Note] ",
		[COLOR_WARNING] = "[Warn] ",

		[COLOR_UI]     = "(UI) ",
		[COLOR_URN1]   = "{",
		[COLOR_URN2]   = "}"
	},
	[1] = {
		[COLOR_TRACE]   = "["ANSI_COLOR_BLUE   "Trace"ANSI_COLOR_RESET"]",
		[COLOR_LOG]     = "["ANSI_COLOR_MAGENTA"Log"ANSI_COLOR_RESET"]  ",
		[COLOR_ERROR]   = "["ANSI_COLOR_RED    "Error"ANSI_COLOR_RESET"]",
		[COLOR_NOTE]    = "["ANSI_COLOR_GREEN  "Note"ANSI_COLOR_RESET"] ",
		[COLOR_WARNING] = "["ANSI_COLOR_YELLOW "Warn"ANSI_COLOR_RESET"] ",

		[COLOR_UI]      = "("ANSI_COLOR_MAGENTA"UI"ANSI_COLOR_RESET") ",
		[COLOR_URN1]    = "{"ANSI_COLOR_BOLD,
		[COLOR_URN2]    = ANSI_COLOR_RESET"}",
	}
};
	
static inline int
_log_vprintf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, va_list args)
{
	sandbox_slave_t *sb = handle;

	int idx = COLOR_LOG;
	if(type == sb->log_trace)
		idx = COLOR_TRACE;
	else if(type == sb->log_error)
		idx = COLOR_ERROR;
	else if(type == sb->log_note)
		idx = COLOR_NOTE;
	else if(type == sb->log_warning)
		idx = COLOR_WARNING;

	char *buf;
	if(vasprintf(&buf, fmt, args) == -1)
		buf = NULL;

	if(buf)
	{
		const int istty = isatty(STDERR_FILENO);
		const char *sep = "\n";
		for(char *bufp = buf, *pch = strsep(&bufp, sep);
			pch;
			pch = strsep(&bufp, sep) )
		{
			if(strlen(pch))
			{
				fprintf(stderr, "%s %s ", prefix[istty][COLOR_UI], prefix[istty][idx]);
				if(sb->plugin_urn)
					fprintf(stderr, "%s%s%s ", prefix[istty][COLOR_URN1], sb->plugin_urn, prefix[istty][COLOR_URN2]);
				fprintf(stderr, "%s\n", pch);
			}
		}

		free(buf);
	}

	return 0;
}

static inline int __attribute__((format(printf, 3, 4)))
_log_printf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
	const int ret = _log_vprintf(handle, type, fmt, args);
  va_end(args);

	return ret;
}

static inline uint32_t
_port_index(LV2UI_Feature_Handle handle, const char *symbol)
{
	sandbox_slave_t *sb = handle;
	uint32_t index = LV2UI_INVALID_PORT_INDEX;

	LilvNode *symbol_uri = lilv_new_string(sb->world, symbol);
	if(symbol_uri)
	{
		const LilvPort *port = lilv_plugin_get_port_by_symbol(sb->plug, symbol_uri);

		if(port)
			index = lilv_port_get_index(sb->plug, port);

		lilv_node_free(symbol_uri);
	}

	return index;
}

static inline void
_write_function(LV2UI_Controller controller, uint32_t index,
	uint32_t size, uint32_t protocol, const void *buf)
{
	sandbox_slave_t *sb = controller;

	const int status = _sandbox_io_send(&sb->io, index, size, protocol, buf);
	(void)status; //TODO
}

static inline uint32_t
_port_subscribe(LV2UI_Feature_Handle handle, uint32_t index, uint32_t protocol,
	const LV2_Feature *const *features)
{
	sandbox_slave_t *sb = handle;

	const sandbox_io_subscription_t sub = {
		.state = 1,
		.protocol = protocol
	};
	_write_function(handle, index, sizeof(sandbox_io_subscription_t), sb->io.ui_port_subscribe, &sub);

	return 0;
}

static inline uint32_t
_port_unsubscribe(LV2UI_Feature_Handle handle, uint32_t index, uint32_t protocol,
	const LV2_Feature *const *features)
{
	sandbox_slave_t *sb = handle;

	const sandbox_io_subscription_t sub = {
		.state = 0,
		.protocol = protocol
	};
	_write_function(handle, index, sizeof(sandbox_io_subscription_t), sb->io.ui_port_subscribe, &sub);

	return 0;
}

static inline bool
_sandbox_recv_cb(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	sandbox_slave_t *sb = handle;

	if(sb->desc && sb->desc->port_event)
		sb->desc->port_event(sb->handle, index, size, protocol, buf);

	return true; // continue handling messages
}

static uint32_t
_sb_uri_to_id(LV2_URI_Map_Callback_Data handle, const char *map, const char *uri)
{
	sandbox_slave_t *sb = handle;
	(void)map;

	return sb->map->map(sb->map->handle, uri);
}

sandbox_slave_t *
sandbox_slave_new(int argc, char **argv, const sandbox_slave_driver_t *driver, void *data)
{
	sandbox_slave_t *sb = calloc(1, sizeof(sandbox_slave_t));
	if(!sb)
	{
		fprintf(stderr, "allocation failed\n");
		goto fail;
	}

	sb->plugin_urn = NULL;
	sb->window_title = "Untitled"; // fall-back
	sb->sample_rate = 44100.f; // fall-back
	sb->update_rate = 25.f; // fall-back

	int c;
	while((c = getopt(argc, argv, "n:p:P:u:U:s:w:r:f:")) != -1)
	{
		switch(c)
		{
			case 'n':
				sb->plugin_urn = optarg;
				break;
			case 'p':
				sb->plugin_uri = optarg;
				break;
			case 'P':
				sb->plugin_bundle_path = optarg;
				break;
			case 'u':
				sb->ui_uri = optarg;
				break;
			case 'U':
				sb->ui_bundle_path = optarg;
				break;
			case 's':
				sb->socket_path = optarg;
				break;
			case 'w':
				sb->window_title = optarg;
				break;
			case 'r':
				sb->sample_rate = atof(optarg);
				break;
			case 'f':
				sb->update_rate = atof(optarg);
				break;
			case '?':
				if( (optopt == 'n') || (optopt == 'p') || (optopt == 'P') || (optopt == 'u') || (optopt == 'U') || (optopt == 's') || (optopt == 'w') || (optopt == 'r') || (optopt == 'f') )
					fprintf(stderr, "Option `-%c' requires an argument.\n", optopt);
				else if(isprint(optopt))
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
				else
					fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
				goto fail;
			default:
				goto fail;
		}
	}

	if(  !sb->plugin_uri
		|| !sb->plugin_bundle_path
		|| !sb->ui_uri
		|| !sb->ui_bundle_path
		|| !sb->socket_path)
	{
		fprintf(stderr, "not enough arguments\n");
		goto fail;
	}

	sb->driver = driver;
	sb->data = data;

	sb->log.handle = sb;
	sb->log.printf = _log_printf;
	sb->log.vprintf = _log_vprintf;

	sb->port_map.handle = sb;
	sb->port_map.port_index = _port_index;

	sb->port_subscribe.handle = sb;
	sb->port_subscribe.subscribe = _port_subscribe;
	sb->port_subscribe.unsubscribe = _port_unsubscribe;

	sb->host_resize.handle = data;
	sb->host_resize.ui_resize = driver->resize_cb;

	if(!(sb->mapper = mapper_new(0x1000000, NULL, NULL, NULL))) // 16M
	{
		fprintf(stderr, "mapper_new failed\n");
		goto fail;
	}

	sb->map = mapper_get_map(sb->mapper);
	sb->unmap = mapper_get_unmap(sb->mapper);
	sb->uri_id.callback_data = sb;
	sb->uri_id.uri_to_id = _sb_uri_to_id;

	sb->log_trace = sb->map->map(sb->map->handle, LV2_LOG__Trace);
	sb->log_error = sb->map->map(sb->map->handle, LV2_LOG__Error);
	sb->log_warning = sb->map->map(sb->map->handle, LV2_LOG__Warning);
	sb->log_note = sb->map->map(sb->map->handle, LV2_LOG__Note);

	if(!(sb->world = lilv_world_new()))
	{
		fprintf(stderr, "lilv_world_new failed\n");
		goto fail;
	}

	sb->plugin_bundle_node = lilv_new_file_uri(sb->world, NULL, sb->plugin_bundle_path);
	if(strcmp(sb->plugin_bundle_path, sb->ui_bundle_path))
	{
		sb->ui_bundle_node = lilv_new_file_uri(sb->world, NULL, sb->ui_bundle_path);
	}

	sb->plugin_node = lilv_new_uri(sb->world, sb->plugin_uri);
	sb->ui_node = lilv_new_uri(sb->world, sb->ui_uri);

	if(!sb->plugin_bundle_node || !sb->plugin_node || !sb->ui_node)
	{
		fprintf(stderr, "lilv_new_uri failed\n");
		goto fail;
	}

	lilv_world_load_bundle(sb->world, sb->plugin_bundle_node);
	if(sb->ui_bundle_node)
	{
		lilv_world_load_bundle(sb->world, sb->ui_bundle_node);
	}

	lilv_world_load_resource(sb->world, sb->plugin_node);
	lilv_world_load_resource(sb->world, sb->ui_node);

	const LilvPlugins *plugins = lilv_world_get_all_plugins(sb->world);
	if(!plugins)
	{
		fprintf(stderr, "lilv_world_get_all_plugins failed\n");
		goto fail;
	}

	if(!(sb->plug = lilv_plugins_get_by_uri(plugins, sb->plugin_node)))
	{
		fprintf(stderr, "lilv_plugins_get_by_uri failed\n");
		goto fail;
	}

	sb->uis = lilv_plugin_get_uis(sb->plug);
	if(!sb->uis)
	{
		fprintf(stderr, "lilv_plugin_get_uis failed\n");
		goto fail;
	}

	if(!(sb->ui = lilv_uis_get_by_uri(sb->uis, sb->ui_node)))
	{
		fprintf(stderr, "lilv_uis_get_by_uri failed\n");
		goto fail;
	}

	const LilvNode *ui_path = lilv_ui_get_binary_uri(sb->ui);
	if(!ui_path)
	{
		fprintf(stderr, "lilv_ui_get_binary_uri failed\n");
		goto fail;
	}

	LilvNode *no_user_resize_uri = lilv_new_uri(sb->world, LV2_UI__noUserResize);
	if(no_user_resize_uri)
	{
		sb->no_user_resize = lilv_world_ask(sb->world, sb->ui_node, no_user_resize_uri, NULL);
		lilv_node_free(no_user_resize_uri);
	}

#if defined(LILV_0_22)
	char *binary_path = lilv_file_uri_parse(lilv_node_as_string(ui_path), NULL);
#else
	const char *binary_path = lilv_uri_to_path(lilv_node_as_string(ui_path));
#endif
	if(!(sb->lib = dlopen(binary_path, RTLD_LAZY | RTLD_LOCAL)))
	{
		fprintf(stderr, "dlopen failed: %s\n", dlerror());
		goto fail;
	}

#if defined(LILV_0_22)
	lilv_free(binary_path);
#endif

	LV2UI_DescriptorFunction desc_func = dlsym(sb->lib, "lv2ui_descriptor");
	if(!desc_func)
	{
		fprintf(stderr, "dlsym failed\n");
		goto fail;
	}

	for(int i=0; true; i++)
	{
		const LV2UI_Descriptor *desc = desc_func(i);
		if(!desc) // sentinel
			break;

		if(!strcmp(desc->URI, sb->ui_uri))
		{
			sb->desc = desc;
			break;
		}
	}

	if(!sb->desc)
	{
		fprintf(stderr, "LV2UI_Descriptor lookup failed\n");
		goto fail;
	}

	if(_sandbox_io_init(&sb->io, sb->map, sb->unmap, sb->socket_path, false, false))
	{
		fprintf(stderr, "_sandbox_io_init failed: are you sure that the host is running?\n");
		goto fail;
	}

	if(driver->init_cb && (driver->init_cb(sb, data) != 0) )
	{
		fprintf(stderr, "driver->init_cb failed\n");
		goto fail;
	}

	sb->initialized = true;
	return sb; // success

fail:
	sandbox_slave_free(sb);
	return NULL;
}

void
sandbox_slave_free(sandbox_slave_t *sb)
{
	if(!sb)
		return;

	if(sb->desc && sb->desc->cleanup && sb->handle)
		sb->desc->cleanup(sb->handle);

	if(sb->driver && sb->driver->deinit_cb && sb->initialized)
		sb->driver->deinit_cb(sb->data);

	_sandbox_io_deinit(&sb->io, false);

	if(sb->lib)
		dlclose(sb->lib);

	if(sb->world)
	{
		if(sb->uis)
		{
			lilv_uis_free(sb->uis);
		}

		if(sb->ui_node)
		{
			lilv_world_unload_resource(sb->world, sb->ui_node);
			lilv_node_free(sb->ui_node);
		}

		if(sb->plugin_node)
		{
			lilv_world_unload_resource(sb->world, sb->plugin_node);
			lilv_node_free(sb->plugin_node);
		}

		if(sb->ui_bundle_node)
		{
			lilv_world_unload_bundle(sb->world, sb->ui_bundle_node);
			lilv_node_free(sb->ui_bundle_node);
		}

		if(sb->plugin_bundle_node)
		{
			lilv_world_unload_bundle(sb->world, sb->plugin_bundle_node);
			lilv_node_free(sb->plugin_bundle_node);
		}

		lilv_world_free(sb->world);
	}

	if(sb->mapper)
	{
		mapper_free(sb->mapper);
	}

	free(sb);
}

void *
sandbox_slave_instantiate(sandbox_slave_t *sb, const LV2_Feature *parent_feature, void *widget)
{
	LV2_Options_Option options [] = {
		[0] = {
			.context = LV2_OPTIONS_INSTANCE,
			.subject = 0,
			.key = sb->io.ui_window_title,
			.size = strlen(sb->plugin_uri) + 1,
			.type = sb->io.forge.String,
			.value = sb->plugin_uri
		},
		[1] = {
			.context = LV2_OPTIONS_INSTANCE,
			.subject = 0,
			.key = sb->io.params_sample_rate,
			.size = sizeof(float),
			.type = sb->io.forge.Float,
			.value = &sb->sample_rate
		},
		[2] = {
			.context = LV2_OPTIONS_INSTANCE,
			.subject = 0,
			.key = sb->io.ui_update_rate,
			.size = sizeof(float),
			.type = sb->io.forge.Float,
			.value = &sb->update_rate
		},
		[3] = {
			.key = 0,
			.value = NULL
		}
	};

	const LV2_Feature map_feature = {
		.URI = LV2_URID__map,
		.data = sb->map
	};
	const LV2_Feature unmap_feature = {
		.URI = LV2_URID__unmap,
		.data = sb->unmap
	};
	const LV2_Feature uri_id_feature= {
		.URI = LV2_URI_MAP_URI,
		.data = &sb->uri_id
	};
	const LV2_Feature log_feature = {
		.URI = LV2_LOG__log,
		.data = &sb->log
	};
	const LV2_Feature port_map_feature = {
		.URI = LV2_UI__portMap,
		.data = &sb->port_map
	};
	const LV2_Feature port_subscribe_feature = {
		.URI = LV2_UI__portSubscribe,
		.data = &sb->port_subscribe
	};
	const LV2_Feature options_feature = {
		.URI = LV2_OPTIONS__options,
		.data = options
	};
	const LV2_Feature resize_feature = {
		.URI = LV2_UI__resize,
		.data = &sb->host_resize
	};

	const LV2_Feature *const features [] = {
		&map_feature,
		&unmap_feature,
		&uri_id_feature,
		&log_feature,
		&port_map_feature,
		&port_subscribe_feature,
		&options_feature,
		sb->host_resize.ui_resize ? &resize_feature : parent_feature,
		sb->host_resize.ui_resize && parent_feature ? parent_feature : NULL,
		NULL
	};

	//FIXME check features

	const LilvNode *ui_bundle_uri = lilv_ui_get_bundle_uri(sb->ui);
#if defined(LILV_0_22)
	char *ui_plugin_bundle_path = lilv_file_uri_parse(lilv_node_as_string(ui_bundle_uri), NULL);
#else
	const char *ui_plugin_bundle_path = lilv_uri_to_path(lilv_node_as_string(ui_bundle_uri));
#endif

	if(sb->desc && sb->desc->instantiate)
	{
		sb->handle = sb->desc->instantiate(sb->desc, sb->plugin_uri,
			ui_plugin_bundle_path, _write_function, sb, widget, features);
	}

#if defined(LILV_0_22)
	lilv_free(ui_plugin_bundle_path);
#endif

	if(sb->handle)
		return sb->handle; // success

	return NULL;
}

int
sandbox_slave_recv(sandbox_slave_t *sb)
{
	if(sb)
		return _sandbox_io_recv(&sb->io, _sandbox_recv_cb, NULL, sb);

	return -1;
}

void
sandbox_slave_wait(sandbox_slave_t *sb)
{
	_sandbox_io_wait(&sb->io);
}

bool
sandbox_slave_timedwait(sandbox_slave_t *sb, const struct timespec *abs_timeout)
{
	return _sandbox_io_timedwait(&sb->io, abs_timeout);
}

const void *
sandbox_slave_extension_data(sandbox_slave_t *sb, const char *URI)
{
	if(sb && sb->desc && sb->desc->extension_data)
		return sb->desc->extension_data(URI);

	return NULL;
}

void
sandbox_slave_run(sandbox_slave_t *sb)
{
	if(sb && sb->driver && sb->driver->run_cb)
		sb->driver->run_cb(sb, sb->update_rate, sb->data);
}

const char *
sandbox_slave_title_get(sandbox_slave_t *sb)
{
	if(sb)
		return sb->window_title;

	return NULL;
}

bool
sandbox_slave_no_user_resize_get(sandbox_slave_t *sb)
{
	if(sb)
		return sb->no_user_resize;

	return false;
}
