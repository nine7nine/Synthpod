/*
 * Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
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
#include <math.h>

#if defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
#	include <pthread_np.h>
typedef cpuset_t cpu_set_t;
#endif

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
			LV2_Atom_Forge_Ref ref;
			LV2_Atom_Sequence *seq_in;
			int64_t last;
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
	atomic_int kill;
	pthread_t thread;

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

	LV2_OSC_Schedule osc_sched;
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
_ntp_now(cross_clock_t *clk, struct timespec *ntp)
{
	cross_clock_gettime(clk, ntp);
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

__realtime static inline void 
_process(prog_t *handle)
{
	pcmi_t *pcmi = handle->pcmi;
	bin_t *bin = &handle->bin;
	sp_app_t *app = bin->app;

	const uint32_t nsamples = handle->frsize;
	int nplay = pcmi_nplay(pcmi);
	int ncapt = pcmi_ncapt(pcmi);
	int play_num;
	int capt_num;

	const uint64_t nanos_per_period = (uint64_t)nsamples * NANO_SECONDS / handle->srate;
	handle->cycle.cur_frames = 0; // initialize frame counter
	_ntp_now(&bin->clk_real, &handle->nxt_ntp);

	snd_seq_queue_status_t *stat = NULL;
	const snd_seq_real_time_t *real_time = NULL;
	snd_seq_real_time_t ref_time;
	snd_seq_queue_status_malloc(&stat);

	pcmi_pcm_start(handle->pcmi);
	while(!atomic_load_explicit(&handle->kill, memory_order_relaxed))
	{
		uint32_t na = pcmi_pcm_wait(pcmi);

		// current time is next time from last cycle
		_ntp_clone(&handle->cur_ntp, &handle->nxt_ntp);

		// extrapolate new nxt_ntp
		struct timespec nxt_ntp;
		_ntp_now(&bin->clk_real, &nxt_ntp);
		_ntp_clone(&handle->nxt_ntp, &nxt_ntp);

		// reset ref_frames
		handle->cycle.ref_frames = handle->cycle.cur_frames;

		// calculate apparent period
		_ntp_add_nanos(&nxt_ntp, nanos_per_period);
		double diff = _ntp_diff(&handle->cur_ntp, &nxt_ntp);

		// calculate apparent samples per period
		handle->cycle.dT = nsamples / diff;
		handle->cycle.dTm1 = 1.0 / handle->cycle.dT;

		// get ALSA sequencer reference timestamp
		snd_seq_get_queue_status(handle->seq, handle->queue, stat);
		real_time = snd_seq_queue_status_get_real_time(stat);
		ref_time.tv_sec = real_time->tv_sec;
		ref_time.tv_nsec = real_time->tv_nsec;

		uint32_t pos = 0;
		for( ; na >= nsamples;
				na -= nsamples,
				handle->cycle.ref_frames += nsamples,
				pos += nsamples,
				_ntp_add_nanos(&handle->nxt_ntp, nanos_per_period) )
		{
			const sp_app_system_source_t *sources = sp_app_get_system_sources(app);
	
			if(sp_app_bypassed(app))
			{
				//const sp_app_system_sink_t *sinks = sp_app_get_system_sinks(app);

				//fprintf(stderr, "app is bypassed\n");

				pcmi_pcm_idle(pcmi, nsamples);

				bin_process_pre(bin, nsamples, true);
				bin_process_post(bin);

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
						chan->midi.seq_in = seq_in; // needed for lv2_atom_sequence_clear
						lv2_atom_forge_set_buffer(forge, seq_in, SEQ_SIZE);
						chan->midi.ref = lv2_atom_forge_sequence_head(forge, &chan->midi.frame, 0);
						chan->midi.last = 0;

						break;
					}

					case SYSTEM_PORT_COM:
					{
						void *seq_in = source->buf;

						LV2_Atom_Forge *forge = &handle->forge;
						LV2_Atom_Forge_Frame frame;
						lv2_atom_forge_set_buffer(forge, seq_in, SEQ_SIZE);
						LV2_Atom_Forge_Ref ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

						const LV2_Atom_Object *obj;
						size_t size;
						while((obj = varchunk_read_request(bin->app_from_com, &size)))
						{
							if(ref)
								ref = lv2_atom_forge_frame_time(forge, 0);
							if(ref)
								ref = lv2_atom_forge_write(forge, obj, size);

							varchunk_read_advance(bin->app_from_com);
						}
						if(ref)
							lv2_atom_forge_pop(forge, &frame);
						else
							lv2_atom_sequence_clear(seq_in);

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

							const bool is_real = snd_seq_ev_is_real(sev);
							const bool is_abs = snd_seq_ev_is_abstime(sev);
							assert(is_real);

							volatile double dd = sev->time.time.tv_sec;
							if(is_abs) // calculate relative time difference
								dd -= ref_time.tv_sec;
							dd += sev->time.time.tv_nsec * 1e-9;
							if(is_abs) // calculate relative time difference
								dd -= ref_time.tv_nsec * 1e-9;

							int64_t frames = dd * handle->srate - pos;
							if(frames < 0)
								frames = 0;
							else if(frames >= nsamples)
								frames = nsamples - 1; //TODO report this

							if(frames < chan->midi.last)
								frames = chan->midi.last; // frame time must be increasing
							else
								chan->midi.last = frames;

							// fix up noteOn(vel=0) -> noteOff(vel=0)
							if(  (len == 3) && ( (handle->m[0] & 0xf0) == 0x90)
								&& (handle->m[2] == 0x0) )
							{
								handle->m[0] = 0x80 | (handle->m[0] & 0x0f);
								handle->m[2] = 0x0;
							}

							if(chan->midi.ref)
								chan->midi.ref = lv2_atom_forge_frame_time(forge, frames);
							if(chan->midi.ref)
								chan->midi.ref = lv2_atom_forge_atom(forge, len, handle->midi_MidiEvent);
							if(chan->midi.ref)
								chan->midi.ref = lv2_atom_forge_write(forge, handle->m, len);
						}
						else
						{
							bin_log_trace(bin, "%s: MIDI event decode failed\n", __func__);
						}
					}
					else
					{
						bin_log_trace(bin, "%s: no matching port for MIDI event\n", __func__);
					}

					if(snd_seq_free_event(sev))
					{
						bin_log_trace(bin, "%s: MIDI event free failed\n", __func__);
					}
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
					if(chan->midi.ref)
						lv2_atom_forge_pop(forge, &chan->midi.frame);
					else
						lv2_atom_sequence_clear(chan->midi.seq_in);
				}
			}
			if(ncapt)
				pcmi_capt_done(pcmi, nsamples);
	
			bin_process_pre(bin, nsamples, false);

			const sp_app_system_sink_t *sinks = sp_app_get_system_sinks(app);

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

							if(atom->type != handle->midi_MidiEvent)
								continue; // ignore non-MIDI events

							snd_seq_event_t sev;
							snd_seq_ev_clear(&sev);
							long consumed;
							const uint8_t *buf = LV2_ATOM_BODY_CONST(atom);
							if( (consumed = snd_midi_event_encode(chan->midi.trans, buf, atom->size, &sev)) != atom->size)
							{
								bin_log_trace(bin, "%s: MIDI encode event failed: %li\n", __func__, consumed);
								continue;
							}

							// absolute timestamp
							volatile double dd = (double)(ev->time.frames + pos) / handle->srate;
							double sec;
							double nsec = modf(dd, &sec);
							struct snd_seq_real_time rtime = {
								.tv_sec = ref_time.tv_sec + sec,
								.tv_nsec = ref_time.tv_nsec + nsec
							};
							while(rtime.tv_nsec >= NANO_SECONDS) // handle overflow
							{
								rtime.tv_sec += 1;
								rtime.tv_nsec -= NANO_SECONDS;
							}

							// schedule midi
							snd_seq_ev_set_source(&sev, chan->midi.port);
							snd_seq_ev_set_subs(&sev); // set broadcasting to subscribers
							snd_seq_ev_schedule_real(&sev, handle->queue, 0, &rtime); // absolute scheduling
							snd_seq_event_output(handle->seq, &sev);
							//snd_seq_drain_output(handle->seq);
						}

						break;
					}

					case SYSTEM_PORT_COM:
					{
						const LV2_Atom_Sequence *seq_out = sink->buf;

						LV2_ATOM_SEQUENCE_FOREACH(seq_out, ev)
						{
							const LV2_Atom *atom = &ev->body;
							
							// try do process events directly
							bin->advance_ui = sp_app_from_ui(bin->app, atom);
							if(!bin->advance_ui) // queue event in ringbuffer instead
							{
								//fprintf(stderr, "plugin ui direct is blocked\n");

								void *ptr;
								size_t size = lv2_atom_total_size(atom);
								if((ptr = varchunk_write_request(bin->app_from_app, size)))
								{
									memcpy(ptr, atom, size);
									varchunk_write_advance(bin->app_from_app, size);
								}
								else
								{
									bin_log_trace(bin, "%s: app_from_app ringbuffer full\n", __func__);
									//FIXME
								}
							}
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

		// increase cur_frames
		handle->cycle.cur_frames = handle->cycle.ref_frames;
		//sched_yield();
	}
	pcmi_pcm_stop(handle->pcmi);
	
	snd_seq_queue_status_free(stat);
}

__non_realtime static void *
_rt_thread(void *data)
{
	prog_t *handle = data;
	bin_t *bin = &handle->bin;

	pthread_t self = pthread_self();

	if(handle->bin.audio_prio)
	{
		struct sched_param schedp;
		memset(&schedp, 0, sizeof(struct sched_param));
		schedp.sched_priority = handle->bin.audio_prio;

		if(schedp.sched_priority)
		{
			if(pthread_setschedparam(self, SCHED_FIFO, &schedp))
				bin_log_error(bin, "%s: pthread_setschedparam failed\n", __func__);
		}
	}

	if(handle->bin.cpu_affinity)
	{
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(0, &cpuset);
		if(pthread_setaffinity_np(self, sizeof(cpu_set_t), &cpuset))
			bin_log_error(bin, "%s: pthread_setaffinity_np failed\n", __func__);
	}

	_process(handle);

	return NULL;
}

__non_realtime static void *
_system_port_add(void *data, system_port_t type, const char *short_name,
	const char *pretty_name, const char *designation, bool input, uint32_t order)
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
				memcpy(&chan->midi.forge, &handle->forge, sizeof(LV2_Atom_Forge)); // initialize forge

				snd_seq_port_info_t *pinfo;
				snd_seq_port_info_malloc(&pinfo);

				snd_seq_port_info_set_name(pinfo, short_name);
				snd_seq_port_info_set_capability(pinfo, input
					? SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE
					: SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ);
				snd_seq_port_info_set_type(pinfo, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
				snd_seq_port_info_set_midi_channels(pinfo, 16);
				snd_seq_port_info_set_midi_voices(pinfo, 64);
				snd_seq_port_info_set_synth_voices(pinfo, 0);
				snd_seq_port_info_set_timestamping(pinfo, 1);
				snd_seq_port_info_set_timestamp_real(pinfo, 1);
				snd_seq_port_info_set_timestamp_queue(pinfo, handle->queue);

				snd_seq_create_port(handle->seq, pinfo);
				chan->midi.port = snd_seq_port_info_get_port(pinfo);
				if(chan->midi.port < 0)
					bin_log_error(bin, "%s: could not create MIDI port\n", __func__);

				snd_seq_port_info_free(pinfo);

				if(snd_midi_event_new(MIDI_SEQ_SIZE, &chan->midi.trans))
					bin_log_error(bin, "%s: could not create MIDI event translator\n", __func__);
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

__non_realtime static void
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

__non_realtime static void
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
		bin_quit(bin);
	}
}

static int
_alsa_init(prog_t *handle, const char *id)
{
	bin_t *bin = &handle->bin;

	// init alsa sequencer
	if(snd_seq_open(&handle->seq, "hw", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK))
		bin_log_error(bin, "%s: could not open sequencer\n", __func__);
	if(snd_seq_set_client_name(handle->seq, id))
		bin_log_error(bin, "%s: could not set name\n", __func__);
	handle->queue = snd_seq_alloc_queue(handle->seq);
	if(handle->queue < 0)
		bin_log_error(bin, "%s: could not allocate queue\n", __func__);
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
	bin_t *bin = &handle->bin;

	if(handle->thread)
	{
		atomic_store_explicit(&handle->kill, 1, memory_order_relaxed);
		pthread_join(handle->thread, NULL);
	}

	if(handle->pcmi)
	{
		pcmi_free(handle->pcmi);

		handle->pcmi = NULL;
	}
	
	if(handle->seq)
	{
		if(snd_seq_drain_output(handle->seq))
			bin_log_error(bin, "%s: draining output failed\n", __func__);
		snd_seq_stop_queue(handle->seq, handle->queue, NULL);
		if(snd_seq_free_queue(handle->seq, handle->queue))
			bin_log_error(bin, "%s: freeing queue failed\n", __func__);
		if(snd_seq_close(handle->seq))
			bin_log_error(bin, "%s: close sequencer failed\n", __func__);

		handle->seq = NULL;
		handle->queue = 0;
	}
}

__non_realtime static int 
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
		return -1;
	}

	// synthpod init
	bin->app_driver.sample_rate = handle->srate;
	bin->app_driver.update_rate = handle->bin.update_rate;
	bin->app_driver.max_block_size = handle->frsize;
	bin->app_driver.min_block_size = 1;
	bin->app_driver.seq_size = handle->seq_size;
	bin->app_driver.num_periods = handle->nfrags;
	
	// app init
	bin->app = sp_app_new(NULL, &bin->app_driver, bin);

	// alsa activate
	atomic_init(&handle->kill, 0);
	if(pthread_create(&handle->thread, NULL, _rt_thread, handle))
		bin_log_error(bin, "%s: creation of realtime thread failed\n", __func__);

	bin_bundle_load(bin, bin->path);

	return 0; // success
}

__non_realtime static int
_save(void *data)
{
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);

	handle->save_state = SAVE_STATE_NSM;
	bin_bundle_save(bin, bin->path);

	return 0; // success
}

static const synthpod_nsm_driver_t nsm_driver = {
	.open = _open,
	.save = _save,
	.show = NULL,
	.hide = NULL
};

// rt
__realtime static double
_osc_schedule_osc2frames(LV2_OSC_Schedule_Handle instance, uint64_t timestamp)
{
	prog_t *handle = instance;

	if(timestamp == 1ULL)
		return 0; // inject at start of period

	const uint64_t time_sec = timestamp >> 32;
	const uint64_t time_frac = timestamp & 0xffffffff;

	const double diff = (time_sec - handle->cur_ntp.tv_sec)
		+ time_frac * 0x1p-32
		- handle->cur_ntp.tv_nsec * 1e-9;

	const double frames = diff * handle->cycle.dT
		- handle->cycle.ref_frames
		+ handle->cycle.cur_frames;

	return frames;
}

// rt
__realtime static uint64_t
_osc_schedule_frames2osc(LV2_OSC_Schedule_Handle instance, double frames)
{
	prog_t *handle = instance;

	double diff = (frames - handle->cycle.cur_frames + handle->cycle.ref_frames)
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

int
main(int argc, char **argv)
{
	mlockall(MCL_CURRENT | MCL_FUTURE);

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

	bin->audio_prio = 70;
	bin->worker_prio = 60;
	bin->num_slaves = sysconf(_SC_NPROCESSORS_ONLN) - 1;
	bin->bad_plugins = false;
	bin->has_gui = false;
	bin->kill_gui = false;
	bin->socket_path = "shm:///synthpod";
	bin->update_rate = 25;
	bin->cpu_affinity = false;

	fprintf(stderr,
		"Synthpod "SYNTHPOD_VERSION"\n"
		"Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)\n"
		"Released under GNU General Public License 3 by Open Music Kontrollers\n");

	// read local configuration if present
	/*FIXME
	Efreet_Ini *ini = _read_config(&handle);
	*/
	
	int c;
	while((c = getopt(argc, argv, "vhgGbkKBaAIOtTxXy:Yw:Wl:d:i:o:r:p:n:s:c:f:")) != -1)
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
					"   [-g]                 load GUI\n"
					"   [-G]                 do NOT load GUI (default)\n"
					"   [-k]                 kill DSP with GUI\n"
					"   [-K]                 do NOT kill DSP with GUI (default)\n"
					"   [-b]                 enable bad plugins\n"
					"   [-B]                 disable bad plugins (default)\n"
					"   [-a]                 enable CPU affinity\n"
					"   [-A]                 disable CPU affinity (default)\n"
					"   [-I]                 disable capture\n"
					"   [-O]                 disable playback\n"
					"   [-t]                 force 2 channel mode\n"
					"   [-T]                 do NOT force 2 channel mode (default)\n"
					"   [-x]                 notify about XRuns\n"
					"   [-X]                 do NOT notify about XRuns (default)\n"
					"   [-y] audio-priority  audio thread realtime priority (70)\n"
					"   [-Y]                 do NOT use audio thread realtime priority\n"
					"   [-w] worker-priority worker thread realtime priority (60)\n"
					"   [-W]                 do NOT use worker thread realtime priority\n"
					"   [-l] link-path       socket link path (shm:///synthpod)\n"
					"   [-d] device          capture/playback device (\"hw:0\")\n"
					"   [-i] capture-device  capture device (\"hw:0\")\n"
					"   [-o] playback-device playback device (\"hw:0\")\n"
					"   [-r] sample-rate     sample rate (48000)\n"
					"   [-p] sample-period   frames per period (1024)\n"
					"   [-n] period-number   number of periods of playback latency (3)\n"
					"   [-s] sequence-size   minimum sequence size (8192)\n"
					"   [-c] slave-cores     number of slave cores (auto)\n"
					"   [-f] update-rate     GUI update rate (25)\n\n"
					, argv[0]);
				return 0;
			case 'g': 
				bin->has_gui = true;
				break;
			case 'G':
				bin->has_gui = false;
				break;
			case 'k': 
				bin->kill_gui = true;
				break;
			case 'K':
				bin->kill_gui = false;
				break;
			case 'b':
				bin->bad_plugins = true;
				break;
			case 'B':
				bin->bad_plugins = false;
				break;
			case 'a':
				bin->cpu_affinity = true;
				break;
			case 'A':
				bin->cpu_affinity = false;
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
			case 'y':
				bin->audio_prio = atoi(optarg);
				break;
			case 'Y':
				bin->audio_prio = 0;
				break;
			case 'w':
				bin->worker_prio = atoi(optarg);
				break;
			case 'W':
				bin->worker_prio = 0;
				break;
			case 'l':
				bin->socket_path = optarg;
				break;
			case 'd':
				handle.do_capt = optarg != NULL;
				handle.do_play = optarg != NULL;
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
			case 'c':
				if(atoi(optarg) < bin->num_slaves)
					bin->num_slaves = atoi(optarg);
				break;
			case 'f':
				bin->update_rate = atoi(optarg);
				break;
			case '?':
				if( (optopt == 'd') || (optopt == 'i') || (optopt == 'o') || (optopt == 'r')
					|| (optopt == 'p') || (optopt == 'n') || (optopt == 's') || (optopt == 'c')
					|| (optopt == 'l') || (optopt == 'f') )
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
	
	bin_init(bin, handle.srate);
	
	LV2_URID_Map *map = bin->map;
	
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

	// run
	bin_run(bin, argv, &nsm_driver);

	// stop
	bin_stop(bin);

	// deinit alsa
	_alsa_deinit(&handle);

	// deinit
	bin_deinit(bin);

	/*FIXME
	if(ini)
		efreet_ini_free(ini);
	*/

	munlockall();

	return 0;
}
