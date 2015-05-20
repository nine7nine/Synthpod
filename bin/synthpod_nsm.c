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

#include <stdlib.h>
#include <unistd.h> // getpid

#include <synthpod_app.h>

#include <osc_stream.h>
#include <osc.h>
#include <varchunk.h>

#include <synthpod_nsm.h>

struct _synthpod_nsm_t {
	char *url;
	char *call;
	char *exe;

	const synthpod_nsm_driver_t *driver;
	void *data;

	osc_stream_t *stream;
	varchunk_t *send;
	varchunk_t *recv;
};

static int
_reply(osc_time_t time, const char *path, const char *fmt, const osc_data_t *buf,
	size_t size, void *data)
{
	synthpod_nsm_t *nsm = data;	

	const char *target;

	const osc_data_t *ptr = buf;
	ptr = osc_get_string(ptr, &target);

	//fprintf(stdout, "synthpod_nsm reply: %s\n", target);

	if(!strcmp(target, "/nsm/server/announce"))
	{
		const char *message;
		const char *manager;
		const char *capabilities;

		ptr = osc_get_string(ptr, &message);
		ptr = osc_get_string(ptr, &manager);
		ptr = osc_get_string(ptr, &capabilities);

		//TODO, e.g. toggle SM LED
	}

	return 1;
}

static int
_error(osc_time_t time, const char *path, const char *fmt, const osc_data_t *buf,
	size_t size, void *data)
{
	synthpod_nsm_t *nsm = data;	

	const char *msg;
	int32_t code;
	const char *err;

	const osc_data_t *ptr = buf;
	ptr = osc_get_string(ptr, &msg);
	ptr = osc_get_int32(ptr, &code);
	ptr = osc_get_string(ptr, &err);

	fprintf(stderr, "synthpod_nsm error: #%i in %s: %s\n", code, msg, err);

	return 1;
}

static int
_client_open(osc_time_t time, const char *path, const char *fmt, const osc_data_t *buf,
	size_t size, void *data)
{
	synthpod_nsm_t *nsm = data;	
	
	const char *dir;
	const char *name;
	const char *id;

	const osc_data_t *ptr = buf;
	ptr = osc_get_string(ptr, &path);
	ptr = osc_get_string(ptr, &name);
	ptr = osc_get_string(ptr, &id);

	// open/create app
	int ret = nsm->driver->open(path, name, id, nsm->data);
	
	osc_data_t *buf0;
	if((buf0 = varchunk_write_request(nsm->send, 256)))
	{
		if(ret == 0)
		{
			ptr = osc_set_vararg(buf0, buf0+256, "/reply", "ss",
				"/nsm/client/open", "opened");
		}
		else
		{
			ptr = osc_set_vararg(buf0, buf0+256, "/error", "sis",
				"/nsm/client/open", 2, "opening failed");
		}

		size_t written = ptr ? ptr - buf0 : 0;
		if(written)
			varchunk_write_advance(nsm->send, written);
		else
			; //TODO
	}

	return 1;
}

static int
_client_save(osc_time_t time, const char *path, const char *fmt, const osc_data_t *buf,
	size_t size, void *data)
{
	synthpod_nsm_t *nsm = data;	
	const osc_data_t *ptr;
	
	// save app
	int ret = nsm->driver->save(nsm->data);
	
	osc_data_t *buf0;
	if((buf0 = varchunk_write_request(nsm->send, 256)))
	{
		if(ret == 0)
		{
			ptr = osc_set_vararg(buf0, buf0+256, "/reply", "ss",
				"/nsm/client/save", "saved");
		}
		else
		{
			ptr = osc_set_vararg(buf0, buf0+256, "/error", "sis",
				"/nsm/client/save", 1, "save failed");
		}

		size_t written = ptr ? ptr - buf0 : 0;
		if(written)
			varchunk_write_advance(nsm->send, written);
		else
			; //TODO
	}

	return 1;
}

static int
_resolve(osc_time_t time, const char *path, const char *fmt, const osc_data_t *buf,
	size_t size, void *data)
{
	synthpod_nsm_t *nsm = data;	

	// send announce message
	pid_t pid = getpid();

	osc_data_t *buf0;
	if((buf0 = varchunk_write_request(nsm->send, 512)))
	{
		osc_data_t *ptr = osc_set_vararg(buf0, buf0+512, "/nsm/server/announce", "sssiii",
			nsm->call, ":message:", nsm->exe, 1, 2, pid);

		size_t written = ptr ? ptr - buf0 : 0;
		if(written)
			varchunk_write_advance(nsm->send, written);
		else
			; //TODO
	}

	return 1;
}

static int
_timeout(osc_time_t time, const char *path, const char *fmt, const osc_data_t *buf,
	size_t size, void *data)
{
	synthpod_nsm_t *nsm = data;	

	printf("_timeout\n");
	//TODO

	return 1;
}

static int
_err(osc_time_t time, const char *path, const char *fmt, const osc_data_t *buf,
	size_t size, void *data)
{
	synthpod_nsm_t *nsm = data;	

	const char *where;
	const char *what;

	const osc_data_t *ptr = buf;
	ptr = osc_get_string(ptr, &where);
	ptr = osc_get_string(ptr, &what);

	printf("_error: %s (%s)\n", where, what);

	return 1;
}

static const osc_method_t methods [] = {
	{"/stream/resolve", "", _resolve},
	{"/stream/timeout", "", _timeout},
	{"/stream/error", "ss", _err},

	{"/reply", NULL, _reply},
	{"/error", "sis", _error},
	
	{"/nsm/client/open", "sss", _client_open},
	{"/nsm/client/save", "", _client_save},

	{NULL, NULL, NULL}
};

static void *
_recv_req(size_t size, void *data)
{
	synthpod_nsm_t *nsm = data;
	
	void *ptr;
	do ptr = varchunk_write_request(nsm->recv, size);
	while(!ptr);
	
	return ptr;
}

static void
_recv_adv(size_t written, void *data)
{
	synthpod_nsm_t *nsm = data;
	
	varchunk_write_advance(nsm->recv, written);

	// process incoming data
	const void *ptr;
	size_t size;
	while((ptr = varchunk_read_request(nsm->recv, &size)))
	{
		if(osc_check_packet(ptr, size))
			osc_dispatch_method(0, ptr, size, methods, NULL, NULL, nsm);
		else
			fprintf(stderr, "_recv_adv: malformed OSC packet\n");

		varchunk_read_advance(nsm->recv);
	}
	
	osc_stream_flush(nsm->stream);
}

static const void *
_send_req(size_t *len, void *data)
{
	synthpod_nsm_t *nsm = data;

	return varchunk_read_request(nsm->send, len);
}

static void
_send_adv(void *data)
{
	synthpod_nsm_t *nsm = data;
	
	varchunk_read_advance(nsm->send);
}
		
static const osc_stream_driver_t osc_driver = {
	.recv_req = _recv_req,
	.recv_adv = _recv_adv,
	.send_req = _send_req,
	.send_adv = _send_adv
};

synthpod_nsm_t *
synthpod_nsm_new(uv_loop_t *loop, const char *exe, const synthpod_nsm_driver_t *nsm_driver, void *data)
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
		nsm->url = strdup(nsm->url);
		if(!nsm->url)
			return NULL;
		
		size_t url_len = strlen(nsm->url);
		if(nsm->url[url_len-1] == '/')
			nsm->url[url_len-1] = '\0';

		nsm->recv = varchunk_new(0x10000);
		nsm->send = varchunk_new(0x10000);
		if(!nsm->recv || !nsm->send)
			return NULL;

		nsm->stream = osc_stream_new(loop, nsm->url,
			&osc_driver, nsm);
		if(!nsm->stream)
			return NULL;
	}
	else
	{
		// directly call open callback
		nsm->driver->open("/home/hp/.local/share/synthpod/state.json", //FIXME
			nsm->call, nsm->exe, nsm->data);
	}

	return nsm;
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

			if(nsm->recv)
				varchunk_free(nsm->recv);
			if(nsm->send)
				varchunk_free(nsm->send);

			free(nsm->url);
		}
		else
		{
			// directly call save callback
			nsm->driver->save(nsm->data);
		}

		free(nsm);
	}
}
