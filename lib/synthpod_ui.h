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

#ifndef _SYNTHPOD_UI_H
#define _SYNTHPOD_UI_H

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/options/options.h>
#include <lv2/lv2plug.in/ns/ext/data-access/data-access.h>
#include <lv2/lv2plug.in/ns/ext/instance-access/instance-access.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#include <lilv/lilv.h>

#include <synthpod_common.h>

// magic to resolve naming issues with EFL on WIN32
#if defined(_WIN32)
// needed for eldbus and mingw32-w64
#	pragma push_macro("interface")
#	undef interface

// needed for evil and mingw32-w64
#	pragma push_macro("__MINGW32__")
# undef __MINGW32__
#endif

#include <Elementary.h>

#if defined(_WIN32)
#	pragma pop_macro("interface")
#	pragma pop_macro("__MINGW32__")
#endif

typedef enum _sp_ui_features_t sp_ui_features_t;
typedef struct _sp_ui_t sp_ui_t;
typedef struct _sp_ui_driver_t sp_ui_driver_t;

typedef void *(*sp_to_request_t)(size_t size, void *data);
typedef void (*sp_to_advance_t)(size_t size, void *data);

typedef void (*sp_opened_t)(void *data, int status);
typedef void (*sp_saved_t)(void *data, int status);
typedef void (*sp_close_t)(void *data);

enum _sp_ui_features_t {
	SP_UI_FEATURE_NEW					= (1 << 0),
	SP_UI_FEATURE_OPEN				= (1 << 1),
	SP_UI_FEATURE_IMPORT_FROM	= (1 << 2),
	SP_UI_FEATURE_SAVE				= (1 << 3),
	SP_UI_FEATURE_SAVE_AS			= (1 << 4),
	SP_UI_FEATURE_EXPORT_TO		= (1 << 5),
	SP_UI_FEATURE_CLOSE				= (1 << 6)
};

struct _sp_ui_driver_t {
	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	int instance_access;

	// from ui
	sp_to_request_t to_app_request;
	sp_to_advance_t to_app_advance;
	
	sp_opened_t opened;
	sp_saved_t saved;
	sp_close_t close;

	sp_ui_features_t features;
};

SYNTHPOD_SYMBOL_EXTERN sp_ui_t *
sp_ui_new(Evas_Object *win, const LilvWorld *world, sp_ui_driver_t *driver,
	void *data, int show_splash);

SYNTHPOD_SYMBOL_EXTERN Evas_Object *
sp_ui_widget_get(sp_ui_t *ui);

SYNTHPOD_SYMBOL_EXTERN void
sp_ui_refresh(sp_ui_t *ui);

SYNTHPOD_SYMBOL_EXTERN void
sp_ui_from_app(sp_ui_t *ui, const LV2_Atom *atom);

SYNTHPOD_SYMBOL_EXTERN void
sp_ui_bundle_new(sp_ui_t *ui);

SYNTHPOD_SYMBOL_EXTERN void
sp_ui_bundle_load(sp_ui_t *ui, const char *bundle_path, int update_path);

SYNTHPOD_SYMBOL_EXTERN void
sp_ui_bundle_save(sp_ui_t *ui, const char *bundle_path, int update_path);

SYNTHPOD_SYMBOL_EXTERN void
sp_ui_resize(sp_ui_t *ui, int w, int h);

SYNTHPOD_SYMBOL_EXTERN void
sp_ui_iterate(sp_ui_t *ui);

SYNTHPOD_SYMBOL_EXTERN void
sp_ui_run(sp_ui_t *ui);

SYNTHPOD_SYMBOL_EXTERN void
sp_ui_free(sp_ui_t *ui);

#endif // _SYNTHPOD_UI_H
