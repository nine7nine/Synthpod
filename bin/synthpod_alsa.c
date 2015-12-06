/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include <asoundlib.h>
#include <pcmi.h>

#include <synthpod_bin.h>

#define MIDI_SEQ_SIZE 2048
#define NANO_SECONDS 1000000000

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
	_Atomic int kill;
	Eina_Thread thread;

	uint32_t srate;
	uint32_t frsize;
	uint32_t nfrags;
	bool twochan;
	bool debug;
	bool do_play;
	bool do_capt;
	uint32_t seq_size;

	const char *io_name;
	const char *play_name;
	const char *capt_name;

	osc_schedule_t osc_sched;
	struct timespec cur_ntp;
	struct timespec nxt_ntp;
	struct {
		uint64_t cur_frames;
		uint64_t ref_frames;
		double dT;
		double dTm1;
	} cycle;
};

static inline void
_ntp_now(struct timespec *ntp)
{
	clock_gettime(CLOCK_REALTIME, ntp);
	ntp->tv_sec += JAN_1970; // convert NTP to OSC time
}

static inline void
_ntp_clone(struct timespec *dst, struct timespec *src)
{
	dst->tv_sec = src->tv_sec;
	dst->tv_nsec = src->tv_nsec;
}

static inline void
_ntp_add_nanos(struct timespec *ntp, uint64_t nanos)
{
	ntp->tv_nsec += nanos;
	while(ntp->tv_nsec >= NANO_SECONDS) // has overflowed
	{
		ntp->tv_sec += 1;
		ntp->tv_nsec -= NANO_SECONDS;
	}
}

static inline double
_ntp_diff(struct timespec *from, struct timespec *to)
{
	double diff = to->tv_sec;
	diff -= from->tv_sec;
	diff += 1e-9 * to->tv_nsec;
	diff -= 1e-9 * from->tv_nsec;

	return diff;
}

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
	int nplay = pcmi_nplay(pcmi);
	int ncapt = pcmi_ncapt(pcmi);
	int play_num;
	int capt_num;

	const uint64_t nanos_per_period = (uint64_t)nsamples * NANO_SECONDS / handle->srate;
	handle->cycle.cur_frames = 0; // initialize frame counter
	_ntp_now(&handle->nxt_ntp);

	pcmi_pcm_start(handle->pcmi);
	while(!atomic_load_explicit(&handle->kill, memory_order_relaxed))
	{
		uint32_t na = pcmi_pcm_wait(pcmi);

		// current time is next time from last cycle
		_ntp_clone(&handle->cur_ntp, &handle->nxt_ntp);

		// extrapolate new nxt_ntp
		struct timespec nxt_ntp;
		_ntp_now(&nxt_ntp);
		_ntp_clone(&handle->nxt_ntp, &nxt_ntp);

		// increase cur_frames
		handle->cycle.cur_frames += na;
		handle->cycle.ref_frames = handle->cycle.cur_frames;

		// calculate apparent period
		_ntp_add_nanos(&nxt_ntp, nanos_per_period);
		double diff = _ntp_diff(&handle->cur_ntp, &nxt_ntp);

		// calculate apparent samples per period
		handle->cycle.dT = nsamples / diff;
		handle->cycle.dTm1 = 1.0 / handle->cycle.dT;

		for( ; na >= nsamples;
				na -= nsamples,
				handle->cycle.ref_frames += nsamples,
				_ntp_add_nanos(&handle->nxt_ntp, nanos_per_period) )
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
				source->type != SYSTEM_PORT_NONE;
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

						if(capt_num < ncapt)
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

					case SYSTEM_PORT_COM:
					{
						void *seq_in = source->buf;

						LV2_Atom_Forge *forge = &handle->forge;
						LV2_Atom_Forge_Frame frame;
						lv2_atom_forge_set_buffer(forge, seq_in, SEQ_SIZE);
						lv2_atom_forge_sequence_head(forge, &frame, 0);

						const LV2_Atom_Object *obj;
						size_t size;
						while((obj = varchunk_read_request(bin->app_from_com, &size)))
						{
							lv2_atom_forge_frame_time(forge, 0);
							lv2_atom_forge_raw(forge, obj, size);
							lv2_atom_forge_pad(forge, size);

							varchunk_read_advance(bin->app_from_com);
						}

						lv2_atom_forge_pop(forge, &frame);

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

							lv2_atom_forge_frame_time(forge, frames);
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
				sink->type != SYSTEM_PORT_NONE;
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

						if(play_num < nplay)
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
								.tv_nsec = ev->time.frames * nanos_per_period
							};
							while(rtime.tv_nsec >= NANO_SECONDS) // handle overflow
							{
								rtime.tv_sec += 1;
								rtime.tv_nsec -= NANO_SECONDS;
							}

							// schedule midi
							snd_seq_ev_set_source(&sev, chan->midi.port);
							snd_seq_ev_set_subs(&sev); // set broadcasting to subscribers
							snd_seq_ev_schedule_real(&sev, handle->queue, 1, &rtime); // relative scheduling
							snd_seq_event_output(handle->seq, &sev);
						}

						break;
					}

					case SYSTEM_PORT_COM:
					{
						const LV2_Atom_Sequence *seq_out = sink->buf;

						LV2_ATOM_SEQUENCE_FOREACH(seq_out, ev)
						{
							const LV2_Atom *atom = &ev->body;
							
							sp_app_from_ui(bin->app, atom);
							//FIXME is this the right place?
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
_system_port_add(void *data, system_port_t type, const char *short_name,
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
		case SYSTEM_PORT_COM:
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

	if(!chan)
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
			if(handle->seq)
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

	if(atomic_load_explicit(&handle->kill, memory_order_relaxed))
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
	if(handle->thread)
	{
		atomic_store_explicit(&handle->kill, 1, memory_order_relaxed);
		eina_thread_join(handle->thread);
	}

	if(handle->pcmi)
	{
		pcmi_free(handle->pcmi);

		handle->pcmi = NULL;
	}
	
	if(handle->seq)
	{
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
	{
		bin->ui_driver.close(bin);
		return -1;
	}

	// synthpod init
	bin->app_driver.sample_rate = handle->srate;
	bin->app_driver.max_block_size = handle->frsize;
	bin->app_driver.min_block_size = 1;
	bin->app_driver.seq_size = handle->seq_size;
	
	// app init
	bin->app = sp_app_new(NULL, &bin->app_driver, bin);

	// alsa activate
	atomic_init(&handle->kill, 0);
	Eina_Bool status = eina_thread_create(&handle->thread,
		EINA_THREAD_URGENT, -1, _rt_thread, handle); //TODO
	if(!status)
		fprintf(stderr, "creation of rt thread failed\n");

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

// rt
static int64_t
_osc_schedule_osc2frames(osc_schedule_handle_t instance, uint64_t timestamp)
{
	prog_t *handle = instance;

	if(timestamp == 1ULL)
		return 0; // inject at start of period

	uint64_t time_sec = timestamp >> 32;
	uint64_t time_frac = timestamp & 0xffffffff;

	double diff = time_sec;
	diff -= handle->cur_ntp.tv_sec;
	diff += time_frac * 0x1p-32;
	diff -= handle->cur_ntp.tv_nsec * 1e-9;

	double frames_d = handle->cycle.ref_frames
		- handle->cycle.cur_frames
		+ diff * handle->cycle.dT;

	int64_t frames = ceil(frames_d);

	return frames;
}

// rt
static uint64_t
_osc_schedule_frames2osc(osc_schedule_handle_t instance, int64_t frames)
{
	prog_t *handle = instance;

	double diff = (double)(frames - handle->cycle.ref_frames + handle->cycle.cur_frames)
		* handle->cycle.dTm1;
	diff += handle->cur_ntp.tv_nsec * 1e-9;
	diff += handle->cur_ntp.tv_sec;

	double time_sec_d;
	double time_frac_d = modf(diff, &time_sec_d);

	uint64_t time_sec = time_sec_d;
	uint64_t time_frac = time_frac_d * 0x1p32;
	if(time_frac >= 0x100000000ULL) // illegal overflow
		time_frac = 0xffffffffULL;

	uint64_t timestamp = (time_sec << 32) | time_frac;

	return timestamp;
}

static Efreet_Ini * 
_read_config(prog_t *handle)
{
	Efreet_Ini *ini = NULL;

	const char *config_home_dir = efreet_config_home_get();
	if(config_home_dir)
	{
		char *config_home_file;
		asprintf(&config_home_file, "%s/synthpod_alsa.ini", config_home_dir);

		if(config_home_file)
		{
			ini = efreet_ini_new(config_home_file);
			if(ini && ini->data)
			{
				unsigned valueboolean;
				int valueint;
				const char *valuestring;
				
				if(efreet_ini_section_set(ini, "synthpod_alsa"))
				{
					handle->bin.has_gui = !efreet_ini_boolean_get(ini, "disable-gui");
					handle->do_play = !efreet_ini_boolean_get(ini, "disable-playback");
					handle->do_capt = !efreet_ini_boolean_get(ini, "disable-capture");
					handle->twochan = efreet_ini_boolean_get(ini, "force-two-channel");
					handle->debug = efreet_ini_boolean_get(ini, "notify-xruns");

					if((valuestring = efreet_ini_string_get(ini, "device")))
						handle->io_name = valuestring;
					if((valuestring = efreet_ini_string_get(ini, "capture-device")))
						handle->capt_name = valuestring;
					if((valuestring = efreet_ini_string_get(ini, "playback-device")))
						handle->play_name = valuestring;
					
					if((valueint = efreet_ini_int_get(ini, "sample-rate")) != -1)
						handle->srate = valueint;
					if((valueint = efreet_ini_int_get(ini, "sample-period")) != -1)
						handle->frsize = valueint;
					if((valueint = efreet_ini_int_get(ini, "period-number")) != -1)
						handle->nfrags = valueint;
					if((valueint = efreet_ini_int_get(ini, "sequence-size")) != -1)
						handle->seq_size = valueint;
				}
				else
					fprintf(stderr, "section 'synthpod_alsa' does not exists\n");
			}

			free(config_home_file);
		} // config_home_file
	} // config_home_dir

	return ini;
}

EAPI_MAIN int
elm_main(int argc, char **argv);
	
EAPI_MAIN int
elm_main(int argc, char **argv)
{
	static prog_t handle;
	bin_t *bin = &handle.bin;

	handle.srate = 48000;
	handle.frsize = 1024;
	handle.nfrags = 3;
	handle.twochan = false;
	handle.debug = false;
	handle.do_play = true;
	handle.do_capt = true;
	handle.seq_size = SEQ_SIZE;

	const char *def = "hw:0";
	handle.io_name = def;
	handle.play_name = NULL;
	handle.capt_name = NULL;

	bin->has_gui = true;

	fprintf(stderr,
		"Synthpod "SYNTHPOD_VERSION"\n"
		"Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)\n"
		"Released under GNU General Public License 3 by Open Music Kontrollers\n");

	// read local configuration if present
	Efreet_Ini *ini = _read_config(&handle);
	
	int c;
	while((c = getopt(argc, argv, "vhgGIOtTxXd:i:o:r:p:n:s:")) != -1)
	{
		switch(c)
		{
			case 'v':
				fprintf(stderr,
					"--------------------------------------------------------------------\n"
					"This program is free software; you can redistribute it and/or modify\n"
					"it under the terms of the GNU General Public License as published by\n"
					"the Free Software Foundation; either version 3 of the License, or\n"
					"(at your option) any later version.\n"
					"\n"
					"This program is distributed in the hope that it will be useful,\n"
					"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
					"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
					"GNU General Public License for more details.\n"
					"\n"
					"You should have received a copy of the GNU General Public License\n"
					"along with this program.  If not, see http://www.gnu.org/licenses.\n\n");
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
					"   [-g]                 enable GUI (default)\n"
					"   [-G]                 disable GUI\n"
					"   [-I]                 disable capture\n"
					"   [-O]                 disable playback\n"
					"   [-t]                 force 2 channel mode\n"
					"   [-T]                 do NOT force 2 channel mode (default)\n"
					"   [-x]                 notify about XRuns\n"
					"   [-X]                 do NOT notify about XRuns (default)\n"
					"   [-d] device          capture/playback device (\"hw:0\")\n"
					"   [-i] capture-device  capture device (\"hw:0\")\n"
					"   [-o] playback-device playback device (\"hw:0\")\n"
					"   [-r] sample-rate     sample rate (48000)\n"
					"   [-p] sample-period   frames per period (1024)\n"
					"   [-n] period-number   number of periods of playback latency (3)\n"
					"   [-s] sequence-size   minimum sequence size (8192)\n\n"
					, argv[0]);
				return 0;
			case 'g':
				bin->has_gui = true;
				break;
			case 'G':
				bin->has_gui = false;
				break;
			case 'I':
				handle.do_capt = false;
				break;
			case 'O':
				handle.do_play = false;
				break;
			case 't':
				handle.twochan = true;
				break;
			case 'T':
				handle.twochan = false;
				break;
			case 'x':
				handle.debug = true;
				break;
			case 'X':
				handle.debug = false;
				break;
			case 'd':
				handle.io_name = optarg;
				break;
			case 'i':
				handle.do_capt = optarg != NULL;
				handle.capt_name = optarg;
				break;
			case 'o':
				handle.do_play = optarg != NULL;
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
				handle.seq_size = MAX(SEQ_SIZE, atoi(optarg));
				break;
			case '?':
				if( (optopt == 'd') || (optopt == 'i') || (optopt == 'o') || (optopt == 'r')
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

	if(!handle.capt_name && handle.do_capt)
		handle.capt_name = handle.io_name;
	if(!handle.play_name && handle.do_play)
		handle.play_name = handle.io_name;
	
	bin_init(bin);
	
	LV2_URID_Map *map = ext_urid_map_get(bin->ext_urid);
	
	lv2_atom_forge_init(&handle.forge, map);
	
	handle.midi_MidiEvent = map->map(map->handle, LV2_MIDI__MidiEvent);
	
	bin->app_driver.system_port_add = _system_port_add;
	bin->app_driver.system_port_del = _system_port_del;
	
	handle.osc_sched.osc2frames = _osc_schedule_osc2frames;
	handle.osc_sched.frames2osc = _osc_schedule_frames2osc;
	handle.osc_sched.handle = &handle;
	bin->app_driver.osc_sched = &handle.osc_sched;
	bin->app_driver.features = SP_APP_FEATURE_FIXED_BLOCK_LENGTH; // always true for ALSA
  if(handle.frsize && !(handle.frsize & (handle.frsize - 1))) // check for powerOf2
		bin->app_driver.features |= SP_APP_FEATURE_POWER_OF_2_BLOCK_LENGTH;

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

	if(ini)
		efreet_ini_free(ini);

	return 0;
}

ELM_MAIN()
