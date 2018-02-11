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

typedef struct _nsmc_t nsmc_t;
typedef struct _nsmc_driver_t nsmc_driver_t;
	
typedef int (*nsmc_open_t)(const char *path, const char *name,
	const char *id, void *data);
typedef int (*nsmc_save_t)(void *data);
typedef int (*nsmc_show_t)(void *data);
typedef int (*nsmc_hide_t)(void *data);

struct _nsmc_driver_t {
	nsmc_open_t open;
	nsmc_save_t save;
	nsmc_show_t show;
	nsmc_hide_t hide;
};

nsmc_t *
nsmc_new(const char *exe, const char *path,
	const nsmc_driver_t *driver, void *data);

void
nsmc_free(nsmc_t *nsm);

void
nsmc_run(nsmc_t *nsm);

void
nsmc_opened(nsmc_t *nsm, int status);

void
nsmc_shown(nsmc_t *nsm);

void
nsmc_hidden(nsmc_t *nsm);

void
nsmc_saved(nsmc_t *nsm, int status);

bool
nsmc_managed(nsmc_t *nsm);
