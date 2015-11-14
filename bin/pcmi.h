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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef void pcmi_t;

pcmi_t *
pcmi_new(const char *play_name, const char *capt_name, uint32_t srate,
	uint32_t frsize, uint32_t nfrags, bool twochan, bool debug);

void
pcmi_free(pcmi_t *pcmi);

void
pcmi_printinfo(pcmi_t *pcmi);

int
pcmi_ncapt(pcmi_t *pcmi);

int
pcmi_nplay(pcmi_t *pcmi);

void
pcmi_pcm_start(pcmi_t *pcmi);

int
pcmi_pcm_wait(pcmi_t *pcmi);

int
pcmi_pcm_idle(pcmi_t *pcmi, uint32_t frsize);

void
pcmi_pcm_stop(pcmi_t *pcmi);

void
pcmi_capt_init(pcmi_t *pcmi, uint32_t frsize);

void
pcmi_capt_chan(pcmi_t *pcmi, uint32_t channel, float *dst, uint32_t frsize);

void
pcmi_capt_done(pcmi_t *pcmi, uint32_t frsize);

void
pcmi_play_init(pcmi_t *pcmi, uint32_t frsize);

void
pcmi_clear_chan(pcmi_t *pcmi, uint32_t channel, uint32_t frsize);

void
pcmi_play_chan(pcmi_t *pcmi, uint32_t channel, const float *src, uint32_t frsize);

void
pcmi_play_done(pcmi_t *pcmi, uint32_t frsize);

#ifdef __cplusplus
}
#endif
