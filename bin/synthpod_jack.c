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
#include <assert.h>
#include <math.h>

#if defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__)
#	include <pthread_np.h>
typedef cpuset_t cpu_set_t;
#endif

#include <synthpod_bin.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/transport.h>
#include <jack/session.h>
#if defined(JACK_HAS_METADATA_API)
#	include <jack/metadata.h>
#	include <jack/uuid.h>
# include <jackey.h>
#endif

#include <osc.lv2/forge.h>
#include <osc.lv2/writer.h>

#define OSC_SIZE 0x800

typedef struct _prog_t prog_t;

struct _prog_t {
	bin_t bin;

	LV2_Atom_Forge forge;

	uint8_t osc_buf [OSC_SIZE]; //TODO how big?

	LV2_OSC_URID osc_urid;

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

	atomic_int kill;
	save_state_t save_state;

	uv_async_t async;

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
	LV2_OSC_Schedule osc_sched;
	struct timespec cur_ntp;
	struct {
		jack_nframes_t cur_frames;
		jack_nframes_t ref_frames;
		jack_time_t cur_usecs;
		jack_time_t nxt_usecs;
		double dT;
		double dTm1;
	} cycle;
#endif
};

static LV2_Atom_Forge_Ref
_trans_event(prog_t *prog,  LV2_Atom_Forge *forge, int rolling, jack_position_t *pos)
{
	LV2_Atom_Forge_Frame frame;

	LV2_Atom_Forge_Ref ref = lv2_atom_forge_frame_time(forge, 0);
	if(ref)
		ref = lv2_atom_forge_object(forge, &frame, 0, prog->time_position);
	{
		if(ref)
			ref = lv2_atom_forge_key(forge, prog->time_frame);
		if(ref)
			ref = lv2_atom_forge_long(forge, pos->frame);

		if(ref)
			ref = lv2_atom_forge_key(forge, prog->time_speed);
		if(ref)
			ref = lv2_atom_forge_float(forge, rolling ? 1.0 : 0.0);

		if(pos->valid & JackPositionBBT)
		{
			float bar_beat = pos->beat - 1 + (pos->tick / pos->ticks_per_beat);
			float bar = pos->bar - 1;

			if(ref)
				ref = lv2_atom_forge_key(forge, prog->time_barBeat);
			if(ref)
				ref = lv2_atom_forge_float(forge, bar_beat);

			if(ref)
				ref = lv2_atom_forge_key(forge, prog->time_bar);
			if(ref)
				ref = lv2_atom_forge_long(forge, bar);

			if(ref)
				ref = lv2_atom_forge_key(forge, prog->time_beatUnit);
			if(ref)
				ref = lv2_atom_forge_int(forge, pos->beat_type);

			if(ref)
				ref = lv2_atom_forge_key(forge, prog->time_beatsPerBar);
			if(ref)
				ref = lv2_atom_forge_float(forge, pos->beats_per_bar);

			if(ref)
				ref = lv2_atom_forge_key(forge, prog->time_beatsPerMinute);
			if(ref)
				ref = lv2_atom_forge_float(forge, pos->beats_per_minute);
		}
	}
	if(ref)
		lv2_atom_forge_pop(forge, &frame);

	return ref;
}

// rt
__realtime static int
_process(jack_nframes_t nsamples, void *data)
{
	prog_t *handle = data;
	bin_t *bin = &handle->bin;
	sp_app_t *app = bin->app;

	if(bin->first)
	{
		pthread_t self = pthread_self();

		if(handle->bin.cpu_affinity)
		{
			cpu_set_t cpuset;
			CPU_ZERO(&cpuset);
			CPU_SET(0, &cpuset);
			if(pthread_setaffinity_np(self, sizeof(cpu_set_t), &cpuset))
				fprintf(stderr, "pthread_setaffinity_np error\n");
		}

		bin->first = false;
	}

#if defined(JACK_HAS_CYCLE_TIMES)
	clock_gettime(CLOCK_REALTIME, &handle->cur_ntp);
	handle->cur_ntp.tv_sec += JAN_1970; // convert NTP to OSC time
	//jack_nframes_t offset = jack_frames_since_cycle_start(handle->client);

	float T;
	jack_get_cycle_times(handle->client, &handle->cycle.cur_frames,
		&handle->cycle.cur_usecs, &handle->cycle.nxt_usecs, &T);
	(void)T;
	
	handle->cycle.ref_frames = handle->cycle.cur_frames;

	// calculate apparent period
	double diff = 1e-6 * (handle->cycle.nxt_usecs - handle->cycle.cur_usecs);

	// calculate apparent samples per period
	handle->cycle.dT = nsamples / diff;
	handle->cycle.dTm1 = 1.0 / handle->cycle.dT;
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

	if(sp_app_bypassed(app)) // aka loading state
	{
		const sp_app_system_sink_t *sinks = sp_app_get_system_sinks(app);

		//fprintf(stderr, "app is bypassed\n");

		// clear output buffers
		for(const sp_app_system_sink_t *sink=sinks;
			sink->type != SYSTEM_PORT_NONE;
			sink++)
		{
			switch(sink->type)
			{
				case SYSTEM_PORT_NONE:
				case SYSTEM_PORT_CONTROL:
				case SYSTEM_PORT_COM:
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

		bin_process_pre(bin, nsamples, true);
		bin_process_post(bin);

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
				LV2_Atom_Forge_Ref ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

				if(ref && trans_changed)
					ref = _trans_event(handle, forge, rolling, &pos);

				int n = jack_midi_get_event_count(in_buf);
				for(int i=0; i<n; i++)
				{
					jack_midi_event_t mev;
					jack_midi_event_get(&mev, in_buf, i);

					//add jack midi event to in_buf
					if(ref)
						ref = lv2_atom_forge_frame_time(forge, mev.time);
					if(ref)
						ref = lv2_atom_forge_atom(forge, mev.size, handle->midi_MidiEvent);
					// fix up noteOn(vel=0) -> noteOff(vel=0)
					if(  (mev.size == 3) && ( (mev.buffer[0] & 0xf0) == 0x90)
						&& (mev.buffer[2] == 0x00) )
					{
						const uint8_t note_off [3] = {
							0x80 | (mev.buffer[0] & 0xf),
							mev.buffer[1],
							0x0
						};
						if(ref)
							ref = lv2_atom_forge_raw(forge, note_off, 3);
					}
					else
					{
						if(ref)
							ref = lv2_atom_forge_raw(forge, mev.buffer, mev.size);
					}
					if(ref)
						lv2_atom_forge_pad(forge, mev.size);
				}
				if(ref)
					lv2_atom_forge_pop(forge, &frame);
				else
					lv2_atom_sequence_clear(seq_in);

				break;
			}

			case SYSTEM_PORT_OSC:
			{
				void *in_buf = jack_port_get_buffer(source->sys_port, nsamples);
				void *seq_in = source->buf;

				LV2_Atom_Forge *forge = &handle->forge;
				LV2_Atom_Forge_Frame frame;
				lv2_atom_forge_set_buffer(forge, seq_in, SEQ_SIZE);
				LV2_Atom_Forge_Ref ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

				if(ref && trans_changed)
					ref = _trans_event(handle, forge, rolling, &pos);

				int n = jack_midi_get_event_count(in_buf);	
				for(int i=0; i<n; i++)
				{
					jack_midi_event_t mev;
					jack_midi_event_get(&mev, (void *)in_buf, i);

					if(ref)
						ref = lv2_atom_forge_frame_time(forge, mev.time);
					if(ref)
						ref = lv2_osc_forge_packet(forge, &handle->osc_urid, handle->bin.map,
							mev.buffer, mev.size); // Note: an invalid packet will return 0
				}
				if(ref)
					lv2_atom_forge_pop(forge, &frame);
				else
					lv2_atom_sequence_clear(seq_in);

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

	// update transport state
	handle->trans.rolling = rolling;
	handle->trans.frame = rolling
		? handle->trans.frame + nsamples
		: pos.frame;
	handle->trans.beats_per_bar = pos.beats_per_bar;
	handle->trans.beat_type = pos.beat_type;
	handle->trans.ticks_per_beat = pos.ticks_per_beat;
	handle->trans.beats_per_minute = pos.beats_per_minute;

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

						if(atom->type != handle->midi_MidiEvent)
							continue; // ignore non-MIDI events

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

						LV2_OSC_Writer writer;
						lv2_osc_writer_initialize(&writer, handle->osc_buf, OSC_SIZE);
						lv2_osc_writer_packet(&writer, &handle->osc_urid, handle->bin.unmap,
							obj->atom.size, &obj->body);
						size_t size;
						uint8_t *osc_buf = lv2_osc_writer_finalize(&writer, &size);

						if(size)
						{
							jack_midi_event_write(out_buf, ev->time.frames,
								osc_buf, size);
						}
					}
				}

				break;
			}

			case SYSTEM_PORT_COM:
			{
				const LV2_Atom_Sequence *seq_out = sink->buf;

				LV2_ATOM_SEQUENCE_FOREACH(seq_out, ev)
				{
					const LV2_Atom *atom = (const LV2_Atom *)&ev->body;

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

	return 0;
}

__non_realtime static void
_session_async(uv_async_t* async)
{
	prog_t *handle = async->data;
	bin_t *bin = &handle->bin;

	jack_session_event_t *ev = handle->session_event;
	
	/*
	printf("_session_async: %s %s %s\n",
		ev->session_dir, ev->client_uuid, ev->command_line);
	*/

	uv_fs_t req;
	uv_fs_realpath(&bin->loop, &req, ev->session_dir, NULL);
	const char *realpath = req.ptr && *(char *)req.ptr
		? req.ptr: ev->session_dir;

	asprintf(&ev->command_line, "synthpod_jack -u %s ${SESSION_DIR}",
		ev->client_uuid);

	switch(ev->type)
	{
		case JackSessionSaveAndQuit:
			atomic_store_explicit(&handle->kill, 1, memory_order_relaxed); // quit after saving
			// fall-through
		case JackSessionSave:
			handle->save_state = SAVE_STATE_JACK;
			bin_bundle_save(bin, realpath);
			break;
		case JackSessionSaveTemplate:
			handle->save_state = SAVE_STATE_JACK;
			bin_bundle_new(bin);
			bin_bundle_save(bin, realpath);
			break;
	}

	uv_fs_req_cleanup(&req);
}

__non_realtime static void
_session(jack_session_event_t *ev, void *data)
{
	prog_t *handle = data;

	handle->session_event = ev;
	uv_async_send(&handle->async);
}

// rt, but can do non-rt stuff, as process won't be called
__non_realtime static int
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

__non_realtime static int
_sample_rate(jack_nframes_t sample_rate, void *data)
{
	prog_t *handle = data;
	bin_t *bin = &handle->bin;

	if(bin->app && (sample_rate != bin->app_driver.sample_rate) )
		fprintf(stderr, "synthpod does not support dynamic sample rate changes\n");

	return 0;
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

	if(atomic_load_explicit(&handle->kill, memory_order_relaxed))
	{
		bin_quit(bin);
	}
}

__non_realtime static void *
_system_port_add(void *data, system_port_t type, const char *short_name,
	const char *pretty_name, const char *designation, bool input, uint32_t order)
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
				if(!jack_uuid_empty(uuid))
				{
					jack_set_property(handle->client, uuid,
						JACKEY_SIGNAL_TYPE, "CV", "text/plain");
				}
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
				if(!jack_uuid_empty(uuid))
				{
					jack_set_property(handle->client, uuid,
						JACKEY_EVENT_TYPES, "OSC", "text/plain");
				}
			}
#endif
			break;
		}

		case SYSTEM_PORT_COM:
		{
			// unsupported, skip
			break;
		}
	}

#if defined(JACK_HAS_METADATA_API)
	if(jack_port)
	{
		jack_uuid_t uuid = jack_port_uuid(jack_port);
		if(!jack_uuid_empty(uuid))
		{
			if(pretty_name)
			{
				jack_set_property(handle->client, uuid,
					JACK_METADATA_PRETTY_NAME, pretty_name, "text/plain");
			}

			if(designation)
			{
				jack_set_property(handle->client, uuid,
					JACKEY_DESIGNATION, designation, "text/plain");
			}

			char order_str [32];
			sprintf(order_str, "%"PRIu32, order);
			jack_set_property(handle->client, uuid,
				JACKEY_ORDER, order_str, "http://www.w3.org/2001/XMLSchema#integer");
		}
	}
#endif

	return jack_port;
}

__non_realtime static void
_system_port_del(void *data, void *sys_port)
{
	bin_t *bin = data;
	prog_t *handle = (void *)bin - offsetof(prog_t, bin);

	jack_port_t *jack_port = sys_port;

	if(!jack_port || !handle->client)
		return;

#if defined(JACK_HAS_METADATA_API)
	jack_uuid_t uuid = jack_port_uuid(jack_port);
	if(!jack_uuid_empty(uuid))
		jack_remove_properties(handle->client, uuid);
#endif
			
	jack_port_unregister(handle->client, jack_port);
}

__non_realtime static void
_shutdown(void *data)
{
	prog_t *handle = data;

	//TODO do this asynchronously?
	handle->client = NULL; // client has died, didn't it?
	bin_quit(&handle->bin);
}

__non_realtime static int
_xrun(void *data)
{
	prog_t *handle = data;

	//TODO do this asynchronously?
	fprintf(stderr, "JACK XRun\n");

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
	if(uuid_str)
		jack_uuid_parse(uuid_str, &uuid);
	else
		jack_uuid_clear(&uuid);

	if(!jack_uuid_empty(uuid))
	{
		jack_set_property(handle->client, uuid,
			JACK_METADATA_PRETTY_NAME, "Synthpod", "text/plain");
	}
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
		if(uuid_str)
			jack_uuid_parse(uuid_str, &uuid);
		else
			jack_uuid_clear(&uuid);

		if(!jack_uuid_empty(uuid))
			jack_remove_properties(handle->client, uuid);
#endif

		jack_deactivate(handle->client);
		jack_client_close(handle->client);
		handle->client = NULL;
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

	// jack init
	if(_jack_init(handle, id))
	{
		return -1;
	}

	// synthpod init
	bin->app_driver.sample_rate = jack_get_sample_rate(handle->client);
	bin->app_driver.update_rate = handle->bin.update_rate;
	bin->app_driver.max_block_size = jack_get_buffer_size(handle->client);
	bin->app_driver.min_block_size = 1;
	bin->app_driver.seq_size = MAX(handle->seq_size,
		jack_port_type_get_buffer_size(handle->client, JACK_DEFAULT_MIDI_TYPE));
	
	// app init
	bin->app = sp_app_new(NULL, &bin->app_driver, bin);

	// jack activate
	atomic_init(&handle->kill, 0);
	jack_activate(handle->client); //TODO check

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

#if defined(JACK_HAS_CYCLE_TIMES)
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
#endif // JACK_HAS_CYCLE_TIMES

int
main(int argc, char **argv)
{
	mlockall(MCL_CURRENT | MCL_FUTURE);

	static prog_t handle;
	bin_t *bin = &handle.bin;

	handle.server_name = NULL;
	handle.session_id = NULL;
	handle.seq_size = SEQ_SIZE;

	bin->audio_prio = 70; // not used
	bin->worker_prio = 60;
	bin->num_slaves = sysconf(_SC_NPROCESSORS_ONLN) - 1;
	bin->bad_plugins = false;
	bin->has_gui = false;
	bin->socket_path = "shm:///synthpod";
	bin->update_rate = 25;
	bin->cpu_affinity = false;

	fprintf(stderr,
		"Synthpod "SYNTHPOD_VERSION"\n"
		"Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)\n"
		"Released under Artistic License 2.0 by Open Music Kontrollers\n");
	
	int c;
	while((c = getopt(argc, argv, "vhgGbBaAw:Wl:n:u:s:c:f:")) != -1)
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
					"   [-b]                 enable bad plugins\n"
					"   [-B]                 disable bad plugins (default)\n"
					"   [-a]                 enable CPU affinity\n"
					"   [-A]                 disable CPU affinity (default)\n"
					"   [-w] worker-priority worker thread realtime priority (60)\n"
					"   [-W]                 do NOT use worker thread realtime priority\n"
					"   [-l] link-path       socket link path (shm:///synthpod)\n"
					"   [-n] server-name     connect to named JACK daemon\n"
					"   [-u] client-uuid     client UUID for JACK session management\n"
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
			case 'w':
				bin->worker_prio = atoi(optarg);
				break;
			case 'W':
				bin->worker_prio = 0;
				break;
			case 'l':
				bin->socket_path = optarg;
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
			case 'c':
				if(atoi(optarg) < bin->num_slaves)
					bin->num_slaves = atoi(optarg);
				break;
			case 'f':
				bin->update_rate = atoi(optarg);
				break;
			case '?':
				if(  (optopt == 'n') || (optopt == 'u') || (optopt == 's') || (optopt == 'c')
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

	bin_init(bin);

	handle.async.data = &handle;
	uv_async_init(&bin->loop, &handle.async, _session_async); //FIXME check

	LV2_URID_Map *map = bin->map;

	lv2_atom_forge_init(&handle.forge, map);
	lv2_osc_urid_init(&handle.osc_urid, map);
	
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
	bin->app_driver.features = SP_APP_FEATURE_POWER_OF_2_BLOCK_LENGTH; // always true for JACK


	// run
	bin_run(bin, argv, &nsm_driver);

	// stop
	bin_stop(bin);

	// deinit JACK
	_jack_deinit(&handle);

	uv_close((uv_handle_t *)&handle.async, NULL);

	// deinit
	bin_deinit(bin);

	munlockall();

	return 0;
}
