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
#include <stdlib.h>

#include <osc_stream_private.h>

static void
_tcp_prefix_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	uv_tcp_t *socket = (uv_tcp_t *)handle;
	osc_stream_tcp_t *tcp = socket->data;
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, payload.tcp);
	osc_stream_driver_t *driver = stream->driver;

	if(suggested_size > OSC_STREAM_BUF_SIZE)
		suggested_size = OSC_STREAM_BUF_SIZE;

	if(tcp->nchunk == sizeof(int32_t))
	{
		buf->base = (char *)&tcp->prefix;
		buf->len = tcp->nchunk;
	}
	else if(driver->recv_req)
	{
		buf->base = (char *)driver->recv_req(suggested_size, stream->data);
		buf->len = suggested_size;
	}
	else
	{
		buf->base = NULL;
		buf->len = 0;
	}
}

static void
_tcp_prefix_recv_cb(uv_stream_t *socket, ssize_t nread, const uv_buf_t *buf)
{
	osc_stream_tcp_t *tcp = socket->data;
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, payload.tcp);
	osc_stream_driver_t *driver = stream->driver;
	osc_stream_tcp_tx_t *tx = (void *)socket - offsetof(osc_stream_tcp_tx_t, socket);

	if(nread > 0)
	{
		if(nread == sizeof(int32_t))
		{
			tcp->nchunk = ntohl(*(int32_t *)buf->base);
		}
		else if(nread == tcp->nchunk)
		{
			if(driver->recv_adv)
				driver->recv_adv(nread, stream->data);
			tcp->nchunk = sizeof(int32_t);
		}
		else // nread != sizeof(int32_t) && nread != nchunk
		{
			//FIXME what should we do here?
			tcp->nchunk = sizeof(int32_t);
			fprintf(stderr, "_tcp_prefix_recv_cb: TCP packet size not matching\n");
		}
	}
	else if (nread < 0)
	{
		if( (nread == UV_EOF) || (nread == UV_ETIMEDOUT) )
			instant_recv(stream, disconnect_msg, sizeof(disconnect_msg));
		else
			fprintf(stderr, "_tcp_prefix_recv_cb: %s\n", uv_err_name(nread));

		int err;
		if((err = uv_read_stop(socket)))
			fprintf(stderr, "uv_read_stop: %s\n", uv_err_name(err));
		uv_close((uv_handle_t *)socket, NULL);
		uv_cancel((uv_req_t *)&tx->req);

		tcp->tx = inlist_remove(tcp->tx, INLIST_GET(tx));
		free(tx);

		//TODO try to reconnect?
	}
	else // nread == 0
		;
}

static void
_tcp_slip_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	uv_tcp_t *socket = (uv_tcp_t *)handle;
	osc_stream_tcp_t *tcp = socket->data;
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, payload.tcp);
	osc_stream_driver_t *driver = stream->driver;

	buf->base = tcp->buf_rx;
	buf->base += tcp->nchunk; // is there remaining chunk from last call?
	buf->len = OSC_STREAM_BUF_SIZE - tcp->nchunk;
}

static void
_tcp_slip_recv_cb(uv_stream_t *socket, ssize_t nread, const uv_buf_t *buf)
{
	osc_stream_tcp_t *tcp = socket->data;
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, payload.tcp);
	osc_stream_driver_t *driver = stream->driver;
	osc_stream_tcp_tx_t *tx = (void *)socket - offsetof(osc_stream_tcp_tx_t, socket);
	
	if(nread > 0)
	{
		char *ptr = tcp->buf_rx;
		nread += tcp->nchunk; // is there remaining chunk from last call?
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
			memmove(tcp->buf_rx, ptr, nread);
			tcp->nchunk = nread;
		}
		else
			tcp->nchunk = 0;
	}
	else if (nread < 0)
	{
		if( (nread == UV_EOF) || (nread == UV_ETIMEDOUT) )
			instant_recv(stream, disconnect_msg, sizeof(disconnect_msg));
		else
			fprintf(stderr, "_tcp_slip_recv_cb: %s\n", uv_err_name(nread));

		int err;
		if((err = uv_read_stop(socket)))
			fprintf(stderr, "uv_read_stop: %s\n", uv_err_name(err));
		uv_close((uv_handle_t *)socket, NULL);
		uv_cancel((uv_req_t *)&tx->req);

		tcp->tx = inlist_remove(tcp->tx, INLIST_GET(tx));
		free(tx);

		//TODO try to reconnect?
	}
	else // nread == 0
		;
}

static void
_sender_connect(uv_connect_t *conn, int status)
{
	uv_tcp_t *socket = (uv_tcp_t *)conn->handle;
	osc_stream_tcp_t *tcp = socket->data;

	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, payload.tcp);
	osc_stream_driver_t *driver = stream->driver;
	osc_stream_tcp_tx_t *tx = INLIST_CONTAINER_GET(tcp->tx, osc_stream_tcp_tx_t);

	if(status)
	{
		fprintf(stderr, "_sender_connect %s\n", uv_err_name(status));
		return; //TODO
	}
	
	instant_recv(stream, connect_msg, sizeof(connect_msg));

	int err;
	if(!tcp->slip)
	{
		tcp->nchunk = sizeof(int32_t); // packet size as TCP preamble
		if((err = uv_read_start((uv_stream_t *)&tx->socket, _tcp_prefix_alloc, _tcp_prefix_recv_cb)))
		{
			fprintf(stderr, "uv_read_start: %s\n", uv_err_name(err));
			return;
		}
	}
	else // tcp->slip
	{
		tcp->nchunk = 0;
		if((err = uv_read_start((uv_stream_t *)&tx->socket, _tcp_slip_alloc, _tcp_slip_recv_cb)))
		{
			fprintf(stderr, "uv_read_start: %s\n", uv_err_name(err));
			return;
		}
	}
}

void
getaddrinfo_tcp_tx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
	uv_loop_t *loop = req->loop;
	osc_stream_tcp_t *tcp = (void *)req - offsetof(osc_stream_tcp_t, req);
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, payload.tcp);
	osc_stream_driver_t *driver = stream->driver;

	if(status >= 0)
	{
		osc_stream_tcp_tx_t *tx = calloc(1, sizeof(osc_stream_tcp_tx_t));
		tx->socket.data = tcp;
		tx->req.data = tcp;
		tcp->tx = inlist_append(NULL, INLIST_GET(tx));

		union {
			const struct sockaddr *ip;
			const struct sockaddr_in *ip4;
			const struct sockaddr_in6 *ip6;
		} src;
		char remote [128] = {'\0'};
		
		src.ip = res->ai_addr;;
		
		int err;
		switch(tcp->version)
		{
			case OSC_STREAM_IP_VERSION_4:
			{
				if((err = uv_ip4_name(src.ip4, remote, 127)))
				{
					fprintf(stderr, "up_ip4_name: %s\n", uv_err_name(err));
					return;
				}
				break;
			}
			case OSC_STREAM_IP_VERSION_6:
			{
				if((err = uv_ip6_name(src.ip6, remote, 127)))
				{
					fprintf(stderr, "up_ip6_name: %s\n", uv_err_name(err));
					return;
				}
				break;
			}
		}
		
		if((err = uv_tcp_init(loop, &tx->socket)))
		{
			fprintf(stderr, "uv_tcp_init: %s\n", uv_err_name(err));
			return;
		}

		union {
			const struct sockaddr ip;
			struct sockaddr_in ip4;
			struct sockaddr_in6 ip6;
		} addr;

		switch(tcp->version)
		{
			case OSC_STREAM_IP_VERSION_4:
			{
				if((err = uv_ip4_addr(remote, ntohs(src.ip4->sin_port), &addr.ip4)))
				{
					fprintf(stderr, "uv_ip4_addr: %s\n", uv_err_name(err));
					return;
				}

				break;
			}
			case OSC_STREAM_IP_VERSION_6:
			{
				if((err = uv_ip6_addr(remote, ntohs(src.ip6->sin6_port), &addr.ip6)))
				{
					fprintf(stderr, "uv_ip6_addr: %s\n", uv_err_name(err));
					return;
				}
				break;
			}
		}
		
		if((err = uv_tcp_connect(&tcp->conn, &tx->socket, &addr.ip, _sender_connect)))
		{
			fprintf(stderr, "uv_tcp_connect: %s\n", uv_err_name(err));
			return;
		}
		if((err = uv_tcp_nodelay(&tx->socket, 1))) // disable Nagle's algo
		{
			fprintf(stderr, "uv_tcp_nodelay: %s\n", uv_err_name(err));
			return;
		}
		if((err = uv_tcp_keepalive(&tx->socket, 1, 5))) // keepalive after 5 seconds
		{
			fprintf(stderr, "uv_tcp_keepalive: %s\n", uv_err_name(err));
			return;
		}

		instant_recv(stream, resolve_msg, sizeof(resolve_msg));
	}
	else
		instant_recv(stream, timeout_msg, sizeof(timeout_msg));

	uv_freeaddrinfo(res);
}

static void
_responder_connect(uv_stream_t *socket, int status)
{
	uv_loop_t *loop = socket->loop;
	osc_stream_tcp_t *tcp = (void *)socket - offsetof(osc_stream_tcp_t, socket);
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, payload.tcp);
	osc_stream_driver_t *driver = stream->driver;

	if(status)
	{
		fprintf(stderr, "_responder_connect: %s\n", uv_err_name(status));
		return;
	}

	instant_recv(stream, connect_msg, sizeof(connect_msg));

	osc_stream_tcp_tx_t *tx = calloc(1, sizeof(osc_stream_tcp_tx_t));
	tx->socket.data = tcp;
	tx->req.data = tcp;
	tcp->tx = inlist_append(tcp->tx, INLIST_GET(tx));

	int err;
	if((err = uv_tcp_init(loop, &tx->socket)))
	{
		fprintf(stderr, "uv_tcp_init: %s\n", uv_err_name(err));
		return;
	}

	if((err = uv_accept((uv_stream_t *)socket, (uv_stream_t *)&tx->socket)))
	{
		fprintf(stderr, "uv_accept: %s\n", uv_err_name(err));
		return;
	}

	if(!tcp->slip)
	{
		tcp->nchunk = sizeof(int32_t); // packet size as TCP preamble
		if((err = uv_read_start((uv_stream_t *)&tx->socket, _tcp_prefix_alloc, _tcp_prefix_recv_cb)))
		{
			fprintf(stderr, "uv_read_start: %s\n", uv_err_name(err));
			return;
		}
	}
	else // tcp->slip
	{
		tcp->nchunk = 0;
		if((err = uv_read_start((uv_stream_t *)&tx->socket, _tcp_slip_alloc, _tcp_slip_recv_cb)))
		{
			fprintf(stderr, "uv_read_start: %s\n", uv_err_name(err));
			return;
		}
	}

	if((err = uv_tcp_nodelay(&tx->socket, 1))) // disable Nagle's algo
	{
		fprintf(stderr, "uv_tcp_nodelay: %s\n", uv_err_name(err));
		return;
	}
	if((err = uv_tcp_keepalive(&tx->socket, 1, 5))) // keepalive after 5 seconds
	{
		fprintf(stderr, "uv_tcp_keepalive: %s\n", uv_err_name(err));
		return;
	}
}

void
getaddrinfo_tcp_rx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
	uv_loop_t *loop = req->loop;
	osc_stream_tcp_t *tcp = (void *)req - offsetof(osc_stream_tcp_t, req);
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, payload.tcp);
	osc_stream_driver_t *driver = stream->driver;

	if(status >= 0)
	{
		int err;
		osc_stream_addr_t addr;
		char remote [128] = {'\0'};
		unsigned int flags = 0;

		tcp->tx = NULL;

		if((err = uv_tcp_init(loop, &tcp->socket)))
		{
			fprintf(stderr, "uv_tcp_init: %s\n", uv_err_name(err));
			return;
		}

		switch(tcp->version)
		{
			case OSC_STREAM_IP_VERSION_4:
			{
				struct sockaddr_in *ptr4 = (struct sockaddr_in *)res->ai_addr;
				if((err = uv_ip4_name(ptr4, remote, 127)))
				{
					fprintf(stderr, "up_ip4_name: %s\n", uv_err_name(err));
					return;
				}
				if((err = uv_ip4_addr(remote, ntohs(ptr4->sin_port), &addr.ip4)))
				{
					fprintf(stderr, "uv_ip4_addr: %s\n", uv_err_name(err));
					return;
				}

				break;
			}
			case OSC_STREAM_IP_VERSION_6:
			{
				struct sockaddr_in6 *ptr6 = (struct sockaddr_in6 *)res->ai_addr;
				if((err = uv_ip6_name(ptr6, remote, 127)))
				{
					fprintf(stderr, "up_ip6_name: %s\n", uv_err_name(err));
					return;
				}
				if((err = uv_ip6_addr(remote, ntohs(ptr6->sin6_port), &addr.ip6)))
				{
					fprintf(stderr, "uv_ip6_addr: %s\n", uv_err_name(err));
					return;
				}
				flags |= UV_TCP_IPV6ONLY; //TODO make this configurable

				break;
			}
		}

		if((err = uv_tcp_bind(&tcp->socket, &addr.ip, flags)))
		{
			fprintf(stderr, "uv_tcp_bind: %s\n", uv_err_name(err));
			return;
		}
		if((err = uv_listen((uv_stream_t *)&tcp->socket, 128, _responder_connect)))
		{
			fprintf(stderr, "uv_listen: %s\n", uv_err_name(err));
			return;
		}

		instant_recv(stream, resolve_msg, sizeof(resolve_msg));
	}
	else
		instant_recv(stream, timeout_msg, sizeof(timeout_msg));

	uv_freeaddrinfo(res);
}

static void
_tcp_send_cb(uv_write_t *req, int status)
{
	osc_stream_tcp_t *tcp = req->data;
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, payload.tcp);
	osc_stream_driver_t *driver = stream->driver;

	tcp->count--;

	if(!status)
	{
		if(!tcp->count) // sent to all clients?
		{
			if(driver->send_adv)
				driver->send_adv(stream->data);

			stream->flushing = 0; // reset flushing flag
			osc_stream_tcp_flush(stream); // look for more data to flush
		}
	}
	else
		fprintf(stderr, "_tcp_send_cb: %s\n", uv_err_name(status));
}

void
osc_stream_tcp_flush(osc_stream_t *stream)
{
	osc_stream_tcp_t *tcp = &stream->payload.tcp;
	osc_stream_driver_t *driver = stream->driver;

	if(stream->flushing) // already flushing?
		return;

	uv_buf_t *msg = tcp->msg;

	if(driver->send_req)
		msg[1].base = (char *)driver->send_req(&msg[1].len, stream->data);

	if(msg[1].base && (msg[1].len > 0) )
	{
		tcp->prefix = htobe32(msg[1].len);
		msg[0].base = (char *)&tcp->prefix;
		msg[0].len = sizeof(int32_t);

		if(tcp->slip)
		{
			msg[1].len = slip_encode(tcp->buf_tx, &msg[1], 1); // discard prefix size
			msg[1].base = tcp->buf_tx;
		}

		int err;
		tcp->count = inlist_count(tcp->tx);
		if(tcp->count)
			stream->flushing = 1; // set flushing flag

		osc_stream_tcp_tx_t *tx;
		INLIST_FOREACH(tcp->tx, tx)
		{
			if((err = uv_write(&tx->req, (uv_stream_t *)&tx->socket, &msg[tcp->slip], 2-tcp->slip, _tcp_send_cb)))
			{
				fprintf(stderr, "uv_write: %s\n", uv_err_name(err));
				stream->flushing = 0;
			}
		}
	}
}
