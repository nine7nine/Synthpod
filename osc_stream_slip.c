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

#include <uv.h>

#define SLIP_END					(char)0300	// indicates end of packet
#define SLIP_ESC					(char)0333	// indicates byte stuffing
#define SLIP_END_REPLACE	(char)0334	// ESC ESC_END means END data byte
#define SLIP_ESC_REPLACE	(char)0335	// ESC ESC_ESC means ESC data byte

// SLIP encoding
size_t
slip_encode(char *buf, uv_buf_t *bufs, int nbufs)
{
	char *dst = buf;

	int i;
	for(i=0; i<nbufs; i++)
	{
		uv_buf_t *ptr = &bufs[i];
		char *base = (char *)ptr->base;
		char *end = base + ptr->len;

		char *src;
		for(src=base; src<end; src++)
			switch(*src)
			{
				case SLIP_END:
					*dst++ = SLIP_ESC;
					*dst++ = SLIP_END_REPLACE;
					break;
				case SLIP_ESC:
					*dst++ = SLIP_ESC;
					*dst++ = SLIP_ESC_REPLACE;
					break;
				default:
					*dst++ = *src;
					break;
			}
	}
	*dst++ = SLIP_END;

	return dst - buf;
}

// inline SLIP decoding
size_t
slip_decode(char *buf, size_t len, size_t *size)
{
	char *src = buf;
	char *end = buf + len;
	char *dst = buf;

	while(src < end)
	{
		if(*src == SLIP_ESC)
		{
			src++;
			if(*src == SLIP_END_REPLACE)
				*dst++ = SLIP_END;
			else if(*src == SLIP_ESC_REPLACE)
				*dst++ = SLIP_ESC;
			else
				; //TODO error
			src++;
		}
		else if(*src == SLIP_END)
		{
			src++;
			*size = dst - buf;
			return src - buf;
		}
		else
		{
			if(src != dst)
				*dst = *src;
			src++;
			dst++;
		}
	}
	*size = 0;
	return 0;
}
