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

#include <pcmi.h>
#include <zita-alsa-pcmi.h>

pcmi_t *
pcmi_new(const char *play_name, const char *capt_name, uint32_t srate,
	uint32_t frsize, uint32_t nfrags, bool twochan, bool debug)
{
	unsigned int opts = 0;
	if(debug)
		opts |= Alsa_pcmi::DEBUG_ALL;
	if(twochan) // force 2 channels
		opts |= Alsa_pcmi::FORCE_2CH;

	Alsa_pcmi *_pcmi = new Alsa_pcmi(play_name, capt_name, NULL,
		srate, frsize, nfrags, opts);
	if(_pcmi->state())
	{
		delete _pcmi;
		return NULL;
	}

	return (pcmi_t *)_pcmi;
}

void
pcmi_free(pcmi_t *pcmi)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	delete _pcmi;
}

void
pcmi_printinfo(pcmi_t *pcmi)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	_pcmi->printinfo();
}

int
pcmi_ncapt(pcmi_t *pcmi)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	return _pcmi->ncapt();
}

int
pcmi_nplay(pcmi_t *pcmi)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	return _pcmi->nplay();
}

void
pcmi_pcm_start(pcmi_t *pcmi)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	_pcmi->pcm_start();
}

int
pcmi_pcm_wait(pcmi_t *pcmi)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	return _pcmi->pcm_wait();
}

int
pcmi_pcm_idle(pcmi_t *pcmi, uint32_t frsize)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	return _pcmi->pcm_idle(frsize);
}

void
pcmi_pcm_stop(pcmi_t *pcmi)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	_pcmi->pcm_stop();
}

void
pcmi_capt_init(pcmi_t *pcmi, uint32_t frsize)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	_pcmi->capt_init(frsize);
}

void
pcmi_capt_chan(pcmi_t *pcmi, uint32_t channel, float *dst, uint32_t frsize)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	_pcmi->capt_chan(channel, dst, frsize);
}

void
pcmi_capt_done(pcmi_t *pcmi, uint32_t frsize)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	_pcmi->capt_done(frsize);
}

void
pcmi_play_init(pcmi_t *pcmi, uint32_t frsize)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	_pcmi->play_init(frsize);
}

void
pcmi_clear_chan(pcmi_t *pcmi, uint32_t channel, uint32_t frsize)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	_pcmi->clear_chan(channel, frsize);
}

void
pcmi_play_chan(pcmi_t *pcmi, uint32_t channel, const float *src, uint32_t frsize)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	_pcmi->play_chan(channel, src, frsize);
}

void
pcmi_play_done(pcmi_t *pcmi, uint32_t frsize)
{
	Alsa_pcmi *_pcmi = (Alsa_pcmi *)pcmi;

	_pcmi->play_done(frsize);
}
