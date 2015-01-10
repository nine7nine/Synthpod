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

extern const char resolve_msg [20];
extern const char timeout_msg [20];
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
