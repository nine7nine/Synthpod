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

#include <synthpod_bin.h>

#define NANO_SECONDS 1000000000

typedef struct _prog_t prog_t;

struct _prog_t {
	bin_t bin;
	
	LV2_Atom_Forge forge;
	
	save_state_t save_state;
	_Atomic int kill;
	Eina_Thread thread;

	uint32_t srate;
	uint32_t frsize;
	uint32_t seq_size;

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
	bin_t *bin = &handle->bin;
	sp_app_t *app = bin->app;
	
	pthread_t self = pthread_self();

	struct sched_param schedp;
	memset(&schedp, 0, sizeof(struct sched_param));
	schedp.sched_priority = 70; //TODO make configurable
	
	if(schedp.sched_priority)
	{
		if(pthread_setschedparam(self, SCHED_FIFO, &schedp))
			fprintf(stderr, "pthread_setschedparam error\n");
	}
		
	const uint32_t nsamples = handle->frsize;

	const uint64_t nanos_per_period = (uint64_t)nsamples * NANO_SECONDS / handle->srate;
	handle->cycle.cur_frames = 0; // initialize frame counter
	_ntp_now(&handle->nxt_ntp);

	const unsigned n_period = 4; // do how many periods per iteration?

	struct timespec sleep_todo = {
		.tv_sec = 0,
		.tv_nsec = nanos_per_period * n_period
	};

	while(!atomic_load_explicit(&handle->kill, memory_order_relaxed))
	{
		struct timespec sleep_rem;
		if(clock_nanosleep(CLOCK_MONOTONIC, 0, &sleep_todo, &sleep_rem)) // has been interrupted?
			clock_nanosleep(CLOCK_MONOTONIC, 0, &sleep_rem, NULL); // sleep for remaining time

		uint32_t na = nsamples * n_period;

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
	
			if(sp_app_bypassed(app))
			{
				const sp_app_system_sink_t *sinks = sp_app_get_system_sinks(app);

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
									//fprintf(stderr, "app_from_ui ringbuffer full\n");
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
	}

	return NULL;
}

static void *
_system_port_add(void *data, system_port_t type, const char *short_name,
	const char *pretty_name, bool input, uint32_t order)
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

static void
_system_port_del(void *data, void *sys_port)
{
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);
	(void)handle;

	// unsupported, skip
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
_open(const char *path, const char *name, const char *id, void *data)
{
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);
	(void)name;

	if(bin->path)
		free(bin->path);
	bin->path = strdup(path);

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

EAPI_MAIN int
elm_main(int argc, char **argv);
	
EAPI_MAIN int
elm_main(int argc, char **argv)
{
	static prog_t handle;
	bin_t *bin = &handle.bin;

	handle.srate = 48000;
	handle.frsize = 1024;
	handle.seq_size = SEQ_SIZE;

	bin->has_gui = true;

	fprintf(stderr,
		"Synthpod "SYNTHPOD_VERSION"\n"
		"Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)\n"
		"Released under Artistic License 2.0 by Open Music Kontrollers\n");

	int c;
	while((c = getopt(argc, argv, "vhgGr:p:s:")) != -1)
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
					"   [-g]                 enable GUI (default)\n"
					"   [-G]                 disable GUI\n"
					"   [-r] sample-rate     sample rate (48000)\n"
					"   [-p] sample-period   frames per period (1024)\n"
					"   [-s] sequence-size   minimum sequence size (8192)\n\n"
					, argv[0]);
				return 0;
			case 'g':
				bin->has_gui = true;
				break;
			case 'G':
				bin->has_gui = false;
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
			case '?':
				if( (optopt == 'r') || (optopt == 'p') || (optopt == 's') )
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
	
	LV2_URID_Map *map = &bin->map;
	
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

	// stop rt thread
	if(handle.thread)
	{
		atomic_store_explicit(&handle.kill, 1, memory_order_relaxed);
		eina_thread_join(handle.thread);
	}

	// deinit
	bin_deinit(bin);

	return 0;
}

ELM_MAIN()
