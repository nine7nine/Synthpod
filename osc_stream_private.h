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

#include <osc_stream.h>

extern const char resolve_msg [20];
extern const char connect_msg [20];
extern const char disconnect_msg [24];

void getaddrinfo_udp_tx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res);
void getaddrinfo_udp_rx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res);
void osc_stream_udp_send(osc_stream_t *stream, const osc_data_t *buf, size_t len);
void osc_stream_udp_send2(osc_stream_t *stream, const uv_buf_t *bufs, size_t bufn);

void getaddrinfo_tcp_tx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res);
void getaddrinfo_tcp_rx_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res);
void osc_stream_tcp_send(osc_stream_t *stream, const osc_data_t *buf, size_t len);
void osc_stream_tcp_send2(osc_stream_t *stream, const uv_buf_t *bufs, size_t bufn);

void osc_stream_pipe_init(uv_loop_t *loop, osc_stream_t *stream, int fd);
void osc_stream_pipe_send(osc_stream_t *stream, const osc_data_t *buf, size_t len);
void osc_stream_pipe_send2(osc_stream_t *stream, const uv_buf_t *bufs, size_t bufn);

size_t slip_encode(char *buf, uv_buf_t *bufs, int nbufs);
size_t slip_decode(char *buf, size_t len, size_t *size);
