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
#include <unistd.h> // getpid

#include <Ecore.h>
#include <Ecore_Con.h>
#include <Ecore_File.h>
#include <Efreet.h>

#include <osc.lv2/writer.h>
#include <osc.lv2/reader.h>

#include <synthpod_app.h>
#include <synthpod_nsm.h>

typedef void (*osc_cb_t)(LV2_OSC_Reader *reader, synthpod_nsm_t *nsm);
typedef struct _osc_msg_t osc_msg_t;

struct _osc_msg_t {
	const char *path;
	osc_cb_t cb;
};

struct _synthpod_nsm_t {
	int managed;

	char *url;
	char *call;
	char *exe;

	const synthpod_nsm_driver_t *driver;
	void *data;

	Ecore_Con_Server *serv;
	Ecore_Event_Handler *add;
	Ecore_Event_Handler *del;
	Ecore_Event_Handler *dat;

	uint8_t send [0x10000];
};

static void
_reply(LV2_OSC_Reader *reader, synthpod_nsm_t *nsm)
{
	const char *target = NULL;
	lv2_osc_reader_get_string(reader, &target);

	//fprintf(stdout, "synthpod_nsm reply: %s\n", target);

	if(target && !strcmp(target, "/nsm/server/announce"))
	{
		const char *message = NULL;
		const char *manager = NULL;
		const char *capabilities = NULL;

		lv2_osc_reader_get_string(reader, &message);
		lv2_osc_reader_get_string(reader, &manager);
		lv2_osc_reader_get_string(reader, &capabilities);

		//TODO, e.g. toggle SM LED
	}
}

static void
_error(LV2_OSC_Reader *reader, synthpod_nsm_t *nsm)
{
	const char *msg = NULL;
	int32_t code = 0;
	const char *err = NULL;

	lv2_osc_reader_get_string(reader, &msg);
	lv2_osc_reader_get_int32(reader, &code);
	lv2_osc_reader_get_string(reader, &err);

	fprintf(stderr, "synthpod_nsm error: #%i in %s: %s\n", code, msg, err);
}

static void
_client_open(LV2_OSC_Reader *reader, synthpod_nsm_t *nsm)
{
	const char *dir = NULL;
	const char *name = NULL;
	const char *id = NULL;

	lv2_osc_reader_get_string(reader, &dir);
	lv2_osc_reader_get_string(reader, &name);
	lv2_osc_reader_get_string(reader, &id);

	// open/create app
	ecore_file_mkpath(dir); // path may not exist yet
	char *synthpod_dir = ecore_file_realpath(dir);
	const char *realpath = synthpod_dir && synthpod_dir[0] ? synthpod_dir : dir;

	if(nsm->driver->open && nsm->driver->open(realpath, name, id, nsm->data))
		fprintf(stderr, "NSM load failed: '%s'\n", dir);

	if(synthpod_dir)
		free(synthpod_dir);
}

static void
_client_save(LV2_OSC_Reader *reader, synthpod_nsm_t *nsm)
{
	// save app
	if(nsm->driver->save && nsm->driver->save(nsm->data))
		fprintf(stderr, "NSM save failed:\n");
}

static void
_client_show_optional_gui(LV2_OSC_Reader *reader, synthpod_nsm_t *nsm)
{
	// show gui
	if(nsm->driver->show && nsm->driver->show(nsm->data))
	{
		fprintf(stderr, "NSM show GUI failed\n");
		return;
	}

	// reply
	LV2_OSC_Writer writer;
	lv2_osc_writer_initialize(&writer, nsm->send, sizeof(nsm->send));
	lv2_osc_writer_message_vararg(&writer, "/nsm/client/gui_is_shown", "");

	size_t written;
	if(lv2_osc_writer_finalize(&writer, &written))
		ecore_con_server_send(nsm->serv, nsm->send, written);
	else
		fprintf(stderr, "OSC sending failed\n");
}

static void
_client_hide_optional_gui(LV2_OSC_Reader *reader, synthpod_nsm_t *nsm)
{
	// hide gui
	if(nsm->driver->hide && nsm->driver->hide(nsm->data))
	{
		fprintf(stderr, "NSM hide GUI failed\n");
		return;
	}

	// reply
	LV2_OSC_Writer writer;
	lv2_osc_writer_initialize(&writer, nsm->send, sizeof(nsm->send));
	lv2_osc_writer_message_vararg(&writer, "/nsm/client/gui_is_hidden", "");

	size_t written;
	if(lv2_osc_writer_finalize(&writer, &written))
		ecore_con_server_send(nsm->serv, nsm->send, written);
	else
		fprintf(stderr, "OSC sending failed\n");
}

static void
_announce(synthpod_nsm_t *nsm)
{
	// send announce message
	pid_t pid = getpid();

	int has_gui = nsm->driver->show && nsm->driver->hide;
	const char *capabilities = has_gui
		? ":message:optional-gui:"
		: ":message:";

	LV2_OSC_Writer writer;
	lv2_osc_writer_initialize(&writer, nsm->send, sizeof(nsm->send));

	LV2_OSC_Writer_Frame bndl, itm;
	lv2_osc_writer_push_bundle(&writer, &bndl, LV2_OSC_IMMEDIATE);
	{
		lv2_osc_writer_push_item(&writer, &itm);
		lv2_osc_writer_message_vararg(&writer, "/nsm/server/announce", "sssiii",
			nsm->call, capabilities, nsm->exe, 1, 2, pid);
		lv2_osc_writer_pop_item(&writer, &itm);
	}
	if(has_gui)
	{
		lv2_osc_writer_push_item(&writer, &itm);
		lv2_osc_writer_message_vararg(&writer, "/nsm/client/gui_is_shown", "");
		lv2_osc_writer_pop_item(&writer, &itm);
	}
	lv2_osc_writer_pop_bundle(&writer, &bndl);

	size_t written;
	if(lv2_osc_writer_finalize(&writer, &written))
		ecore_con_server_send(nsm->serv, nsm->send, written);
	else
		fprintf(stderr, "OSC sending failed\n");
}

static const osc_msg_t messages [] = {
	{"/reply", _reply},
	{"/error", _error},

	{"/nsm/client/open", _client_open},
	{"/nsm/client/save", _client_save},
	{"/nsm/client/show_optional_gui", _client_show_optional_gui},
	{"/nsm/client/hide_optional_gui", _client_hide_optional_gui},

	{NULL, NULL}
};

static Eina_Bool
_con_add(void *data, int type, void *info)
{
	synthpod_nsm_t *nsm = data;

	assert(type == ECORE_CON_EVENT_SERVER_ADD);

	//printf("_client_add\n");
	//TODO
			
	_announce(nsm);

	return EINA_TRUE;
}

static Eina_Bool
_con_del(void *data, int type, void *info)
{
	assert(type == ECORE_CON_EVENT_SERVER_DEL);
	
	//printf("_client_del\n");
	//TODO

	return EINA_TRUE;
}

static void
_unpack_messages(LV2_OSC_Reader *reader, size_t len, synthpod_nsm_t *nsm)
{
	if(lv2_osc_reader_is_message(reader))
	{
		const char *path;
		const char *fmt;
		lv2_osc_reader_get_string(reader, &path);
		lv2_osc_reader_get_string(reader, &fmt);
		for(const osc_msg_t *msg = messages; msg->cb; msg++)
		{
			if(!strcmp(msg->path, path))
			{
				msg->cb(reader, nsm);
				break;
			}
		}
	}
	else if(lv2_osc_reader_is_bundle(reader))
	{
		OSC_READER_BUNDLE_FOREACH(reader, itm, len)
		{
			LV2_OSC_Reader reader2;
			lv2_osc_reader_initialize(&reader2, itm->body, itm->size);
			_unpack_messages(&reader2, itm->size, nsm);
		}
	}
}

static Eina_Bool
_con_dat(void *data, int type, void *info)
{
	synthpod_nsm_t *nsm = data;
	Ecore_Con_Event_Client_Data *ev = info;

	assert(type == ECORE_CON_EVENT_SERVER_DATA);
	
	printf("_client_data\n");

	LV2_OSC_Reader reader;
	lv2_osc_reader_initialize(&reader, ev->data, ev->size);
	_unpack_messages(&reader, ev->size, nsm);

	return EINA_TRUE;
}

synthpod_nsm_t *
synthpod_nsm_new(const char *exe, const char *path,
	const synthpod_nsm_driver_t *nsm_driver, void *data)
{
	if(!nsm_driver)
		return NULL;

	synthpod_nsm_t *nsm = calloc(1, sizeof(synthpod_nsm_t));
	if(!nsm)
		return NULL;

	nsm->driver = nsm_driver;
	nsm->data = data;

	nsm->call = strdup("Synthpod");
	nsm->exe = exe ? strdup(exe) : NULL;
	
	nsm->url = getenv("NSM_URL");
	if(nsm->url)
	{
		nsm->managed = 1;

		nsm->url = strdup(nsm->url);
		if(!nsm->url)
			return NULL;
		
		//printf("url: %s\n", nsm->url);

		Ecore_Con_Type type;
		if(!strncmp(nsm->url, "osc.udp", 7))
			type = ECORE_CON_REMOTE_UDP;
		else if(!strncmp(nsm->url, "osc.tcp", 7))
			type = ECORE_CON_REMOTE_TCP;
		else
			goto fail;

		char *addr = strstr(nsm->url, "://");
		if(!addr)
			goto fail;
		addr += 3; // skip "://"

		char *dst = strchr(addr, ':');
		if(!dst)
			goto fail;
		*dst++ = '\0';

		uint16_t port;
		if(sscanf(dst, "%hu", &port) != 1)
			goto fail;
		
		printf("NSM URL: %s, dst: %hu\n", addr, port);

		if(strstr(addr, "localhost"))
			addr = "127.0.0.1"; // forces ecore_con to use IPv4

		nsm->serv = ecore_con_server_connect(type,
			addr, port, nsm);
		if(!nsm->serv)
			goto fail;

		nsm->add = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_ADD, _con_add, nsm);
		nsm->del = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DEL, _con_del, nsm);
		nsm->dat = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DATA, _con_dat, nsm);

		if(type == ECORE_CON_REMOTE_UDP)
			_announce(nsm);
	}
	else
	{
		nsm->managed = 0;

		if(path)
		{
			ecore_file_mkpath(path); // path may not exist yet
			char *synthpod_dir = ecore_file_realpath(path);
			const char *realpath = synthpod_dir && synthpod_dir[0] ? synthpod_dir : path;

			if(nsm->driver->open && nsm->driver->open(realpath, nsm->call, nsm->exe, nsm->data))
				fprintf(stderr, "NSM load failed: '%s'\n", path);

			if(synthpod_dir)
				free(synthpod_dir);
		}
		else
		{
#if !defined(_WIN32)
			const char *home_dir = getenv("HOME");
#else
			const char *home_dir = evil_homedir_get();
#endif

			char *synthpod_dir = NULL;
			asprintf(&synthpod_dir, "%s/.lv2/Synthpod_default.preset.lv2", home_dir);
			if(synthpod_dir)
			{
				ecore_file_mkpath(synthpod_dir); // path may not exist yet

				if(nsm->driver->open && nsm->driver->open(synthpod_dir, nsm->call, nsm->exe, nsm->data))
					fprintf(stderr, "NSM load failed: '%s'\n", synthpod_dir);

				free(synthpod_dir);
			}
		}
	}

	return nsm;

fail:
	if(nsm->url)
		free(nsm->url);

	return NULL;
}

void
synthpod_nsm_free(synthpod_nsm_t *nsm)
{
	if(nsm)
	{
		if(nsm->call)
			free(nsm->call);
		if(nsm->exe)
			free(nsm->exe);

		if(nsm->url)
		{
			if(nsm->add)
				ecore_event_handler_del(nsm->add);
			if(nsm->del)
				ecore_event_handler_del(nsm->del);
			if(nsm->dat)
				ecore_event_handler_del(nsm->dat);
			if(nsm->serv)
				ecore_con_server_del(nsm->serv);

			free(nsm->url);
		}

		free(nsm);
	}
}

void
synthpod_nsm_opened(synthpod_nsm_t *nsm, int status)
{
	if(!nsm)
		return;

	LV2_OSC_Writer writer;
	bool ret = false;
	lv2_osc_writer_initialize(&writer, nsm->send, sizeof(nsm->send));

	if(status == 0)
	{
		ret = lv2_osc_writer_message_vararg(&writer, "/reply", "ss",
			"/nsm/client/open", "opened");
	}
	else
	{
		ret = lv2_osc_writer_message_vararg(&writer, "/error", "sis",
			"/nsm/client/open", 2, "opening failed");
	}

	size_t written;
	if(lv2_osc_writer_finalize(&writer, &written))
		ecore_con_server_send(nsm->serv, nsm->send, written);
	else
		fprintf(stderr, "OSC sending failed\n");
}

void
synthpod_nsm_saved(synthpod_nsm_t *nsm, int status)
{
	if(!nsm)
		return;

	LV2_OSC_Writer writer;
	bool ret = false;
	lv2_osc_writer_initialize(&writer, nsm->send, sizeof(nsm->send));

	if(status == 0)
	{
		ret = lv2_osc_writer_message_vararg(&writer, "/reply", "ss",
			"/nsm/client/save", "saved");
	}
	else
	{
		ret = lv2_osc_writer_message_vararg(&writer, "/error", "sis",
			"/nsm/client/save", 1, "save failed");
	}

	size_t written;
	if(lv2_osc_writer_finalize(&writer, &written))
		ecore_con_server_send(nsm->serv, nsm->send, written);
	else
		fprintf(stderr, "OSC sending failed\n");
}

int
synthpod_nsm_managed()
{
	return getenv("NSM_URL") ? 1 : 0;
}
