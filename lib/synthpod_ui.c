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

#include <stdlib.h>

#include <synthpod_ui.h>
#include <synthpod_private.h>

struct _sp_ui_t {
	sp_ui_driver_t *driver;
	void *data;

	LilvWorld *world;
	const LilvPlugins *plugs;

	reg_t regs;
	LV2_Atom_Forge forge;

	Evas_Object *win;
};

//TODO use it!
static inline int
_sp_ui_to_app(sp_ui_t *ui, LV2_Atom *atom)
{
	return ui->driver->to_app_cb(atom, ui->data);
}

sp_ui_t *
sp_ui_new(Evas_Object *win, sp_ui_driver_t *driver, void *data)
{
	elm_init(0, NULL);

	if(!driver || !data)
		return NULL;

	sp_ui_t *ui = calloc(1, sizeof(sp_ui_t));
	if(!ui)
		return NULL;

	ui->driver = driver;
	ui->data = data;
	ui->win = win;
	
	lv2_atom_forge_init(&ui->forge, ui->driver->map);

	ui->world = lilv_world_new();
	lilv_world_load_all(ui->world);
	ui->plugs = lilv_world_get_all_plugins(ui->world);

	sp_regs_init(&ui->regs, ui->world, driver->map);

	return ui;
}

Evas_Object *
sp_ui_widget_get(sp_ui_t *ui)
{
	return ui->win;
}

void
sp_ui_from_app(sp_ui_t *ui, const LV2_Atom *atom, void *data)
{
	const LV2_Atom_Object *ao = (const LV2_Atom_Object *)atom;
	const LV2_Atom_Object_Body *body = &ao->body;

	if(body->otype == ui->regs.synthpod.module_add.urid)
	{
		//TODO
	}
	else if(body->otype == ui->regs.synthpod.module_del.urid)
	{
		//TODO
	}
}

void
sp_ui_resize(sp_ui_t *ui, int w, int h)
{
	evas_object_resize(ui->win, w, h);
}

void
sp_ui_iterate(sp_ui_t *ui)
{
	ecore_main_loop_iterate();
}

void
sp_ui_run(sp_ui_t *ui)
{
	elm_run();
}

void
sp_ui_free(sp_ui_t *ui)
{
	if(!ui)
		return;

	evas_object_del(ui->win);
	
	sp_regs_deinit(&ui->regs);

	lilv_world_free(ui->world);

	free(ui);

	elm_shutdown();
}
