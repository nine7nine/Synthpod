/*
 * Copyright (c) 2014 Hanspeter Portner (dev@open-music-kontrollers.ch)
 * 
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 * 
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 * 
 *     1. The origin of this software must not be misrepresented; you must not
 *     claim that you wrote the original software. If you use this software
 *     in a product, an acknowledgment in the product documentation would be
 *     appreciated but is not required.
 * 
 *     2. Altered source versions must be plainly marked as such, and must not be
 *     misrepresented as being the original software.
 * 
 *     3. This notice may not be removed or altered from any source
 *     distribution.
 */

#include <stdio.h>

#include <osc_stream_private.h>

static void
_pipe_slip_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	uv_pipe_t *socket = (uv_pipe_t *)handle;
	osc_stream_pipe_t *pipe = (void *)socket - offsetof(osc_stream_pipe_t, socket);
	osc_stream_t *stream = (void *)pipe - offsetof(osc_stream_t, payload.pipe);
	osc_stream_cb_t *cb = &stream->cb;

	buf->base = cb->buf;
	buf->base += pipe->nchunk; // is there remaining chunk from last call?
	buf->len = OSC_STREAM_BUF_SIZE - pipe->nchunk;
}

static void
_pipe_slip_recv_cb(uv_stream_t *socket, ssize_t nread, const uv_buf_t *buf)
{
	osc_stream_pipe_t *pipe = (void *)socket - offsetof(osc_stream_pipe_t, socket);
	osc_stream_t *stream = (void *)pipe - offsetof(osc_stream_t, payload.pipe);
	osc_stream_cb_t *cb = &stream->cb;

	if(nread > 0)
	{
		char *ptr = cb->buf;
		nread += pipe->nchunk; // is there remaining chunk from last call?
		size_t size;
		size_t parsed;
		while( (parsed = slip_decode(ptr, nread, &size)) && (nread > 0) )
		{
			if(cb->recv)
				cb->recv(stream, (osc_data_t *)ptr, size, cb->data);
			ptr += parsed;
			nread -= parsed;
		}
		if(nread > 0) // is there remaining chunk for next call?
		{
			memmove(cb->buf, ptr, nread);
			pipe->nchunk = nread;
		}
		else
			pipe->nchunk = 0;
	}
	else if (nread < 0)
	{
		if(nread == UV_EOF)
		{
			if(cb->recv)
				cb->recv(stream, (osc_data_t *)disconnect_msg, sizeof(disconnect_msg), cb->recv);
		}
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
	osc_stream_cb_t *cb = &stream->cb;
	pipe->nchunk = 0;
	pipe->fd = 0;

	int err;
	if((err = uv_pipe_init(loop, &pipe->socket, 0)))
		fprintf(stderr, "uv_pipe_init: %s\n", uv_err_name(err));
	if((err = uv_pipe_open(&pipe->socket, pipe->fd)))
		fprintf(stderr, "uv_pipe_open: %s\n", uv_err_name(err));
	if((err = uv_read_start((uv_stream_t *)&pipe->socket, _pipe_slip_alloc, _pipe_slip_recv_cb)))
		fprintf(stderr, "uv_read_start: %s", uv_err_name(err));
			
	if(cb->recv)
		cb->recv(stream, (osc_data_t *)connect_msg, sizeof(connect_msg), cb->recv);
}

static void
_pipe_send_cb(uv_write_t *req, int status)
{
	osc_stream_pipe_t *pipe = (void *)req - offsetof(osc_stream_pipe_t, req);
	osc_stream_t *stream = (void *)pipe - offsetof(osc_stream_t, payload.pipe);
	osc_stream_cb_t *cb = &stream->cb;

	if(!status)
	{
		if(cb->send)
			cb->send(stream, pipe->len, cb->data);
	}
	else
		fprintf(stderr, "_pipe_send_cb: %s\n", uv_err_name(status));
}

void
osc_stream_pipe_send(osc_stream_t *stream, const osc_data_t *buf, size_t len)
{
	osc_stream_pipe_t *pipe = &stream->payload.pipe;
	pipe->len = len;

	uv_buf_t msg [1] = {
		[0] = {
			.base = (char *)buf,
			.len = len
		}
	};

	static char bb [OSC_STREAM_BUF_SIZE]; //FIXME
	msg[0].len = slip_encode(bb, &msg[0], 1);
	msg[0].base = bb;

	int err;
	if((err =	uv_write(&pipe->req, (uv_stream_t *)&pipe->socket, &msg[0], 1, _pipe_send_cb)))
		fprintf(stderr, "uv_write: %s", uv_err_name(err));
}

void
osc_stream_pipe_send2(osc_stream_t *stream, const uv_buf_t *bufs, size_t bufn)
{
	osc_stream_pipe_t *pipe = &stream->payload.pipe;
	pipe->len = 0;

	int i;
	for(i=0; i<bufn; i++)
		pipe->len += bufs[i].len;

	static char bb [OSC_STREAM_BUF_SIZE]; //FIXME
	uv_buf_t msg [1];
	msg[0].len = slip_encode(bb, (uv_buf_t *)bufs, bufn);
	msg[0].base = bb;

	int err;
	if((err =	uv_write(&pipe->req, (uv_stream_t *)&pipe->socket, &msg[0], 1, _pipe_send_cb)))
		fprintf(stderr, "uv_write: %s", uv_err_name(err));
}
