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
