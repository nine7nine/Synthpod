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

#ifndef _SYNTHPOD_UI_PRIVATE_H
#define _SYNTHPOD_UI_PRIVATE_H

#include <stdlib.h>
#include <string.h>

#include <synthpod_ui.h>
#include <synthpod_private.h>
#include <patcher.h>
#include <smart_slider.h>
#include <smart_meter.h>
#include <smart_spinner.h>
#include <smart_toggle.h>
#include <smart_bitmask.h>
#include <lv2_external_ui.h> // kxstudio kx-ui extension
#include <zero_writer.h>

#if defined(SANDBOX_LIB)
#	include <sandbox_master.h>
#endif

#define NUM_UI_FEATURES 17
#define MODLIST_UI "/synthpod/modlist/ui"
#define MODGRID_UI "/synthpod/modgrid/ui"
#define FROM_APP_NUM 23

typedef struct _mod_t mod_t;
typedef struct _mod_ui_t mod_ui_t;
typedef struct _mod_ui_driver_t mod_ui_driver_t;
typedef struct _port_t port_t;
typedef struct _group_t group_t;
typedef struct _property_t property_t;
typedef struct _point_t point_t;
typedef struct _midi_controller_t midi_controller_t;

typedef enum _port_designation_t port_designation_t;
typedef enum _plug_info_type_t plug_info_type_t;
typedef enum _group_type_t group_type_t;
typedef enum _mod_ui_type_t mod_ui_type_t;
typedef struct _plug_info_t plug_info_t;

typedef struct _from_app_t from_app_t;
typedef void (*from_app_cb_t)(sp_ui_t *ui, const LV2_Atom *atom);

typedef void (*mod_ui_driver_call_t)(mod_t *data);
typedef void (*mod_ui_driver_event_t)(mod_t *data, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf);

struct _mod_ui_driver_t {
	mod_ui_driver_call_t show;
	mod_ui_driver_call_t hide;
	mod_ui_driver_event_t port_event;
};

enum _port_designation_t {
	PORT_DESIGNATION_ALL = 0,

	PORT_DESIGNATION_LEFT,
	PORT_DESIGNATION_RIGHT,
	PORT_DESIGNATION_CENTER,
	PORT_DESIGNATION_SIDE,
	PORT_DESIGNATION_CENTER_LEFT,
	PORT_DESIGNATION_CENTER_RIGHT,
	PORT_DESIGNATION_SIDE_LEFT,
	PORT_DESIGNATION_SIDE_RIGHT,
	PORT_DESIGNATION_REAR_LEFT,
	PORT_DESIGNATION_REAR_RIGHT,
	PORT_DESIGNATION_REAR_CENTER,
	PORT_DESIGNATION_LOW_FREQUENCY_EFFECTS
};

enum _plug_info_type_t {
	PLUG_INFO_TYPE_NAME = 0,
	PLUG_INFO_TYPE_URI,
	PLUG_INFO_TYPE_VERSION,
	PLUG_INFO_TYPE_LICENSE,
	PLUG_INFO_TYPE_PROJECT,
	PLUG_INFO_TYPE_BUNDLE_URI,
	PLUG_INFO_TYPE_AUTHOR_NAME,
	PLUG_INFO_TYPE_AUTHOR_EMAIL,
	PLUG_INFO_TYPE_AUTHOR_HOMEPAGE,
	PLUG_INFO_TYPE_COMMENT,

	PLUG_INFO_TYPE_MAX
};

enum _group_type_t {
	GROUP_TYPE_PORT			= 0,
	GROUP_TYPE_PROPERTY	= 1
};

enum _mod_ui_type_t {
	MOD_UI_TYPE_UNSUPPORTED = 0,

#if !defined(SANDBOX_LIB) || !defined(SANDBOX_EFL)
	MOD_UI_TYPE_EFL,
#endif
#if !defined(SANDBOX_LIB) || !defined(SANDBOX_SHOW)
	MOD_UI_TYPE_SHOW,
#endif
#if !defined(SANDBOX_LIB) || !defined(SANDBOX_KX)
	MOD_UI_TYPE_KX,
#endif

#if defined(SANDBOX_LIB)
#	if defined(SANDBOX_X11)
	MOD_UI_TYPE_SANDBOX_X11,
#	endif
#	if defined(SANDBOX_GTK2)
	MOD_UI_TYPE_SANDBOX_GTK2,
#	endif
#	if defined(SANDBOX_GTK3)
	MOD_UI_TYPE_SANDBOX_GTK3,
#	endif
#	if defined(SANDBOX_QT4)
	MOD_UI_TYPE_SANDBOX_QT4,
#	endif
#	if defined(SANDBOX_QT5)
	MOD_UI_TYPE_SANDBOX_QT5,
#	endif
#	if defined(SANDBOX_EFL)
	MOD_UI_TYPE_SANDBOX_EFL,
#	endif
#	if defined(SANDBOX_SHOW)
	MOD_UI_TYPE_SANDBOX_SHOW,
#	endif
#	if defined(SANDBOX_KX)
	MOD_UI_TYPE_SANDBOX_KX,
#	endif
#endif
};

struct _plug_info_t {
	plug_info_type_t type;
	const LilvPlugin *plug;
};

struct _mod_ui_t {
	mod_t *mod;
	const LilvUI *ui;
	LV2_URID urid;
	Eina_Module *lib;
	const LV2UI_Descriptor *descriptor;
	LV2UI_Handle handle;
	mod_ui_type_t type;
	const mod_ui_driver_t *driver;

	union {
		// Eo UI
		struct {
			Evas_Object *widget;
			Evas_Object *win;
		} eo;

		// custom UIs via the LV2UI_{Show,Idle}_Interface extensions
		struct {
			const LV2UI_Idle_Interface *idle_iface;
			const LV2UI_Show_Interface *show_iface;
			int dead;
			int visible;
			Ecore_Animator *anim;
		} show;

		// kx external-ui
		struct {
			LV2_External_UI_Widget *widget;
			int dead;
			Ecore_Animator *anim;
		} kx;

#if defined(SANDBOX_LIB)
		struct {
			sandbox_master_t *sb;
			sandbox_master_driver_t driver;
			Ecore_Exe *exe;
			Ecore_Fd_Handler *fd;
			Ecore_Event_Handler *del;
			char socket_path [64]; //TODO how big
		} sbox;
#endif
	};
};

struct _mod_t {
	u_id_t uid;

	sp_ui_t *ui;
	int selected;

	char *name;

	char *pset_label;

	// features
	LV2_Feature feature_list [NUM_UI_FEATURES];
	const LV2_Feature *features [NUM_UI_FEATURES + 1];

	// self
	const LilvPlugin *plug;
	LilvUIs *all_uis;
	LilvNodes *presets;
	LV2_URID subject;
	Eina_List *banks;

	// ports
	unsigned num_ports;
	port_t *ports;

	// properties
	Eina_List *static_properties;
	Eina_List *dynamic_properties;
	LilvNodes *writs;
	LilvNodes *reads;

	// UI color
	int col;

	// LV2UI_Port_Map extention
	LV2UI_Port_Map port_map;

	// log
	LV2_Log_Log log;

	// LV2UI_Port_Subscribe extension
	LV2UI_Port_Subscribe port_subscribe;

	// zero copy writer extension
	Zero_Writer_Schedule zero_writer;

	// opts
	struct {
		LV2_Options_Option options [3];
	} opts;

	// port-groups
	Eina_Hash *groups;

	struct {
		LV2_External_UI_Host host;
	} kx;

	Eina_List *mod_uis;
	mod_ui_t *mod_ui;

	// standard "automatic" UI
	struct {
		LV2UI_Descriptor descriptor;
		Elm_Object_Item *elmnt;
		Elm_Object_Item *grid;
		Evas_Object *frame;
		Evas_Object *list;
	} std;

	struct {
		int source;
		int sink;
	} system;
};

struct _group_t {
	group_type_t type;
	mod_t *mod;
	LilvNode *node;
	Eina_List *children;
};

struct _port_t {
	mod_t *mod;
	int selected;
	int subscriptions;

	const LilvPort *tar;
	uint32_t index;

	LilvNode *group;

	port_direction_t direction; // input, output
	port_type_t type; // audio, CV, control, atom
	port_atom_type_t atom_type; // MIDI, OSC, Time
	port_designation_t designation; // left, right, ...
	port_buffer_type_t buffer_type; // none, sequence
	int patchable; // support patch:Message

	bool integer;
	bool toggled;
	bool is_bitmask;
	bool logarithmic;
	LilvScalePoints *points;
	LV2_URID unit;

	float dflt;
	float min;
	float max;

	float peak;

	struct {
		Evas_Object *widget;
		int monitored;
	} std;
};

struct _point_t {
	char *label;
	union {
		char *s;
		double *d;
	};
};

struct _property_t {
	mod_t *mod;
	int selected;
	int editable;

	char *label;
	char *comment;
	LV2_URID tar_urid;
	LV2_URID type_urid;
	bool is_bitmask;

	struct {
		Elm_Object_Item *elmnt;
		Evas_Object *widget;
		Evas_Object *entry;
	} std;

	float minimum;
	float maximum;

	Eina_List *scale_points;
	LV2_URID unit;
};

struct _from_app_t {
	LV2_URID protocol;
	from_app_cb_t cb;
};

struct _sp_ui_t {
	sp_ui_driver_t *driver;
	void *data;

	char *bundle_path;

	int embedded;
	LilvWorld *world;
	const LilvPlugins *plugs;

	reg_t regs;
	LV2_Atom_Forge forge;

	Evas_Object *win;
	Evas_Object *vbox;
	Evas_Object *popup;
	Evas_Object *message;
	Evas_Object *feedback;
	Evas_Object *mainmenu;
	Evas_Object *uimenu;
	Evas_Object *statusline;
	Evas_Object *spin_cols;
	Evas_Object *spin_rows;
	Evas_Object *fileselector;

	int colors_max;
	int *colors_vec;

	Evas_Object *mainpane;

	Evas_Object *plugwin;
	Evas_Object *pluglist;
	Evas_Object *pluginfo;

	Evas_Object *patchwin;
	Evas_Object *matrix;
	float zoom;
	port_type_t matrix_type;
	Elm_Object_Item *matrix_audio;
	port_designation_t matrix_audio_designation;
	Elm_Object_Item *matrix_atom;
	Elm_Object_Item *matrix_event;
	Elm_Object_Item *matrix_control;
	Elm_Object_Item *matrix_cv;
	port_atom_type_t matrix_atom_type;
	Elm_Object_Item *matrix_atom_midi;
	Elm_Object_Item *matrix_atom_osc;
	Elm_Object_Item *matrix_atom_time;
	Elm_Object_Item *matrix_atom_patch;
	Elm_Object_Item *matrix_atom_xpress;

	Evas_Object *modlist;

	Evas_Object *modgrid;

	Elm_Genlist_Item_Class *plugitc;
	Elm_Genlist_Item_Class *listitc;
	Elm_Genlist_Item_Class *moditc;
	Elm_Genlist_Item_Class *stditc;
	Elm_Genlist_Item_Class *psetitc;
	Elm_Genlist_Item_Class *psetbnkitc;
	Elm_Genlist_Item_Class *psetitmitc;
	Elm_Genlist_Item_Class *psetsaveitc;
	Elm_Gengrid_Item_Class *griditc;
	Elm_Genlist_Item_Class *propitc;
	Elm_Genlist_Item_Class *grpitc;

	Elm_Object_Item *sink_itm;

	int dirty;

	LV2_URI_Map_Feature uri_to_id;

	int ncols;
	int nrows;
	float nleft;

	from_app_t from_apps [FROM_APP_NUM];
};

struct _midi_controller_t {
	uint8_t controller;
	const char *symbol;
};

static inline void *
__sp_ui_to_app_request(sp_ui_t *ui, size_t size)
{
	if(ui->driver->to_app_request && !ui->dirty)
		return ui->driver->to_app_request(size, ui->data);
	else
		return NULL;
}

#define _sp_ui_to_app_request(APP, SIZE) \
	ASSUME_ALIGNED(__sp_ui_to_app_request((APP), (SIZE)))

static inline void
_sp_ui_to_app_advance(sp_ui_t *ui, size_t size)
{
	if(ui->driver->to_app_advance && !ui->dirty)
		ui->driver->to_app_advance(size, ui->data);
}

static int
_urid_cmp(const void *data1, const void *data2)
{
	const property_t *prop1 = data1;
	const property_t *prop2 = data2;
	if(!prop1 || !prop2)
		return 1;

	return prop1->tar_urid < prop2->tar_urid
		? -1
		: (prop1->tar_urid > prop2->tar_urid
			? 1
			: 0);
}

static int
_urid_find(const void *data1, const void *data2)
{
	const property_t *prop1 = data1;
	const LV2_URID *tar_urid = data2;

	return prop1->tar_urid < *tar_urid
		? -1
		: (prop1->tar_urid > *tar_urid
			? 1
			: 0);
}

/*
 * ui
 */
void
_mod_ui_toggle_raw(mod_t *mod, mod_ui_t *mod_ui);

void
_ext_ui_write_function(LV2UI_Controller controller, uint32_t index,
	uint32_t size, uint32_t protocol, const void *buffer);

const LV2UI_Descriptor *
_ui_dlopen(const LilvUI *ui, Eina_Module **lib);

void
_ui_write_function(LV2UI_Controller controller, uint32_t port,
	uint32_t size, uint32_t protocol, const void *buffer);

void
_smart_mouse_in(void *data, Evas_Object *obj, void *event_info);

void
_smart_mouse_out(void *data, Evas_Object *obj, void *event_info);

/*
 * ui_app
 */
void
sp_ui_from_app_fill(sp_ui_t *ui);

/*
 * ui_modgrid
 */
void
_modgrid_item_size_update(sp_ui_t *ui);

void
_modgrid_itc_add(sp_ui_t *ui);

void
_modgrid_set_callbacks(sp_ui_t *ui);

/*
 * ui_modlist
 */
void
_modlist_refresh(sp_ui_t *ui);

void
_modlist_set_callbacks(sp_ui_t *ui);

void
_modlist_clear(sp_ui_t *ui, bool clear_system_ports, bool propagate);

void
_modlist_itc_add(sp_ui_t *ui);

/*
 * ui_mod
 */
mod_t *
_sp_ui_mod_add(sp_ui_t *ui, const char *uri, u_id_t uid);

void
_sp_ui_mod_del(sp_ui_t *ui, mod_t *mod);

void
_mod_del_widgets(mod_t *mod);

void
_mod_visible_set(mod_t *mod, int state, LV2_URID urid);

void
_mod_subscription_set(mod_t *mod, const LilvUI *ui_ui, int state);

group_t *
_mod_group_get(mod_t *mod, const char *group_lbl, int group_type,
	LilvNode *node, Elm_Object_Item **parent, bool expand);

void
_module_patch_get_all(mod_t *mod);

/*
 * ui_port
 */
void
_ui_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf);

void
_ui_port_tooltip_add(sp_ui_t *ui, Elm_Object_Item *elmnt, port_t *port);

void
_port_subscription_set(mod_t *mod, uint32_t index, uint32_t protocol, int state);

void
_port_itc_add(sp_ui_t *ui);


/*
 * ui_prop
 */
void
_property_free(property_t *prop);

void
_property_remove(mod_t *mod, group_t *group, property_t *prop);

void
_ui_property_tooltip_add(sp_ui_t *ui, Elm_Object_Item *elmnt, property_t *prop);

void
_mod_set_property(mod_t *mod, LV2_URID property_val, const LV2_Atom *value);

int
_propitc_cmp(const void *data1, const void *data2);

void
_property_itc_add(sp_ui_t *ui);

/*
 * ui_patcher
 */
void
_patches_update(sp_ui_t *ui);

/*
 * ui_efl
 */
extern const mod_ui_driver_t efl_ui_driver;

/*
 * ui_show
 */
extern const mod_ui_driver_t show_ui_driver;

/*
 * ui_kx
 */
void
_kx_ui_closed(LV2UI_Controller controller);

extern const mod_ui_driver_t kx_ui_driver;

/*
 * ui_sbox
 */
#if defined(SANDBOX_LIB)
extern const mod_ui_driver_t sbox_ui_driver;
#endif

/*
 * ui_midi
 */
const char *
_midi_note_lookup(float value);

const char *
_midi_controller_lookup(float value);


/*
 * ui_group
 */
int
_grpitc_cmp(const void *data1, const void *data2);

Eina_Bool
_groups_foreach(const Eina_Hash *hash, const void *key, void *data, void *fdata);

void
_group_itc_add(sp_ui_t *ui);

/*
 * ui_std
 */
int
_stditc_cmp(const void *data1, const void *data2);

void
_std_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf);

void
_std_ui_write_function(LV2UI_Controller controller, uint32_t index,
	uint32_t size, uint32_t protocol, const void *buf);

/*
 * ui_pluglist
 */
void
_pluglist_itc_add(sp_ui_t *ui);

void
_menu_plugin(void *data, Evas_Object *obj, void *event_info);

/*
 * ui_menu
 */
void
_theme_key_down(void *data, Evas *e, Evas_Object *obj, void *event_info);

void
_menu_add(sp_ui_t *ui);

/*
 * ui_matrix
 */
void
_menu_matrix(void *data, Evas_Object *obj, void *event_info);

#endif
