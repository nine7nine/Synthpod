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
//#define TEST_URI "http://open-music-kontrollers.ch/lv2/chimaera#simulator"

EAPI_MAIN int
elm_main(int argc, char **argv)
{
	// init app
	app_t *app = app_new();

	// add plugin
	mod_t *mod1 = app_mod_add(app,
		"http://open-music-kontrollers.ch/lv2/chimaera#simulator");
	mod_t *mod2 = app_mod_add(app,
		"http://open-music-kontrollers.ch/lv2/chimaera#visualizer");

	// run main loop
	app_run(app);
	elm_run();
	app_stop(app);

	// deinit app
	app_mod_del(app, mod2);
	app_mod_del(app, mod1);
	app_free(app);
	
	return 0;
}

ELM_MAIN();
