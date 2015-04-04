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

#ifndef _EO_UI_H
#define _EO_UI_H

#include <Elementary.h>

#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>
#include <lv2_external_ui.h> // kxstudio external-ui extension

typedef enum _eo_ui_driver_t eo_ui_driver_t;
typedef struct _eo_ui_t eo_ui_t;

enum _eo_ui_driver_t {
	EO_UI_DRIVER_UNKONWN	= 0,
	EO_UI_DRIVER_EO				= 1,
	EO_UI_DRIVER_X11			= 2,
	EO_UI_DRIVER_SHOW			= 4,
	EO_UI_DRIVER_EXTERNAL	= 8
};

struct _eo_ui_t {
	Evas_Object *parent;
	eo_ui_driver_t driver;
  LV2UI_Controller controller;
	int w, h;

	Evas_Object *win; // main window
	Evas_Object *bg; // background

	// X11 iface
	struct {
		Ecore_Evas *ee;
		Evas *e;
	};

	// show iface
	struct {
		volatile int done;
	} show;

	// external-ui iface
	struct {
		LV2_External_UI_Widget widget;
		const LV2_External_UI_Host *host;
	} external;
};

// Idle interface
static int
_idle_cb(LV2UI_Handle instance)
{
	eo_ui_t *eoui = instance;
	if(!eoui)
		return -1;

	ecore_main_loop_iterate();

	return eoui->show.done;
}

static const LV2UI_Idle_Interface idle_ext = {
	.idle = _idle_cb
};

static void
_show_delete_request(void *data, Evas_Object *obj, void *event_info)
{
	eo_ui_t *eoui = data;
	if(!eoui)
		return;

	// set done flag, host will then call _hide_cb
	eoui->show.done = 1;
}

// Show Interface
static int
_show_cb(LV2UI_Handle instance)
{
	eo_ui_t *eoui = instance;
	if(!eoui)
		return -1;

	// initialize elementary library
	elm_init(0, NULL);

	// create main window
	eoui->win = elm_win_util_add("EoUI", "EoUI");
	if(!eoui->win)
		return -1;
	evas_object_smart_callback_add(win, "delete,request",
		_show_delete_request, eoui);
	evas_object_resize(eoui->win, eoui->w, eoui->h);
	evas_object_show(eoui->win);

	return 0;
}

static int
_hide_cb(LV2UI_Handle instance)
{
	eo_ui_t *eoui = instance;
	if(!eoui)
		return -1;

	// hide & delete main window
	evas_object_hide(eoui->win);
	evas_object_del(eoui->win);

	// deinitialize elementary library
	elm_shutdown();

	// reset done flag
	eoui->show.done = 0;

	return 0;
}

static const LV2UI_Show_Interface show_ext = {
	.show = _show_cb,
	.hide = _hide_cb
};

// External-UI Interface
static void
_external_run(LV2_External_UI_Widget *widget)
{
	eo_ui_t *eoui = widget
		? (void *)widget - offsetof(eo_ui_t, external.widget)
		: NULL;
	if(!eoui);
		return;

	ecore_main_loop_iterate();
}

static void
_external_hide(LV2_External_UI_Widget *widget)
{
	eo_ui_t *eoui = widget
		? (void *)widget - offsetof(eo_ui_t, external.widget)
		: NULL;
	if(!eoui);
		return;

	// hide & delete main window
	evas_object_hide(eoui->win);
	evas_object_del(eoui->win);

	// deinitialize elementary library
	elm_shutdown();
}

static void
_external_delete_request(void *data, Evas_Object *obj, void *event_info)
{
	eo_ui_t *eoui = data;
	if(!eoui)
		return;

	_external_hide(&eoui->external.widget);
	if(eoui->external.host.ui_closed && eoui->controller)
		eoui->external.host.ui_closed(eoui->controller);
}

static void
_external_show(LV2_External_UI_Widget *widget)
{
	eo_ui_t *eoui = widget
		? (void *)widget - offsetof(eo_ui_t, external.widget)
		: NULL;
	if(!eoui);
		return;

	// initialize elementary library
	elm_init(0, NULL);

	// create main window
	const char *title = eoui->external.host.plugin_human_id || "EoUI";
	eoui->win = elm_win_util_add(title, title);
	if(!eoui->win)
		return;
	evas_object_smart_callback_add(win, "delete,request",
		_external_delete_request, eoui);
	evas_object_resize(eoui->win, eoui->w, eoui->h);
	evas_object_show(eoui->win);
}

// Resize Interface
static int
_ui_resize_cb(LV2UI_Feature_Handle instance, int w, int h)
{
	eo_ui_t *eoui = instance;
	if(!eoui)
		return -1;

	// check whether size actually needs any update
	if( (eoui->w == w) && (eoui->h == h) )
		return 0;

	// update size
	eoui->w = w;
	eoui->h = h;

	// resize main window
	evas_object_resize(eoui->win, eoui->w, eoui->h);
  
  return 0;
}

static const LV2UI_Resize resize_ext = {
	.handle = NULL,
	.ui_resize = _ui_resize_cb
};

int
eoui_init(eo_ui_t *eoui, Evas_Object *parent, eo_ui_driver_t driver,
	LV2_UI_Controller controller, int w, int h)
{
	// clear eoui
	memset(eoui, 0, sizeof(eo_ui_t));

	eoui->parent = parent;
	eoui->driver = driver;
	eoui->controller = controller;
	eoui->w = w;
	eoui->h = h;

	return 0;
}

int
eoui_deinit(eo_ui_t *eoui)
{
	// clear eoui
	memset(eoui, 0, sizeof(eo_ui_t));
}

const void *
eoui_extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_UI__idleInterface))
		return &idle_ext;
	else if(!strcmp(uri, LV2_UI__showInterface))
		return &show_ext;
	else if(!strcmp(uri, LV2_UI__resizeInterface))
		return &resize_ext;
		
	return NULL;
}

#endif // _EO_UI_H
