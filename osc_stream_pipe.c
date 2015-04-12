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

#include <osc_stream_private.h>

static void
_pipe_slip_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	uv_pipe_t *socket = (uv_pipe_t *)handle;
	osc_stream_pipe_t *pipe = (void *)socket - offsetof(osc_stream_pipe_t, socket);
	osc_stream_t *stream = (void *)pipe - offsetof(osc_stream_t, payload.pipe);
	osc_stream_driver_t *driver = stream->driver;

	buf->base = pipe->buf_rx;
	buf->base += pipe->nchunk; // is there remaining chunk from last call?
	buf->len = OSC_STREAM_BUF_SIZE - pipe->nchunk;
}

static void
_pipe_slip_recv_cb(uv_stream_t *socket, ssize_t nread, const uv_buf_t *buf)
{
	osc_stream_pipe_t *pipe = (void *)socket - offsetof(osc_stream_pipe_t, socket);
	osc_stream_t *stream = (void *)pipe - offsetof(osc_stream_t, payload.pipe);
	osc_stream_driver_t *driver = stream->driver;

	if(nread > 0)
	{
		char *ptr = pipe->buf_rx;
		nread += pipe->nchunk; // is there remaining chunk from last call?
		size_t size;
		size_t parsed;
		while( (parsed = slip_decode(ptr, nread, &size)) && (nread > 0) )
		{
			void *tar;
			if((tar = driver->recv_req(size, stream->data)))
			{
				memcpy(tar, ptr, size);
				driver->recv_adv(size, stream->data);
			}
			ptr += parsed;
			nread -= parsed;
		}
		if(nread > 0) // is there remaining chunk for next call?
		{
			memmove(pipe->buf_rx, ptr, nread);
			pipe->nchunk = nread;
		}
		else
			pipe->nchunk = 0;
	}
	else if (nread < 0)
	{
		if(nread == UV_EOF)
			instant_recv(stream, disconnect_msg, sizeof(disconnect_msg));
		else
			fprintf(stderr, "_pipe_slip_recv_cb: %s\n", uv_err_name(nread));

		int err;
		if((err = uv_read_stop(socket)))
			fprintf(stderr, "uv_read_stop: %s\n", uv_err_name(err));
		uv_close((uv_handle_t *)socket, NULL);

		//TODO try to reconnect?
	}
	else // nread == 0
		;
}

void
osc_stream_pipe_init(uv_loop_t *loop, osc_stream_t *stream, int fd)
{
	osc_stream_pipe_t *pipe = &stream->payload.pipe;
	osc_stream_driver_t *driver = stream->driver;
	pipe->nchunk = 0;
	pipe->fd = 0;

	int err;
	if((err = uv_pipe_init(loop, &pipe->socket, 0)))
		fprintf(stderr, "uv_pipe_init: %s\n", uv_err_name(err));
	if((err = uv_pipe_open(&pipe->socket, pipe->fd)))
		fprintf(stderr, "uv_pipe_open: %s\n", uv_err_name(err));
	if((err = uv_read_start((uv_stream_t *)&pipe->socket, _pipe_slip_alloc, _pipe_slip_recv_cb)))
		fprintf(stderr, "uv_read_start: %s", uv_err_name(err));
			
	instant_recv(stream, connect_msg, sizeof(connect_msg));
}

static void
_pipe_send_cb(uv_write_t *req, int status)
{
	osc_stream_pipe_t *pipe = (void *)req - offsetof(osc_stream_pipe_t, req);
	osc_stream_t *stream = (void *)pipe - offsetof(osc_stream_t, payload.pipe);
	osc_stream_driver_t *driver = stream->driver;

	if(!status)
	{
		stream->flushing = 0; // reset flushing flag
		osc_stream_udp_flush(stream); // look for more data to flush
	}
	else
		fprintf(stderr, "_pipe_send_cb: %s\n", uv_err_name(status));
}

void
osc_stream_pipe_flush(osc_stream_t *stream)
{
	osc_stream_pipe_t *pipe = &stream->payload.pipe;
	osc_stream_driver_t *driver = stream->driver;

	if(stream->flushing) // already flushing?
		return;

	uv_buf_t src = {
		.base = NULL,
		.len = 0
	};

	if(driver->send_req)
		src.base = (char *)driver->send_req(&src.len, stream->data);

	if(src.base && (src.len > 0))
	{
		stream->flushing = 1; // set flushing flag

		// slip encode it to temporary buffer
		uv_buf_t *msg = &pipe->msg;
		msg->base = pipe->buf_tx;
		msg->len = slip_encode(msg->base, &src, 1);
	
		if(driver->send_adv)
			driver->send_adv(stream->data);

		int err;
		if((err =	uv_write(&pipe->req, (uv_stream_t *)&pipe->socket, msg, 1, _pipe_send_cb)))
		{
			fprintf(stderr, "uv_udp_send: %s\n", uv_err_name(err));
			stream->flushing = 0;
		}
	}
}
