/*
 * Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
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
#include <math.h>

#if defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
#	include <pthread_np.h>
typedef cpuset_t cpu_set_t;
#endif

#include <synthpod_bin.h>

#define NANO_SECONDS 1000000000

typedef struct _prog_t prog_t;

struct _prog_t {
	bin_t bin;
	
	LV2_Atom_Forge forge;
	
	atomic_int kill;
	pthread_t thread;

	uint32_t srate;
	uint32_t frsize;
	uint32_t nfrags;
	uint32_t seq_size;

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
	bin_t *bin = &handle->bin;
	sp_app_t *app = bin->app;

	const uint32_t nsamples = handle->frsize;

	const uint64_t nanos_per_period = (uint64_t)nsamples * NANO_SECONDS / handle->srate;
	handle->cycle.cur_frames = 0; // initialize frame counter
	_ntp_now(&bin->clk_real, &handle->nxt_ntp);

	const unsigned n_period = handle->nfrags;

	struct timespec sleep_to;
	cross_clock_gettime(&bin->clk_mono, &sleep_to);

	while(!atomic_load_explicit(&handle->kill, memory_order_relaxed))
	{
		cross_clock_nanosleep(&bin->clk_mono, true, &sleep_to);
		_ntp_add_nanos(&sleep_to, nanos_per_period * n_period);

		uint32_t na = nsamples * n_period;

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

		for( ; na >= nsamples;
				na -= nsamples,
				handle->cycle.ref_frames += nsamples,
				_ntp_add_nanos(&handle->nxt_ntp, nanos_per_period) )
		{
			const sp_app_system_source_t *sources = sp_app_get_system_sources(app);
	
			if(sp_app_bypassed(app))
			{
				//const sp_app_system_sink_t *sinks = sp_app_get_system_sinks(app);

				//fprintf(stderr, "app is bypassed\n");

				bin_process_pre(bin, nsamples, true);
				bin_process_post(bin);

				continue;
			}

			// fill input buffers
			for(const sp_app_system_source_t *source=sources;
				source->type != SYSTEM_PORT_NONE;
				source++)
			{
				switch(source->type)
				{
					case SYSTEM_PORT_NONE:
					case SYSTEM_PORT_AUDIO:
					case SYSTEM_PORT_CONTROL:
					case SYSTEM_PORT_CV:
					case SYSTEM_PORT_OSC:
					case SYSTEM_PORT_MIDI:
						break;

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
								ref = lv2_atom_forge_raw(forge, obj, size);
							if(ref)
								lv2_atom_forge_pad(forge, size);

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

			bin_process_pre(bin, nsamples, false);

			const sp_app_system_sink_t *sinks = sp_app_get_system_sinks(app);

			// fill output buffers
			for(const sp_app_system_sink_t *sink=sinks;
				sink->type != SYSTEM_PORT_NONE;
				sink++)
			{
				switch(sink->type)
				{
					case SYSTEM_PORT_NONE:
					case SYSTEM_PORT_CONTROL:
					case SYSTEM_PORT_CV:
					case SYSTEM_PORT_OSC:
					case SYSTEM_PORT_AUDIO:
					case SYSTEM_PORT_MIDI:
						break;
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

			bin_process_post(bin);
		}

		// increase cur_frames
		handle->cycle.cur_frames = handle->cycle.ref_frames;
		//sched_yield();
	}
}

__non_realtime static void *
_rt_thread(void *data)
{
	prog_t *handle = data;
	bin_t *bin = &handle->bin;

	bin->dsp_thread = pthread_self();

	if(handle->bin.audio_prio)
	{
		struct sched_param schedp;
		memset(&schedp, 0, sizeof(struct sched_param));
		schedp.sched_priority = handle->bin.audio_prio;

		if(schedp.sched_priority)
		{
			if(pthread_setschedparam(bin->dsp_thread, SCHED_FIFO, &schedp))
				bin_log_error(bin, "%s: pthread_setschedparam error\n", __func__);
		}
	}

	if(handle->bin.cpu_affinity)
	{
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(0, &cpuset);
		if(pthread_setaffinity_np(bin->dsp_thread, sizeof(cpu_set_t), &cpuset))
			bin_log_error(bin, "%s: pthread_setaffinity_np error\n", __func__);
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
	(void)handle;

	switch(type)
	{
		case SYSTEM_PORT_NONE:
		case SYSTEM_PORT_CONTROL:
		case SYSTEM_PORT_AUDIO:
		case SYSTEM_PORT_CV:
		case SYSTEM_PORT_MIDI:
		case SYSTEM_PORT_OSC:
		case SYSTEM_PORT_COM:
			// unsupported, skip
			break;
	}

	return NULL;
}

__non_realtime static void
_system_port_del(void *data, void *sys_port)
{
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);
	(void)handle;

	// unsupported, skip
}

__non_realtime static int 
_open(const char *path, const char *name, const char *id, void *data)
{
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);
	(void)name;

	const bool switch_over = bin->app ? true : false;

	if(bin->path)
		free(bin->path);
	bin->path = strdup(path);

	if(!switch_over)
	{
		// synthpod init
		bin->app_driver.sample_rate = handle->srate;
		bin->app_driver.update_rate = handle->bin.update_rate;
		bin->app_driver.max_block_size = handle->frsize;
		bin->app_driver.min_block_size = 1;
		bin->app_driver.seq_size = handle->seq_size;
		bin->app_driver.num_periods = handle->nfrags;
		
		// app init
		bin->app = sp_app_new(NULL, &bin->app_driver, bin);

		// dummy activate
		atomic_init(&handle->kill, 0);
		if(pthread_create(&handle->thread, NULL, _rt_thread, handle))
			bin_log_error(bin, "%s: creation of realtime thread failed\n", __func__);
	}

	bin_bundle_load(bin, bin->path);

	return 0; // success
}

__non_realtime static int
_save(void *data)
{
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);

	bin_bundle_save(bin, bin->path);

	return 0; // success
}

__non_realtime static int
_show(void *data)
{
	bin_t *bin = data;

	return bin_show(bin);
}

__non_realtime static int
_hide(void *data)
{
	bin_t *bin = data;

	return bin_hide(bin);
}

__non_realtime static bool
_visibility(void *data)
{
	bin_t *bin = data;

	return bin_visibility(bin);
}

static const nsmc_driver_t nsm_driver = {
	.open = _open,
	.save = _save,
	.show = _show,
	.hide = _hide,
	.visibility = _visibility,
	.capability = NSMC_CAPABILITY_MESSAGE
		| NSMC_CAPABILITY_SWITCH
		| NSMC_CAPABILITY_OPTIONAL_GUI
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
	handle.nfrags = 3; //TODO make this configurable
	handle.seq_size = SEQ_SIZE;

	bin->audio_prio = 70;
	bin->worker_prio = 60;
	bin->num_slaves = sysconf(_SC_NPROCESSORS_ONLN) - 1;
	bin->bad_plugins = false;
	bin->has_gui = false;
	bin->kill_gui = false;
	bin->threaded_gui = false;
	snprintf(bin->socket_path, sizeof(bin->socket_path), "shm:///synthpod-%i", getpid());
	bin->update_rate = 25;
	bin->cpu_affinity = false;

	fprintf(stderr,
		"Synthpod "SYNTHPOD_VERSION"\n"
		"Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)\n"
		"Released under Artistic License 2.0 by Open Music Kontrollers\n");

	int c;
	while((c = getopt(argc, argv, "vhgGkKtTbBaAy:Yw:Wul:r:p:s:c:f:")) != -1)
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
					"   [-g]                 load GUI\n"
					"   [-G]                 do NOT load GUI (default)\n"
					"   [-k]                 kill DSP with GUI\n"
					"   [-K]                 do NOT kill DSP with GUI (default)\n"
					"   [-t]                 run GUI in threaded mode\n"
					"   [-T]                 run GUI in separate process (default)\n"
					"   [-b]                 enable bad plugins\n"
					"   [-B]                 disable bad plugins (default)\n"
					"   [-a]                 enable CPU affinity\n"
					"   [-A]                 disable CPU affinity (default)\n"
					"   [-y] audio-priority  audio thread realtime priority (70)\n"
					"   [-Y]                 do NOT use audio thread realtime priority\n"
					"   [-w] worker-priority worker thread realtime priority (60)\n"
					"   [-W]                 do NOT use worker thread realtime priority\n"
					"   [-u]                 show alternate UI\n"
					"   [-l] link-path       socket link path (shm:///synthpod)\n"
					"   [-r] sample-rate     sample rate (48000)\n"
					"   [-p] sample-period   frames per period (1024)\n"
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
			case 't': 
				bin->threaded_gui = true;
				break;
			case 'T':
				bin->threaded_gui = false;
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
			case 'u':
				bin->d2tk_gui = true;
				break;
			case 'l':
				snprintf(bin->socket_path, sizeof(bin->socket_path), "%s", optarg);
				break;
			case 'r':
				handle.srate = atoi(optarg);
				break;
			case 'p':
				handle.frsize = atoi(optarg);
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
				if(  (optopt == 'r') || (optopt == 'p') || (optopt == 's') || (optopt == 'c')
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

	bin_init(bin, handle.srate);
	
	LV2_URID_Map *map = bin->map;
	
	lv2_atom_forge_init(&handle.forge, map);
	
	bin->app_driver.system_port_add = _system_port_add;
	bin->app_driver.system_port_del = _system_port_del;
	
	handle.osc_sched.osc2frames = _osc_schedule_osc2frames;
	handle.osc_sched.frames2osc = _osc_schedule_frames2osc;
	handle.osc_sched.handle = &handle;
	bin->app_driver.osc_sched = &handle.osc_sched;
	bin->app_driver.features = SP_APP_FEATURE_FIXED_BLOCK_LENGTH; // always true for DUMMY
  if(handle.frsize && !(handle.frsize & (handle.frsize - 1))) // check for powerOf2
		bin->app_driver.features |= SP_APP_FEATURE_POWER_OF_2_BLOCK_LENGTH;

	// run
	bin_run(bin, "Synthpod-DUMMY", argv, &nsm_driver);

	// stop
	bin_stop(bin);

	// stop rt thread
	if(handle.thread)
	{
		atomic_store_explicit(&handle.kill, 1, memory_order_relaxed);
		pthread_join(handle.thread, NULL);
	}

	// deinit
	bin_deinit(bin);

	munlockall();

	return 0;
}
