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

static void
_menu_fileselector_del(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	ui->fileselector = NULL;
}

static inline Evas_Object *
_menu_fileselector_new(sp_ui_t *ui, const char *title, Eina_Bool is_save, Evas_Smart_Cb cb)
{
	Evas_Object *win = elm_win_add(ui->win, title, ELM_WIN_BASIC);
	if(win)
	{
		elm_win_title_set(win, title);
		elm_win_autodel_set(win, EINA_TRUE);
		evas_object_smart_callback_add(win, "delete,request", _menu_fileselector_del, ui);
		evas_object_resize(win, 640, 480);
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

		Evas_Object *fileselector = elm_fileselector_add(win);
		if(fileselector)
		{
			elm_fileselector_path_set(fileselector, ui->bundle_path);
			elm_fileselector_is_save_set(fileselector, is_save);
			elm_fileselector_folder_only_set(fileselector, EINA_TRUE);
			elm_fileselector_expandable_set(fileselector, EINA_TRUE);
			elm_fileselector_multi_select_set(fileselector, EINA_FALSE);
			elm_fileselector_hidden_visible_set(fileselector, EINA_TRUE);
			evas_object_size_hint_weight_set(fileselector, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(fileselector, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_smart_callback_add(fileselector, "done", cb, ui);
			evas_object_smart_callback_add(fileselector, "activated", cb, ui);
			evas_object_show(fileselector);
			elm_win_resize_object_add(win, fileselector);
		} // widget

		return win;
	}

	return NULL;
}

static inline void
_feedback(sp_ui_t *ui, const char *message)
{
	elm_object_text_set(ui->message, message);
	if(evas_object_visible_get(ui->feedback))
		evas_object_hide(ui->feedback);
	evas_object_show(ui->feedback);
}

static void
_menu_new(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	if(!ui)
		return;

	sp_ui_bundle_new(ui);

	_feedback(ui, "New project");
}

static void
_menu_open(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	const char *bundle_path = event_info;
	if(bundle_path)
	{
		int update_path = ui->driver->features & SP_UI_FEATURE_OPEN ? 1 : 0;
		_modlist_clear(ui, true, false); // clear system ports
		sp_ui_bundle_load(ui, bundle_path, update_path);

		_feedback(ui, "Open project");
	}

	if(ui->fileselector)
	{
		evas_object_del(ui->fileselector);
		ui->fileselector = NULL;
	}
}

static inline void
_menu_open_fileselector(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(!ui->fileselector)
		ui->fileselector = _menu_fileselector_new(ui, "Open / Import", EINA_FALSE, _menu_open);
}

static void
_menu_save_as(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	const char *bundle_path = event_info;
	if(bundle_path)
	{
		int update_path = ui->driver->features & SP_UI_FEATURE_SAVE_AS ? 1 : 0;
		sp_ui_bundle_save(ui, bundle_path, update_path);

		_feedback(ui, "Save project");
	}

	if(ui->fileselector)
	{
		evas_object_del(ui->fileselector);
		ui->fileselector = NULL;
	}
}

static inline void
_menu_save_as_fileselector(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(!ui->fileselector)
		ui->fileselector = _menu_fileselector_new(ui, "Save as / Export", EINA_TRUE, _menu_save_as);
}

static void
_menu_save(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(ui && ui->bundle_path)
	{
		sp_ui_bundle_save(ui, ui->bundle_path, 0);

		_feedback(ui, "Save project");
	}
}

static void
_menu_close(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(ui && ui->driver->close)
	{
		ui->driver->close(ui->data);
	}
}

static void
_menu_about(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(!ui || !ui->popup)
		return;

	elm_popup_timeout_set(ui->popup, 0.f);
	if(evas_object_visible_get(ui->popup))
		evas_object_hide(ui->popup);
	else
		evas_object_show(ui->popup);
}

void
_theme_key_down(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	const Evas_Event_Key_Down *ev = event_info;

	const Eina_Bool cntrl = evas_key_modifier_is_set(ev->modifiers, "Control");
	const Eina_Bool shift = evas_key_modifier_is_set(ev->modifiers, "Shift");
	(void)shift;
	
	//printf("_theme_key_down: %s %i %i\n", ev->key, cntrl, shift);

	if(cntrl)
	{
		if(!strcmp(ev->key, "n")
			&& (ui->driver->features & SP_UI_FEATURE_NEW) )
		{
			_menu_new(ui, NULL, NULL);
		}
		else if(!strcmp(ev->key, "o")
			&& (ui->driver->features & SP_UI_FEATURE_OPEN) )
		{
			_menu_open_fileselector(ui, NULL, NULL);
		}
		else if(!strcmp(ev->key, "i")
			&& (ui->driver->features & SP_UI_FEATURE_IMPORT_FROM) )
		{
			_menu_open_fileselector(ui, NULL, NULL);
		}
		else if(!strcmp(ev->key, "s")
			&& (ui->driver->features & SP_UI_FEATURE_SAVE) )
		{
			_menu_save(ui, NULL, NULL);
		}
		else if(!strcmp(ev->key, "S")
			&& (ui->driver->features & SP_UI_FEATURE_SAVE_AS) )
		{
			_menu_save_as_fileselector(ui, NULL, NULL);
		}
		else if(!strcmp(ev->key, "e")
			&& (ui->driver->features & SP_UI_FEATURE_EXPORT_TO) )
		{
			_menu_save_as_fileselector(ui, NULL, NULL);
		}
		else if(!strcmp(ev->key, "q")
			&& (ui->driver->features & SP_UI_FEATURE_CLOSE) )
		{
			_menu_close(ui, NULL, NULL);
		}
		else if(!strcmp(ev->key, "h"))
		{
			_menu_about(ui, NULL, NULL);
		}
		else if(!strcmp(ev->key, "m"))
		{
			_menu_matrix(ui, NULL, NULL);
		}
		else if(!strcmp(ev->key, "p"))
		{
			_menu_plugin(ui, NULL, NULL);
		}
	}
}

void
_menu_add(sp_ui_t *ui)
{
	ui->mainmenu = elm_win_main_menu_get(ui->win);
	if(ui->mainmenu)
	{
		evas_object_show(ui->mainmenu);

		Elm_Object_Item *elmnt;

		if(ui->driver->features & SP_UI_FEATURE_NEW)
		{
			elmnt = elm_menu_item_add(ui->mainmenu, NULL, "document-new", "New", _menu_new, ui);
			elm_object_item_tooltip_text_set(elmnt, "Ctrl + 'N'");
		}

		if(ui->driver->features & (SP_UI_FEATURE_OPEN | SP_UI_FEATURE_IMPORT_FROM) )
		{
			if(ui->driver->features & SP_UI_FEATURE_OPEN)
			{
				elmnt = elm_menu_item_add(ui->mainmenu, NULL, "document-new", "Open", _menu_open_fileselector, ui);
				elm_object_item_tooltip_text_set(elmnt, "Ctrl + 'O'");
			}
			else if(ui->driver->features & SP_UI_FEATURE_IMPORT_FROM)
			{
				elmnt = elm_menu_item_add(ui->mainmenu, NULL, "document-import", "Import", _menu_open_fileselector, ui);
				elm_object_item_tooltip_text_set(elmnt, "Ctrl + 'I'");
			}
		}

		if(ui->driver->features & SP_UI_FEATURE_SAVE)
		{
			elmnt = elm_menu_item_add(ui->mainmenu, NULL, "document-save", "Save", _menu_save, ui);
			elm_object_item_tooltip_text_set(elmnt, "Ctrl + 'S'");
		}

		if(ui->driver->features & (SP_UI_FEATURE_SAVE_AS | SP_UI_FEATURE_EXPORT_TO) )
		{
			if(ui->driver->features & SP_UI_FEATURE_SAVE_AS)
			{
				elmnt = elm_menu_item_add(ui->mainmenu, NULL, "document-save-as", "Save as", _menu_save_as_fileselector, ui);
				elm_object_item_tooltip_text_set(elmnt, "Ctrl + Shift + 'S'");
			}
			else if(ui->driver->features & SP_UI_FEATURE_EXPORT_TO)
			{
				elmnt = elm_menu_item_add(ui->mainmenu, NULL, "document-export", "Export", _menu_save_as_fileselector, ui);
				elm_object_item_tooltip_text_set(elmnt, "Ctrl + 'E'");
			}
		}

		elmnt = elm_menu_item_add(ui->mainmenu, NULL, "list-add", "Plugin", _menu_plugin, ui);
		elm_object_item_tooltip_text_set(elmnt, "Ctrl + 'P'");

		elmnt = elm_menu_item_add(ui->mainmenu, NULL, "applications-system", "Matrix", _menu_matrix, ui);
		elm_object_item_tooltip_text_set(elmnt, "Ctrl + 'M'");

		if(ui->driver->features & SP_UI_FEATURE_CLOSE)
		{
			elmnt = elm_menu_item_add(ui->mainmenu, NULL, "application-exit", "Quit", _menu_close, ui);
			elm_object_item_tooltip_text_set(elmnt, "Ctrl + 'Q'");
		}

		elmnt = elm_menu_item_add(ui->mainmenu, NULL, "help-about", "About", _menu_about, ui);
		elm_object_item_tooltip_text_set(elmnt, "Ctrl + 'H'");
	}
}
