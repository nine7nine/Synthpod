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

#include <ext_worker.h>

#include <Eina.h>

struct _ext_worker_t {
	LV2_Worker_Schedule schedule;
	LV2_Worker_Interface *interface;
};

static LV2_Worker_Status
_worker_schedule(LV2_Worker_Schedule_Handle handle,
	uint32_t size, const void *data)
{
	//TODO ringbuffer etc.

	return LV2_WORKER_SUCCESS;
}

ext_worker_t *
ext_worker_new()
{
	ext_worker_t *ext_worker = malloc(sizeof(ext_worker_t));

	//TODO initialize
	ext_worker->schedule.handle = ext_worker;
	ext_worker->schedule.schedule_work = _worker_schedule;

	return ext_worker;
}

void
ext_worker_free(ext_worker_t *ext_worker)
{
	free(ext_worker);
}
