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

#include <osc_stream.h>

#include <inlist.h>
#include <osc.h>

#ifndef OSC_STREAM_BUF_SIZE
#	define OSC_STREAM_BUF_SIZE 2048 //TODO how big?
#endif

typedef enum _osc_stream_type_t osc_stream_type_t;
typedef enum _osc_stream_ip_version_t osc_stream_ip_version_t;

typedef struct _osc_stream_udp_t osc_stream_udp_t;
typedef struct _osc_stream_udp_tx_t osc_stream_udp_tx_t;
typedef struct _osc_stream_tcp_t osc_stream_tcp_t;
typedef struct _osc_stream_tcp_tx_t osc_stream_tcp_tx_t;
typedef struct _osc_stream_pipe_t osc_stream_pipe_t;

typedef union _osc_stream_addr_t osc_stream_addr_t;

enum _osc_stream_type_t {
	OSC_STREAM_TYPE_UDP,
	OSC_STREAM_TYPE_TCP,
	OSC_STREAM_TYPE_PIPE
};

enum _osc_stream_ip_version_t {
	OSC_STREAM_IP_VERSION_4,
	OSC_STREAM_IP_VERSION_6,
};

union _osc_stream_addr_t {
	const struct sockaddr ip;
	struct sockaddr_in ip4;
	struct sockaddr_in6 ip6;
};
	
struct _osc_stream_udp_tx_t {
	osc_stream_addr_t addr;
	uv_udp_send_t req;
	uv_buf_t msg;
};

struct _osc_stream_udp_t {
	osc_stream_ip_version_t version;
	int server;
	uv_getaddrinfo_t req;
	uv_udp_t socket;
	osc_stream_udp_tx_t tx;
};

struct _osc_stream_tcp_tx_t {
	INLIST;
	uv_tcp_t socket;
	uv_write_t req;
};

struct _osc_stream_tcp_t {
	osc_stream_ip_version_t version;
	int slip;
	int server;
	uv_getaddrinfo_t req;
	
	Inlist *tx;
	int count;
	size_t nchunk;

	// responder only
	uv_tcp_t socket;

	//sender only
	uv_connect_t conn;

	int32_t prefix;
	uv_buf_t msg[2];
	char buf_rx [OSC_STREAM_BUF_SIZE];
	char buf_tx [OSC_STREAM_BUF_SIZE];
};

struct _osc_stream_pipe_t {
	uv_pipe_t socket;
	uv_write_t req;
	uv_buf_t msg;
	int fd;
	size_t nchunk;
	char buf_rx [OSC_STREAM_BUF_SIZE];
	char buf_tx [OSC_STREAM_BUF_SIZE];
};

struct _osc_stream_t {
	osc_stream_type_t type;
	osc_stream_driver_t *driver;
	void *data;
	int flushing;

	union {
		osc_stream_udp_t udp;
		osc_stream_tcp_t tcp;
		osc_stream_pipe_t pipe;
	} payload;
};

extern const char resolve_msg [20];
extern const char timeout_msg [20];
extern const char connect_msg [20];
extern const char disconnect_msg [24];

void instant_recv(osc_stream_t *stream, const void *buf, size_t size);

void getaddrinfo_udp_tx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res);
void getaddrinfo_udp_rx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res);
void osc_stream_udp_flush(osc_stream_t *stream);

void getaddrinfo_tcp_tx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res);
void getaddrinfo_tcp_rx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res);
void osc_stream_tcp_flush(osc_stream_t *stream);

void osc_stream_pipe_init(uv_loop_t *loop, osc_stream_t *stream, int fd);
void osc_stream_pipe_flush(osc_stream_t *stream);

size_t slip_encode(char *buf, uv_buf_t *bufs, int nbufs);
size_t slip_decode(char *buf, size_t len, size_t *size);
