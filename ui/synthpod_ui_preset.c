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

#include <synthpod_ui_private.h>

typedef bool (*populate_t)(sp_ui_t *ui, const LilvPlugin *plug, const char *match);

static char * 
_presetlist_label_get(void *data, Evas_Object *obj, const char *part)
{
	//FIXME
	return NULL;
}

static void
_presetlist_del(void *data, Evas_Object *obj)
{
	//FIXME
}

static void
_menu_preset_del(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	ui->presetwin = NULL;
}

static inline Evas_Object *
_menu_preset_new(sp_ui_t *ui)
{
	const char *title = "Plugin";
	Evas_Object *win = elm_win_add(ui->win, title, ELM_WIN_BASIC);
	if(win)
	{
		elm_win_title_set(win, title);
		elm_win_autodel_set(win, EINA_TRUE);
		evas_object_smart_callback_add(win, "delete,request", _menu_preset_del, ui);
		evas_object_resize(win, 960, 480);
		evas_object_show(win);

		Evas_Object *bg = elm_bg_add(win);
		if(bg)
		{
			elm_bg_color_set(bg, 64, 64, 64);
			evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(bg, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(bg);
			elm_win_resize_object_add(win, bg);
		} // bg

		return win;
	}

	return NULL;
}

void
_menu_preset(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(!ui->presetwin)
		ui->presetwin = _menu_preset_new(ui);
}

void
_presetlist_itc_add(sp_ui_t *ui)
{
	ui->presetitc = elm_genlist_item_class_new();
	if(ui->presetitc)
	{
		ui->presetitc->item_style = "default_style";
		ui->presetitc->func.text_get = _presetlist_label_get;
		ui->presetitc->func.content_get = NULL;
		ui->presetitc->func.state_get = NULL;
		ui->presetitc->func.del = _presetlist_del;
	}
}
