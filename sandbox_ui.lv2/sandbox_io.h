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

#ifndef _SANDBOX_IO_H
#define _SANDBOX_IO_H

#include <inttypes.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define NETATOM_IMPLEMENTATION
#include <netatom.lv2/netatom.h>

#include <varchunk.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/parameters/parameters.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RDF_PREFIX "http://www.w3.org/1999/02/22-rdf-syntax-ns#"

typedef struct _sandbox_io_subscription_t sandbox_io_subscription_t;
typedef struct _sandbox_io_shm_body_t sandbox_io_shm_body_t;
typedef struct _sandbox_io_shm_t sandbox_io_shm_t;
typedef struct _sandbox_io_t sandbox_io_t;

typedef void (*_sandbox_io_recv_cb_t)(void *data, uint32_t index, uint32_t size,
	uint32_t format, const void *buf);
typedef void (*_sandbox_io_subscribe_cb_t)(void *data, uint32_t index,
	uint32_t protocol, bool state);

struct _sandbox_io_subscription_t {
	uint32_t protocol;
	int32_t state;
};

#define SANDBOX_BUFFER_SIZE 0x100000 // 1M

struct _sandbox_io_shm_body_t {
	sem_t sem;
	varchunk_t varchunk;
	uint8_t buf [SANDBOX_BUFFER_SIZE];
};

struct _sandbox_io_shm_t {
	sandbox_io_shm_body_t to_master;
	sandbox_io_shm_body_t from_master;
};

struct _sandbox_io_t {
	bool is_master;
	bool drop;

	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;

	netatom_t *netatom;
	LV2_Atom_Forge_Frame frame;
	LV2_Atom_Forge forge;

	LV2_URID float_protocol;
	LV2_URID peak_protocol;
	LV2_URID event_transfer;
	LV2_URID atom_transfer;
	LV2_URID core_index;
	LV2_URID rdf_value;
	LV2_URID ui_protocol;
	LV2_URID ui_period_start;
	LV2_URID ui_period_size;
	LV2_URID ui_peak;
	LV2_URID ui_close_request;
	LV2_URID ui_window_title;
	LV2_URID ui_port_subscribe;
	LV2_URID ui_update_rate;
	LV2_URID params_sample_rate;

	char *name;
	sandbox_io_shm_t *shm;
};

static inline int
_sandbox_io_recv(sandbox_io_t *io, _sandbox_io_recv_cb_t recv_cb,
	_sandbox_io_subscribe_cb_t subscribe_cb, void *data)
{
	const uint8_t *buf = NULL;
	size_t sz;
	bool close_request = false;

	sandbox_io_shm_body_t *rx = io->is_master
		? &io->shm->to_master
		: &io->shm->from_master;

	while((buf = varchunk_read_request(&rx->varchunk, &sz)))
	{
		const LV2_Atom *atom = netatom_deserialize(io->netatom, (uint8_t *)buf, sz);
		if(atom)
		{
			if(!lv2_atom_forge_is_object_type(&io->forge, atom->type))
				continue;

			const LV2_Atom_Object *obj = (const LV2_Atom_Object *)atom;

			if(obj->body.otype == io->float_protocol)
			{
				const LV2_Atom_Int *index = NULL;
				const LV2_Atom_Float *value = NULL;

				LV2_Atom_Object_Query q [] = {
					{io->core_index, (const LV2_Atom **)&index},
					{io->rdf_value, (const LV2_Atom **)&value},
					{0, NULL}
				};
				lv2_atom_object_query(obj, q);

				if(  index && (index->atom.type == io->forge.Int)
						&& (index->atom.size == sizeof(int32_t))
					&& value && (value->atom.type == io->forge.Float)
						&& (value->atom.size == sizeof(float)) )
				{
					recv_cb(data, index->body,
						sizeof(float), io->float_protocol, &value->body);
					recv_cb(data, index->body,
						sizeof(float), 0, &value->body);
				}
			}
			else if(obj->body.otype == io->peak_protocol)
			{
				const LV2_Atom_Int *index = NULL;
				const LV2_Atom_Int *period_start = NULL;
				const LV2_Atom_Int *period_size = NULL;
				const LV2_Atom_Float *peak= NULL;

				LV2_Atom_Object_Query q [] = {
					{io->core_index, (const LV2_Atom **)&index},
					{io->ui_period_start, (const LV2_Atom **)&period_start},
					{io->ui_period_size, (const LV2_Atom **)&period_size},
					{io->ui_peak, (const LV2_Atom **)&peak},
					{0, NULL}
				};
				lv2_atom_object_query(obj, q);

				if(  index && (index->atom.type == io->forge.Int)
						&& (index->atom.size == sizeof(int32_t))
					&& period_start && (period_start->atom.type == io->forge.Int)
						&& (period_start->atom.size == sizeof(int32_t))
					&& period_size && (period_size->atom.type == io->forge.Int)
						&& (period_size->atom.size == sizeof(int32_t))
					&& peak && (peak->atom.type == io->forge.Float)
						&& (peak->atom.size == sizeof(float)) )
					{
						const LV2UI_Peak_Data peak_data = {
							.period_start = period_start->body,
							.period_size = period_size->body,
							.peak = peak->body
						};
						recv_cb(data, index->body,
							sizeof(LV2UI_Peak_Data), io->peak_protocol, &peak_data);
					}
			}
			else if( (obj->body.otype == io->event_transfer)
				|| (obj->body.otype == io->atom_transfer) )
			{
				const LV2_Atom_Int *index = NULL;
				const LV2_Atom *value = NULL;

				LV2_Atom_Object_Query q [] = {
					{io->core_index, (const LV2_Atom **)&index},
					{io->rdf_value, (const LV2_Atom **)&value},
					{0, NULL}
				};
				lv2_atom_object_query(obj, q);

				if(  index && (index->atom.type == io->forge.Int)
						&& (index->atom.size == sizeof(int32_t))
					&& value)
				{
					recv_cb(data, index->body,
						lv2_atom_total_size(value), obj->body.otype, value);
				}
			}
			else if(obj->body.otype == io->ui_port_subscribe)
			{
				const LV2_Atom_Int *index = NULL;
				const LV2_Atom_URID *protocol = NULL;
				const LV2_Atom_Bool *value = NULL;

				LV2_Atom_Object_Query q [] = {
					{io->core_index, (const LV2_Atom **)&index},
					{io->ui_protocol, (const LV2_Atom **)&protocol},
					{io->rdf_value, (const LV2_Atom **)&value},
					{0, NULL}
				};
				lv2_atom_object_query(obj, q);

				if(  index && (index->atom.type == io->forge.Int)
						&& (index->atom.size == sizeof(int32_t))
					&& value && (value->atom.type == io->forge.Bool)
					&& protocol && (protocol->atom.type == io->forge.URID))
				{
					if(subscribe_cb)
						subscribe_cb(data, index->body, protocol->body, value->body);
				}
			}
			else if(obj->body.otype == io->ui_close_request)
			{
				close_request = true;
			}
		}
		else
		{
			fprintf(stderr, "_sandbox_io_recv: netatom_deserialize failed\n");
		}

		varchunk_read_advance(&rx->varchunk);
	}

	if(close_request)
		return -1; // received ui:closeRequest

	return 0;
}

static inline int
_sandbox_io_send(sandbox_io_t *io, uint32_t index,
	uint32_t size, uint32_t protocol, const void *buf)
{
	sandbox_io_shm_body_t *tx = io->is_master
		? &io->shm->from_master
		: &io->shm->to_master;

	// reserve 8192 additional bytes for the parent atom and dictionary
	const size_t req_sz = size + 8192; //FIXME is this enough?
	size_t max_sz;

	uint8_t *buf_tx;
	if((buf_tx = varchunk_write_request_max(&tx->varchunk, req_sz, &max_sz)))
	{
		LV2_Atom_Forge_Frame frame;

		if(protocol == 0)
			protocol = io->float_protocol;

		lv2_atom_forge_set_buffer(&io->forge, buf_tx, max_sz);
		lv2_atom_forge_object(&io->forge, &frame, 0, protocol);

		lv2_atom_forge_key(&io->forge, io->core_index);
		lv2_atom_forge_int(&io->forge, index);

		if(protocol == io->float_protocol)
		{
			const float *value = buf;

			lv2_atom_forge_key(&io->forge, io->rdf_value);
			lv2_atom_forge_float(&io->forge, *value);
		}
		else if(protocol == io->peak_protocol)
		{
			const LV2UI_Peak_Data *peak_data = buf;

			lv2_atom_forge_key(&io->forge, io->ui_period_start);
			lv2_atom_forge_int(&io->forge, peak_data->period_start);

			lv2_atom_forge_key(&io->forge, io->ui_period_size);
			lv2_atom_forge_int(&io->forge, peak_data->period_size);

			lv2_atom_forge_key(&io->forge, io->ui_peak);
			lv2_atom_forge_float(&io->forge, peak_data->peak);
		}
		else if( (protocol == io->event_transfer)
			|| (protocol == io->atom_transfer) )
		{
			const LV2_Atom *atom = buf;
			LV2_Atom_Forge_Ref ref;

			lv2_atom_forge_key(&io->forge, io->rdf_value);
			ref = lv2_atom_forge_atom(&io->forge, atom->size, atom->type);
			lv2_atom_forge_write(&io->forge, LV2_ATOM_BODY_CONST(atom), atom->size);

			LV2_Atom *src= lv2_atom_forge_deref(&io->forge, ref);
		}
		else if(protocol == io->ui_port_subscribe)
		{
			const sandbox_io_subscription_t *sub = buf;

			lv2_atom_forge_key(&io->forge, io->rdf_value);
			lv2_atom_forge_bool(&io->forge, sub->state);

			lv2_atom_forge_key(&io->forge, io->ui_protocol);
			lv2_atom_forge_urid(&io->forge, protocol);
		}
		else if(protocol == io->ui_close_request)
		{
			// nothing to add
		}

		lv2_atom_forge_pop(&io->forge, &frame);

		size_t wrt_sz;
		const uint8_t *buf_rx = netatom_serialize(io->netatom, (LV2_Atom *)buf_tx, max_sz, &wrt_sz);
		if(buf_rx)
		{
			varchunk_write_advance(&tx->varchunk, wrt_sz);
			sem_post(&tx->sem);

			return 0; // success
		}
		else
		{
			fprintf(stderr, "_sandbox_io_send: netatom_serialize failed\n"); //FIXME
		}
	}
	else
	{
		fprintf(stderr, "_sandbox_io_send: buffer overflow\n"); //FIXME
	}

	return -1; // failed
}

static inline void
_sandbox_io_wait(sandbox_io_t *io)
{
	sandbox_io_shm_body_t *rx = io->is_master
		? &io->shm->to_master
		: &io->shm->from_master;

	sem_wait(&rx->sem);
}

static inline bool
_sandbox_io_timedwait(sandbox_io_t *io, const struct timespec *abs_timeout)
{
	sandbox_io_shm_body_t *rx = io->is_master
		? &io->shm->to_master
		: &io->shm->from_master;

	if(sem_timedwait(&rx->sem, abs_timeout) == -1)
		return errno == ETIMEDOUT;

	return false;
}

static inline void
_sandbox_io_signal(sandbox_io_t *io)
{
	sandbox_io_shm_body_t *rx = io->is_master
		? &io->shm->to_master
		: &io->shm->from_master;

	sem_post(&rx->sem);
}

static inline int
_sandbox_io_init(sandbox_io_t *io, LV2_URID_Map *map, LV2_URID_Unmap *unmap,
	const char *socket_path, bool is_master, bool drop_messages)
{
	io->map = map;
	io->unmap = unmap;

	io->is_master = is_master;
	io->drop = drop_messages;

	const bool is_shm = strncmp(socket_path, "shm://", 6) == 0;
	const bool is_tcp = strncmp(socket_path, "tcp://", 6) == 0;
	const bool swap = is_tcp ? true : false;

	if(!(io->netatom = netatom_new(io->map, io->unmap, swap)))
		return -1;

	const size_t total_size = sizeof(sandbox_io_shm_t);

	const char *name = is_shm
		? &socket_path[6]
		: NULL;
	if(!name)
		return -1;

	io->name = strdup(name);
	if(!io->name)
		return -1;

	const int fd = io->is_master
		? shm_open(io->name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)
		: shm_open(io->name, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd == -1)
		return -1;

	if(  (ftruncate(fd, total_size) == -1)
		|| ((io->shm = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
					MAP_SHARED, fd, 0)) == MAP_FAILED) )
	{
		if(io->is_master)
			shm_unlink(io->name);

		return -1;
	}
	close(fd);

	if(io->is_master)
	{
		if(sem_init(&io->shm->from_master.sem, 1, 0) == -1)
			return -1;
		if(sem_init(&io->shm->to_master.sem, 1, 0) == -1)
			return -1;

		varchunk_init(&io->shm->from_master.varchunk, SANDBOX_BUFFER_SIZE, true);
		varchunk_init(&io->shm->to_master.varchunk, SANDBOX_BUFFER_SIZE, true);
	}

	lv2_atom_forge_init(&io->forge, map);

	io->float_protocol = map->map(map->handle, LV2_UI_PREFIX"floatProtocol");
	io->peak_protocol = map->map(map->handle, LV2_UI_PREFIX"peakProtocol");
	io->event_transfer = map->map(map->handle, LV2_ATOM__eventTransfer);
	io->atom_transfer = map->map(map->handle, LV2_ATOM__atomTransfer);
	io->core_index = map->map(map->handle, LV2_CORE__index);
	io->rdf_value = map->map(map->handle, RDF_PREFIX"value");
	io->ui_protocol = map->map(map->handle, LV2_UI_PREFIX"protocol");
	io->ui_period_start = map->map(map->handle, LV2_UI_PREFIX"periodStart");
	io->ui_period_size = map->map(map->handle, LV2_UI_PREFIX"periodSize");
	io->ui_peak = map->map(map->handle, LV2_UI_PREFIX"peak");
	io->ui_close_request = map->map(map->handle, LV2_UI_PREFIX"closeRequest");
	io->ui_window_title = map->map(map->handle, LV2_UI__windowTitle);
	io->ui_port_subscribe = map->map(map->handle, LV2_UI__portSubscribe);
	io->ui_update_rate = map->map(map->handle, LV2_UI__updateRate);
	io->params_sample_rate = map->map(map->handle, LV2_PARAMETERS__sampleRate);

	return 0;
}

static inline void
_sandbox_io_deinit(sandbox_io_t *io, bool terminate)
{
	if(terminate)
	{
		_sandbox_io_send(io, 0, 0, io->ui_close_request, NULL);
		usleep(100000); // wait 100ms, for timer-based UIs to receive message
	}

	const size_t total_size = sizeof(sandbox_io_shm_t);
	if(io->shm)
	{
		if(io->is_master)
		{
			sem_destroy(&io->shm->from_master.sem);
			sem_destroy(&io->shm->to_master.sem);
		}

		munmap(io->shm, total_size);
		if(io->is_master)
			shm_unlink(io->name);
	}
	if(io->name)
		free(io->name);

	if(io->netatom)
		netatom_free(io->netatom);
}

#ifdef __cplusplus
}
#endif

#endif
