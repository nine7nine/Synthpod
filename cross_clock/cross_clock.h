/*
 * Copyright (c) 2018 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#ifndef _CROSS_CLOCK_H
#define _CROSS_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include <stdbool.h>

#ifdef __APPLE__
#	include <mach/clock.h>
#	include <mach/mach.h>
#endif

#ifndef CROSS_CLOCK_API
#	define CROSS_CLOCK_API static
#endif

typedef struct _cross_clock_t cross_clock_t;

typedef enum _cross_clock_id_t {
	CROSS_CLOCK_REALTIME,
	CROSS_CLOCK_MONOTONIC
} cross_clock_id_t;

CROSS_CLOCK_API int
cross_clock_init(cross_clock_t *clock, cross_clock_id_t clock_type);

CROSS_CLOCK_API int
cross_clock_deinit(cross_clock_t *clock);

CROSS_CLOCK_API int
cross_clock_gettime(cross_clock_t *clock, struct timespec *ts);

CROSS_CLOCK_API int
cross_clock_nanosleep(cross_clock_t *clock, bool absolute,
	const struct timespec *ts);

#ifdef CROSS_CLOCK_IMPLEMENTATION

struct _cross_clock_t {
#ifdef __APPLE__
	clock_serv_t serv;
#else
	clockid_t id;
#endif
};

CROSS_CLOCK_API int
cross_clock_init(cross_clock_t *clock, cross_clock_id_t clock_type)
{
	switch(clock_type)
	{
		case CROSS_CLOCK_REALTIME:
		{
#ifdef __APPLE__
			host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &clock->serv);
#else
			clock->id = CLOCK_REALTIME;
#endif
		}	break;
		case CROSS_CLOCK_MONOTONIC:
		{
#ifdef __APPLE__
			host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &clock->serv);
#else
			clock->id = CLOCK_MONOTONIC;
#endif
		}	break;
		default:
		{
		} return -1;
	}

	return 0;
}

CROSS_CLOCK_API int
cross_clock_deinit(cross_clock_t *clock)
{
#ifdef __APPLE__
	mach_port_deallocate(mach_task_self(), clock->serv);
#else
	(void)clock;
#endif

	return 0;
}

CROSS_CLOCK_API int
cross_clock_gettime(cross_clock_t *clock, struct timespec *ts)
{
	int res;

#ifdef __APPLE__
	mach_timespec_t mts;
	res = clock_get_time(clock->serv, &mts);

	if(res == 0)
	{
		ts->tv_sec = mts.tv_sec;
		ts->tv_nsec = mts.tv_nsec;
	}
#else
	res = clock_gettime(clock->id, ts);
#endif

	return res;
}

CROSS_CLOCK_API int
cross_clock_nanosleep(cross_clock_t *clock, bool absolute,
	const struct timespec *ts)
{
	int res;

#ifdef __APPLE__
	const mach_timespec_t mts = {
		.tv_sec = ts->tv_sec,
		.tv_nsec = ts->tv_nsec
	};
	const sleep_type_t flag = absolute ? TIME_ABSOLUTE : TIME_RELATIVE;
	mach_timespec_t mrm;

	res = clock_sleep(clock->serv, flag, mts, &mrm);
#else
	const int flag = absolute ? TIMER_ABSTIME : 0;

	res = clock_nanosleep(clock->id, flag, ts, NULL);
#endif

	return res;
}

#endif // CROSS_CLOCK_IMPLEMENTATION

#ifdef __cplusplus
}
#endif

#endif //_CROSS_CLOCK_H
