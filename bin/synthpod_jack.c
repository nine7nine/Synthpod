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

#include <synthpod_bin.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/transport.h>
#include <jack/session.h>
#if defined(JACK_HAS_METADATA_API)
#	include <jack/metadata.h>
#	include <jack/uuid.h>
#endif

#ifndef MAX
#	define MAX(A, B) ((A) > (B) ? (A) : (B))
#endif

#define OSC_SIZE 0x800

typedef struct _prog_t prog_t;

struct _prog_t {
	bin_t bin;

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

	LV2_URID time_position;
	LV2_URID time_barBeat;
	LV2_URID time_bar;
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

	struct {
		jack_transport_state_t rolling;
		jack_nframes_t frame;
		float beats_per_bar;
		float beat_type;
		double ticks_per_beat;
		double beats_per_minute;
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
		double dT;
		double dTm1;
	} cycle;
#endif
};

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
			float bar_beat = pos->beat - 1 + (pos->tick / pos->ticks_per_beat);
			float bar = pos->bar - 1;

			lv2_atom_forge_key(forge, prog->time_barBeat);
			lv2_atom_forge_float(forge, bar_beat);

			lv2_atom_forge_key(forge, prog->time_bar);
			lv2_atom_forge_long(forge, bar);

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
				int32_t i = 0;
				ptr = osc_get_int32(ptr, &i);
				osc_forge_int32(&handle->oforge, forge, i);
				break;
			}
			case 'f':
			{
				float f = 0.f;
				ptr = osc_get_float(ptr, &f);
				osc_forge_float(&handle->oforge, forge, f);
				break;
			}
			case 's':
			case 'S':
			{
				const char *s = "";
				ptr = osc_get_string(ptr, &s);
				osc_forge_string(&handle->oforge, forge, s);
				break;
			}
			case 'b':
			{
				osc_blob_t b = {.size = 0, .payload=NULL};
				ptr = osc_get_blob(ptr, &b);
				osc_forge_blob(&handle->oforge, forge, b.size, b.payload);
				break;
			}

			case 'h':
			{
				int64_t h = 0;
				ptr = osc_get_int64(ptr, &h);
				osc_forge_int64(&handle->oforge, forge, h);
				break;
			}
			case 'd':
			{
				double d = 0.f;
				ptr = osc_get_double(ptr, &d);
				osc_forge_double(&handle->oforge, forge, d);
				break;
			}
			case 't':
			{
				uint64_t t = 0;
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
				char c = '\0';
				ptr = osc_get_char(ptr, &c);
				osc_forge_char(&handle->oforge, forge, c);
				break;
			}
			case 'm':
			{
				const uint8_t *m = NULL;
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

	osc_data_t *itm = NULL;
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

//FIXME
static int64_t
_osc_schedule_osc2frames(osc_schedule_handle_t instance, uint64_t timestamp);
static uint64_t
_osc_schedule_frames2osc(osc_schedule_handle_t instance, int64_t frames);
//FIXME

// rt
static int
_process(jack_nframes_t nsamples, void *data)
{
	prog_t *handle = data;
	bin_t *bin = &handle->bin;
	sp_app_t *app = bin->app;

#if defined(JACK_HAS_CYCLE_TIMES)
	clock_gettime(CLOCK_REALTIME, &handle->ntp);
	jack_nframes_t offset = jack_frames_since_cycle_start(handle->client);

	jack_get_cycle_times(handle->client, &handle->cycle.cur_frames,
		&handle->cycle.cur_usecs, &handle->cycle.nxt_usecs, &handle->cycle.T);

	handle->cycle.ref_frames = handle->cycle.cur_frames + offset;
	handle->ntp.tv_sec += JAN_1970; // convert NTP to OSC time
	handle->cycle.dT = 1e6 * (double)nsamples
		/ (double)(handle->cycle.nxt_usecs - handle->cycle.cur_usecs);
	handle->cycle.dTm1 = 1e-6 * (double)(handle->cycle.nxt_usecs - handle->cycle.cur_usecs)
		/ (double)nsamples;
	/* less exact
	handle->cycle.dT = 1e6 * (double)nsamples / (double)handle->cycle.T;
	handle->cycle.dTm1 = 1e-6 * (double)handle->cycle.T / (double)nsamples;
	*/

	/*
	// debug
	int64_t frame1 = 12345678LL;
	uint64_t osc1 = _osc_schedule_frames2osc(handle, frame1);
	int64_t frame2 = _osc_schedule_osc2frames(handle, osc1);
	printf("%li %li\n", frame1, frame2);
	*/
#endif

	// get transport position
	jack_position_t pos;
	jack_transport_state_t rolling = jack_transport_query(handle->client, &pos) == JackTransportRolling;
	int trans_changed = (rolling != handle->trans.rolling)
		|| (pos.frame != handle->trans.frame)
		|| (pos.beats_per_bar != handle->trans.beats_per_bar)
		|| (pos.beat_type != handle->trans.beat_type)
		|| (pos.ticks_per_beat != handle->trans.ticks_per_beat)
		|| (pos.beats_per_minute != handle->trans.beats_per_minute);

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
	handle->trans.rolling = rolling;
	handle->trans.frame = rolling
		? handle->trans.frame + nsamples
		: pos.frame;
	handle->trans.beats_per_bar = pos.beats_per_bar;
	handle->trans.beat_type = pos.beat_type;
	handle->trans.ticks_per_beat = pos.ticks_per_beat;
	handle->trans.beats_per_minute = pos.beats_per_minute;

	bin_process_pre(bin, nsamples, paused);

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
	
	bin_process_post(bin);

	return 0;
}

// ui
static void
_session_async(void *data)
{
	prog_t *handle = data;
	bin_t *bin = &handle->bin;

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
			sp_ui_bundle_save(bin->ui, ev->session_dir, 1);
			break;
		case JackSessionSaveTemplate:
			handle->save_state = SAVE_STATE_JACK;
			sp_ui_bundle_new(bin->ui);
			sp_ui_bundle_save(bin->ui, ev->session_dir, 1);
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

// rt, but can do non-rt stuff, as process won't be called
static int
_buffer_size(jack_nframes_t block_size, void *data)
{
	prog_t *handle = data;
	bin_t *bin = &handle->bin;

	//printf("JACK: new buffer size: %p %u %u\n",
	//	handle->app, handle->app_driver.max_block_size, block_size);

	if(bin->app)
		return sp_app_nominal_block_length(bin->app, block_size);

	return 0;
}

// non-rt
static int
_sample_rate(jack_nframes_t sample_rate, void *data)
{
	prog_t *handle = data;
	bin_t *bin = &handle->bin;

	if(bin->app && (sample_rate != bin->app_driver.sample_rate) )
		fprintf(stderr, "synthpod does not support dynamic sample rate changes\n");

	return 0;
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
		elm_exit();
	}
}

static void *
_system_port_add(void *data, system_port_t type, const char *short_name,
	const char *pretty_name, int input)
{
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);
	
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
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);

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
	elm_exit();
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
	jack_options_t opts = JackNullOption | JackNoStartServer;
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
	if(jack_set_sample_rate_callback(handle->client, _sample_rate, handle))
		return -1;
	if(jack_set_buffer_size_callback(handle->client, _buffer_size, handle))
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
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);
	(void)name;

	if(bin->path)
		free(bin->path);
	bin->path = strdup(path);

	// jack init
	if(_jack_init(handle, id))
	{
		bin->ui_driver.close(bin);
		return -1;
	}

	// synthpod init
	bin->app_driver.sample_rate = jack_get_sample_rate(handle->client);
	bin->app_driver.max_block_size = jack_get_buffer_size(handle->client);
	bin->app_driver.min_block_size = 1;
	bin->app_driver.seq_size = MAX(handle->seq_size,
		jack_port_type_get_buffer_size(handle->client, JACK_DEFAULT_MIDI_TYPE));
	
	// app init
	bin->app = sp_app_new(NULL, &bin->app_driver, bin);

	// jack activate
	jack_activate(handle->client); //TODO check

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

#if defined(JACK_HAS_CYCLE_TIMES)
// rt
static int64_t
_osc_schedule_osc2frames(osc_schedule_handle_t instance, uint64_t timestamp)
{
	prog_t *handle = instance;

	if(timestamp == 1ULL)
		return 0; // inject at start of period

	uint64_t time_sec = timestamp >> 32;
	uint64_t time_frac = timestamp & 0xffffffff;

	volatile double diff = time_sec;
	diff -= handle->ntp.tv_sec;
	diff += time_frac * 0x1p-32;
	diff -= handle->ntp.tv_nsec * 1e-9;

	double frames_d = handle->cycle.ref_frames
		- handle->cycle.cur_frames
		+ diff * handle->cycle.dT;

	int64_t frames = round(frames_d);

	return frames;
}

// rt
static uint64_t
_osc_schedule_frames2osc(osc_schedule_handle_t instance, int64_t frames)
{
	prog_t *handle = instance;

	volatile double diff = (double)(frames - handle->cycle.ref_frames + handle->cycle.cur_frames)
		* handle->cycle.dTm1;
	diff += handle->ntp.tv_nsec * 1e-9;
	diff += handle->ntp.tv_sec;

	double time_sec_d;
	double time_frac_d = modf(diff, &time_sec_d);

	uint64_t time_sec = time_sec_d;
	uint64_t time_frac = time_frac_d * 0x1p32;
	if(time_frac >= 0x100000000ULL) // illegal overflow
		time_frac = 0xffffffffULL;

	uint64_t timestamp = (time_sec << 32) | time_frac;

	return timestamp;
}
#endif // JACK_HAS_CYCLE_TIMES

EAPI_MAIN int
elm_main(int argc, char **argv);

EAPI_MAIN int
elm_main(int argc, char **argv)
{
	static prog_t handle;
	bin_t *bin = &handle.bin;

	handle.server_name = NULL;
	handle.session_id = NULL;
	handle.seq_size = SEQ_SIZE;

	bin->has_gui = true;

	fprintf(stderr,
		"Synthpod "SYNTHPOD_VERSION"\n"
		"Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)\n"
		"Released under Artistic License 2.0 by Open Music Kontrollers\n");
	
	int c;
	while((c = getopt(argc, argv, "vhGn:u:s:")) != -1)
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
					"   [-n] server-name     connect to named JACK daemon\n"
					"   [-u] client-uuid     client UUID for JACK session management\n"
					"   [-s] sequence-size   minimum sequence size (8192)\n\n"
					, argv[0]);
				return 0;
			case 'G':
				bin->has_gui = false;
				break;
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

	bin_init(bin);

	LV2_URID_Map *map = ext_urid_map_get(bin->ext_urid);

	lv2_atom_forge_init(&handle.forge, map);
	osc_forge_init(&handle.oforge, map);
	
	handle.midi_MidiEvent = map->map(map->handle, LV2_MIDI__MidiEvent);

	handle.time_position = map->map(map->handle, LV2_TIME__Position);
	handle.time_barBeat = map->map(map->handle, LV2_TIME__barBeat);
	handle.time_bar = map->map(map->handle, LV2_TIME__bar);
	handle.time_beatUnit = map->map(map->handle, LV2_TIME__beatUnit);
	handle.time_beatsPerBar = map->map(map->handle, LV2_TIME__beatsPerBar);
	handle.time_beatsPerMinute = map->map(map->handle, LV2_TIME__beatsPerMinute);
	handle.time_frame = map->map(map->handle, LV2_TIME__frame);
	handle.time_framesPerSecond = map->map(map->handle, LV2_TIME__framesPerSecond);
	handle.time_speed = map->map(map->handle, LV2_TIME__speed);

	bin->app_driver.system_port_add = _system_port_add;
	bin->app_driver.system_port_del = _system_port_del;

#if defined(JACK_HAS_CYCLE_TIMES)
	handle.osc_sched.osc2frames = _osc_schedule_osc2frames;
	handle.osc_sched.frames2osc = _osc_schedule_frames2osc;
	handle.osc_sched.handle = &handle;
	bin->app_driver.osc_sched = &handle.osc_sched;
#else
	bin->app_driver.osc_sched = NULL;
#endif

	bin->ui_driver.saved = _ui_saved;

	bin->ui_driver.features = SP_UI_FEATURE_NEW | SP_UI_FEATURE_SAVE | SP_UI_FEATURE_CLOSE;
	if(synthpod_nsm_managed() || handle.session_id)
		bin->ui_driver.features |= SP_UI_FEATURE_IMPORT_FROM | SP_UI_FEATURE_EXPORT_TO;
	else
		bin->ui_driver.features |= SP_UI_FEATURE_OPEN | SP_UI_FEATURE_SAVE_AS;

	// run
	bin_run(bin, argv, &nsm_driver);

	// stop
	bin_stop(bin);

	// deinit JACK
	_jack_deinit(&handle);

	// deinit
	bin_deinit(bin);

	return 0;
}

ELM_MAIN()
