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

#include <asoundlib.h>
#include <pcmi.h>

#include <synthpod_bin.h>

#define MIDI_SEQ_SIZE 2048

typedef enum _chan_type_t chan_type_t;
typedef struct _prog_t prog_t;
typedef struct _chan_t chan_t;

enum _chan_type_t {
	CHAN_TYPE_PCMI,
	CHAN_TYPE_MIDI
};

struct _chan_t {
	chan_type_t type;

	union {
		struct {
			//TODO
		} audio;
		struct {
			int port;
			snd_midi_event_t *trans;
			LV2_Atom_Forge forge;
			LV2_Atom_Forge_Frame frame;
		} midi;
	};
};

struct _prog_t {
	bin_t bin;
	
	LV2_Atom_Forge forge;
	
	LV2_URID midi_MidiEvent;

	pcmi_t *pcmi;
	snd_seq_t *seq;
	int queue;
	uint8_t m [MIDI_SEQ_SIZE];

	save_state_t save_state;
	volatile int kill;
	Eina_Thread thread;

	uint32_t srate;
	uint32_t frsize;
	uint32_t nfrags;
	int twochan;
	int debug;

	const char *play_name;
	const char *capt_name;
};

static void *
_rt_thread(void *data, Eina_Thread thread)
{
	prog_t *handle = data;
	pcmi_t *pcmi = handle->pcmi;
	bin_t *bin = &handle->bin;
	sp_app_t *app = bin->app;
	
	pthread_t self = pthread_self();

	struct sched_param schedp;
	memset(&schedp, 0, sizeof(struct sched_param));
	schedp.sched_priority = 70; //TODO make configurable
	
	if(schedp.sched_priority)
	{
		if(pthread_setschedparam(self, SCHED_RR, &schedp))
			fprintf(stderr, "pthread_setschedparam error\n");
	}
		
	const uint32_t nsamples = handle->frsize;
	const size_t sample_buf_size = sizeof(float) * nsamples;
	int nplay = pcmi_nplay(pcmi);
	int ncapt = pcmi_ncapt(pcmi);
	int play_num;
	int capt_num;

	pcmi_pcm_start(handle->pcmi);
	while(!handle->kill)
	{
		for(int na = pcmi_pcm_wait(pcmi); na >= nsamples; na -= nsamples)
		{
			const sp_app_system_source_t *sources = sp_app_get_system_sources(app);
			const sp_app_system_sink_t *sinks = sp_app_get_system_sinks(app);
	
			int paused = sp_app_paused(app);
			if(paused == 1) // aka loading state
			{
				pcmi_pcm_idle(pcmi, nsamples);

				continue;
			}

			// fill input buffers
			if(ncapt)
				pcmi_capt_init(pcmi, nsamples);
			capt_num = 0;
			for(const sp_app_system_source_t *source=sources;
				(source->type != SYSTEM_PORT_NONE) && (capt_num < ncapt);
				source++)
			{
				chan_t *chan = source->sys_port;

				switch(source->type)
				{
					case SYSTEM_PORT_NONE:
					case SYSTEM_PORT_CONTROL:
					case SYSTEM_PORT_CV:
					case SYSTEM_PORT_OSC:
						break;

					case SYSTEM_PORT_AUDIO:
					{

						pcmi_capt_chan(pcmi, capt_num++, source->buf, nsamples);

						break;
					}
					
					case SYSTEM_PORT_MIDI:
					{
						void *seq_in = source->buf;

						// initialize LV2 event port
						LV2_Atom_Forge *forge = &chan->midi.forge;
						lv2_atom_forge_set_buffer(forge, seq_in, SEQ_SIZE);
						lv2_atom_forge_sequence_head(forge, &chan->midi.frame, 0);

						break;
					}
				}
			}

			// read incoming MIDI and dispatch to corresponding MIDI port
			{
				snd_seq_event_t *sev;
				while(snd_seq_event_input_pending(handle->seq, 1) > 0)
				{
					chan_t *chan = NULL;

					// get event
					snd_seq_event_input(handle->seq, &sev);

					// search for matching port
					for(const sp_app_system_source_t *source=sources;
						source->type != SYSTEM_PORT_NONE;
						source++)
					{
						if(source->type == SYSTEM_PORT_MIDI)
						{
							chan_t *_chan = source->sys_port;

							if(_chan->midi.port == sev->dest.port)
							{
								chan = _chan;
								break; // right port found, break out of loop 
							}
						}
					}

					if(chan)
					{
						long len;
						if((len = snd_midi_event_decode(chan->midi.trans, handle->m,
							MIDI_SEQ_SIZE, sev)) > 0)
						{
							LV2_Atom_Forge *forge = &chan->midi.forge;

							int64_t frames = 0;
							/* FIXME
							if( (sev->flags & (SND_SEQ_TIME_STAMP_MASK | SND_SEQ_TIME_MODE_MASK))
								== (SND_SEQ_TIME_STAMP_REAL | SND_SEQ_TIME_MODE_REL) )
							{
								frames = sev->time.time.tv_nsec * 1e-9 * handle->srate;
							}
							*/

							lv2_atom_forge_frame_time(forge, 0);
							lv2_atom_forge_atom(forge, len, handle->midi_MidiEvent);
							lv2_atom_forge_raw(forge, handle->m, len);
							lv2_atom_forge_pad(forge, len);
						}
						else
							fprintf(stderr, "event decode failed\n");
					}
					else
						fprintf(stderr, "no matching port for event\n");

					if(snd_seq_free_event(sev))
						fprintf(stderr, "event free failed\n");
				}
			}
						
			for(const sp_app_system_source_t *source=sources;
				source->type != SYSTEM_PORT_NONE;
				source++)
			{
				chan_t *chan = source->sys_port;

				if(source->type == SYSTEM_PORT_MIDI)
				{
					LV2_Atom_Forge *forge = &chan->midi.forge;

					// finalize LV2 event port
					lv2_atom_forge_pop(forge, &chan->midi.frame);
				}
			}
			if(ncapt)
				pcmi_capt_done(pcmi, nsamples);
	
			bin_process_pre(bin, nsamples, paused);

			// fill output buffers
			if(nplay)
				pcmi_play_init(pcmi, nsamples);
			play_num = 0;
			for(const sp_app_system_sink_t *sink=sinks;
				(sink->type != SYSTEM_PORT_NONE) && (play_num < nplay);
				sink++)
			{
				chan_t *chan = sink->sys_port;

				switch(sink->type)
				{
					case SYSTEM_PORT_NONE:
					case SYSTEM_PORT_CONTROL:
					case SYSTEM_PORT_CV:
					case SYSTEM_PORT_OSC:
						break;

					case SYSTEM_PORT_AUDIO:
					{

						pcmi_play_chan(pcmi, play_num++, sink->buf, nsamples);

						break;
					}

					case SYSTEM_PORT_MIDI:
					{
						const LV2_Atom_Sequence *seq_out = sink->buf;

						LV2_ATOM_SEQUENCE_FOREACH(seq_out, ev)
						{
							const LV2_Atom *atom = &ev->body;

							snd_seq_event_t sev;
							snd_seq_ev_clear(&sev);
							if(snd_midi_event_encode(chan->midi.trans, LV2_ATOM_BODY_CONST(atom), atom->size, &sev) != atom->size)
								fprintf(stderr, "encode event failed\n");
						
							// relative timestamp
							struct snd_seq_real_time rtime = {
								.tv_sec = 0,
								.tv_nsec = (float)ev->time.frames * 1e9 / handle->srate //TODO improve
							};
							while(rtime.tv_nsec > 1000000000) // handle overflow
							{
								rtime.tv_sec++;
								rtime.tv_nsec -= 1000000000;
							}

							// schedule midi
							snd_seq_ev_set_source(&sev, chan->midi.port);
							snd_seq_ev_set_subs(&sev); // set broadcasting to subscribers
							snd_seq_ev_schedule_real(&sev, handle->queue, 1, &rtime); // relative scheduling
							snd_seq_event_output(handle->seq, &sev);
						}

						break;
					}
				}
			}
			snd_seq_drain_output(handle->seq); //TODO is this rt-safe?

			// clear unused output channels
			while(play_num<nplay)
			{
				pcmi_clear_chan(pcmi, play_num++, nsamples);
			}

			if(nplay)
				pcmi_play_done(pcmi, nsamples);
		
			bin_process_post(bin);
		}
	}
	pcmi_pcm_stop(handle->pcmi);

	return NULL;
}

static void *
_system_port_add(void *data, System_Port_Type type, const char *short_name,
	const char *pretty_name, int input)
{
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);

	chan_t *chan = NULL;

	switch(type)
	{
		case SYSTEM_PORT_NONE:
		{
			// skip
			break;
		}

		case SYSTEM_PORT_CONTROL:
		{
			// unsupported, skip
			break;
		}

		case SYSTEM_PORT_AUDIO:
		{
			chan = calloc(1, sizeof(chan_t));
			if(chan)
			{
				chan->type = CHAN_TYPE_PCMI;
				//TODO
			}

			break;
		}
		case SYSTEM_PORT_CV:
		{
			// unsupported, skip
			break;
		}

		case SYSTEM_PORT_MIDI:
		{
			chan = calloc(1, sizeof(chan_t));
			if(chan)
			{
				chan->type = CHAN_TYPE_MIDI;

				unsigned int caps = input
					? SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE
					: SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;

				chan->midi.port = snd_seq_create_simple_port(handle->seq, short_name,
					caps, SND_SEQ_PORT_TYPE_APPLICATION);
				if(chan->midi.port < 0)
					fprintf(stderr, "could not create port\n");

				if(snd_midi_event_new(MIDI_SEQ_SIZE, &chan->midi.trans))
					fprintf(stderr, "could not create event\n");
				snd_midi_event_init(chan->midi.trans);
				if(input)
					snd_midi_event_reset_encode(chan->midi.trans);
				else
					snd_midi_event_reset_decode(chan->midi.trans);
				snd_midi_event_no_status(chan->midi.trans, 1);
			}

			break;
		}
		case SYSTEM_PORT_OSC:
		{
			// unsupported, skip
			break;
		}
	}

	return chan;
}

static void
_system_port_del(void *data, void *sys_port)
{
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);

	chan_t *chan = sys_port;

	if(!chan || !handle->seq)
		return;

	switch(chan->type)
	{
		case CHAN_TYPE_PCMI:
		{
			//TODO

			break;
		}
		case CHAN_TYPE_MIDI:
		{
			snd_midi_event_free(chan->midi.trans);
			snd_seq_delete_simple_port(handle->seq, chan->midi.port);

			break;
		}
	}

	free(chan);
}

static void
_ui_saved(void *data, int status)
{
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);

	//printf("_ui_saved: %i\n", status);
	if(handle->save_state == SAVE_STATE_NSM)
	{
		synthpod_nsm_saved(bin->nsm, status);
	}
	handle->save_state = SAVE_STATE_INTERNAL;

	if(handle->kill)
	{
		elm_exit();
	}
}

static int
_alsa_init(prog_t *handle, const char *id)
{
	// init alsa sequencer
	if(snd_seq_open(&handle->seq, "hw", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK))
		fprintf(stderr, "could not open sequencer\n");
	if(snd_seq_set_client_name(handle->seq, id))
		fprintf(stderr, "could not set name\n");
	handle->queue = snd_seq_alloc_queue(handle->seq);
	if(handle->queue < 0)
		fprintf(stderr, "could not allocate queue\n");
	snd_seq_start_queue(handle->seq, handle->queue, NULL);

	// init alsa pcm
	handle->pcmi = pcmi_new(handle->play_name, handle->capt_name,
		handle->srate, handle->frsize, handle->nfrags, handle->twochan, handle->debug);
	if(!handle->pcmi)
		return -1;
	pcmi_printinfo(handle->pcmi);

	return 0;
}

static void
_alsa_deinit(prog_t *handle)
{
	handle->kill = 1;
	eina_thread_join(handle->thread);

	pcmi_free(handle->pcmi);
	handle->pcmi = NULL;
				
	if(snd_seq_drain_output(handle->seq))
		fprintf(stderr, "draining output failed\n");
	snd_seq_stop_queue(handle->seq, handle->queue, NULL);
	if(snd_seq_free_queue(handle->seq, handle->queue))
		fprintf(stderr, "freeing queue failed\n");
	if(snd_seq_close(handle->seq))
		fprintf(stderr, "close sequencer failed\n");
	handle->seq = NULL;
	handle->queue = 0;
}

static int 
_open(const char *path, const char *name, const char *id, void *data)
{
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);
	(void)name;

	if(bin->path)
		free(bin->path);
	bin->path = strdup(path);

	// alsa init
	if(_alsa_init(handle, id))
		return -1;

	// synthpod init
	bin->app_driver.sample_rate = handle->srate;
	bin->app_driver.max_block_size = handle->frsize;
	bin->app_driver.min_block_size = 1;
	bin->app_driver.seq_size = SEQ_SIZE;
	
	// app init
	bin->app = sp_app_new(NULL, &bin->app_driver, bin);

	// alsa activate
	Eina_Bool status = eina_thread_create(&handle->thread,
		EINA_THREAD_URGENT, -1, _rt_thread, handle); //TODO

	sp_ui_bundle_load(bin->ui, bin->path, 1);

	return 0; // success
}

static int
_save(void *data)
{
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);

	handle->save_state = SAVE_STATE_NSM;
	sp_ui_bundle_save(bin->ui, bin->path, 1);

	return 0; // success
}

static const synthpod_nsm_driver_t nsm_driver = {
	.open = _open,
	.save = _save,
	.show = _show,
	.hide = _hide
};
	
EAPI_MAIN int
elm_main(int argc, char **argv)
{
	static prog_t handle;
	bin_t *bin = &handle.bin;

	handle.srate = 48000;
	handle.frsize = 1024;
	handle.nfrags = 3;
	handle.twochan = 0;
	handle.debug = 0;

	const char *def = "hw:0";
	handle.play_name = def;
	handle.capt_name = def;

	bin->has_gui = true;

	fprintf(stderr,
		"Synthpod "SYNTHPOD_VERSION"\n"
		"Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)\n"
		"Released under Artistic License 2.0 by Open Music Kontrollers\n");
	
	int c;
	while((c = getopt(argc, argv, "vhG2di:o:r:p:n:s:")) != -1)
	{
		switch(c)
		{
			case 'v':
				fprintf(stderr,
					"--------------------------------------------------------------------\n"
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
					"--------------------------------------------------------------------\n"
					"USAGE\n"
					"   %s [OPTIONS] [BUNDLE_PATH]\n"
					"\n"
					"OPTIONS\n"
					"   [-v]                 print version and full license information\n"
					"   [-h]                 print usage information\n"
					"   [-G]                 disable GUI\n"
					"   [-2]                 force 2 channel mode\n"
					"   [-d]                 enable debugging\n"
					"   [-i] capture-device  capture device (\"hw:0\")\n"
					"   [-o] playback-device playback device (\"hw:0\")\n"
					"   [-r] sample-rate     sample rate (48000)\n"
					"   [-p] sample-period   frames per period (1024)\n"
					"   [-n] period-number   number of periods of playback latency (3)\n"
					"   [-s] sequence-size   minimum sequence size (8192)\n\n"
					, argv[0]);
				return 0;
			case 'G':
				bin->has_gui = false;
				break;
			case '2':
				handle.twochan = 1;
				break;
			case 'd':
				handle.debug = 1;
				break;
			case 'i':
				handle.capt_name = optarg;
				break;
			case 'o':
				handle.play_name = optarg;
				break;
			case 'r':
				handle.srate = atoi(optarg);
				break;
			case 'p':
				handle.frsize = atoi(optarg);
				break;
			case 'n':
				handle.nfrags = atoi(optarg);
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
	
	bin_init(bin);
	
	LV2_URID_Map *map = ext_urid_map_get(bin->ext_urid);
	LV2_URID_Unmap *unmap = ext_urid_unmap_get(bin->ext_urid);
	
	lv2_atom_forge_init(&handle.forge, map);
	
	handle.midi_MidiEvent = map->map(map->handle, LV2_MIDI__MidiEvent);
	
	bin->app_driver.system_port_add = _system_port_add;
	bin->app_driver.system_port_del = _system_port_del;
	
	bin->app_driver.osc_sched = NULL;

	bin->ui_driver.saved = _ui_saved;

	bin->ui_driver.features = SP_UI_FEATURE_NEW | SP_UI_FEATURE_SAVE | SP_UI_FEATURE_CLOSE;
	if(synthpod_nsm_managed())
		bin->ui_driver.features |= SP_UI_FEATURE_IMPORT_FROM | SP_UI_FEATURE_EXPORT_TO;
	else
		bin->ui_driver.features |= SP_UI_FEATURE_OPEN | SP_UI_FEATURE_SAVE_AS;

	// run
	bin_run(bin, argv, &nsm_driver);

	// stop
	bin_stop(bin);

	// deinit alsa
	_alsa_deinit(&handle);

	// deinit
	bin_deinit(bin);

	return 0;
}

ELM_MAIN()
