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
#include <assert.h>

#include <synthpod_app.h>
#if defined(BUILD_UI)
#	include <synthpod_ui.h>
#else
# include <Ecore.h>
# include <Ecore_File.h>
#endif
#include <ext_urid.h>
#include <varchunk.h>
#include <lv2_osc.h>
#include <osc.h>

#include <synthpod_nsm.h>

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/time/time.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/transport.h>
#include <jack/session.h>
#if defined(JACK_HAS_METADATA_API)
#	include <jack/metadata.h>
#	include <jack/uuid.h>
#endif

#include <Eina.h>

#ifndef MAX
#	define MAX(A, B) ((A) > (B) ? (A) : (B))
#endif

#define CHUNK_SIZE 0x10000
#define SEQ_SIZE 0x2000
#define OSC_SIZE 0x800

#	define JAN_1970 (uint64_t)0x83aa7e80
#	define SLICE (double)0x0.00000001p0 // smallest NTP time slice

typedef enum _save_state_t save_state_t;
typedef struct _prog_t prog_t;

enum _save_state_t {
	SAVE_STATE_INTERNAL = 0,
	SAVE_STATE_NSM,
	SAVE_STATE_JACK
};

struct _prog_t {
	ext_urid_t *ext_urid;

	sp_app_t *app;
	sp_app_driver_t app_driver;
	
	varchunk_t *app_to_worker;
	varchunk_t *app_from_worker;

	varchunk_t *app_to_log;

	char *path;
	synthpod_nsm_t *nsm;

#if defined(BUILD_UI)
	sp_ui_t *ui;
	sp_ui_driver_t ui_driver;

	varchunk_t *app_to_ui;
	varchunk_t *app_from_ui;

	varchunk_t *ui_to_app;
	varchunk_t *ui_from_app;

	Ecore_Animator *ui_anim;
	Evas_Object *win;
#else
	Ecore_Event_Handler *sig;
#endif

	LV2_Atom_Forge forge;

	osc_data_t osc_buf [OSC_SIZE]; //TODO how big?
	osc_data_t *osc_ptr;
	osc_data_t *osc_end;
	int bndl_cnt;
	osc_data_t *bndl [32]; // 32 nested bundles should be enough

	osc_forge_t oforge;
	int frame_cnt;
	LV2_Atom_Forge_Frame frame [32][2]; // 32 nested bundles should be enough

	LV2_URID midi_MidiEvent;

	LV2_URID log_entry;
	LV2_URID log_error;
	LV2_URID log_note;
	LV2_URID log_trace;
	LV2_URID log_warning;

	LV2_URID time_position;
	LV2_URID time_barBeat;
	LV2_URID time_bar;
	LV2_URID time_beat;
	LV2_URID time_beatUnit;
	LV2_URID time_beatsPerBar;
	LV2_URID time_beatsPerMinute;
	LV2_URID time_frame;
	LV2_URID time_framesPerSecond;
	LV2_URID time_speed;

	volatile int kill;
	save_state_t save_state;

	char *server_name;
	char *session_id;
	jack_client_t *client;
	jack_session_event_t *session_event;
	uint32_t seq_size;
	
	volatile int worker_dead;
	Eina_Thread worker_thread;
	Eina_Semaphore worker_sem;
	bool worker_sem_needs_release;

	struct {
		jack_transport_state_t rolling;
		jack_nframes_t frame;
		double bpm;
	} trans;

#if defined(JACK_HAS_CYCLE_TIMES)
	osc_schedule_t osc_sched;
	struct timespec ntp;
	struct {
		jack_nframes_t cur_frames;
		jack_nframes_t ref_frames;
		jack_time_t cur_usecs;
		jack_time_t nxt_usecs;
		float T;
	} cycle;
#endif
};

static const synthpod_nsm_driver_t nsm_driver; // forwared-declaration

// non-rt worker-thread
static void *
_worker_thread(void *data, Eina_Thread thread)
{
	prog_t *handle = data;

	while(!handle->worker_dead)
	{
		eina_semaphore_lock(&handle->worker_sem);

		size_t size;
		const void *body;
		while((body = varchunk_read_request(handle->app_to_worker, &size)))
		{
			sp_worker_from_app(handle->app, size, body);
			varchunk_read_advance(handle->app_to_worker);
		}

		const char *trace;
		while((trace = varchunk_read_request(handle->app_to_log, &size)))
		{
			fprintf(stderr, "[Trace] %s\n", trace);

			varchunk_read_advance(handle->app_to_log);
		}
	}

	return NULL;
}

#if defined(BUILD_UI)
// non-rt ui-thread
static void
_ui_delete_request(void *data, Evas_Object *obj, void *event)
{
	prog_t *handle = data;

	elm_exit();	
}

// non-rt ui-thread
static Eina_Bool
_ui_animator(void *data)
{
	prog_t *handle = data;

	size_t size;
	const LV2_Atom *atom;
	while((atom = varchunk_read_request(handle->app_to_ui, &size)))
	{
		sp_ui_from_app(handle->ui, atom);
		varchunk_read_advance(handle->app_to_ui);
	}

	return EINA_TRUE; // continue animator
}
#else
static Eina_Bool
_quit(void *data, int type, void *info)
{
	prog_t *handle = data;

	ecore_main_loop_quit();

	return EINA_TRUE;
}
#endif // BUILD_UI

static void
_trans_event(prog_t *prog,  LV2_Atom_Forge *forge, int rolling, jack_position_t *pos)
{
	LV2_Atom_Forge_Frame frame;

	lv2_atom_forge_frame_time(forge, 0);
	lv2_atom_forge_object(forge, &frame, 0, prog->time_position);
	{
		lv2_atom_forge_key(forge, prog->time_frame);
		lv2_atom_forge_long(forge, pos->frame);

		lv2_atom_forge_key(forge, prog->time_speed);
		lv2_atom_forge_float(forge, rolling ? 1.0 : 0.0);

		if(pos->valid & JackPositionBBT)
		{
			lv2_atom_forge_key(forge, prog->time_barBeat);
			lv2_atom_forge_float(forge,
				pos->beat - 1 + (pos->tick / pos->ticks_per_beat));

			lv2_atom_forge_key(forge, prog->time_bar);
			lv2_atom_forge_long(forge, pos->bar - 1);

			lv2_atom_forge_key(forge, prog->time_beatUnit);
			lv2_atom_forge_int(forge, pos->beat_type);

			lv2_atom_forge_key(forge, prog->time_beatsPerBar);
			lv2_atom_forge_float(forge, pos->beats_per_bar);

			lv2_atom_forge_key(forge, prog->time_beatsPerMinute);
			lv2_atom_forge_float(forge, pos->beats_per_minute);
		}
	}
	lv2_atom_forge_pop(forge, &frame);
}

// rt
static void
_bundle_in(osc_time_t timestamp, void *data)
{
	prog_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;

	//TODO check return
	osc_forge_bundle_push(&handle->oforge, forge,
		handle->frame[handle->frame_cnt++], timestamp);
}

// rt
static void
_bundle_out(osc_time_t timestamp, void *data)
{
	prog_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;

	osc_forge_bundle_pop(&handle->oforge, forge,
		handle->frame[--handle->frame_cnt]);
}

// rt
static int
_message(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	prog_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame frame [2];

	const osc_data_t *ptr = buf;
	osc_forge_message_push(&handle->oforge, forge, frame, path, fmt);

	for(const char *type = fmt; *type; type++)
		switch(*type)
		{
			case 'i':
			{
				int32_t i;
				ptr = osc_get_int32(ptr, &i);
				osc_forge_int32(&handle->oforge, forge, i);
				break;
			}
			case 'f':
			{
				float f;
				ptr = osc_get_float(ptr, &f);
				osc_forge_float(&handle->oforge, forge, f);
				break;
			}
			case 's':
			case 'S':
			{
				const char *s;
				ptr = osc_get_string(ptr, &s);
				osc_forge_string(&handle->oforge, forge, s);
				break;
			}
			case 'b':
			{
				osc_blob_t b;
				ptr = osc_get_blob(ptr, &b);
				osc_forge_blob(&handle->oforge, forge, b.size, b.payload);
				break;
			}

			case 'h':
			{
				int64_t h;
				ptr = osc_get_int64(ptr, &h);
				osc_forge_int64(&handle->oforge, forge, h);
				break;
			}
			case 'd':
			{
				double d;
				ptr = osc_get_double(ptr, &d);
				osc_forge_double(&handle->oforge, forge, d);
				break;
			}
			case 't':
			{
				uint64_t t;
				ptr = osc_get_timetag(ptr, &t);
				osc_forge_timestamp(&handle->oforge, forge, t);
				break;
			}

			case 'T':
			case 'F':
			case 'N':
			case 'I':
			{
				break;
			}

			case 'c':
			{
				char c;
				ptr = osc_get_char(ptr, &c);
				osc_forge_char(&handle->oforge, forge, c);
				break;
			}
			case 'm':
			{
				const uint8_t *m;
				ptr = osc_get_midi(ptr, &m);
				osc_forge_midi(&handle->oforge, forge, 3, m + 1); // skip port byte
				break;
			}
		}

	osc_forge_message_pop(&handle->oforge, forge, frame);

	return 1;
}

static const osc_method_t methods [] = {
	{NULL, NULL, _message},

	{NULL, NULL, NULL}
};

// rt
static void
_bundle_push_cb(uint64_t timestamp, void *data)
{
	prog_t *handle = data;

	handle->osc_ptr = osc_start_bundle(handle->osc_ptr, handle->osc_end, timestamp,
		&handle->bndl[handle->bndl_cnt++]);
}

// rt
static void
_bundle_pop_cb(void *data)
{
	prog_t *handle = data;

	handle->osc_ptr = osc_end_bundle(handle->osc_ptr, handle->osc_end,
		handle->bndl[--handle->bndl_cnt]);
}

// rt
static void
_message_cb(const char *path, const char *fmt, const LV2_Atom_Tuple *body,
	void *data)
{
	prog_t *handle = data;

	osc_data_t *ptr = handle->osc_ptr;
	const osc_data_t *end = handle->osc_end;

	osc_data_t *itm;
	if(handle->bndl_cnt)
		ptr = osc_start_bundle_item(ptr, end, &itm);

	ptr = osc_set_path(ptr, end, path);
	ptr = osc_set_fmt(ptr, end, fmt);

	const LV2_Atom *itr = lv2_atom_tuple_begin(body);
	for(const char *type = fmt;
		*type && !lv2_atom_tuple_is_end(LV2_ATOM_BODY(body), body->atom.size, itr);
		type++, itr = lv2_atom_tuple_next(itr))
	{
		switch(*type)
		{
			case 'i':
			{
				ptr = osc_set_int32(ptr, end, ((const LV2_Atom_Int *)itr)->body);
				break;
			}
			case 'f':
			{
				ptr = osc_set_float(ptr, end, ((const LV2_Atom_Float *)itr)->body);
				break;
			}
			case 's':
			case 'S':
			{
				ptr = osc_set_string(ptr, end, LV2_ATOM_BODY_CONST(itr));
				break;
			}
			case 'b':
			{
				ptr = osc_set_blob(ptr, end, itr->size, LV2_ATOM_BODY(itr));
				break;
			}

			case 'h':
			{
				ptr = osc_set_int64(ptr, end, ((const LV2_Atom_Long *)itr)->body);
				break;
			}
			case 'd':
			{
				ptr = osc_set_double(ptr, end, ((const LV2_Atom_Double *)itr)->body);
				break;
			}
			case 't':
			{
				ptr = osc_set_timetag(ptr, end, ((const LV2_Atom_Long *)itr)->body);
				break;
			}

			case 'T':
			case 'F':
			case 'N':
			case 'I':
			{
				break;
			}

			case 'c':
			{
				ptr = osc_set_char(ptr, end, ((const LV2_Atom_Int *)itr)->body);
				break;
			}
			case 'm':
			{
				const uint8_t *src = LV2_ATOM_BODY_CONST(itr);
				const uint8_t dst [4] = {
					0x00, // port byte
					itr->size >= 1 ? src[0] : 0x00,
					itr->size >= 2 ? src[1] : 0x00,
					itr->size >= 3 ? src[2] : 0x00
				};
				ptr = osc_set_midi(ptr, end, dst);
				break;
			}
		}
	}
	
	if(handle->bndl_cnt)
		ptr = osc_end_bundle_item(ptr, end, itm);

	handle->osc_ptr = ptr;
}

// rt
static int
_process(jack_nframes_t nsamples, void *data)
{
	prog_t *handle = data;
	sp_app_t *app = handle->app;

#if defined(JACK_HAS_CYCLE_TIMES)
	clock_gettime(CLOCK_REALTIME, &handle->ntp);
	jack_nframes_t offset = jack_frames_since_cycle_start(handle->client);

	jack_get_cycle_times(handle->client, &handle->cycle.cur_frames,
		&handle->cycle.cur_usecs, &handle->cycle.nxt_usecs, &handle->cycle.T);

	handle->cycle.ref_frames = handle->cycle.cur_frames + offset;
	handle->ntp.tv_sec += JAN_1970; // convert NTP to OSC time
	handle->cycle.T = (float)nsamples / (handle->cycle.nxt_usecs - handle->cycle.cur_usecs);
#endif

	// get transport position
	jack_position_t pos;
	int rolling = jack_transport_query(handle->client, &pos) == JackTransportRolling;
	int trans_changed = (rolling != handle->trans.rolling)
		|| (pos.frame != handle->trans.frame)
		|| (pos.beats_per_minute != handle->trans.bpm);

	const size_t sample_buf_size = sizeof(float) * nsamples;
	const sp_app_system_source_t *sources = sp_app_get_system_sources(app);
	const sp_app_system_sink_t *sinks = sp_app_get_system_sinks(app);

	int paused = sp_app_paused(app);
	if(paused == 1) // aka loading state
	{
		// clear output buffers
		for(const sp_app_system_sink_t *sink=sinks;
			sink->type != SYSTEM_PORT_NONE;
			sink++)
		{
			switch(sink->type)
			{
				case SYSTEM_PORT_NONE:
				case SYSTEM_PORT_CONTROL:
					break;

				case SYSTEM_PORT_AUDIO:
				case SYSTEM_PORT_CV:
				{
					void *out_buf = jack_port_get_buffer(sink->sys_port, nsamples);
					memset(out_buf, 0x0, sample_buf_size);
					break;
				}
				case SYSTEM_PORT_MIDI:
				case SYSTEM_PORT_OSC:
				{
					void *out_buf = jack_port_get_buffer(sink->sys_port, nsamples);
					jack_midi_clear_buffer(out_buf);
					break;
				}
			}
		}

		return 0;
	}

	//TODO use __builtin_assume_aligned

	// fill input buffers
	for(const sp_app_system_source_t *source=sources;
		source->type != SYSTEM_PORT_NONE;
		source++)
	{
		switch(source->type)
		{
			case SYSTEM_PORT_NONE:
			case SYSTEM_PORT_CONTROL:
				break;

			case SYSTEM_PORT_AUDIO:
			case SYSTEM_PORT_CV:
			{
				const void *in_buf = jack_port_get_buffer(source->sys_port, nsamples);
				memcpy(source->buf, in_buf, sample_buf_size);
				break;
			}
			case SYSTEM_PORT_MIDI:
			{
				void *in_buf = jack_port_get_buffer(source->sys_port, nsamples);
				void *seq_in = source->buf;

				LV2_Atom_Forge *forge = &handle->forge;
				LV2_Atom_Forge_Frame frame;
				lv2_atom_forge_set_buffer(forge, seq_in, SEQ_SIZE);
				lv2_atom_forge_sequence_head(forge, &frame, 0);

				if(trans_changed)
					_trans_event(handle, forge, rolling, &pos);

				int n = jack_midi_get_event_count(in_buf);
				for(int i=0; i<n; i++)
				{
					jack_midi_event_t mev;
					jack_midi_event_get(&mev, in_buf, i);

					//add jack midi event to in_buf
					lv2_atom_forge_frame_time(forge, mev.time);
					lv2_atom_forge_atom(forge, mev.size, handle->midi_MidiEvent);
					lv2_atom_forge_raw(forge, mev.buffer, mev.size);
					lv2_atom_forge_pad(forge, mev.size);
				}
				lv2_atom_forge_pop(forge, &frame);

				break;
			}

			case SYSTEM_PORT_OSC:
			{
				void *in_buf = jack_port_get_buffer(source->sys_port, nsamples);
				void *seq_in = source->buf;

				LV2_Atom_Forge *forge = &handle->forge;
				LV2_Atom_Forge_Frame frame;
				lv2_atom_forge_set_buffer(forge, seq_in, SEQ_SIZE);
				lv2_atom_forge_sequence_head(forge, &frame, 0);

				if(trans_changed)
					_trans_event(handle, forge, rolling, &pos);

				int n = jack_midi_get_event_count(in_buf);	
				for(int i=0; i<n; i++)
				{
					jack_midi_event_t mev;
					jack_midi_event_get(&mev, (void *)in_buf, i);

					//add jack osc event to in_buf
					if(osc_check_packet(mev.buffer, mev.size))
					{
						lv2_atom_forge_frame_time(forge, mev.time);
						osc_dispatch_method(mev.buffer, mev.size, methods,
							_bundle_in, _bundle_out, handle);
					}
				}
				lv2_atom_forge_pop(forge, &frame);

				break;
			}
		}
	}

	// update transport state
	handle->trans.frame = rolling
		? pos.frame + nsamples
		: pos.frame;
	handle->trans.bpm = pos.beats_per_minute;
	handle->trans.rolling = rolling;

	// read events from worker
	if(!paused) // aka not saving state
	{
		size_t size;
		const void *body;
		int n = 0;
		while((body = varchunk_read_request(handle->app_from_worker, &size))
			&& (n++ < 10) ) //FIXME limit to how many events?
		{
			sp_app_from_worker(handle->app, size, body);
			varchunk_read_advance(handle->app_from_worker);
		}
	}

	// run synthpod app pre
	sp_app_run_pre(handle->app, nsamples);

#if defined(BUILD_UI)
	// read events from UI
	if(!paused) // aka not saving state
	{
		size_t size;
		const LV2_Atom *atom;
		int n = 0;
		while((atom = varchunk_read_request(handle->app_from_ui, &size))
			&& (n++ < 10) ) //FIXME limit to how many events?
		{
			sp_app_from_ui(handle->app, atom);
			varchunk_read_advance(handle->app_from_ui);
		}
	}
#endif // BUILD_UI
	
	// run synthpod app post
	sp_app_run_post(handle->app, nsamples);

	// fill output buffers
	for(const sp_app_system_sink_t *sink=sinks;
		sink->type != SYSTEM_PORT_NONE;
		sink++)
	{
		switch(sink->type)
		{
			case SYSTEM_PORT_NONE:
			case SYSTEM_PORT_CONTROL:
				break;

			case SYSTEM_PORT_AUDIO:
			case SYSTEM_PORT_CV:
			{
				void *out_buf = jack_port_get_buffer(sink->sys_port, nsamples);
				memcpy(out_buf, sink->buf, sample_buf_size);
				break;
			}
			case SYSTEM_PORT_MIDI:
			{
				void *out_buf = jack_port_get_buffer(sink->sys_port, nsamples);
				const LV2_Atom_Sequence *seq_out = sink->buf;

				// fill midi output buffer
				jack_midi_clear_buffer(out_buf);
				if(seq_out)
				{
					LV2_ATOM_SEQUENCE_FOREACH(seq_out, ev)
					{
						const LV2_Atom *atom = &ev->body;

						jack_midi_event_write(out_buf, ev->time.frames,
							LV2_ATOM_BODY_CONST(atom), atom->size);
					}
				}

				break;
			}

			case SYSTEM_PORT_OSC:
			{
				void *out_buf = jack_port_get_buffer(sink->sys_port, nsamples);
				const LV2_Atom_Sequence *seq_out = sink->buf;

				// fill midi output buffer
				jack_midi_clear_buffer(out_buf);
				if(seq_out)
				{
					LV2_ATOM_SEQUENCE_FOREACH(seq_out, ev)
					{
						const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

						handle->osc_ptr = handle->osc_buf;
						handle->osc_end = handle->osc_buf + OSC_SIZE;

						osc_atom_event_unroll(&handle->oforge, obj, _bundle_push_cb,
							_bundle_pop_cb, _message_cb, handle);

						size_t size = handle->osc_ptr
							? handle->osc_ptr - handle->osc_buf
							: 0;

						if(size)
						{
							jack_midi_event_write(out_buf, ev->time.frames,
								handle->osc_buf, size);
						}
					}
				}

				break;
			}
		}
	}
	
	if(handle->worker_sem_needs_release)
	{
		eina_semaphore_release(&handle->worker_sem, 1);
		handle->worker_sem_needs_release = false;
	}

	return 0;
}

// ui
static void
_session_async(void *data)
{
	prog_t *handle = data;

	jack_session_event_t *ev = handle->session_event;
	
	/*
	printf("_session_async: %s %s %s\n",
		ev->session_dir, ev->client_uuid, ev->command_line);
	*/

	asprintf(&ev->command_line, "synthpod_jack -u %s $(SESSION_DIR)",
		ev->client_uuid);

	switch(ev->type)
	{
		case JackSessionSaveAndQuit:
			handle->kill = 1; // quit after saving
			// fall-through
		case JackSessionSave:
			handle->save_state = SAVE_STATE_JACK;
#if defined(BUILD_UI) //FIXME
			sp_ui_bundle_save(handle->ui, ev->session_dir, 1);
#endif
			break;
		case JackSessionSaveTemplate:
			handle->save_state = SAVE_STATE_JACK;
#if defined(BUILD_UI) //FIXME
			sp_ui_bundle_new(handle->ui);
			sp_ui_bundle_save(handle->ui, ev->session_dir, 1);
#endif
			break;
	}
}

// non-rt
static void
_session(jack_session_event_t *ev, void *data)
{
	prog_t *handle = data;

	handle->session_event = ev;
	ecore_main_loop_thread_safe_call_async(_session_async, data);
}

#if defined(BUILD_UI)
// rt
static void *
_app_to_ui_request(size_t size, void *data)
{
	prog_t *handle = data;

	return varchunk_write_request(handle->app_to_ui, size);
}
static void
_app_to_ui_advance(size_t size, void *data)
{
	prog_t *handle = data;

	varchunk_write_advance(handle->app_to_ui, size);
}

// non-rt ui-thread
static void *
_ui_to_app_request(size_t size, void *data)
{
	prog_t *handle = data;

	void *ptr;
	do
		ptr = varchunk_write_request(handle->app_from_ui, size);
	while(!ptr); // wait until there is enough space

	return ptr;
}
static void
_ui_to_app_advance(size_t size, void *data)
{
	prog_t *handle = data;

	varchunk_write_advance(handle->app_from_ui, size);
}

static void
_ui_opened(void *data, int status)
{
	prog_t *handle = data;

	//printf("_ui_opened: %i\n", status);
	synthpod_nsm_opened(handle->nsm, status);
}
static void
_ui_saved(void *data, int status)
{
	prog_t *handle = data;

	//printf("_ui_saved: %i\n", status);
	if(handle->save_state == SAVE_STATE_NSM)
	{
		synthpod_nsm_saved(handle->nsm, status);
	}
	else if(handle->save_state == SAVE_STATE_JACK)
	{
		jack_session_event_t *ev = handle->session_event;
		if(ev)
		{
			if(status != 0)
				ev->flags |= JackSessionSaveError;
			jack_session_reply(handle->client, ev);
			jack_session_event_free(ev);
		}
		handle->session_event = NULL;
	}
	handle->save_state = SAVE_STATE_INTERNAL;

	if(handle->kill)
	{
#if defined(BUILD_UI)
		elm_exit();
#else
		ecore_main_loop_quit();
#endif
	}
}
static void
_ui_close(void *data)
{
	prog_t *handle = data;

	//printf("_ui_close\n");
	elm_exit();
}
#endif // BUILD_UI

// rt
static void *
_app_to_worker_request(size_t size, void *data)
{
	prog_t *handle = data;

	return varchunk_write_request(handle->app_to_worker, size);
}
static void
_app_to_worker_advance(size_t size, void *data)
{
	prog_t *handle = data;

	varchunk_write_advance(handle->app_to_worker, size);
	handle->worker_sem_needs_release = true;
}

// non-rt worker-thread
static void *
_worker_to_app_request(size_t size, void *data)
{
	prog_t *handle = data;

	void *ptr;
	do
		ptr = varchunk_write_request(handle->app_from_worker, size);
	while(!ptr); // wait until there is enough space

	return ptr;
}
static void
_worker_to_app_advance(size_t size, void *data)
{
	prog_t *handle = data;

	varchunk_write_advance(handle->app_from_worker, size);
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_vprintf(void *data, LV2_URID type, const char *fmt, va_list args)
{
	prog_t *handle = data;

	if(type == handle->log_trace)
	{
		char *trace;
		if((trace = varchunk_write_request(handle->app_to_log, 1024)))
		{
			vsprintf(trace, fmt, args);

			size_t written = strlen(trace) + 1;
			varchunk_write_advance(handle->app_to_log, written);
			handle->worker_sem_needs_release = true;
		}
	}
	else // !log_trace
	{
		const char *type_str = NULL;
		if(type == handle->log_entry)
			type_str = "Entry";
		else if(type == handle->log_error)
			type_str = "Error";
		else if(type == handle->log_note)
			type_str = "Note";
		else if(type == handle->log_warning)
			type_str = "Warning";

		//TODO send to UI?

		fprintf(stderr, "[%s] ", type_str);
		vfprintf(stderr, fmt, args);
		fputc('\n', stderr);

		return 0;
	}

	return -1;
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_printf(void *data, LV2_URID type, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(data, type, fmt, args);
  va_end(args);

	return ret;
}

static void *
_system_port_add(void *data, System_Port_Type type, const char *short_name,
	const char *pretty_name, int input)
{
	prog_t *handle = data;
	
	//printf("_system_port_add: %s\n", short_name);

	jack_port_t *jack_port = NULL;

	unsigned long flags = input ? JackPortIsInput : JackPortIsOutput;

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
			jack_port = jack_port_register(handle->client, short_name,
				JACK_DEFAULT_AUDIO_TYPE, flags, 0);
			break;
		}
		case SYSTEM_PORT_CV:
		{
			jack_port = jack_port_register(handle->client, short_name,
				JACK_DEFAULT_AUDIO_TYPE, flags, 0);

#if defined(JACK_HAS_METADATA_API)
			if(jack_port)
			{
				jack_uuid_t uuid = jack_port_uuid(jack_port);
				jack_set_property(handle->client, uuid,
					"http://jackaudio.org/metadata/signal-type", "CV", "text/plain");
			}
#endif
			break;
		}

		case SYSTEM_PORT_MIDI:
		{
			jack_port = jack_port_register(handle->client, short_name,
				JACK_DEFAULT_MIDI_TYPE, flags, 0);
			break;
		}
		case SYSTEM_PORT_OSC:
		{
			jack_port = jack_port_register(handle->client, short_name,
				JACK_DEFAULT_MIDI_TYPE, flags, 0);

#if defined(JACK_HAS_METADATA_API)
			if(jack_port)
			{
				jack_uuid_t uuid = jack_port_uuid(jack_port);
				jack_set_property(handle->client, uuid,
					"http://jackaudio.org/metadata/event-types", "OSC", "text/plain");
			}
#endif
			break;
		}
	}

#if defined(JACK_HAS_METADATA_API)
	if(jack_port && pretty_name)
	{
		jack_uuid_t uuid = jack_port_uuid(jack_port);
		jack_set_property(handle->client, uuid,
			JACK_METADATA_PRETTY_NAME, pretty_name, "text/plain");
	}
#endif

	return jack_port;
}

static void
_system_port_del(void *data, void *sys_port)
{
	//printf("_system_port_del\n");

	prog_t *handle = data;
	jack_port_t *jack_port = sys_port;

	if(!jack_port || !handle->client)
		return;

#if defined(JACK_HAS_METADATA_API)
	jack_uuid_t uuid = jack_port_uuid(jack_port);
	jack_remove_properties(handle->client, uuid);
#endif
			
	jack_port_unregister(handle->client, jack_port);
}

static void
_shutdown_async(void *data)
{
	prog_t *handle = data;

#if defined(BUILD_UI)
	elm_exit();
#else
	ecore_main_loop_quit();
#endif
}

static void
_shutdown(void *data)
{
	prog_t *handle = data;

	handle->client = NULL; // client has died, didn't it?
	ecore_main_loop_thread_safe_call_async(_shutdown_async, handle);
}

static void
_xrun_async(void *data)
{
	prog_t *handle = data;

	fprintf(stderr, "JACK XRun\n");
}

static int
_xrun(void *data)
{
	prog_t *handle = data;

	ecore_main_loop_thread_safe_call_async(_xrun_async, handle);

	return 0;
}

static int
_jack_init(prog_t *handle, const char *id)
{
	jack_options_t opts = JackNullOption;
	if(handle->server_name)
		opts |= JackServerName;
	if(handle->session_id)
		opts |= JackSessionID;

	jack_status_t status;
	if(!(handle->client = jack_client_open(id, opts, &status,
		handle->server_name ? handle->server_name : handle->session_id,
		handle->server_name ? handle->session_id : NULL)))
	{
		return -1;
	}

	//TODO check status

	// set client pretty name
#if defined(JACK_HAS_METADATA_API)
	jack_uuid_t uuid;
	const char *client_name = jack_get_client_name(handle->client);
	const char *uuid_str = jack_get_uuid_for_client_name(handle->client, client_name);
	jack_uuid_parse(uuid_str, &uuid);
	jack_set_property(handle->client, uuid,
		JACK_METADATA_PRETTY_NAME, "Synthpod", "text/plain");
#endif

	// set client process callback
	if(jack_set_process_callback(handle->client, _process, handle))
		return -1;
	if(jack_set_session_callback(handle->client, _session, handle))
		return -1;
	jack_on_shutdown(handle->client, _shutdown, handle);
	jack_set_xrun_callback(handle->client, _xrun, handle);

	return 0;
}

static void
_jack_deinit(prog_t *handle)
{
	if(handle->client)
	{
		// remove client properties
#if defined(JACK_HAS_METADATA_API)
		jack_uuid_t uuid;
		const char *client_name = jack_get_client_name(handle->client);
		const char *uuid_str = jack_get_uuid_for_client_name(handle->client, client_name);
		jack_uuid_parse(uuid_str, &uuid);
		jack_remove_properties(handle->client, uuid);
#endif

		jack_deactivate(handle->client);
		jack_client_close(handle->client);
		handle->client = NULL;
	}
}

static int 
_open(const char *path, const char *name, const char *id, void *data)
{
	(void)name;
	prog_t *handle = data;

	if(handle->path)
		free(handle->path);
	handle->path = strdup(path);

	// jack init
	if(_jack_init(handle, id))
		return -1;

	// synthpod init
	handle->app_driver.sample_rate = jack_get_sample_rate(handle->client);
	handle->app_driver.max_block_size = jack_get_buffer_size(handle->client);
	handle->app_driver.min_block_size = jack_get_buffer_size(handle->client);
	handle->app_driver.seq_size = MAX(handle->seq_size,
		jack_port_type_get_buffer_size(handle->client, JACK_DEFAULT_MIDI_TYPE));
	
	// app init
	handle->app = sp_app_new(NULL, &handle->app_driver, handle);

	// jack activate
	jack_activate(handle->client); //TODO check

#if defined(BUILD_UI) //FIXME
	sp_ui_bundle_load(handle->ui, handle->path, 1);
#endif

	return 0; // success
}

static int
_save(void *data)
{
	prog_t *handle = data;

	handle->save_state = SAVE_STATE_NSM;
#if defined(BUILD_UI) //FIXME
	sp_ui_bundle_save(handle->ui, handle->path, 1);
#endif

	return 0; // success
}

#if defined(BUILD_UI)
static int
_show(void *data)
{
	prog_t *handle = data;

	evas_object_show(handle->win);
	
	return 0;
}

static int
_hide(void *data)
{
	prog_t *handle = data;

	evas_object_hide(handle->win);

	return 0;
}
#endif // BUILD_UI

static const synthpod_nsm_driver_t nsm_driver = {
	.open = _open,
	.save = _save,
#if defined(BUILD_UI)
	.show = _show,
	.hide = _hide
#else
	.show = NULL,
	.hide = NULL
#endif // BUILD_UI
};

#if defined(JACK_HAS_CYCLE_TIMES)
// rt
static int64_t
_osc_schedule_osc2frames(osc_schedule_handle_t instance, uint64_t timestamp)
{
	prog_t *handle = instance;

	if(timestamp == 1ULL)
		return 0; // inject at start of period

	uint32_t time_sec = timestamp >> 32;
	uint32_t time_frac = timestamp & 0xffffffff;

	double diff = time_sec;
	diff -= handle->ntp.tv_sec;
	diff += time_frac * SLICE;
	diff -= handle->ntp.tv_nsec * 1e-9;
	diff *= 1e6; // convert s to us

	int64_t frames = handle->cycle.ref_frames
		- handle->cycle.cur_frames
		+ handle->cycle.T * diff;

	return frames;
}

// rt
static uint64_t
_osc_schedule_frames2osc(osc_schedule_handle_t instance, int64_t frames)
{
	prog_t *handle = instance;

	double diff = (frames - handle->cycle.ref_frames + handle->cycle.cur_frames) / handle->cycle.T;
	diff *= 1e-6; // convert us to s;
	uint64_t secs = trunc(diff);
	uint64_t nsecs = (diff - secs) * 1e9;

	uint64_t time_sec = handle->ntp.tv_sec;
	uint64_t time_frac = handle->ntp.tv_nsec;
	
	time_sec += secs;
	time_frac += nsecs;

	while(time_frac > 1000000000)
	{
		time_sec += 1;
		time_frac -= 1000000000;
	}
	
	time_frac *= 4.295; // ns -> frac

	uint64_t timestamp = (time_sec << 32) | time_frac;

	return timestamp;
}
#endif // JACK_HAS_CYCLE_TIMES

#if defined(BUILD_UI)
EAPI_MAIN int
elm_main(int argc, char **argv)
#else
int
main(int argc, char **argv)
#endif
{
	static prog_t handle;

	handle.server_name = NULL;
	handle.session_id = NULL;
	handle.seq_size = SEQ_SIZE;
	
	char c;
	while((c = getopt(argc, argv, "vhn:u:s:")) != -1)
	{
		switch(c)
		{
			case 'v':
				fprintf(stderr, "Synthpod "SYNTHPOD_VERSION"\n"
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
					"   [-n] server-name     connect to named JACK daemon\n"
					"   [-u] client-uuid     client UUID for JACK session management\n"
					"   [-s] sequence-size   minimum sequence size\n\n"
					, argv[0]);
				return 0;
			case 'n':
				handle.server_name = optarg;
				break;
			case 'u':
				handle.session_id = optarg;
				break;
			case 's':
				handle.seq_size = MAX(SEQ_SIZE, atoi(optarg));
				break;
			case '?':
				if( (optopt == 'n') || (optopt == 'u') || (optopt == 's') )
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

	// varchunk init
#if defined(BUILD_UI)
	handle.app_to_ui = varchunk_new(CHUNK_SIZE);
	handle.app_from_ui = varchunk_new(CHUNK_SIZE);
#endif
	handle.app_to_worker = varchunk_new(CHUNK_SIZE);
	handle.app_from_worker = varchunk_new(CHUNK_SIZE);

	handle.app_to_log = varchunk_new(CHUNK_SIZE);

	// ext_urid init
	handle.ext_urid = ext_urid_new();
	LV2_URID_Map *map = ext_urid_map_get(handle.ext_urid);
	LV2_URID_Unmap *unmap = ext_urid_unmap_get(handle.ext_urid);

	lv2_atom_forge_init(&handle.forge, map);
	osc_forge_init(&handle.oforge, map);
	
	handle.midi_MidiEvent = map->map(map->handle, LV2_MIDI__MidiEvent);

	handle.log_entry = map->map(map->handle, LV2_LOG__Entry);
	handle.log_error = map->map(map->handle, LV2_LOG__Error);
	handle.log_note = map->map(map->handle, LV2_LOG__Note);
	handle.log_trace = map->map(map->handle, LV2_LOG__Trace);
	handle.log_warning = map->map(map->handle, LV2_LOG__Warning);

	handle.time_position = map->map(map->handle, LV2_TIME__Position);
	handle.time_barBeat = map->map(map->handle, LV2_TIME__barBeat);
	handle.time_bar = map->map(map->handle, LV2_TIME__bar);
	handle.time_beat = map->map(map->handle, LV2_TIME__beat);
	handle.time_beatUnit = map->map(map->handle, LV2_TIME__beatUnit);
	handle.time_beatsPerBar = map->map(map->handle, LV2_TIME__beatsPerBar);
	handle.time_beatsPerMinute = map->map(map->handle, LV2_TIME__beatsPerMinute);
	handle.time_frame = map->map(map->handle, LV2_TIME__frame);
	handle.time_framesPerSecond = map->map(map->handle, LV2_TIME__framesPerSecond);
	handle.time_speed = map->map(map->handle, LV2_TIME__speed);

	handle.app_driver.map = map;
	handle.app_driver.unmap = unmap;
	handle.app_driver.log_printf = _log_printf;
	handle.app_driver.log_vprintf = _log_vprintf;
#if defined(BUILD_UI)
	handle.app_driver.to_ui_request = _app_to_ui_request;
	handle.app_driver.to_ui_advance = _app_to_ui_advance;
#else
	handle.app_driver.to_ui_request = NULL;
	handle.app_driver.to_ui_advance = NULL;
#endif
	handle.app_driver.to_worker_request = _app_to_worker_request;
	handle.app_driver.to_worker_advance = _app_to_worker_advance;
	handle.app_driver.to_app_request = _worker_to_app_request;
	handle.app_driver.to_app_advance = _worker_to_app_advance;
	handle.app_driver.system_port_add = _system_port_add;
	handle.app_driver.system_port_del = _system_port_del;

#if defined(JACK_HAS_CYCLE_TIMES)
	handle.osc_sched.osc2frames = _osc_schedule_osc2frames;
	handle.osc_sched.frames2osc = _osc_schedule_frames2osc;
	handle.osc_sched.handle = &handle;
	handle.app_driver.osc_sched = &handle.osc_sched;
#else
	handle.app_driver.osc_sched = NULL;
#endif

#if defined(BUILD_UI)
	handle.ui_driver.map = map;
	handle.ui_driver.unmap = unmap;
	handle.ui_driver.to_app_request = _ui_to_app_request;
	handle.ui_driver.to_app_advance = _ui_to_app_advance;
	handle.ui_driver.opened = _ui_opened;
	handle.ui_driver.saved = _ui_saved;
	handle.ui_driver.close = _ui_close;
	handle.ui_driver.instance_access = 1; // enabled
	handle.ui_driver.features = SP_UI_FEATURE_NEW | SP_UI_FEATURE_SAVE | SP_UI_FEATURE_CLOSE;
	if(synthpod_nsm_managed() || handle.session_id)
		handle.ui_driver.features |= SP_UI_FEATURE_IMPORT_FROM | SP_UI_FEATURE_EXPORT_TO;
	else
		handle.ui_driver.features |= SP_UI_FEATURE_OPEN | SP_UI_FEATURE_SAVE_AS;

	// create main window
	handle.ui_anim = ecore_animator_add(_ui_animator, &handle);
	handle.win = elm_win_util_standard_add("synthpod", "Synthpod");
	evas_object_smart_callback_add(handle.win, "delete,request", _ui_delete_request, &handle);
	evas_object_resize(handle.win, 1280, 720);
	evas_object_show(handle.win);

	// ui init
	handle.ui = sp_ui_new(handle.win, NULL, &handle.ui_driver, &handle, 1);
#endif

	// NSM init
	const char *exe = strrchr(argv[0], '/');
	exe = exe ? exe + 1 : argv[0]; // we only want the program name without path
	handle.nsm = synthpod_nsm_new(exe, argv[optind], &nsm_driver, &handle); //TODO check

	// init semaphores
	eina_semaphore_new(&handle.worker_sem, 0);

	// threads init
	Eina_Bool status = eina_thread_create(&handle.worker_thread,
		EINA_THREAD_URGENT, -1, _worker_thread, &handle); //TODO

#if defined(BUILD_UI)
	// main loop
	elm_run();

	// ui deinit
	sp_ui_free(handle.ui);

	evas_object_del(handle.win);
	ecore_animator_del(handle.ui_anim);
#else
	handle.sig = ecore_event_handler_add(ECORE_EVENT_SIGNAL_EXIT, _quit, &handle);

	ecore_main_loop_begin();

	ecore_event_handler_del(handle.sig);
#endif // BUILD_UI

	// threads deinit
	handle.worker_dead = 1; // atomic operation
	eina_semaphore_release(&handle.worker_sem, 1);
	eina_thread_join(handle.worker_thread);

	// NSM deinit
	synthpod_nsm_free(handle.nsm);

	// deinit semaphores
	eina_semaphore_free(&handle.worker_sem);

	if(handle.path)
		free(handle.path);

	// deinit JACK
	_jack_deinit(&handle);

	// synthpod deinit
	sp_app_free(handle.app);

	// ext_urid deinit
	ext_urid_free(handle.ext_urid);

	// varchunk deinit
#if defined(BUILD_UI)
	varchunk_free(handle.app_to_ui);
	varchunk_free(handle.app_from_ui);
#endif
	varchunk_free(handle.app_to_log);
	varchunk_free(handle.app_to_worker);
	varchunk_free(handle.app_from_worker);

	return 0;
}

#if defined(BUILD_UI)
ELM_MAIN()
#endif
