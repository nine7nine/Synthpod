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

#ifndef _OSC_STREAM_H_
#define _OSC_STREAM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <uv.h>
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
typedef struct _osc_stream_cb_t osc_stream_cb_t;
typedef struct _osc_stream_t osc_stream_t;

typedef union _osc_stream_addr_t osc_stream_addr_t;

typedef void (*osc_stream_recv_cb_t) (osc_stream_t *stream, osc_data_t *buf, size_t len , void *data);
typedef void (*osc_stream_send_cb_t) (osc_stream_t *stream, size_t len , void *data);

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
	size_t len;
};

struct _osc_stream_cb_t {
	osc_stream_recv_cb_t recv;
	osc_stream_send_cb_t send;
	void *data;
	char buf [OSC_STREAM_BUF_SIZE];
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
	size_t len;
	int count;
	size_t nchunk;

	// responder only
	uv_tcp_t socket;

	//sender only
	uv_connect_t conn;
};

struct _osc_stream_pipe_t {
	uv_pipe_t socket;
	uv_write_t req;
	size_t len;
	int fd;
	size_t nchunk;
};

struct _osc_stream_t {
	osc_stream_type_t type;

	union {
		osc_stream_udp_t udp;
		osc_stream_tcp_t tcp;
		osc_stream_pipe_t pipe;
	} payload;

	osc_stream_cb_t cb;
};

int osc_stream_init(uv_loop_t *loop, osc_stream_t *stream, const char *addr, osc_stream_recv_cb_t recv_cb, osc_stream_send_cb_t send_cb, void *data);
void osc_stream_deinit(osc_stream_t *stream);
void osc_stream_send(osc_stream_t *stream, const osc_data_t *buf, size_t len);
void osc_stream_send2(osc_stream_t *stream, const uv_buf_t *bufs, size_t bufn);

#ifdef __cplusplus
}
#endif

#endif
