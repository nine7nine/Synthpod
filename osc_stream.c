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

#include <osc_stream_private.h>

#include <stdio.h>
#include <stdlib.h>

const char resolve_msg [20]		= "/stream/resolve\0,\0\0\0";
const char timeout_msg [20]		= "/stream/timeout\0,\0\0\0";
const char connect_msg [20]		= "/stream/connect\0,\0\0\0";
const char disconnect_msg [24]	= "/stream/disconnect\0\0,\0\0\0";

#if defined(__WINDOWS__)
static inline char *
strndup(const char *s, size_t n)
{
    char *result;
    size_t len = strlen (s);
    if (n < len) len = n;
    result = (char *) malloc (len + 1);
    if (!result) return 0;
    result[len] = '\0';
    return (char *) strncpy (result, s, len);
}
#endif

static int
_osc_stream_parse_protocol(osc_stream_t *stream, const char *addr, const char **url)
{
	if(!strncmp(addr, "osc.udp", 7))
	{
		addr += 7;
			
		stream->type = OSC_STREAM_TYPE_UDP;

		if(!strncmp(addr, "://", 3))
		{
			stream->payload.udp.version = OSC_STREAM_IP_VERSION_4;
			addr += 3;
		}
		else if(!strncmp(addr, "4://", 4))
		{
			stream->payload.udp.version = OSC_STREAM_IP_VERSION_4;
			addr += 4;
		}
		else if(!strncmp(addr, "6://", 4))
		{
			stream->payload.udp.version = OSC_STREAM_IP_VERSION_6;
			addr += 4;
		}
		else
			return -1;
	}
	else if(!strncmp(addr, "osc.tcp", 7))
	{
		addr += 7;
			
		stream->type = OSC_STREAM_TYPE_TCP;
		stream->payload.tcp.slip = 0;

		if(!strncmp(addr, "://", 3))
		{
			stream->payload.tcp.version = OSC_STREAM_IP_VERSION_4;
			addr += 3;
		}
		else if(!strncmp(addr, "4://", 4))
		{
			stream->payload.tcp.version = OSC_STREAM_IP_VERSION_4;
			addr += 4;
		}
		else if(!strncmp(addr, "6://", 4))
		{
			stream->payload.tcp.version = OSC_STREAM_IP_VERSION_6;
			addr += 4;
		}
		else
			return -1;
	}
	else if(!strncmp(addr, "osc.slip.tcp", 12))
	{
		addr += 12;
		
		stream->type = OSC_STREAM_TYPE_TCP;
		stream->payload.tcp.slip = 1;

		if(!strncmp(addr, "://", 3))
		{
			stream->payload.tcp.version = OSC_STREAM_IP_VERSION_4;
			addr += 3;
		}
		else if(!strncmp(addr, "4://", 4))
		{
			stream->payload.tcp.version = OSC_STREAM_IP_VERSION_4;
			addr += 4;
		}
		else if(!strncmp(addr, "6://", 4))
		{
			stream->payload.tcp.version = OSC_STREAM_IP_VERSION_6;
			addr += 4;
		}
		else
			return -1;
	}
	else if(!strncmp(addr, "osc.pipe://", 11))
	{
		stream->type = OSC_STREAM_TYPE_PIPE;
		addr += 11;
	}
	else
		return -1;

	*url = addr;

	return 0;
}

static int
_osc_stream_parse_url(uv_loop_t *loop, osc_stream_t *stream, const char *url)
{
	switch(stream->type)
	{
		case OSC_STREAM_TYPE_UDP:
		{
			osc_stream_udp_t* udp = &stream->payload.udp;
			const char *service = strchr(url, ':');
				
			if(!service)
			{
				fprintf(stderr, "url must have a service port\n");
				return -1;
			}

			if( (udp->server = (url == service)) )
			{
				// resolve destination address
				const char *node = udp->version == OSC_STREAM_IP_VERSION_4 ? "0.0.0.0" : "::";

				const struct addrinfo hints = {
					.ai_family = udp->version == OSC_STREAM_IP_VERSION_4 ? PF_INET : PF_INET6,
					.ai_socktype = SOCK_DGRAM,
					.ai_protocol = IPPROTO_UDP,
					.ai_flags = 0
				};

				uv_getaddrinfo(loop, &udp->req, getaddrinfo_udp_rx_cb, node, service+1, &hints);
			}
			else // !udp->server
			{
				// resolve destination address
				char *node = strndup(url, service-url);

				const struct addrinfo hints = {
					.ai_family = udp->version == OSC_STREAM_IP_VERSION_4 ? PF_INET : PF_INET6,
					.ai_socktype = SOCK_DGRAM,
					.ai_protocol = IPPROTO_UDP,
					.ai_flags = 0
				};

				uv_getaddrinfo(loop, &udp->req, getaddrinfo_udp_tx_cb, node, service+1, &hints);
				free(node);
			}

			break;
		}
		case OSC_STREAM_TYPE_TCP:
		{
			osc_stream_tcp_t *tcp = &stream->payload.tcp;
			char *service = strchr(url, ':');

			if(!service)
			{
				fprintf(stderr, "url must have a service port\n");
				return -1;
			}

			if( (tcp->server = (url == service)) )
			{
				// resolve destination address
				const char *node = tcp->version == OSC_STREAM_IP_VERSION_4 ? "0.0.0.0" : "::";

				const struct addrinfo hints = {
					.ai_family = tcp->version == OSC_STREAM_IP_VERSION_4 ? PF_INET : PF_INET6,
					.ai_socktype = SOCK_STREAM,
					.ai_protocol = IPPROTO_TCP,
					.ai_flags = 0
				};

				uv_getaddrinfo(loop, &tcp->req, getaddrinfo_tcp_rx_cb, node, service+1, &hints);
			}
			else // !tcp->server
			{
				// resolve destination address
				char *node = strndup(url, service-url);

				const struct addrinfo hints = {
					.ai_family = tcp->version == OSC_STREAM_IP_VERSION_4 ? PF_INET : PF_INET6,
					.ai_socktype = SOCK_STREAM,
					.ai_protocol = IPPROTO_TCP,
					.ai_flags = 0
				};

				uv_getaddrinfo(loop, &tcp->req, getaddrinfo_tcp_tx_cb, node, service+1, &hints);
				free(node);
			}

			break;
		}
		case OSC_STREAM_TYPE_PIPE:
		{
			// check if file exists
			uv_fs_t req;
			uv_fs_stat(loop, &req, url, NULL); // synchronous
			if(req.result)
			{
				fprintf(stderr, "cannot find file: '%s'\n", url);
				return -1;
			}
			
			//TODO
			// check if file can be used as pipe
		
			int fd;
#if defined(__WINDOWS__)
			if( (fd = open(url, 0, 0)) < 0)
#else
			if( (fd = open(url, O_RDWR | O_NOCTTY | O_NONBLOCK, 0)) < 0)
#endif
			{
				fprintf(stderr, "cannot open file\n");
				return -1;
			}

			uv_handle_type type = uv_guess_handle(fd);
			if( (type != UV_NAMED_PIPE) && (type != UV_STREAM) && (type != UV_TTY))
			{
				fprintf(stderr, "not a serial line\n");
				return -1;
			}

			osc_stream_pipe_init(loop, stream, fd);

			break;
		}
	}

	return 0;
}

int
osc_stream_init(uv_loop_t *loop, osc_stream_t *stream, const char *addr, osc_stream_recv_cb_t recv_cb, osc_stream_send_cb_t send_cb, void *data)
{
	const char *url = NULL;
	if(_osc_stream_parse_protocol(stream, addr, &url))
	{
		fprintf(stderr, "unsupported protocol in address %s\n", addr);
		return -1;
	}

	if(_osc_stream_parse_url(loop, stream, url))
	{
		fprintf(stderr, "unsupported url in address %s\n", url);
		return -1;
	}

	stream->cb.recv = recv_cb;
	stream->cb.send = send_cb;
	stream->cb.data = data;

	return 0;
}

void
osc_stream_deinit(osc_stream_t *stream)
{
	int err;

	switch(stream->type)
	{
		case OSC_STREAM_TYPE_UDP:
		{
			osc_stream_udp_t *udp = &stream->payload.udp;

			if(uv_is_active((uv_handle_t *)&udp->socket))
			{
				if((err =	uv_udp_recv_stop(&udp->socket)))
					fprintf(stderr, "uv_udp_recv_stop: %s\n", uv_err_name(err));
				uv_close((uv_handle_t *)&udp->socket, NULL);
			}

			break;
		}
		case OSC_STREAM_TYPE_TCP:
		{
			osc_stream_tcp_t *tcp = &stream->payload.tcp;

			// close clients
			osc_stream_tcp_tx_t *tx;
			Inlist *l;
			INLIST_FOREACH_SAFE(tcp->tx, l, tx)
			{
				if(uv_is_active((uv_handle_t *)&tx->socket))
				{
					if((err = uv_read_stop((uv_stream_t *)&tx->socket)))
						fprintf(stderr, "uv_read_stop: %s\n", uv_err_name(err));
					uv_close((uv_handle_t *)&tx->socket, NULL);
				}

				if(uv_is_active((uv_handle_t *)&tx->req))
					uv_cancel((uv_req_t *)&tx->req);

				tcp->tx = inlist_remove(tcp->tx, INLIST_GET(tx));
				free(tx);
			}

			// close server
			if(tcp->server)
			{
				if(uv_is_active((uv_handle_t *)&tcp->socket))
					uv_close((uv_handle_t *)&tcp->socket, NULL);
			}
			else
			{
				if(uv_is_active((uv_handle_t *)&tcp->conn))
					uv_cancel((uv_req_t *)&tcp->conn);
			}

			break;
		}
		case OSC_STREAM_TYPE_PIPE:
		{
			osc_stream_pipe_t *pipe = &stream->payload.pipe;

			if(uv_is_active((uv_handle_t *)&pipe->socket))
			{
				if((err = uv_read_stop((uv_stream_t *)&pipe->socket)))
					fprintf(stderr, "uv_read_stop: %s\n", uv_err_name(err));
				uv_close((uv_handle_t *)&pipe->socket, NULL);
			}
			//TODO close(pipe->fd)?

			break;
		}
	}

	stream->cb.recv = NULL;
	stream->cb.send = NULL;
	stream->cb.data = NULL;
}

void
osc_stream_send(osc_stream_t *stream, const osc_data_t *buf, size_t len)
{
	switch(stream->type)
	{
		case OSC_STREAM_TYPE_UDP:
		{
			osc_stream_udp_send(stream, buf, len);
			break;
		}
		case OSC_STREAM_TYPE_TCP:
		{
			osc_stream_tcp_send(stream, buf, len);
			break;
		}
		case OSC_STREAM_TYPE_PIPE:
		{
			osc_stream_pipe_send(stream, buf, len);
			break;
		}
	}
}

void
osc_stream_send2(osc_stream_t *stream, const uv_buf_t *bufs, size_t bufn)
{
	switch(stream->type)
	{
		case OSC_STREAM_TYPE_UDP:
		{
			osc_stream_udp_send2(stream, bufs, bufn);
			break;
		}
		case OSC_STREAM_TYPE_TCP:
		{
			osc_stream_tcp_send2(stream, bufs, bufn);
			break;
		}
		case OSC_STREAM_TYPE_PIPE:
		{
			osc_stream_pipe_send2(stream, bufs, bufn);
			break;
		}
	}
}
