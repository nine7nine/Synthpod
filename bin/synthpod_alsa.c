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

#include <pcmi.h>
//#include <seq.h>

#include <Elementary.h>

typedef struct _prog_t prog_t;

struct _prog_t {
	pcmi_t *pcmi;
	volatile int kill;
	Eina_Thread thread;

	uint32_t srate;
	uint32_t frsize;
	uint32_t nfrags;
};

void *
_rt_thread(void *data, Eina_Thread thread)
{
	prog_t *prog = data;
	pcmi_t *pcmi = prog->pcmi;
	
	pthread_t self = pthread_self();

	struct sched_param schedp;
	memset(&schedp, 0, sizeof(struct sched_param));
	schedp.sched_priority = 70; //TODO make configurable
	
	if(schedp.sched_priority)
	{
		if(pthread_setschedparam(self, SCHED_RR, &schedp))
			fprintf(stderr, "pthread_setschedparam error\n");
	}

	const int nplay = pcmi_nplay(pcmi);
	const int ncapt = pcmi_ncapt(pcmi);

	float inp [ncapt][prog->frsize];
	float out [nplay][prog->frsize];

	pcmi_pcm_start(pcmi);

	while(!prog->kill)
	{
		int na = pcmi_pcm_wait(pcmi);

		while(na >= prog->frsize)
		{
			if( ncapt)
			{
				pcmi_capt_init(pcmi, prog->frsize);
				for(int i=0; i<ncapt; i++)
					pcmi_capt_chan(pcmi, i, inp[i], prog->frsize);
				pcmi_capt_done(pcmi, prog->frsize);
			}

			if(nplay)
			{
				//memcpy(out[0], inp[0], prog->frsize * sizeof(float));
				//memcpy(out[1], inp[1], prog->frsize * sizeof(float));

				for(int i=0; i<nplay; i++)
					for(int j=0; j<prog->frsize; j++)
						out[i][j] = (float)rand() / RAND_MAX;

				pcmi_play_init(pcmi, prog->frsize);
				for(int i=0; i<nplay; i++)
					pcmi_play_chan(pcmi, i, out[i], prog->frsize);
				pcmi_play_done(pcmi, prog->frsize);
			}

			na -= prog->frsize;
		}
	}

	pcmi_pcm_stop(pcmi);

	return NULL;
}
	
#if defined(BUILD_UI)
EAPI_MAIN int
elm_main(int argc, char **argv)
#else
int
main(int argc, char **argv)
#endif
{
	static prog_t prog;
	prog.srate = 48000;
	prog.frsize = 1024;
	prog.nfrags = 3;
	int twochan = 0;

	const char *def = "default";
	const char *play_name = def;
	const char *capt_name = def;
	
	int c;
	while((c = getopt(argc, argv, "vh2i:o:r:p:n:s:")) != -1)
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
					"   [-2]                 force 2 channel mode\n"
					"   [-i] capture-device  capture device\n"
					"   [-o] playback-device playback device\n"
					"   [-r] sample-rate     sample rate\n"
					"   [-p] sample-period   frames per period\n"
					"   [-n] period-number   number of periods of playback latency\n"
					"   [-s] sequence-size   minimum sequence size\n\n"
					, argv[0]);
				return 0;
			case '2':
				twochan = 1;
				break;
			case 'i':
				capt_name = optarg;
				break;
			case 'o':
				play_name = optarg;
				break;
			case 'r':
				prog.srate = atoi(optarg);
				break;
			case 'p':
				prog.frsize = atoi(optarg);
				break;
			case 'n':
				prog.nfrags = atoi(optarg);
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

	prog.pcmi = pcmi_new(play_name, capt_name, prog.srate, prog.frsize, prog.nfrags, twochan);
	pcmi_printinfo(prog.pcmi);
	
	Eina_Bool status = eina_thread_create(&prog.thread,
		EINA_THREAD_URGENT, -1, _rt_thread, &prog); //TODO

	// create main window
	//ui_anim = ecore_animator_add(_ui_animator, &handle);
	Evas_Object *win = elm_win_util_standard_add("synthpod", "Synthpod");
	//evas_object_smart_callback_add(win, "delete,request", _ui_delete_request, &handle);
	evas_object_resize(win, 1280, 720);
	evas_object_show(win);

#if defined(BUILD_UI)
	elm_run();
#else
	ecore_main_loop_begin();
#endif

	evas_object_del(win);

	prog.kill = 1;
	eina_thread_join(prog.thread);

	pcmi_free(prog.pcmi);

	return 0;
}

#if defined(BUILD_UI)
ELM_MAIN()
#endif
