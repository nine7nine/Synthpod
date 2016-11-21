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

#include <osc_stream.h>

#include <osc.lv2/writer.h>
#include <osc.lv2/reader.h>

#include <synthpod_common.h>
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

	osc_stream_t *stream;

	uv_loop_t *loop;

	uint8_t *recv;
	uint8_t send [0x10000];
	size_t sz;
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

	uv_fs_t req;
	uv_fs_realpath(nsm->loop, &req, dir, NULL);
	const char *realpath = req.path && *(char *)req.path ? req.path : dir;

	if(nsm->driver->open && nsm->driver->open(realpath, name, id, nsm->data))
		fprintf(stderr, "NSM load failed: '%s'\n", dir);

	uv_fs_req_cleanup(&req);
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
		osc_stream_flush(nsm->stream);
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
		osc_stream_flush(nsm->stream);
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
		osc_stream_flush(nsm->stream);
	else
		fprintf(stderr, "OSC sending failed\n");
}

static void
_client_connect(LV2_OSC_Reader *reader, synthpod_nsm_t *nsm)
{
	_announce(nsm);
}

static void
_client_disconnect(LV2_OSC_Reader *reader, synthpod_nsm_t *nsm)
{
	// nothing
}

static const osc_msg_t messages [] = {
	{"/reply", _reply},
	{"/error", _error},

	{"/nsm/client/open", _client_open},
	{"/nsm/client/save", _client_save},
	{"/nsm/client/show_optional_gui", _client_show_optional_gui},
	{"/nsm/client/hide_optional_gui", _client_hide_optional_gui},

	{"/stream/connect", _client_connect},
	{"/stream/dicsonnect", _client_disconnect},

	{NULL, NULL}
};

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

static void *
_recv_req(size_t size, void *data)
{
	synthpod_nsm_t *nsm = data;

	nsm->recv = malloc(size);

	return nsm->recv;
}

static void
_recv_adv(size_t written, void *data)
{
	synthpod_nsm_t *nsm = data;

	LV2_OSC_Reader reader;
	lv2_osc_reader_initialize(&reader, nsm->recv, written);
	_unpack_messages(&reader, written, nsm);

	free(nsm->recv);
}

static const void *
_send_req(size_t *len, void *data)
{
	synthpod_nsm_t *nsm = data;

	*len = nsm->sz;

	return nsm->sz ? nsm->send : NULL;
}

static void
_send_adv(void *data)
{
	synthpod_nsm_t *nsm = data;

	nsm->sz = 0;
}

static void
_free(void *data)
{
	synthpod_nsm_t *nsm = data;

	// do nothing
}

static const osc_stream_driver_t driver = {
	.recv_req = _recv_req,
	.recv_adv = _recv_adv,
	.send_req = _send_req,
	.send_adv = _send_adv,
	.free = _free
};

synthpod_nsm_t *
synthpod_nsm_new(const char *exe, const char *path, uv_loop_t *loop,
	const synthpod_nsm_driver_t *nsm_driver, void *data)
{
	if(!nsm_driver)
		return NULL;

	synthpod_nsm_t *nsm = calloc(1, sizeof(synthpod_nsm_t));
	if(!nsm)
		return NULL;

	nsm->loop = loop;
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
		
		nsm->stream = osc_stream_new(nsm->loop, nsm->url, &driver, nsm);
	}
	else
	{
		nsm->managed = 0;

		if(path)
		{
			uv_fs_t req;
			uv_fs_realpath(nsm->loop, &req, path, NULL);
			const char *realpath = req.ptr && *(char *)req.ptr ? req.ptr : path;

			if(nsm->driver->open && nsm->driver->open(realpath, nsm->call, nsm->exe, nsm->data))
				fprintf(stderr, "NSM load failed: '%s'\n", path);

			uv_fs_req_cleanup(&req);
		}
		else
		{
			const char *home_dir = getenv("HOME");

			char *synthpod_dir = NULL;
			asprintf(&synthpod_dir, "%s/.lv2/Synthpod_default.preset.lv2", home_dir);
			if(synthpod_dir)
			{
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
			if(nsm->stream)
				osc_stream_free(nsm->stream);

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
		osc_stream_flush(nsm->stream);
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
		osc_stream_flush(nsm->stream);
	else
		fprintf(stderr, "OSC sending failed\n");
}

int
synthpod_nsm_managed()
{
	return getenv("NSM_URL") ? 1 : 0;
}
