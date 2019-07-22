/*
 * Copyright (c) 2018-2019 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#include <stdarg.h>
#include <stdio.h>

#include <d2tk/hash.h>

#include "mum.h"

#define SEED 12345

__attribute__((always_inline))
static inline size_t
_sz(const void *data, ssize_t nbytes)
{
	if(nbytes == -1) // is a null-terminated string
	{
		return strlen((const char *)data);
	}

	return nbytes;
}

D2TK_API uint64_t
d2tk_hash(const void *data, ssize_t nbytes)
{
	nbytes = _sz(data, nbytes);

	return mum_hash(data, nbytes, SEED);
}

D2TK_API uint64_t
d2tk_hash_foreach(const void *data, ssize_t nbytes, ...)
{
	va_list args;
	const void *src;

	nbytes = _sz(data, nbytes);

	// derive total temporary buffer size
	size_t sz = nbytes;

	va_start(args, nbytes);

	while( (src = va_arg(args, const void *)) )
	{
		sz += _sz(src, va_arg(args, int));
	}

	va_end(args);

	// fill temporary bufffer
	uint8_t *dst = alloca(sz);
	if(dst)
	{
		size_t off = 0;

		memcpy(&dst[off], data, nbytes);
		off += nbytes;

		va_start(args, nbytes);

		while( (src = va_arg(args, const void *)) )
		{
			nbytes = _sz(src, va_arg(args, int));

			memcpy(&dst[off], src, nbytes);
			off += nbytes;
		}

		va_end(args);

		return mum_hash(dst, sz, SEED);
	}

	return 0;
}
