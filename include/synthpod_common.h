/*
 * Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#ifndef _SYNTHPOD_COMMON_H
#define _SYNTHPOD_COMMON_H

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <ftw.h>

#define SYNTHPOD_PREFIX					"http://open-music-kontrollers.ch/lv2/synthpod#"

#define SYNTHPOD_STEREO_URI			SYNTHPOD_PREFIX"stereo"
#define SYNTHPOD_COMMON_NK_URI	SYNTHPOD_PREFIX"common_4_nk"
#define SYNTHPOD_ROOT_NK_URI	  SYNTHPOD_PREFIX"root_4_nk"

#ifdef _WIN32
#	define SYNTHPOD_SYMBOL_EXTERN __declspec(dllexport)
#	define mlock(...)
#	define munlock(...)
#else
#	define SYNTHPOD_SYMBOL_EXTERN __attribute__((visibility("default")))
#	include <sys/mman.h> // mlock
#endif

#include <xpress.lv2/xpress.h>

#define __realtime __attribute__((annotate("realtime")))
#define __non_realtime __attribute__((annotate("non-realtime")))

static inline int
mkpath(char *path)
{
	struct stat sb;
	char *slash;
	bool done = false;

	slash = path;

	while(!done)
	{
		slash += strspn(slash, "/");
		slash += strcspn(slash, "/");

		done = *slash == '\0';
		*slash = '\0';

		if(stat(path, &sb))
		{
			if(  (errno != ENOENT)
				|| (mkdir(path, 0777) && (errno != EEXIST)) )
			{
				return -1;
			}
		}
		else if(!S_ISDIR(sb.st_mode))
		{
			return -1;
		}

		*slash = '/';
	}

	return 0;
}

static inline int
mkpath_const(const char *path)
{
	char *dup = strdup(path);
	if(!dup)
		return -1;

	const int ret = mkpath(dup);
	free(dup);
	return ret;
}

static inline int
_unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
	return remove(fpath);
}

static inline int
rmrf_const(const char *path)
{
	return nftw(path, _unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}

#endif // _SYNTHPOD_COMMON_H
