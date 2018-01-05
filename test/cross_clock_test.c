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

#include <assert.h>

#define CROSS_CLOCK_IMPLEMENTATION
#include <cross_clock/cross_clock.h>

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	cross_clock_t clock;
	struct timespec ts = {
		.tv_sec = 1,
		.tv_nsec = 0
	};

	assert(cross_clock_init(&clock, CROSS_CLOCK_MONOTONIC) == 0);
	assert(cross_clock_nanosleep(&clock, false, &ts) == 0);
	assert(cross_clock_gettime(&clock, &ts) == 0);
	assert( (ts.tv_sec != 0) || (ts.tv_nsec != 0) );
	assert(cross_clock_deinit(&clock) == 0);

	return 0;
}
