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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include <zita-alsa-pcmi.h>
#include <seq.h>

//#include <synthpod_app.h>
//#include <synthpod_ui.h>
//#include <synthpod_nsm.h>
//#include <ext_urid.h>

#include <Ecore.h>
#include <Eina.h>

class prog_t {
	public:
		prog_t(const char *play_name, const char *capt_name, unsigned srate,
			unsigned int frsize, unsigned int nfrags);
		~prog_t();

		static void *rt_thread(void *data, Eina_Thread thread);

	private:
		void *_rt_thread();

		Alsa_pcmi *_pcmi;
		volatile bool _kill;
		Eina_Thread _thread;

		unsigned int _srate;
		unsigned int _frsize;
		unsigned int _nfrags;
};

prog_t::prog_t(const char *play_name, const char *capt_name, unsigned srate,
	unsigned int frsize, unsigned int nfrags)
	:_kill(false), _srate(srate), _frsize(frsize), _nfrags(nfrags)
{
	unsigned int opts = Alsa_pcmi::DEBUG_ALL;
	//opts |= Alsa_pcmi::FORCE_2CH;

	_pcmi = new Alsa_pcmi(play_name, capt_name, NULL, srate, frsize, nfrags, opts);
	_pcmi->printinfo();
	
	Eina_Bool status = eina_thread_create(&_thread,
		EINA_THREAD_URGENT, -1, &prog_t::rt_thread, this); //TODO
}

prog_t::~prog_t()
{
	_kill = true;
	eina_thread_join(_thread);

	delete _pcmi;
}

void *
prog_t::rt_thread(void *data, Eina_Thread thread)
{
	prog_t *prog = (prog_t *)data;
	
	pthread_t self = pthread_self();

	struct sched_param schedp;
	memset(&schedp, 0, sizeof(struct sched_param));
	schedp.sched_priority = 70; //TODO make configurable
	
	if(schedp.sched_priority)
	{
		if(pthread_setschedparam(self, SCHED_RR, &schedp))
			fprintf(stderr, "pthread_setschedparam error\n");
	}

	return prog->_rt_thread();
}

void *
prog_t::_rt_thread()
{
	const int channels = 2;
	float inp [channels][_frsize];
	float out [channels][_frsize];
	
	_pcmi->pcm_start();

	while(!_kill)
	{
		int na = _pcmi->pcm_wait();

		while(na >= _frsize)
		{
			_pcmi->capt_init(_frsize);
			for(int i=0; i<channels; i++)
				_pcmi->capt_chan(i, inp[i], _frsize);
			_pcmi->capt_done(_frsize);

			//memcpy(out[0], inp[0], _frsize * sizeof(float));
			//memcpy(out[1], inp[1], _frsize * sizeof(float));

			for(int i=0; i<channels; i++)
				for(int j=0; j<_frsize; j++)
					out[i][j] = (float)rand() / RAND_MAX;

			_pcmi->play_init(_frsize);
			for(int i=0; i<channels; i++)
				_pcmi->play_chan(i, out[i], _frsize);
			_pcmi->play_done(_frsize);

			na -= _frsize;
		}
	}

	_pcmi->pcm_stop();

	return NULL;
}
	
int
main(int argc, char **argv)
{
	const char *play_name = "hw:0";
	const char *capt_name = "hw:0";
	unsigned int srate = 48000;
	unsigned int frsize = 1024;
	unsigned int nfrags = 2;
	
	int c;
	while((c = getopt(argc, argv, "vhi:o:r:p:n:s:")) != -1)
	{
		switch(c)
		{
			case 'v':
				fprintf(stderr, "Synthpod " SYNTHPOD_VERSION "\n"
					"\n"
					"Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)\n"
					"\n"
					"This is free software: you can redistribute it and/or modify\n"
					"it under the terms of the Artistic License 2.0 as published by\n"
					"The Perl Foundation.\n"
					"\n"
					"This source is distributed in the hope that it will be useful,\n"
					"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
					"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
					"Artistic License 2.0 for more details.\n"
					"\n"
					"You should have received a copy of the Artistic License 2.0\n"
					"along the source as a COPYING file. If not, obtain it from\n"
					"http://www.perlfoundation.org/artistic_license_2_0.\n\n");
				return 0;
			case 'h':
				fprintf(stderr,
					"USAGE\n"
					"   %s [OPTIONS] [BUNDLE_PATH]\n"
					"\n"
					"OPTIONS\n"
					"   [-v]                 print version and license information\n"
					"   [-h]                 print usage information\n"
					"   [-i] capture-device  capture device\n"
					"   [-o] playback-device playback device\n"
					"   [-r] sample-rate     sample rate\n"
					"   [-p] sample-period   frames per period\n"
					"   [-n] period-number   number of periods of playback latency\n"
					"   [-s] sequence-size   minimum sequence size\n\n"
					, argv[0]);
				return 0;
			case 'i':
				capt_name = optarg;
				break;
			case 'o':
				play_name = optarg;
				break;
			case 'r':
				srate = atoi(optarg);
				break;
			case 'p':
				frsize = atoi(optarg);
				break;
			case 'n':
				nfrags = atoi(optarg);
				break;
			case 's':
				//TODO
				break;
			case '?':
				if( (optopt == 'i') || (optopt == 'o') || (optopt == 'r')
					  || (optopt == 'p') || (optopt == 'n') || (optopt == 's') )
					fprintf(stderr, "Option `-%c' requires an argument.\n", optopt);
				else if(isprint(optopt))
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
				else
					fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
				return -1;
			default:
				return -1;
		}
	}

	ecore_init();
	eina_init();

	prog_t *prog = new prog_t(play_name, capt_name, srate, frsize, nfrags);
	if(prog)
	{
		ecore_main_loop_begin();

		delete prog;
	}

	eina_shutdown();
	ecore_shutdown();

	return 0;
}
