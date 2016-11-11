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

#ifndef _SYNTHPOD_BIN_H
#define _SYNTHPOD_BIN_H

#include <synthpod_app.h>

#include <Eina.h>
#include <Ecore.h>
#include <Ecore_File.h>
#include <Efreet.h>

#include <stdatomic.h>

#include <symap.h>
#include <varchunk.h>
#include <sandbox_master.h>

#include <synthpod_nsm.h>

#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/time/time.h>

#define SEQ_SIZE 0x2000
#define JAN_1970 (uint64_t)0x83aa7e80

typedef enum _save_state_t save_state_t;
typedef struct _light_sem_t light_sem_t;
typedef struct _bin_t bin_t;

enum _save_state_t {
	SAVE_STATE_INTERNAL = 0,
	SAVE_STATE_NSM,
	SAVE_STATE_JACK
};

struct _light_sem_t {
	Eina_Semaphore sem;
	_Atomic int count;
	int spin;
};

struct _bin_t {
	Symap *symap;
	LV2_URID_Map map;
	LV2_URID_Unmap unmap;
	xpress_map_t xmap;
	atomic_flag map_lock;
	
	sp_app_t *app;
	sp_app_driver_t app_driver;
	
	varchunk_t *app_to_worker;
	varchunk_t *app_from_worker;
	varchunk_t *app_to_log;

	varchunk_t *app_from_com;

	bool advance_ui;
	varchunk_t *app_from_app;

	char *path;
	synthpod_nsm_t *nsm;

	bool has_gui;

	varchunk_t *app_to_ui;
	varchunk_t *app_from_ui;
	
	Ecore_Animator *ui_anim;
	
	_Atomic int worker_dead;
	Eina_Thread worker_thread;
	light_sem_t worker_sem;

	LV2_URID log_error;
	LV2_URID log_note;
	LV2_URID log_trace;
	LV2_URID log_warning;
	LV2_URID atom_eventTransfer;

	LV2_Log_Log log;

	Eina_Thread self;
	atomic_flag trace_lock;

	int audio_prio;
	int worker_prio;
	int num_slaves;
	bool bad_plugins;
	const char *socket_path;

	sandbox_master_driver_t sb_driver;
	sandbox_master_t *sb;
	Ecore_Fd_Handler *hndl;
	Ecore_Event_Handler *del;
	Ecore_Exe *exe;
};

void
bin_init(bin_t *bin);

void
bin_run(bin_t *bin, char **argv, const synthpod_nsm_driver_t *nsm_driver);

void
bin_stop(bin_t *bin);

void
bin_deinit(bin_t *bin);

void
bin_process_pre(bin_t *bin, uint32_t nsamples, bool bypassed);

void
bin_process_post(bin_t *bin);

void
bin_bundle_new(bin_t *bin);

void
bin_bundle_load(bin_t *bin, const char *bundle_path);

void
bin_bundle_save(bin_t *bin, const char *bundle_path);

#endif // _SYNTHPOD_BIN_H
