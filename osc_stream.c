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

OSC_STREAM_API osc_stream_t *
osc_stream_new(uv_loop_t *loop, const char *addr, 
	osc_stream_driver_t *driver, void *data)
{
	osc_stream_t *stream = calloc(1, sizeof(osc_stream_t));
	if(!stream)
		return NULL;

	const char *url = NULL;
	if(_osc_stream_parse_protocol(stream, addr, &url))
	{
		fprintf(stderr, "unsupported protocol in address %s\n", addr);
		return NULL;
	}

	if(_osc_stream_parse_url(loop, stream, url))
	{
		fprintf(stderr, "unsupported url in address %s\n", url);
		return NULL;
	}

	stream->driver = driver;
	stream->data = data;

	return stream;
}

static void
_udp_close_cb(uv_handle_t *handle)
{
	uv_udp_t *socket = (uv_udp_t *)handle;
	osc_stream_udp_t *udp = (void *)socket - offsetof(osc_stream_udp_t, socket);
	osc_stream_t *stream = (void *)udp - offsetof(osc_stream_t, payload.udp);

	free(stream);
}

static void
_tcp_close_client_cb(uv_handle_t *handle)
{
	uv_stream_t *socket = (uv_stream_t *)handle;
	osc_stream_tcp_tx_t *tx = (void *)socket - offsetof(osc_stream_tcp_tx_t, socket);
	osc_stream_tcp_t *tcp = (void *)tx - offsetof(osc_stream_tcp_t, tx);
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, payload.tcp);

	if(!tcp->server) // is client
		free(stream);
}

static void
_tcp_close_server_cb(uv_handle_t *handle)
{
	uv_stream_t *socket = (uv_stream_t *)handle;
	osc_stream_tcp_t *tcp = (void *)socket - offsetof(osc_stream_tcp_t, socket);
	osc_stream_t *stream = (void *)tcp - offsetof(osc_stream_t, payload.tcp);

	free(stream);
}

static void
_pipe_close_cb(uv_handle_t *handle)
{
	uv_stream_t *socket = (uv_stream_t *)handle;
	osc_stream_pipe_t *pipe = (void *)socket - offsetof(osc_stream_pipe_t, socket);
	osc_stream_t *stream = (void *)pipe - offsetof(osc_stream_t, payload.pipe);

	free(stream);
}

OSC_STREAM_API void
osc_stream_free(osc_stream_t *stream)
{
	int err;

	switch(stream->type)
	{
		case OSC_STREAM_TYPE_UDP:
		{
			osc_stream_udp_t *udp = &stream->payload.udp;

			// cancel resolve
			uv_cancel((uv_req_t *)&udp->req);

			if(uv_is_active((uv_handle_t *)&udp->socket))
			{
				if((err =	uv_udp_recv_stop(&udp->socket)))
					fprintf(stderr, "uv_udp_recv_stop: %s\n", uv_err_name(err));
			}
			uv_close((uv_handle_t *)&udp->socket, _udp_close_cb);

			break;
		}
		case OSC_STREAM_TYPE_TCP:
		{
			osc_stream_tcp_t *tcp = &stream->payload.tcp;
		
			// cancel resolve
			uv_cancel((uv_req_t *)&tcp->req);

			// close clients
			osc_stream_tcp_tx_t *tx = &stream->payload.tcp.tx;
			if(uv_is_active((uv_handle_t *)&tx->req))
				uv_cancel((uv_req_t *)&tx->req);

			if(tcp->connected)	
			{
				if(uv_is_active((uv_handle_t *)&tx->socket))
				{
					if((err = uv_read_stop((uv_stream_t *)&tx->socket)))
						fprintf(stderr, "uv_read_stop: %s\n", uv_err_name(err));
				}
				uv_close((uv_handle_t *)&tx->socket, _tcp_close_client_cb);
			}

			// close server
			if(tcp->server)
			{
				if(uv_is_active((uv_handle_t *)&tcp->socket))
					uv_close((uv_handle_t *)&tcp->socket, _tcp_close_server_cb);
				else
					free(stream);
			}
			else
			{
				if(uv_is_active((uv_handle_t *)&tcp->conn))
					uv_cancel((uv_req_t *)&tcp->conn);

				if(!tcp->connected)
					free(stream);
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
			}
			uv_close((uv_handle_t *)&pipe->socket, _pipe_close_cb);

			//TODO close(pipe->fd)?

			break;
		}
	}
}

OSC_STREAM_API void
osc_stream_flush(osc_stream_t *stream)
{
	switch(stream->type)
	{
		case OSC_STREAM_TYPE_UDP:
		{
			osc_stream_udp_flush(stream);
			break;
		}
		case OSC_STREAM_TYPE_TCP:
		{
			osc_stream_tcp_flush(stream);
			break;
		}
		case OSC_STREAM_TYPE_PIPE:
		{
			osc_stream_pipe_flush(stream);
			break;
		}
	}
}
