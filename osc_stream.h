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

#ifndef _OSC_STREAM_H_
#define _OSC_STREAM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <uv.h>

typedef struct _osc_stream_t osc_stream_t;
typedef struct _osc_stream_driver_t osc_stream_driver_t;

typedef void *(*osc_stream_recv_req_t)(size_t size, void *data);
typedef void (*osc_stream_recv_adv_t)(size_t written, void *data);

typedef const void *(*osc_stream_send_req_t)(size_t *len, void *data);
typedef void (*osc_stream_send_adv_t)(void *data);

struct _osc_stream_driver_t {
	osc_stream_recv_req_t recv_req;
	osc_stream_recv_adv_t recv_adv;
	osc_stream_send_req_t send_req;
	osc_stream_send_adv_t send_adv;
};

osc_stream_t *
osc_stream_new(uv_loop_t *loop, const char *addr, osc_stream_driver_t *driver,
	void *data);

void
osc_stream_free(osc_stream_t *stream);

void
osc_stream_flush(osc_stream_t *stream);

#ifdef __cplusplus
}
#endif

#endif
