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

#include <Elementary.h>

#include <app.h>

//#define TEST_URI "http://open-music-kontrollers.ch/lv2/nuklear#cloak"
//#define TEST_URI "http://open-music-kontrollers.ch/lv2/chimaera#injector"
#define TEST_URI "http://open-music-kontrollers.ch/lv2/chimaera#simulator"

static void
_delete_request(void *data, Evas_Object *obj, void *event)
{
	elm_exit();
}

EAPI_MAIN int
elm_main(int argc, char **argv)
{
	// init app
	app_t *app = app_new();

	// init elm
	Evas_Object *win = elm_win_util_standard_add("synthpod", "Synthpod");
	evas_object_smart_callback_add(win, "delete,request", _delete_request, NULL);
	evas_object_resize(win, 800, 450);
	evas_object_show(win);
	app->ui.win = win;

	Evas_Object *box = elm_box_add(win);
	elm_box_horizontal_set(box, EINA_FALSE);
	elm_box_homogeneous_set(box, EINA_FALSE);
	elm_box_padding_set(box, 5, 5);
	evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(box);
	elm_win_resize_object_add(win, box);
	app->ui.box = box;

	// add plugin
	mod_t *mod = app_mod_add(app, TEST_URI);

	// run main loop
	app_run(app);
	elm_run();
	app_stop(app);

	// deinit app
	app_mod_del(app, mod);
	app_free(app);

	// deinit elm
	evas_object_hide(win);
	evas_object_del(box);
	evas_object_del(win);
	
	return 0;
}

ELM_MAIN();
