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
#include <string.h>

#include <synthpod_ui.h>
#include <synthpod_private.h>
#include <patcher.h>
#include <smart_slider.h>
#include <smart_meter.h>
#include <smart_spinner.h>
#include <smart_toggle.h>
#include <lv2_external_ui.h> // kxstudio kx-ui extension
#include <zero_writer.h>

#define NUM_UI_FEATURES 19
#define MODLIST_UI "/synthpod/modlist/ui"
#define MODGRID_UI "/synthpod/modgrid/ui"

typedef struct _mod_t mod_t;
typedef struct _mod_ui_t mod_ui_t;
typedef struct _port_t port_t;
typedef struct _group_t group_t;
typedef struct _property_t property_t;
typedef struct _point_t point_t;

typedef enum _plug_info_type_t plug_info_type_t;
typedef enum _group_type_t group_type_t;
typedef struct _plug_info_t plug_info_t;

typedef struct _from_app_t from_app_t;
typedef void (*from_app_cb_t)(sp_ui_t *ui, const LV2_Atom *atom);

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

	MOD_UI_TYPE_EO,
	MOD_UI_TYPE_X11,
	MOD_UI_TYPE_SHOW,
	MOD_UI_TYPE_KX,

	MOD_UI_TYPE_MAX
};

struct _plug_info_t {
	plug_info_type_t type;
	const LilvPlugin *plug;
};

struct _mod_ui_t {
	const LilvUI *ui;
	LV2_URID urid;
	Eina_Module *lib;
	const LV2UI_Descriptor *descriptor;
	LV2UI_Handle handle;
	int type;

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

		// X11 UI
		struct {
			const LV2UI_Idle_Interface *idle_iface;
			const LV2UI_Resize *client_resize_iface;
			Evas_Object *win;
			Ecore_X_Window xwin;
			Ecore_Animator *anim;
		} x11;

		// TODO MOD UI
		// TODO GtkUI
		// TODO Qt4UI
		// TODO Qt5UI
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

	// extension data
	LV2_Extension_Data_Feature ext_data;

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
		LV2_Options_Option options [2];
	} opts;

	// port-groups
	Eina_Hash *groups;

	struct {
		LV2_External_UI_Host host;
	} kx;
	struct {
		LV2UI_Resize host_resize_iface;
	} x11;
	Eina_List *mod_uis;
	mod_ui_t *mod_ui;

	// standard "automatic" UI
	struct {
		LV2UI_Descriptor descriptor;
		Elm_Object_Item *elmnt;
		Elm_Object_Item *grid;
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
	port_buffer_type_t buffer_type; // none, sequence
	int patchable; // support patch:Message

	bool integer;
	bool toggled;
	bool logarithmic;
	LilvScalePoints *points;
	char *unit;

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

	struct {
		Elm_Object_Item *elmnt;
		Evas_Object *widget;
		Evas_Object *entry;
	} std;

	float minimum;
	float maximum;

	Eina_List *scale_points;
	char *unit;
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
	Evas_Object *selector;
	Evas_Object *mainmenu;
	Evas_Object *statusline;
	Evas_Object *fileselector;

	int colors_max;
	int *colors_vec;

	Evas_Object *mainpane;

	Evas_Object *plugwin;
	Evas_Object *pluglist;

	Evas_Object *patchwin;
	Evas_Object *matrix;
	port_type_t matrix_type;
	Elm_Object_Item *matrix_audio;
	Elm_Object_Item *matrix_atom;
	Elm_Object_Item *matrix_event;
	Elm_Object_Item *matrix_control;
	Elm_Object_Item *matrix_cv;
	port_atom_type_t matrix_atom_type;
	Elm_Object_Item *matrix_atom_midi;
	Elm_Object_Item *matrix_atom_osc;
	Elm_Object_Item *matrix_atom_time;
	Elm_Object_Item *matrix_atom_patch;

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
};

struct _from_app_t {
	LV2_URID protocol;
	from_app_cb_t cb;
};

static const char *keys [12] = {
	"C", "#C",
	"D", "#D",
	"E",
	"F", "#F",
	"G", "#G",
	"A", "#A",
	"H"
};

static inline const char *
_note(uint8_t val, uint8_t *octave)
{
	*octave = val / 12;

	return keys[val % 12];
}

#define FROM_APP_NUM 16
static from_app_t from_apps [FROM_APP_NUM];

static int
_from_app_cmp(const void *itm1, const void *itm2)
{
	const from_app_t *from_app1 = itm1;
	const from_app_t *from_app2 = itm2;

	return _signum(from_app1->protocol, from_app2->protocol);
}

static Eina_Bool
_elm_config_changed(void *data, int ev_type, void *ev)
{
	sp_ui_t *ui = data;

	/* FIXME
	if(ui->patchgrid)
		elm_gengrid_item_size_set(ui->patchgrid, ELM_SCALE_SIZE(360), ELM_SCALE_SIZE(360));
	*/

	return ECORE_CALLBACK_PASS_ON;
}

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

// non-rt || rt with LV2_LOG__Trace
static int
_log_vprintf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, va_list args)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;

	char prefix [32]; //TODO how big?
	char buf [1024]; //TODO how big?

	snprintf(prefix, 32, "(UI)  {%i} ", mod->uid);
	vsnprintf(buf, 1024, fmt, args);

	char *pch = strtok(buf, "\n");
	while(pch)
	{
		if(ui->driver->log)
			ui->driver->log->printf(ui->driver->log->handle, type, "%s%s\n", prefix, pch);
		pch = strtok(NULL, "\n");
	}

	return 0;
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_printf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(handle, type, fmt, args);
  va_end(args);

	return ret;
}

static int
_grpitc_cmp(const void *data1, const void *data2)
{
	const Elm_Object_Item *itm1 = data1;
	const Elm_Object_Item *itm2 = data2;

	const Elm_Genlist_Item_Class *class1 = elm_genlist_item_item_class_get(itm1);
	const Elm_Genlist_Item_Class *class2 = elm_genlist_item_item_class_get(itm2);

	if(class1->refcount < class2->refcount)
		return -1;
	else if(class1->refcount > class2->refcount)
		return 1;

	group_t *grp1 = elm_object_item_data_get(itm1);
	group_t *grp2 = elm_object_item_data_get(itm2);

	// compare group type or property module uid
	return grp1->type < grp2->type
		? -1
		: (grp1->type > grp2->type
			? 1
			: 0);
}

static int
_stditc_cmp(const void *data1, const void *data2)
{
	const Elm_Object_Item *itm1 = data1;
	const Elm_Object_Item *itm2 = data2;
	if(!itm1 || !itm2)
		return 1;

	port_t *port1 = elm_object_item_data_get(itm1);
	port_t *port2 = elm_object_item_data_get(itm2);
	if(!port1 || !port2)
		return 1;

	// compare port indeces
	return port1->index < port2->index
		? -1
		: (port1->index > port2->index
			? 1
			: 0);
}

static int
_propitc_cmp(const void *data1, const void *data2)
{
	const Elm_Object_Item *itm1 = data1;
	const Elm_Object_Item *itm2 = data2;
	if(!itm1 || !itm2)
		return 1;

	property_t *prop1 = elm_object_item_data_get(itm1);
	property_t *prop2 = elm_object_item_data_get(itm2);
	if(!prop1 || !prop2)
		return 1;

	// compare property URIDs
	return prop1->tar_urid < prop2->tar_urid
		? -1
		: (prop1->tar_urid > prop2->tar_urid
			? 1
			: 0);
}

static void
_mod_set_property(mod_t *mod, LV2_URID property_val, const LV2_Atom *value)
{
	sp_ui_t *ui = mod->ui;

	//printf("ui got patch:Set: %u %u\n",
	//	mod->uid, property_val);

	property_t *prop;
	if(  (prop = eina_list_search_sorted(mod->static_properties, _urid_find, &property_val))
		|| (prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &property_val)) )
	{
		if(prop->std.widget &&
			(    (prop->type_urid == value->type)
				|| (prop->type_urid + value->type == ui->forge.Int + ui->forge.Bool)
			) )
		{
			if(prop->scale_points)
			{
				if(prop->type_urid == ui->forge.String)
				{
					smart_spinner_key_set(prop->std.widget, LV2_ATOM_BODY_CONST(value));
				}
				else if(prop->type_urid == ui->forge.Int)
				{
					int32_t val = ((const LV2_Atom_Int *)value)->body;
					smart_spinner_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Float)
				{
					float val = ((const LV2_Atom_Float *)value)->body;
					smart_spinner_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Long)
				{
					int64_t val = ((const LV2_Atom_Long *)value)->body;
					smart_spinner_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Double)
				{
					double val = ((const LV2_Atom_Double *)value)->body;
					smart_spinner_value_set(prop->std.widget, val);
				}
				//TODO do other types
			}
			else // !scale_points
			{
				if(  (prop->type_urid == ui->forge.String)
					|| (prop->type_urid == ui->forge.URI) )
				{
					const char *val = LV2_ATOM_BODY_CONST(value);
					if(prop->editable)
						elm_entry_entry_set(prop->std.entry, val);
					else
						elm_object_text_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Path)
				{
					const char *val = LV2_ATOM_BODY_CONST(value);
					//elm_object_text_set(prop->std.widget, val); TODO ellipsis on button text
					if(prop->editable)
						elm_fileselector_path_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Int)
				{
					int32_t val = ((const LV2_Atom_Int *)value)->body;
					smart_slider_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.URID)
				{
					uint32_t val = ((const LV2_Atom_URID *)value)->body;
					smart_slider_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Long)
				{
					int64_t val = ((const LV2_Atom_Long *)value)->body;
					smart_slider_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Float)
				{
					float val = ((const LV2_Atom_Float *)value)->body;
					smart_slider_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Double)
				{
					double val = ((const LV2_Atom_Double *)value)->body;
					smart_slider_value_set(prop->std.widget, val);
				}
				else if(prop->type_urid == ui->forge.Bool)
				{
					int val = ((const LV2_Atom_Bool *)value)->body;
					smart_toggle_value_set(prop->std.widget, val);
				}
			}
		}
	}
}

static inline void
_ui_port_tooltip_add(sp_ui_t *ui, Elm_Object_Item *elmnt, port_t *port)
{
	mod_t *mod = port->mod;

	LilvNodes *nodes = lilv_port_get_value(mod->plug, port->tar,
		ui->regs.rdfs.comment.node);
	LilvNode *node = nodes
		? lilv_nodes_get_first(nodes) //FIXME delete?
		: NULL;

	if(node)
		elm_object_item_tooltip_text_set(elmnt, lilv_node_as_string(node));

	if(nodes)
		lilv_nodes_free(nodes);
}

static inline void
_ui_property_tooltip_add(sp_ui_t *ui, Elm_Object_Item *elmnt, property_t *prop)
{
	if(prop->comment)
		elm_object_item_tooltip_text_set(elmnt, prop->comment);
}

static inline void
_property_free(property_t *prop)
{
	if(prop->label)
		free(prop->label); // strdup

	if(prop->comment)
		free(prop->comment); // strdup

	if(prop->unit)
		free(prop->unit); // strdup

	point_t *p;
	EINA_LIST_FREE(prop->scale_points, p)
	{
		if(p->label)
			free(p->label);

		if(p->s)
			free(p->s);

		free(p);
	}

	free(prop);
}

static inline void
_property_remove(mod_t *mod, group_t *group, property_t *prop)
{
	if(group)
		group->children = eina_list_remove(group->children, prop);

	mod->dynamic_properties = eina_list_remove(mod->dynamic_properties, prop);

	if(prop->std.elmnt)
		elm_object_item_del(prop->std.elmnt);
}

static inline group_t *
_mod_group_get(mod_t *mod, const char *group_lbl, int group_type,
	LilvNode *node, Elm_Object_Item **parent, bool expand)
{
	sp_ui_t *ui = mod->ui;

	*parent = eina_hash_find(mod->groups, group_lbl);

	if(*parent)
	{
		return elm_object_item_data_get(*parent);
	}
	else
	{
		group_t *group = calloc(1, sizeof(group_t));

		if(group)
		{
			group->type = group_type;
			group->mod = mod;
			group->node = node;

			*parent = elm_genlist_item_sorted_insert(mod->std.list,
				ui->grpitc, group, NULL, ELM_GENLIST_ITEM_GROUP, _grpitc_cmp, NULL, NULL);
			elm_genlist_item_select_mode_set(*parent, ELM_OBJECT_SELECT_MODE_NONE);

			if(*parent)
			{
				eina_hash_add(mod->groups, group_lbl, *parent);

				return group;
			}

			free(group);
		}

		*parent = NULL;
	}

	return NULL;
}

static inline void
_std_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;
	port_t *port = &mod->ports[index]; //FIXME handle patch:Response

	//printf("_std_port_event: %u %u %u\n", index, size, protocol);

	if(protocol == 0)
		protocol = ui->regs.port.float_protocol.urid;

	if(protocol == ui->regs.port.float_protocol.urid)
	{
		float val = *(float *)buf;

		// we should not set a value lower/higher than min/max for widgets
		//FIXME should be done by smart_*_value_set
		if(val < port->min)
			val = port->min;
		if(val > port->max)
			val = port->max;

		if(port->std.widget)
		{
			if(port->toggled)
				smart_toggle_value_set(port->std.widget, floor(val));
			else if(port->points)
				smart_spinner_value_set(port->std.widget, val);
			else // integer or float
				smart_slider_value_set(port->std.widget, val);
		}
	}
	else if(protocol == ui->regs.port.peak_protocol.urid)
	{
		const LV2UI_Peak_Data *peak_data = buf;
		//TODO smooth/filter signal?
		port->peak = peak_data->peak;

		smart_meter_value_set(port->std.widget, port->peak);
	}
	else if(protocol == ui->regs.port.event_transfer.urid)
	{
		const LV2_Atom_Object *obj = buf;

		if(  lv2_atom_forge_is_object_type(&ui->forge, obj->atom.type)
			&& (obj->body.id != ui->regs.synthpod.feedback_block.urid) ) // dont' feedback patch messages from UI itself!
		{
			// check for patch:Set
			if(obj->body.otype == ui->regs.patch.set.urid)
			{
				const LV2_Atom_URID *subject = NULL;
				const LV2_Atom_URID *property = NULL;
				const LV2_Atom *value = NULL;

				LV2_Atom_Object_Query q[] = {
					{ ui->regs.patch.subject.urid, (const LV2_Atom **)&subject },
					{ ui->regs.patch.property.urid, (const LV2_Atom **)&property },
					{ ui->regs.patch.value.urid, &value },
					{ 0, NULL }
				};
				lv2_atom_object_query(obj, q);

				bool subject_match = subject && (subject->atom.type == ui->forge.URID)
					? subject->body == mod->subject
					: true;

				if(subject_match && property && (property->atom.type == ui->forge.URID) && value)
					_mod_set_property(mod, property->body, value);
			}
			// check for patch:Put
			else if(obj->body.otype == ui->regs.patch.put.urid)
			{
				const LV2_Atom_URID *subject = NULL;
				const LV2_Atom_Object *body = NULL;

				LV2_Atom_Object_Query q[] = {
					{ ui->regs.patch.subject.urid, (const LV2_Atom **)&subject },
					{ ui->regs.patch.body.urid, (const LV2_Atom **)&body },
					{ 0, NULL }
				};
				lv2_atom_object_query(obj, q);

				bool subject_match = subject && (subject->atom.type == ui->forge.URID)
					? subject->body == mod->subject
					: true;

				if(subject_match && body && lv2_atom_forge_is_object_type(&ui->forge, body->atom.type))
				{
					LV2_ATOM_OBJECT_FOREACH(body, prop)
					{
						_mod_set_property(mod, prop->key, &prop->value);
					}
				}
			}
			// check for patch:Patch
			else if(obj->body.otype == ui->regs.patch.patch.urid)
			{
				const LV2_Atom_URID *subject = NULL;
				const LV2_Atom_Object *add = NULL;
				const LV2_Atom_Object *remove = NULL;

				LV2_Atom_Object_Query q[] = {
					{ ui->regs.patch.subject.urid, (const LV2_Atom **)&subject },
					{ ui->regs.patch.add.urid, (const LV2_Atom **)&add },
					{ ui->regs.patch.remove.urid, (const LV2_Atom **)&remove },
					{ 0, NULL }
				};
				lv2_atom_object_query(obj, q);

				if(  (!subject || (subject->atom.type == ui->forge.URID))
					&& add && lv2_atom_forge_is_object_type(&ui->forge, add->atom.type)
					&& remove && lv2_atom_forge_is_object_type(&ui->forge, remove->atom.type))
				{
					Elm_Object_Item *parent;
					const char *group_lbl = "*Properties*";
					group_t *group = _mod_group_get(mod, group_lbl, GROUP_TYPE_PROPERTY, NULL, &parent, true);

					LV2_ATOM_OBJECT_FOREACH(remove, atom_prop)
					{
						if(atom_prop->key == ui->regs.patch.readable.urid)
						{
							if(subject && (subject->body != mod->subject))
								continue; // ignore alien patch events

							const LV2_URID tar_urid = ((const LV2_Atom_URID *)&atom_prop->value)->body;
							if(tar_urid == ui->regs.patch.wildcard.urid)
							{
								// delete all readable dynamic properties of this module
								Eina_List *l1, *l2;
								property_t *prop;
								EINA_LIST_FOREACH_SAFE(mod->dynamic_properties, l1, l2, prop)
								{
									if(prop->editable)
										continue; // skip writable

									_property_remove(mod, group, prop);
									_property_free(prop);
								}
							}
							else // !wildcard
							{
								property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

								if(prop)
								{
									_property_remove(mod, group, prop);
									_property_free(prop);
								}
							}
						}
						else if(atom_prop->key == ui->regs.patch.writable.urid)
						{
							if(subject && (subject->body != mod->subject))
								continue; // ignore alien patch events

							const LV2_URID tar_urid = ((const LV2_Atom_URID *)&atom_prop->value)->body;
							if(tar_urid == ui->regs.patch.wildcard.urid)
							{
								// delete all readable dynamic properties of this module
								Eina_List *l1, *l2;
								property_t *prop;
								EINA_LIST_FOREACH_SAFE(mod->dynamic_properties, l1, l2, prop)
								{
									if(!prop->editable)
										continue; // skip readable

									_property_remove(mod, group, prop);
									_property_free(prop);
								}
							}
							else // !wildcard
							{
								property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

								if(prop)
								{
									_property_remove(mod, group, prop);
									_property_free(prop);
								}
							}
						}
						else if(atom_prop->key == ui->regs.rdfs.label.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop && prop->label)
							{
								free(prop->label);
								prop->label = NULL;
							}
						}
						else if(atom_prop->key == ui->regs.rdfs.comment.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop && prop->comment)
							{
								free(prop->comment);
								prop->comment = NULL;
							}
						}
						else if(atom_prop->key == ui->regs.rdfs.range.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
								prop->type_urid = 0;
						}
						else if(atom_prop->key == ui->regs.core.minimum.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
								prop->minimum = 0.f;
						}
						else if(atom_prop->key == ui->regs.core.maximum.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
								prop->maximum = 1.f;
						}
						else if(atom_prop->key == ui->regs.units.unit.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop && prop->unit)
							{
								free(prop->unit);
								prop->unit = NULL;
							}
						}
						else if(atom_prop->key == ui->regs.core.scale_point.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;
							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								point_t *p;
								EINA_LIST_FREE(prop->scale_points, p)
								{
									free(p->label);
									free(p->s);
									free(p);
								}
							}
						}
					}

					LV2_ATOM_OBJECT_FOREACH(add, atom_prop)
					{
						if(atom_prop->key == ui->regs.patch.readable.urid)
						{
							if(subject && (subject->body != mod->subject))
								continue; // ignore alien patch events

							property_t *prop = calloc(1, sizeof(property_t));
							if(prop)
							{
								prop->mod = mod;
								prop->editable = 0;
								prop->tar_urid = ((const LV2_Atom_URID *)&atom_prop->value)->body;
								prop->label = NULL; // not yet known
								prop->comment = NULL; // not yet known
								prop->type_urid = 0; // not yet known
								prop->minimum = 0.f; // not yet known
								prop->maximum = 1.f; // not yet known
								prop->unit = NULL; // not yet known

								mod->dynamic_properties = eina_list_sorted_insert(mod->dynamic_properties, _urid_cmp, prop);

								// append property to corresponding group
								if(group)
									group->children = eina_list_append(group->children, prop);

								// append property to UI
								if(parent && elm_genlist_item_expanded_get(parent)) //TODO remove duplicate code
								{
									Elm_Object_Item *elmnt = elm_genlist_item_sorted_insert(mod->std.list,
										ui->propitc, prop, parent, ELM_GENLIST_ITEM_NONE, _propitc_cmp,
										NULL, NULL);
									if(elmnt)
									{
										int select_mode = ELM_OBJECT_SELECT_MODE_NONE;
										elm_genlist_item_select_mode_set(elmnt, select_mode);
										_ui_property_tooltip_add(ui, elmnt, prop);
										prop->std.elmnt = elmnt;
									}
								}
							}
						}
						else if(atom_prop->key == ui->regs.patch.writable.urid)
						{
							if(subject && (subject->body != mod->subject))
								continue; // ignore alien patch events

							property_t *prop = calloc(1, sizeof(property_t));
							if(prop)
							{
								prop->mod = mod;
								prop->editable = 1;
								prop->tar_urid = ((const LV2_Atom_URID *)&atom_prop->value)->body;
								prop->label = NULL; // not yet known
								prop->comment = NULL; // not yet known
								prop->type_urid = 0; // not yet known
								prop->minimum = 0.f; // not yet known
								prop->maximum = 1.f; // not yet known
								prop->unit = NULL; // not yet known

								mod->dynamic_properties = eina_list_sorted_insert(mod->dynamic_properties, _urid_cmp, prop);

								// append property to corresponding group
								if(group)
									group->children = eina_list_append(group->children, prop);

								// append property to UI
								if(parent && elm_genlist_item_expanded_get(parent)) //TODO remove duplicate code
								{
									Elm_Object_Item *elmnt = elm_genlist_item_sorted_insert(mod->std.list,
										ui->propitc, prop, parent, ELM_GENLIST_ITEM_NONE, _propitc_cmp,
										NULL, NULL);
									if(elmnt)
									{
										int select_mode = (prop->type_urid == ui->forge.String)
											|| (prop->type_urid == ui->forge.URI)
												? ELM_OBJECT_SELECT_MODE_DEFAULT
												: ELM_OBJECT_SELECT_MODE_NONE;
										elm_genlist_item_select_mode_set(elmnt, select_mode);
										_ui_property_tooltip_add(ui, elmnt, prop);
										prop->std.elmnt = elmnt;
									}
								}
							}
						}
						else if(atom_prop->key == ui->regs.rdfs.label.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								prop->label = strndup(LV2_ATOM_BODY_CONST(&atom_prop->value), atom_prop->value.size);
								if(prop->std.elmnt)
									elm_genlist_item_update(prop->std.elmnt);
							}
						}
						else if(atom_prop->key == ui->regs.rdfs.comment.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								prop->comment = strndup(LV2_ATOM_BODY_CONST(&atom_prop->value), atom_prop->value.size);
								if(prop->std.elmnt)
								{
									_ui_property_tooltip_add(ui, prop->std.elmnt, prop);
									elm_genlist_item_update(prop->std.elmnt);
								}
							}
						}
						else if(atom_prop->key == ui->regs.rdfs.range.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								prop->type_urid = ((const LV2_Atom_URID *)&atom_prop->value)->body;
								if(prop->std.elmnt)
									elm_genlist_item_update(prop->std.elmnt);
							}
						}
						else if(atom_prop->key == ui->regs.core.minimum.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								if(atom_prop->value.type == ui->forge.Int)
									prop->minimum = ((const LV2_Atom_Int *)&atom_prop->value)->body;
								else if(atom_prop->value.type == ui->forge.Long)
									prop->minimum = ((const LV2_Atom_Long *)&atom_prop->value)->body;
								else if(atom_prop->value.type == ui->forge.Float)
									prop->minimum = ((const LV2_Atom_Float *)&atom_prop->value)->body;
								else if(atom_prop->value.type == ui->forge.Double)
									prop->minimum = ((const LV2_Atom_Double *)&atom_prop->value)->body;

								if(prop->std.elmnt)
									elm_genlist_item_update(prop->std.elmnt);
							}
						}
						else if(atom_prop->key == ui->regs.core.maximum.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								if(atom_prop->value.type == ui->forge.Int)
									prop->maximum = ((const LV2_Atom_Int *)&atom_prop->value)->body;
								else if(atom_prop->value.type == ui->forge.Long)
									prop->maximum = ((const LV2_Atom_Long *)&atom_prop->value)->body;
								else if(atom_prop->value.type == ui->forge.Float)
									prop->maximum = ((const LV2_Atom_Float *)&atom_prop->value)->body;
								else if(atom_prop->value.type == ui->forge.Double)
									prop->maximum = ((const LV2_Atom_Double *)&atom_prop->value)->body;

								if(prop->std.elmnt)
									elm_genlist_item_update(prop->std.elmnt);
							}
						}
						else if(atom_prop->key == ui->regs.units.unit.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								if(atom_prop->value.type == ui->forge.URID)
								{
									const char *uri = ui->driver->unmap->unmap(ui->driver->unmap->handle,
										((const LV2_Atom_URID *)&atom_prop->value)->body);

									if(uri)
									{
										LilvNode *unit = lilv_new_uri(ui->world, uri);
										if(unit)
										{
											LilvNode *symbol = lilv_world_get(ui->world, unit, ui->regs.units.symbol.node, NULL);
											if(symbol)
											{
												prop->unit = strdup(lilv_node_as_string(symbol));
												lilv_node_free(symbol);
											}

											lilv_node_free(unit);
										}
									}
								}

								if(prop->std.elmnt)
									elm_genlist_item_update(prop->std.elmnt);
							}
						}
						else if(atom_prop->key == ui->regs.core.scale_point.urid)
						{
							const LV2_URID tar_urid = subject ? subject->body : 0;

							property_t *prop = eina_list_search_sorted(mod->dynamic_properties, _urid_find, &tar_urid);

							if(prop)
							{
								const LV2_Atom_Object *point_obj = (const LV2_Atom_Object *)&atom_prop->value;

								const LV2_Atom_String *point_label = NULL;
								const LV2_Atom *point_value = NULL;

								LV2_Atom_Object_Query point_q[] = {
									{ ui->regs.rdfs.label.urid, (const LV2_Atom **)&point_label },
									{ ui->regs.rdf.value.urid, (const LV2_Atom **)&point_value },
									{ 0, NULL }
								};
								lv2_atom_object_query(point_obj, point_q);

								if(point_label && point_value)
								{
									point_t *p = calloc(1, sizeof(point_t));
									p->label = strndup(LV2_ATOM_BODY_CONST(point_label), point_label->atom.size);
									if(point_value->type == ui->forge.Int)
									{
										p->d = calloc(1, sizeof(double));
										*p->d = ((const LV2_Atom_Int *)point_value)->body;
									}
									else if(point_value->type == ui->forge.Float)
									{
										p->d = calloc(1, sizeof(double));
										*p->d = ((const LV2_Atom_Float *)point_value)->body;
									}
									else if(point_value->type == ui->forge.Long)
									{
										p->d = calloc(1, sizeof(double));
										*p->d = ((const LV2_Atom_Long *)point_value)->body;
									}
									else if(point_value->type == ui->forge.Double)
									{
										p->d = calloc(1, sizeof(double));
										*p->d = ((const LV2_Atom_Double *)point_value)->body;
									}
									//FIXME do other types
									else if(point_value->type == ui->forge.String)
									{
										p->s = strndup(LV2_ATOM_BODY_CONST(point_value), point_value->size);
									}

									prop->scale_points = eina_list_append(prop->scale_points, p);

									if(prop->std.elmnt)
										elm_genlist_item_update(prop->std.elmnt);
								}
							}
						}
					}
				}
				else
					fprintf(stderr, "patch:Patch one of patch:subject, patch:add, patch:add missing\n");
			}
		}
	}
	else
		fprintf(stderr, "unknown protocol\n");
}

static inline void
_eo_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;
	mod_ui_t *mod_ui = mod->mod_ui;

	//printf("_eo_port_event: %u %u %u\n", index, size, protocol);

	if(  mod_ui
		&& mod_ui->ui
		&& mod_ui->descriptor
		&& mod_ui->descriptor->port_event
		&& mod_ui->handle)
	{
		if(mod_ui->eo.win)
			mod_ui->descriptor->port_event(mod_ui->handle, index, size, protocol, buf);
	}
}

static uint32_t
_port_index(LV2UI_Feature_Handle handle, const char *symbol)
{
	mod_t *mod = handle;
	LilvNode *symbol_uri = lilv_new_string(mod->ui->world, symbol);
	const LilvPort *port = lilv_plugin_get_port_by_symbol(mod->plug, symbol_uri);
	lilv_node_free(symbol_uri);

	return port
		? lilv_port_get_index(mod->plug, port)
		: LV2UI_INVALID_PORT_INDEX;
}

static inline void
_ui_port_update_request(mod_t *mod, uint32_t index)
{
	sp_ui_t *ui = mod->ui;

	size_t size = sizeof(transmit_port_refresh_t);
	transmit_port_refresh_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_refresh_fill(&ui->regs, &ui->forge, trans, size, mod->uid, index);
		_sp_ui_to_app_advance(ui, size);
	}
}

static inline void
_port_subscription_set(mod_t *mod, uint32_t index, uint32_t protocol, int state)
{
	sp_ui_t *ui = mod->ui;

	if(protocol == 0)
		protocol = ui->regs.port.float_protocol.urid;

	size_t size = sizeof(transmit_port_subscribed_t);
	transmit_port_subscribed_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_subscribed_fill(&ui->regs, &ui->forge, trans, size,
			mod->uid, index, protocol, state);
		_sp_ui_to_app_advance(ui, size);
	}

	if(state == 1)
		_ui_port_update_request(mod, index);
}

static uint32_t
_port_subscribe(LV2UI_Feature_Handle handle, uint32_t index, uint32_t protocol,
	const LV2_Feature *const *features)
{
	mod_t *mod = handle;

	_port_subscription_set(mod, index, protocol, 1);

	return 0;
}

static uint32_t
_port_unsubscribe(LV2UI_Feature_Handle handle, uint32_t index, uint32_t protocol,
	const LV2_Feature *const *features)
{
	mod_t *mod = handle;

	_port_subscription_set(mod, index, protocol, 0);

	return 0;
}

static inline void
_ui_mod_selected_request(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	// request module selected state
	size_t size = sizeof(transmit_module_selected_t);
	transmit_module_selected_t *trans0 = _sp_ui_to_app_request(ui, size);
	if(trans0)
	{
		_sp_transmit_module_selected_fill(&ui->regs, &ui->forge, trans0, size, mod->uid, -1);
		_sp_ui_to_app_advance(ui, size);
	}

	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		// request port selected state
		{
			size = sizeof(transmit_port_selected_t);
			transmit_port_selected_t *trans1 = _sp_ui_to_app_request(ui, size);
			if(trans1)
			{
				_sp_transmit_port_selected_fill(&ui->regs, &ui->forge, trans1, size, mod->uid, port->index, -1);
				_sp_ui_to_app_advance(ui, size);
			}
		}

		// request port monitored state
		{
			size = sizeof(transmit_port_monitored_t);
			transmit_port_monitored_t *trans2 = _sp_ui_to_app_request(ui, size);
			if(trans2)
			{
				_sp_transmit_port_monitored_fill(&ui->regs, &ui->forge, trans2, size, mod->uid, port->index, -1);
				_sp_ui_to_app_advance(ui, size);
			}
		}
	}
}

static inline void
_ui_mod_visible_request(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	// request module visible state
	size_t size = sizeof(transmit_module_visible_t);
	transmit_module_visible_t *trans0 = _sp_ui_to_app_request(ui, size);
	if(trans0)
	{
		_sp_transmit_module_visible_fill(&ui->regs, &ui->forge, trans0, size, mod->uid, -1, 0);
		_sp_ui_to_app_advance(ui, size);
	}
}

static inline void
_ui_mod_embedded_request(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	// request module embedded state
	size_t size = sizeof(transmit_module_embedded_t);
	transmit_module_embedded_t *trans0 = _sp_ui_to_app_request(ui, size);
	if(trans0)
	{
		_sp_transmit_module_embedded_fill(&ui->regs, &ui->forge, trans0, size, mod->uid, -1);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void //XXX check with _zero_writer_request/advance
_ui_write_function(LV2UI_Controller controller, uint32_t port,
	uint32_t size, uint32_t protocol, const void *buffer)
{
	mod_t *mod = controller;
	sp_ui_t *ui = mod->ui;
	port_t *tar = &mod->ports[port];

	// ignore output ports
	if(tar->direction != PORT_DIRECTION_INPUT)
	{
		fprintf(stderr, "_ui_write_function: UI can only write to input port\n");
		return;
	}

	// handle special meaning of protocol=0
	if(protocol == 0)
		protocol = ui->regs.port.float_protocol.urid;

	if(protocol == ui->regs.port.float_protocol.urid)
	{
		assert(size == sizeof(float));
		size_t len = sizeof(transfer_float_t);
		transfer_float_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			_sp_transfer_float_fill(&ui->regs, &ui->forge, trans, mod->uid,
				tar->index, buffer);
			_sp_ui_to_app_advance(ui, len);
		}
	}
	else if(protocol == ui->regs.port.atom_transfer.urid)
	{
		size_t len = sizeof(transfer_atom_t) + lv2_atom_pad_size(size);
		transfer_atom_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			_sp_transfer_atom_fill(&ui->regs, &ui->forge, trans, mod->uid, tar->index,
				size, buffer);
			_sp_ui_to_app_advance(ui, len);
		}
	}
	else if(protocol == ui->regs.port.event_transfer.urid)
	{
		size_t len = sizeof(transfer_atom_t) + lv2_atom_pad_size(size);
		transfer_atom_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			_sp_transfer_event_fill(&ui->regs, &ui->forge, trans, mod->uid, tar->index,
				size, buffer);
			_sp_ui_to_app_advance(ui, len);
		}
	}
}

static inline void
_show_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;
	mod_ui_t *mod_ui = mod->mod_ui;
	sp_ui_t *ui = mod->ui;

	//printf("_show_port_event: %u %u %u\n", index, size, protocol);

	if(  mod_ui
		&& mod_ui->ui
		&& mod_ui->descriptor
		&& mod_ui->descriptor->port_event
		&& mod_ui->handle)
	{
		mod_ui->descriptor->port_event(mod_ui->handle,
			index, size, protocol, buf);
		if(protocol == ui->regs.port.float_protocol.urid)
		{
			// send it twice for plugins that expect "0" instead of float_protocol URID
			mod_ui->descriptor->port_event(mod_ui->handle,
				index, size, 0, buf);
		}
	}
}

static inline void
_kx_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;
	mod_ui_t *mod_ui = mod->mod_ui;
	sp_ui_t *ui = mod->ui;

	//printf("_kx_port_event: %u %u %u\n", index, size, protocol);

	if(  mod_ui
		&& mod_ui->ui
		&& mod_ui->descriptor
		&& mod_ui->descriptor->port_event
		&& mod_ui->handle)
	{
		mod_ui->descriptor->port_event(mod_ui->handle,
			index, size, protocol, buf);
		if(protocol == ui->regs.port.float_protocol.urid)
		{
			// send it twice for plugins that expect "0" instead of float_protocol URID
			mod_ui->descriptor->port_event(mod_ui->handle,
				index, size, 0, buf);
		}
	}
}

static inline void
_x11_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;
	mod_ui_t *mod_ui = mod->mod_ui;
	sp_ui_t *ui = mod->ui;

	//printf("_x11_port_event: %u %u %u\n", index, size, protocol);

	if(  mod_ui
		&& mod_ui->ui
		&& mod_ui->descriptor
		&& mod_ui->descriptor->port_event
		&& mod_ui->handle)
	{
		mod_ui->descriptor->port_event(mod_ui->handle,
			index, size, protocol, buf);
		if(protocol == ui->regs.port.float_protocol.urid)
		{
			// send it twice for plugins that expect "0" instead of float_protocol URID
			mod_ui->descriptor->port_event(mod_ui->handle,
				index, size, 0, buf);
		}
	}
}

static inline void
_ui_port_event(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	mod_t *mod = handle;
	mod_ui_t *mod_ui = mod->mod_ui;

	//printf("_ui_port_event: %u %u %u %u\n", mod->uid, index, size, protocol);

	_std_port_event(mod, index, size, protocol, buf);

	if(mod_ui)
	{
		if(mod_ui->type == MOD_UI_TYPE_EO)
			_eo_port_event(mod, index, size, protocol, buf);
		else if(mod_ui->type == MOD_UI_TYPE_SHOW)
			_show_port_event(mod, index, size, protocol, buf);
		else if(mod_ui->type == MOD_UI_TYPE_KX)
			_kx_port_event(mod, index, size, protocol, buf);
		else if(mod_ui->type == MOD_UI_TYPE_X11)
			_x11_port_event(mod, index, size, protocol, buf);
	}
}

static void
_ext_ui_write_function(LV2UI_Controller controller, uint32_t port,
	uint32_t size, uint32_t protocol, const void *buffer)
{
	mod_t *mod = controller;
	sp_ui_t *ui = mod->ui;

	// to StdUI
	_std_port_event(controller, port, size, protocol, buffer);

	// to rt-thread
	const LV2_Atom_Object *obj = buffer;
	if(  lv2_atom_forge_is_object_type(&ui->forge, obj->atom.type)
		&& ( (obj->body.otype == ui->regs.patch.set.urid)
			|| (obj->body.otype == ui->regs.patch.put.urid)
			|| (obj->body.otype == ui->regs.patch.patch.urid) ) ) //TODO support more patch messages
	{
		// set feedback block flag on object id
		// TODO can we do this without a malloc?
		LV2_Atom_Object *clone = malloc(size);
		if(clone)
		{
			memcpy(clone, obj, size);
			clone->body.id = ui->regs.synthpod.feedback_block.urid;
			_ui_write_function(controller, port, size, protocol, clone);
			free(clone);
		}
	}
	else // no feedback block flag needed
		_ui_write_function(controller, port, size, protocol, buffer);
}

static void
_std_ui_write_function(LV2UI_Controller controller, uint32_t port,
	uint32_t size, uint32_t protocol, const void *buffer)
{
	mod_t *mod = controller;
	mod_ui_t *mod_ui = mod->mod_ui;

	// to rt-thread
	_ui_write_function(controller, port, size, protocol, buffer);

	if(mod_ui)
	{
		if(mod_ui->type == MOD_UI_TYPE_EO)
			_eo_port_event(controller, port, size, protocol, buffer);
		if(mod_ui->type == MOD_UI_TYPE_SHOW)
			_show_port_event(controller, port, size, protocol, buffer);
		if(mod_ui->type == MOD_UI_TYPE_KX)
			_kx_port_event(controller, port, size, protocol, buffer);
		if(mod_ui->type == MOD_UI_TYPE_X11)
			_x11_port_event(controller, port, size, protocol, buffer);
	}
}

static void
_mod_subscription_set(mod_t *mod, const LilvUI *ui_ui, int state)
{
	sp_ui_t *ui = mod->ui;

	// subscribe manually for port notifications
	const LilvNode *plug_uri_node = lilv_plugin_get_uri(mod->plug);

	LilvNodes *notifs = lilv_world_find_nodes(ui->world,
		lilv_ui_get_uri(ui_ui), ui->regs.port.notification.node, NULL);
	LILV_FOREACH(nodes, n, notifs)
	{
		const LilvNode *notif = lilv_nodes_get(notifs, n);
		LilvNode *plug = lilv_world_get(ui->world, notif,
			ui->regs.ui.plugin.node, NULL);

		if(plug && !lilv_node_equals(plug, plug_uri_node))
		{
			lilv_node_free(plug);
			continue; // notification not for this plugin 
		}

		LilvNode *ind = lilv_world_get(ui->world, notif,
			ui->regs.core.index.node, NULL);
		LilvNode *sym = lilv_world_get(ui->world, notif,
			ui->regs.core.symbol.node, NULL);
		LilvNode *prot = lilv_world_get(ui->world, notif,
			ui->regs.ui.protocol.node, NULL);

		uint32_t index = LV2UI_INVALID_PORT_INDEX;
		if(ind)
		{
			index = lilv_node_as_int(ind);
		}
		else if(sym)
		{
			const LilvPort *port = lilv_plugin_get_port_by_symbol(mod->plug, sym);
			index = lilv_port_get_index(mod->plug, port);
		}

		if(index != LV2UI_INVALID_PORT_INDEX)
		{
			port_t *port = &mod->ports[index];

			if(prot) // protocol specified
			{
				if(lilv_node_equals(prot, ui->regs.port.float_protocol.node))
					_port_subscription_set(mod, index, ui->regs.port.float_protocol.urid, state);
				else if(lilv_node_equals(prot, ui->regs.port.peak_protocol.node))
					_port_subscription_set(mod, index, ui->regs.port.peak_protocol.urid, state);
				else if(lilv_node_equals(prot, ui->regs.port.atom_transfer.node))
					_port_subscription_set(mod, index, ui->regs.port.atom_transfer.urid, state);
				else if(lilv_node_equals(prot, ui->regs.port.event_transfer.node))
					_port_subscription_set(mod, index, ui->regs.port.event_transfer.urid, state);
			}
			else // no protocol specified, we have to guess according to port type
			{
				if(port->type == PORT_TYPE_CONTROL)
					_port_subscription_set(mod, index, ui->regs.port.float_protocol.urid, state);
				else if(port->type == PORT_TYPE_AUDIO)
					_port_subscription_set(mod, index, ui->regs.port.peak_protocol.urid, state);
				else if(port->type == PORT_TYPE_CV)
					_port_subscription_set(mod, index, ui->regs.port.peak_protocol.urid, state);
				else if(port->type == PORT_TYPE_ATOM)
				{
					if(port->buffer_type == PORT_BUFFER_TYPE_SEQUENCE)
						_port_subscription_set(mod, index, ui->regs.port.event_transfer.urid, state);
					else
						_port_subscription_set(mod, index, ui->regs.port.atom_transfer.urid, state);
				}
			}

			//TODO handle ui:notifyType

			/*
			printf("port has notification for: %s %s %u %u %u\n",
				lilv_node_as_string(sym),
				lilv_node_as_uri(prot),
				index,
				ui->regs.port.atom_transfer.urid,
				ui->regs.port.event_transfer.urid);
			*/
		}

		if(plug)
			lilv_node_free(plug);
		if(ind)
			lilv_node_free(ind);
		if(sym)
			lilv_node_free(sym);
		if(prot)
			lilv_node_free(prot);
	}
	lilv_nodes_free(notifs);
}

static inline void
_mod_visible_set(mod_t *mod, int state, LV2_URID urid)
{
	sp_ui_t *ui = mod->ui;

	// set module visible state
	const size_t size = sizeof(transmit_module_visible_t);
	transmit_module_visible_t *trans1 = _sp_ui_to_app_request(ui, size);
	if(trans1)
	{
		_sp_transmit_module_visible_fill(&ui->regs, &ui->forge, trans1, size, mod->uid, state, urid);
		_sp_ui_to_app_advance(ui, size);
	}
}

static inline void
_mod_embedded_set(mod_t *mod, int state)
{
	sp_ui_t *ui = mod->ui;

	// set module embedded state
	const size_t size = sizeof(transmit_module_embedded_t);
	transmit_module_embedded_t *trans1 = _sp_ui_to_app_request(ui, size);
	if(trans1)
	{
		_sp_transmit_module_embedded_fill(&ui->regs, &ui->forge, trans1, size, mod->uid, state);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_show_ui_hide(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;
	mod_ui_t *mod_ui = mod->mod_ui;

	// stop animator
	if(mod_ui->show.anim)
	{
		ecore_animator_del(mod_ui->show.anim);
		mod_ui->show.anim = NULL;
	}

	// hide UI
	if(mod_ui->show.show_iface && mod_ui->show.show_iface->hide && mod_ui->handle)
	{
		if(mod_ui->show.show_iface->hide(mod_ui->handle))
			fprintf(stderr, "show_iface->hide failed\n");
		else
			mod_ui->show.visible = 0; // toggle visibility flag
	}

	// unsubscribe all ports
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 0);
	}

	// unsubscribe from notifications
	_mod_subscription_set(mod, mod_ui->ui, 0);

	// call cleanup 
	if(mod_ui->descriptor && mod_ui->descriptor->cleanup && mod_ui->handle)
		mod_ui->descriptor->cleanup(mod_ui->handle);
	mod_ui->handle = NULL;
	mod_ui->show.idle_iface = NULL;
	mod_ui->show.show_iface = NULL;

	mod->mod_ui = NULL;

	_mod_visible_set(mod, 0, 0);
}

static Eina_Bool
_show_ui_animator(void *data)
{
	mod_t *mod = data;
	mod_ui_t *mod_ui = mod->mod_ui;

	int res = 0;
	if(mod_ui->show.idle_iface && mod_ui->show.idle_iface->idle && mod_ui->handle)
		res = mod_ui->show.idle_iface->idle(mod_ui->handle);

	if(res) // UI requests to be hidden
	{
		_show_ui_hide(mod);

		return EINA_FALSE; // stop animator
	}

	return EINA_TRUE; // retrigger animator
}

static void
_show_ui_show(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;
	mod_ui_t *mod_ui = mod->mod_ui;

	if(!mod_ui->descriptor)
		return;

	const LilvNode *plugin_uri = lilv_plugin_get_uri(mod->plug);
	const char *plugin_string = lilv_node_as_string(plugin_uri);

	const LilvNode *bundle_uri = lilv_ui_get_bundle_uri(mod_ui->ui);
#if defined(LILV_0_22)
	char *bundle_path = lilv_file_uri_parse(lilv_node_as_string(bundle_uri), NULL);
#else
	const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));
#endif

	// subscribe to ports
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];
		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
	}

	// subscribe to notifications
	_mod_subscription_set(mod, mod_ui->ui, 1);

	// instantiate UI
	void *dummy;
	mod_ui->handle = mod_ui->descriptor->instantiate(
		mod_ui->descriptor,
		plugin_string,
		bundle_path,
		_ext_ui_write_function,
		mod,
		&dummy,
		mod->features);

#if defined(LILV_0_22)
	lilv_free(bundle_path);
#endif

	if(!mod_ui->handle)
		return;

	// get show iface if any
	if(mod_ui->descriptor->extension_data)
		mod_ui->show.show_iface = mod_ui->descriptor->extension_data(LV2_UI__showInterface);

	if(!mod_ui->show.show_iface)
		return;

	// show UI
	if(mod_ui->show.show_iface && mod_ui->show.show_iface->show && mod_ui->handle)
	{
		if(mod_ui->show.show_iface->show(mod_ui->handle))
			fprintf(stderr, "show_iface->show failed\n");
		else
			mod_ui->show.visible = 1; // toggle visibility flag
	}

	// get idle iface if any
	if(mod_ui->descriptor->extension_data)
		mod_ui->show.idle_iface = mod_ui->descriptor->extension_data(LV2_UI__idleInterface);

	// start animator
	if(mod_ui->show.idle_iface)
		mod_ui->show.anim = ecore_animator_add(_show_ui_animator, mod);

	_mod_visible_set(mod, 1, mod_ui->urid);
}

static void
_kx_ui_cleanup(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;
	mod_ui_t *mod_ui = mod->mod_ui;

	// stop animator
	if(mod_ui->kx.anim)
	{
		ecore_animator_del(mod_ui->kx.anim);
		mod_ui->kx.anim = NULL;
	}

	// unsubscribe all ports
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 0);
	}

	// unsubscribe from notifications
	_mod_subscription_set(mod, mod_ui->ui, 0);

	// call cleanup 
	if(mod_ui->descriptor && mod_ui->descriptor->cleanup && mod_ui->handle)
		mod_ui->descriptor->cleanup(mod_ui->handle);
	mod_ui->handle = NULL;
	mod_ui->kx.widget = NULL;
	mod_ui->kx.dead = 0;

	mod->mod_ui = NULL;
}

static Eina_Bool
_kx_ui_animator(void *data)
{
	mod_t *mod = data;
	mod_ui_t *mod_ui = mod->mod_ui;

	LV2_EXTERNAL_UI_RUN(mod_ui->kx.widget);

	if(mod_ui->kx.dead)
	{
		_kx_ui_cleanup(mod);

		return EINA_FALSE; // stop animator
	}

	return EINA_TRUE; // retrigger animator
}

static void
_kx_ui_show(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;
	mod_ui_t *mod_ui = mod->mod_ui;

	if(!mod_ui->descriptor)
		return;

	const LilvNode *plugin_uri = lilv_plugin_get_uri(mod->plug);
	const char *plugin_string = lilv_node_as_string(plugin_uri);

	const LilvNode *bundle_uri = lilv_ui_get_bundle_uri(mod_ui->ui);
#if defined(LILV_0_22)
	char *bundle_path = lilv_file_uri_parse(lilv_node_as_string(bundle_uri), NULL);
#else
	const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));
#endif

	// subscribe to ports
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];
		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
	}

	// subscribe to notifications
	_mod_subscription_set(mod, mod_ui->ui, 1);

	// instantiate UI
	mod_ui->handle = mod_ui->descriptor->instantiate(
		mod_ui->descriptor,
		plugin_string,
		bundle_path,
		_ext_ui_write_function,
		mod,
		(void **)&mod_ui->kx.widget,
		mod->features);

#if defined(LILV_0_22)
	lilv_free(bundle_path);
#endif

	if(!mod_ui->handle)
		return;

	// show UI
	LV2_EXTERNAL_UI_SHOW(mod_ui->kx.widget);

	// start animator
	mod_ui->kx.anim = ecore_animator_add(_kx_ui_animator, mod);

	_mod_visible_set(mod, 1, mod_ui->urid);
}

static void
_kx_ui_hide(mod_t *mod)
{
	mod_ui_t *mod_ui = mod->mod_ui;

	// hide UI
	if(mod_ui->kx.anim) // UI is running
		LV2_EXTERNAL_UI_HIDE(mod_ui->kx.widget);

	// cleanup
	_kx_ui_cleanup(mod);

	_mod_visible_set(mod, 0, 0);
}
 
// plugin ui has been closed manually
static void
_kx_ui_closed(LV2UI_Controller controller)
{
	mod_t *mod = controller;
	mod_ui_t *mod_ui = mod->mod_ui;

	if(!mod_ui || !mod_ui->ui)
		return;

	// mark for cleanup
	mod_ui->kx.dead = 1;
}

static int
_ui_host_resize(LV2UI_Feature_Handle handle, int w, int h)
{
	mod_t *mod = handle;
	mod_ui_t *mod_ui = mod->mod_ui;

	//printf("_ui_host_resize: %i %i\n", w, h);
	if( (mod_ui->type == MOD_UI_TYPE_X11) && mod_ui->x11.win)
		evas_object_resize(mod_ui->x11.win, w, h);
	else if( (mod_ui->type == MOD_UI_TYPE_EO) && mod_ui->eo.win)
		evas_object_resize(mod_ui->eo.win, w, h);

	return 0;
}

static Eina_Bool
_x11_ui_animator(void *data)
{
	mod_t *mod = data;
	mod_ui_t *mod_ui = mod->mod_ui;

	if(mod_ui->x11.idle_iface && mod_ui->x11.idle_iface->idle && mod_ui->handle)
		mod_ui->x11.idle_iface->idle(mod_ui->handle);

	return EINA_TRUE; // retrigger animator
}

static void
_x11_ui_hide(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;
	mod_ui_t *mod_ui = mod->mod_ui;

	// stop animator
	if(mod_ui->x11.anim)
	{
		ecore_animator_del(mod_ui->x11.anim);
		mod_ui->x11.anim = NULL;
	}

	// unsubscribe all ports
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 0);
	}

	// unsubscribe from notifications
	_mod_subscription_set(mod, mod_ui->ui, 0);

	// call cleanup 
	if(mod_ui->descriptor && mod_ui->descriptor->cleanup && mod_ui->handle)
		mod_ui->descriptor->cleanup(mod_ui->handle);
	mod_ui->handle = NULL;

	evas_object_del(mod_ui->x11.win);
	mod_ui->x11.win = NULL;
	mod_ui->x11.xwin = 0;
	mod_ui->x11.idle_iface = NULL;

	mod->mod_ui = NULL;

	_mod_visible_set(mod, 0, 0);
}

static void
_x11_delete_request(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;

	_x11_ui_hide(mod);
}

static void
_x11_ui_client_resize(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	mod_ui_t *mod_ui = mod->mod_ui;

	int w, h;
	evas_object_geometry_get(obj, NULL, NULL, &w, &h);

	//printf("_x11_ui_client_resize: %i %i\n", w, h);
	mod_ui->x11.client_resize_iface->ui_resize(mod_ui->handle, w, h);
}

static inline void
_eo_ui_hide(mod_t *mod)
{
	mod_ui_t *mod_ui = mod->mod_ui;

	if(mod_ui->eo.win)
		evas_object_del(mod_ui->eo.win);
	mod_ui->handle = NULL;
	mod_ui->eo.widget = NULL;
	mod_ui->eo.win = NULL;

	mod->mod_ui = NULL;

	_mod_visible_set(mod, 0, 0);
}

static void
_full_delete_request(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;

	_eo_ui_hide(mod);
}

static inline Evas_Object *
_eo_widget_create(Evas_Object *parent, mod_t *mod)
{
	sp_ui_t *ui = mod->ui;
	mod_ui_t *mod_ui = mod->mod_ui;

	if(!mod_ui->ui || !mod_ui->descriptor)
		return NULL;

	const LilvNode *plugin_uri = lilv_plugin_get_uri(mod->plug);
	const char *plugin_string = NULL;
	if(plugin_uri)
		plugin_string = lilv_node_as_string(plugin_uri);

	const LilvNode *bundle_uri = lilv_ui_get_bundle_uri(mod_ui->ui);
#if defined(LILV_0_22)
	char *bundle_path = lilv_file_uri_parse(lilv_node_as_string(bundle_uri), NULL);
#else
	const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));
#endif

	// subscribe automatically to all non-atom ports by default
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
	}

	// set subscriptions for notifications
	_mod_subscription_set(mod, mod_ui->ui, 1);

	// instantiate UI
	mod_ui->eo.widget = NULL;

	if(mod_ui->descriptor->instantiate)
	{
		mod->feature_list[2].data = parent;

		mod_ui->handle = mod_ui->descriptor->instantiate(
			mod_ui->descriptor,
			plugin_string,
			bundle_path,
			_ext_ui_write_function,
			mod,
			(void **)&(mod_ui->eo.widget),
			mod->features);

		mod->feature_list[2].data = NULL;
	}

#if defined(LILV_0_22)
	lilv_free(bundle_path);
#endif

	if(!mod_ui->handle || !mod_ui->eo.widget)
		return NULL;

	return mod_ui->eo.widget;
}

static inline void
_eo_ui_show(mod_t *mod)
{
	mod_ui_t *mod_ui = mod->mod_ui;
	sp_ui_t *ui = mod->ui;

	// add fullscreen EoUI
	Evas_Object *win = elm_win_add(ui->win, mod->name, ELM_WIN_BASIC);
	if(win)
	{
		elm_win_title_set(win, mod->name);
		evas_object_smart_callback_add(win, "delete,request", _full_delete_request, mod);
		evas_object_resize(win, 800, 450);

		mod_ui->eo.win = win;

		Evas_Object *bg = elm_bg_add(win);
		if(bg)
		{
			elm_bg_color_set(bg, 64, 64, 64);
			evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(bg, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(bg);
			elm_win_resize_object_add(win, bg);
		} // bg

		Evas_Object *widget = _eo_widget_create(win, mod);
		if(widget)
		{
			evas_object_size_hint_weight_set(widget, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(widget, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(widget);
			elm_win_resize_object_add(win, widget);
		} // widget

		evas_object_show(win);

		_mod_visible_set(mod, 1, mod_ui->urid);
	} // win
}

static inline char *
_mod_get_name(mod_t *mod)
{
	const LilvPlugin *plug = mod->plug;
	if(plug)
	{
		LilvNode *name_node = lilv_plugin_get_name(plug);
		if(name_node)
		{
			const char *name_str = lilv_node_as_string(name_node);

			char *dup = NULL;
			if(name_str)
				asprintf(&dup, "%s (#%u)", name_str, mod->uid);
			
			lilv_node_free(name_node);

			return dup; //XXX needs to be freed
		}
	}

	return NULL;
}

static void
_x11_ui_show(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;
	mod_ui_t *mod_ui = mod->mod_ui;

	if(!mod_ui->descriptor)
		return;

	const LilvNode *plugin_uri = lilv_plugin_get_uri(mod->plug);
	const char *plugin_string = lilv_node_as_string(plugin_uri);

	const LilvNode *bundle_uri = lilv_ui_get_bundle_uri(mod_ui->ui);
#if defined(LILV_0_22)
	char *bundle_path = lilv_file_uri_parse(lilv_node_as_string(bundle_uri), NULL);
#else
	const char *bundle_path = lilv_uri_to_path(lilv_node_as_string(bundle_uri));
#endif

	// subscribe to ports
	for(unsigned i=0; i<mod->num_ports; i++)
	{
		port_t *port = &mod->ports[i];
		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
	}

	// subscribe to notifications
	_mod_subscription_set(mod, mod_ui->ui, 1);

	mod_ui->x11.win = elm_win_add(ui->win, mod->name, ELM_WIN_BASIC);
	if(mod_ui->x11.win)
	{
		elm_win_title_set(mod_ui->x11.win, mod->name);
		evas_object_smart_callback_add(mod_ui->x11.win, "delete,request", _x11_delete_request, mod);
		evas_object_resize(mod_ui->x11.win, 800, 450);
		evas_object_show(mod_ui->x11.win);
		mod_ui->x11.xwin = elm_win_xwindow_get(mod_ui->x11.win);
	}

	void *dummy;
	mod->feature_list[2].data = (void *)((uintptr_t)mod_ui->x11.xwin);

	// instantiate UI
	mod_ui->handle = mod_ui->descriptor->instantiate(
		mod_ui->descriptor,
		plugin_string,
		bundle_path,
		_ext_ui_write_function,
		mod,
		&dummy,
		mod->features);

#if defined(LILV_0_22)
	lilv_free(bundle_path);
#endif

	mod->feature_list[2].data = NULL;

	if(!mod_ui->handle)
		return;

	// get interfaces
	if(mod_ui->descriptor->extension_data)
	{
		// get idle iface
		mod_ui->x11.idle_iface = mod_ui->descriptor->extension_data(LV2_UI__idleInterface);

		// get resize iface
		mod_ui->x11.client_resize_iface = mod_ui->descriptor->extension_data(LV2_UI__resize);
		if(mod_ui->x11.client_resize_iface)
			evas_object_event_callback_add(mod_ui->x11.win, EVAS_CALLBACK_RESIZE, _x11_ui_client_resize, mod);
	}

	// start animator
	if(mod_ui->x11.idle_iface)
		mod_ui->x11.anim = ecore_animator_add(_x11_ui_animator, mod);
	
	_mod_visible_set(mod, 1, mod_ui->urid);
}

//XXX do code cleanup from here upwards

static const LV2UI_Descriptor *
_ui_dlopen(const LilvUI *ui, Eina_Module **lib)
{
	const LilvNode *ui_uri = lilv_ui_get_uri(ui);
	const LilvNode *binary_uri = lilv_ui_get_binary_uri(ui);
	if(!ui_uri || !binary_uri)
		return NULL;

	const char *ui_string = lilv_node_as_string(ui_uri);
#if defined(LILV_0_22)
	char *binary_path = lilv_file_uri_parse(lilv_node_as_string(binary_uri), NULL);
#else
	const char *binary_path = lilv_uri_to_path(lilv_node_as_string(binary_uri));
#endif
	if(!ui_string || !binary_path)
		return NULL;

	*lib = eina_module_new(binary_path);

#if defined(LILV_0_22)
	lilv_free(binary_path);
#endif

	if(!*lib)
		return NULL;

	if(!eina_module_load(*lib))
	{
		eina_module_free(*lib);
		*lib = NULL;

		return NULL;
	}

	LV2UI_DescriptorFunction ui_descfunc = NULL;
	ui_descfunc = eina_module_symbol_get(*lib, "lv2ui_descriptor");

	if(!ui_descfunc)
		goto fail;

	// search for a matching UI
	for(int i=0; 1; i++)
	{
		const LV2UI_Descriptor *ui_desc = ui_descfunc(i);

		if(!ui_desc) // end of UI list
			break;
		else if(!strcmp(ui_desc->URI, ui_string))
			return ui_desc; // matching UI found
	}

fail:
	eina_module_unload(*lib);
	eina_module_free(*lib);
	*lib = NULL;

	return NULL;
}

static void * //XXX check with _ui_write_function
_zero_writer_request(Zero_Writer_Handle handle, uint32_t port, uint32_t size,
	uint32_t protocol)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;
	port_t *tar = &mod->ports[port];

	//printf("_zero_writer_request: %u\n", size);

	// ignore output ports
	if(tar->direction != PORT_DIRECTION_INPUT)
	{
		fprintf(stderr, "_zero_writer_request: UI can only write to input port\n");
		return NULL;
	}

	// float protocol not supported by zero_writer
	assert( (protocol == ui->regs.port.atom_transfer.urid)
		|| (protocol == ui->regs.port.event_transfer.urid) );

	if(protocol == ui->regs.port.atom_transfer.urid)
	{
		size_t len = sizeof(transfer_atom_t) + lv2_atom_pad_size(size);
		transfer_atom_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			return _sp_transfer_atom_fill(&ui->regs, &ui->forge, trans, mod->uid,
				tar->index, size, NULL);
		}
	}
	else if(protocol == ui->regs.port.event_transfer.urid)
	{
		size_t len = sizeof(transfer_atom_t) + lv2_atom_pad_size(size);
		transfer_atom_t *trans = _sp_ui_to_app_request(ui, len);
		if(trans)
		{
			return _sp_transfer_event_fill(&ui->regs, &ui->forge, trans, mod->uid,
				tar->index, size, NULL);
		}
	}

	return NULL; // protocol not supported 
}

static void // XXX check with _ui_write_function
_zero_writer_advance(Zero_Writer_Handle handle, uint32_t written)
{
	mod_t *mod = handle;
	sp_ui_t *ui = mod->ui;

	//printf("_zero_writer_advance: %u\n", written);

	size_t len = sizeof(transfer_atom_t) + lv2_atom_pad_size(written);
	_sp_ui_to_app_advance(ui, len);
}

static int
_sp_ui_next_col(sp_ui_t *ui)
{
	int col = 0;
	int count = INT_MAX;
	for(int i=1; i<ui->colors_max; i++)
	{
		if(ui->colors_vec[i] < count)
		{
			count = ui->colors_vec[i];
			col = i;
		}
	}

	ui->colors_vec[col] += 1;
	return col;
}

static int
_bank_cmp(const void *data1, const void *data2)
{
	const LilvNode *node1 = data1;
	const LilvNode *node2 = data2;
	if(!node1 || !node2)
		return 1;

	return lilv_node_equals(node1, node2)
		? 0
		: -1;
}

static inline void
_sp_ui_mod_port_add(sp_ui_t *ui, mod_t *mod, uint32_t i, port_t *tar, const LilvPort *port)
{
	// discover port groups
	tar->group = lilv_port_get(mod->plug, port, ui->regs.group.group.node);

	tar->mod = mod;
	tar->tar = port;
	tar->index = i;
	tar->direction = lilv_port_is_a(mod->plug, port, ui->regs.port.input.node)
		? PORT_DIRECTION_INPUT
		: PORT_DIRECTION_OUTPUT;

	if(lilv_port_is_a(mod->plug, port, ui->regs.port.audio.node))
	{
		tar->type =  PORT_TYPE_AUDIO;
	}
	else if(lilv_port_is_a(mod->plug, port, ui->regs.port.cv.node))
	{
		tar->type = PORT_TYPE_CV;
	}
	else if(lilv_port_is_a(mod->plug, port, ui->regs.port.control.node))
	{
		tar->type = PORT_TYPE_CONTROL;

		LilvNode *dflt_node;
		LilvNode *min_node;
		LilvNode *max_node;
		lilv_port_get_range(mod->plug, tar->tar, &dflt_node, &min_node, &max_node);
		tar->dflt = dflt_node ? lilv_node_as_float(dflt_node) : 0.f;
		tar->min = min_node ? lilv_node_as_float(min_node) : 0.f;
		tar->max = max_node ? lilv_node_as_float(max_node) : 1.f;
		lilv_node_free(dflt_node);
		lilv_node_free(min_node);
		lilv_node_free(max_node);

		tar->integer = lilv_port_has_property(mod->plug, tar->tar, ui->regs.port.integer.node);
		tar->toggled = lilv_port_has_property(mod->plug, tar->tar, ui->regs.port.toggled.node);
		tar->logarithmic = lilv_port_has_property(mod->plug, tar->tar, ui->regs.port.logarithmic.node);
		int enumeration = lilv_port_has_property(mod->plug, port, ui->regs.port.enumeration.node);
		tar->points = enumeration
			? lilv_port_get_scale_points(mod->plug, port)
			: NULL;

		// force positive logarithmic range
		if(tar->logarithmic)
		{
			if(tar->min <= 0.f)
				tar->min = FLT_MIN; // smallest positive normalized float
		}

		// force max > min
		if(tar->max <= tar->min)
			tar->max = tar->min + FLT_MIN;

		// force min <= dflt <= max
		if(tar->dflt < tar->min)
			tar->dflt = tar->min;
		if(tar->dflt > tar->max)
			tar->dflt = tar->max;
	}
	else if(lilv_port_is_a(mod->plug, port, ui->regs.port.atom.node)) 
	{
		tar->type = PORT_TYPE_ATOM;
		tar->buffer_type = PORT_BUFFER_TYPE_SEQUENCE;
		//tar->buffer_type = lilv_port_is_a(mod->plug, port, ui->regs.port.sequence.node)
		//	? PORT_BUFFER_TYPE_SEQUENCE
		//	: PORT_BUFFER_TYPE_NONE; //TODO

		// does this port support patch:Message?
		tar->patchable = lilv_port_supports_event(mod->plug, port, ui->regs.patch.message.node);

		tar->atom_type = 0;
		if(lilv_port_supports_event(mod->plug, port, ui->regs.port.midi.node))
			tar->atom_type |= PORT_ATOM_TYPE_MIDI;
		if(lilv_port_supports_event(mod->plug, port, ui->regs.port.osc_event.node))
			tar->atom_type |= PORT_ATOM_TYPE_OSC;
		if(lilv_port_supports_event(mod->plug, port, ui->regs.port.time_position.node))
			tar->atom_type |= PORT_ATOM_TYPE_TIME;
		if(lilv_port_supports_event(mod->plug, port, ui->regs.patch.message.node))
			tar->atom_type |= PORT_ATOM_TYPE_PATCH;
	}
	else if(lilv_port_is_a(mod->plug, port, ui->regs.port.event.node)) 
	{
		tar->type = PORT_TYPE_EVENT;

		tar->atom_type = 0;
	}

	// get port unit
	LilvNode *unit = lilv_port_get(mod->plug, tar->tar, ui->regs.units.unit.node);
	if(unit)
	{
		LilvNode *symbol = lilv_world_get(ui->world, unit, ui->regs.units.symbol.node, NULL);
		if(symbol)
		{
			tar->unit = strdup(lilv_node_as_string(symbol));
			lilv_node_free(symbol);
		}

		lilv_node_free(unit);
	}
}

static inline property_t *
_sp_ui_mod_static_prop_add(sp_ui_t *ui, mod_t *mod, const LilvNode *writable, int editable)
{
	property_t *prop = calloc(1, sizeof(property_t));
	if(!prop)
		return NULL;

	const char *writable_str = lilv_node_as_uri(writable);

	prop->mod = mod;
	prop->editable = editable;
	prop->label = NULL;
	prop->comment = NULL;
	prop->tar_urid = ui->driver->map->map(ui->driver->map->handle, writable_str);
	prop->type_urid = 0; // invalid type
	prop->minimum = 0.f; // not yet known
	prop->maximum = 1.f; // not yet known
	prop->unit = NULL; // not yet known

	// get rdfs:label
	LilvNode *label = lilv_world_get(ui->world, writable,
		ui->regs.rdfs.label.node, NULL);
	if(label)
	{
		const char *label_str = lilv_node_as_string(label);

		if(label_str)
			prop->label = strdup(label_str);

		lilv_node_free(label);
	}

	// get rdfs:comment
	LilvNode *comment = lilv_world_get(ui->world, writable,
		ui->regs.rdfs.comment.node, NULL);
	if(comment)
	{
		const char *comment_str = lilv_node_as_string(comment);

		if(comment_str)
			prop->comment = strdup(comment_str);

		lilv_node_free(comment);
	}

	// get type of patch:writable
	LilvNode *type = lilv_world_get(ui->world, writable,
		ui->regs.rdfs.range.node, NULL);
	if(type)
	{
		const char *type_str = lilv_node_as_string(type);

		//printf("with type: %s\n", type_str);
		prop->type_urid = ui->driver->map->map(ui->driver->map->handle, type_str);

		lilv_node_free(type);
	}

	// get lv2:minimum
	LilvNode *minimum = lilv_world_get(ui->world, writable,
		ui->regs.core.minimum.node, NULL);
	if(minimum)
	{
		prop->minimum = lilv_node_as_float(minimum);

		lilv_node_free(minimum);
	}

	// get lv2:maximum
	LilvNode *maximum = lilv_world_get(ui->world, writable,
		ui->regs.core.maximum.node, NULL);
	if(maximum)
	{
		prop->maximum = lilv_node_as_float(maximum);

		lilv_node_free(maximum);
	}

	// get units:unit
	LilvNode *unit = lilv_world_get(ui->world, writable,
		ui->regs.units.unit.node, NULL);
	if(unit)
	{
		LilvNode *symbol = lilv_world_get(ui->world, unit, ui->regs.units.symbol.node, NULL);
		if(symbol)
		{
			prop->unit = strdup(lilv_node_as_string(symbol));
			lilv_node_free(symbol);
		}

		lilv_node_free(unit);
	}
	
	LilvNodes *spoints = lilv_world_find_nodes(ui->world, writable,
		ui->regs.core.scale_point.node, NULL);
	if(spoints)
	{
		LILV_FOREACH(nodes, n, spoints)
		{
			const LilvNode *point = lilv_nodes_get(spoints, n);
			LilvNode *point_label = lilv_world_get(ui->world, point,
				ui->regs.rdfs.label.node, NULL);
			LilvNode *point_value = lilv_world_get(ui->world, point,
				ui->regs.rdf.value.node, NULL);

			if(point_label && point_value)
			{
				point_t *p = calloc(1, sizeof(point_t));
				p->label = strdup(lilv_node_as_string(point_label));
				if(prop->type_urid == ui->forge.Int)
				{
					p->d = calloc(1, sizeof(double));
					*p->d = lilv_node_as_float(point_value);
				}
				else if(prop->type_urid == ui->forge.Float)
				{
					p->d = calloc(1, sizeof(double));
					*p->d = lilv_node_as_float(point_value);
				}
				else if(prop->type_urid == ui->forge.Long)
				{
					p->d = calloc(1, sizeof(double));
					*p->d = lilv_node_as_float(point_value);
				}
				else if(prop->type_urid == ui->forge.Double)
				{
					p->d = calloc(1, sizeof(double));
					*p->d = lilv_node_as_float(point_value);
				}
				//FIXME do other types
				else if(prop->type_urid == ui->forge.String)
				{
					p->s = strdup(lilv_node_as_string(point_value));
				}

				prop->scale_points = eina_list_append(prop->scale_points, p);

				if(prop->std.elmnt)
					elm_genlist_item_update(prop->std.elmnt);
			}

			if(point_label)
				lilv_node_free(point_label);
			if(point_value)
				lilv_node_free(point_value);
		}
			
		lilv_nodes_free(spoints);
	}

	return prop;
}

static mod_t *
_sp_ui_mod_add(sp_ui_t *ui, const char *uri, u_id_t uid, LV2_Handle inst,
	data_access_t data_access)
{
	LilvNode *uri_node = lilv_new_uri(ui->world, uri);
	if(!uri_node)
		return NULL;

	const LilvPlugin *plug = lilv_plugins_get_by_uri(ui->plugs, uri_node);
	lilv_node_free(uri_node);
	if(!plug)
		return NULL;

	const LilvNode *plugin_uri = lilv_plugin_get_uri(plug);
	const char *plugin_string = NULL;
	if(plugin_uri)
		plugin_string = lilv_node_as_string(plugin_uri);

	if(!lilv_plugin_verify(plug))
		return NULL;

	mod_t *mod = calloc(1, sizeof(mod_t));
	if(!mod)
		return NULL;

	mod->pset_label = strdup("unnamed"); // TODO check

	mod->ui = ui;
	mod->uid = uid;
	mod->plug = plug;
	mod->num_ports = lilv_plugin_get_num_ports(plug);
	mod->subject = ui->driver->map->map(ui->driver->map->handle, plugin_string);

	mod->name = _mod_get_name(mod);

	// populate port_map
	mod->port_map.handle = mod;
	mod->port_map.port_index = _port_index;

	// populate log
	mod->log.handle = mod;
	mod->log.printf = _log_printf;
	mod->log.vprintf = _log_vprintf;

	// populate port_subscribe
	mod->port_subscribe.handle = mod;
	mod->port_subscribe.subscribe = _port_subscribe;
	mod->port_subscribe.unsubscribe = _port_unsubscribe;

	// populate zero-writer
	mod->zero_writer.handle = mod;
	mod->zero_writer.request = _zero_writer_request;
	mod->zero_writer.advance = _zero_writer_advance;

	// populate external_ui_host
	mod->kx.host.ui_closed = _kx_ui_closed;
	mod->kx.host.plugin_human_id = mod->name;

	// populate extension_data
	mod->ext_data.data_access = data_access;

	// populate port_event for StdUI
	mod->std.descriptor.port_event = _std_port_event;

	// populate x11 resize
	mod->x11.host_resize_iface.ui_resize = _ui_host_resize;
	mod->x11.host_resize_iface.handle = mod;

	// populate options
	mod->opts.options[0].context = LV2_OPTIONS_INSTANCE;
	mod->opts.options[0].subject = 0;
	mod->opts.options[0].key = ui->regs.ui.window_title.urid;
	mod->opts.options[0].size = 8;
	mod->opts.options[0].type = ui->forge.String;
	mod->opts.options[0].value = mod->name;

	//TODO provide sample rate, buffer size, etc

	mod->opts.options[1].key = 0; // sentinel
	mod->opts.options[1].value = NULL; // sentinel

	// populate UI feature list
	int nfeatures = 0;
	mod->feature_list[nfeatures].URI = LV2_URID__map;
	mod->feature_list[nfeatures++].data = ui->driver->map;

	mod->feature_list[nfeatures].URI = LV2_URID__unmap;
	mod->feature_list[nfeatures++].data = ui->driver->unmap;

	mod->feature_list[nfeatures].URI = LV2_UI__parent;
	mod->feature_list[nfeatures++].data = NULL; // will be filled in before instantiation

	mod->feature_list[nfeatures].URI = LV2_LOG__log;
	mod->feature_list[nfeatures++].data = &mod->log;

	mod->feature_list[nfeatures].URI = LV2_UI__portMap;
	mod->feature_list[nfeatures++].data = &mod->port_map;

	mod->feature_list[nfeatures].URI = LV2_UI__portSubscribe;
	mod->feature_list[nfeatures++].data = &mod->port_subscribe;

	mod->feature_list[nfeatures].URI = LV2_UI__idleInterface;
	mod->feature_list[nfeatures++].data = NULL;

	mod->feature_list[nfeatures].URI = LV2_EXTERNAL_UI__Host;
	mod->feature_list[nfeatures++].data = &mod->kx.host;

	mod->feature_list[nfeatures].URI = LV2_EXTERNAL_UI__Widget;
	mod->feature_list[nfeatures++].data = NULL;

	mod->feature_list[nfeatures].URI = LV2_EXTERNAL_UI_DEPRECATED_URI;
	mod->feature_list[nfeatures++].data = &mod->kx.host;

	mod->feature_list[nfeatures].URI = LV2_UI__resize;
	mod->feature_list[nfeatures++].data = &mod->x11.host_resize_iface;

	mod->feature_list[nfeatures].URI = LV2_OPTIONS__options;
	mod->feature_list[nfeatures++].data = mod->opts.options;

	if(data_access)
	{
		mod->feature_list[nfeatures].URI = LV2_DATA_ACCESS_URI;
		mod->feature_list[nfeatures++].data = &mod->ext_data;
	}

	if(ui->driver->instance_access && inst)
	{
		mod->feature_list[nfeatures].URI = LV2_INSTANCE_ACCESS_URI;
		mod->feature_list[nfeatures++].data = inst;
	}

	//FIXME do we want to support this? it's marked as DEPRECATED in LV2 spec
	{
		mod->feature_list[nfeatures].URI = LV2_UI_PREFIX"makeSONameResident";
		mod->feature_list[nfeatures++].data = NULL;
	}
	{
		mod->feature_list[nfeatures].URI = LV2_UI_PREFIX"makeResident";
		mod->feature_list[nfeatures++].data = NULL;
	}

	mod->feature_list[nfeatures].URI = SYNTHPOD_WORLD;
	mod->feature_list[nfeatures++].data = ui->world;

	mod->feature_list[nfeatures].URI = ZERO_WRITER__schedule;
	mod->feature_list[nfeatures++].data = &mod->zero_writer;

	mod->feature_list[nfeatures].URI = LV2_URI_MAP_URI;
	mod->feature_list[nfeatures++].data = &ui->uri_to_id;

	assert(nfeatures <= NUM_UI_FEATURES);

	for(int i=0; i<nfeatures; i++)
		mod->features[i] = &mod->feature_list[i];
	mod->features[nfeatures] = NULL; // sentinel

	// discover system modules
	if(!strcmp(uri, SYNTHPOD_PREFIX"source"))
		mod->system.source = 1;
	else if(!strcmp(uri, SYNTHPOD_PREFIX"sink"))
		mod->system.sink = 1;

	mod->ports = calloc(mod->num_ports, sizeof(port_t));
	if(mod->ports)
	{
		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *tar = &mod->ports[i];
			const LilvPort *port = lilv_plugin_get_port_by_index(plug, i);

			if(port)
				_sp_ui_mod_port_add(ui, mod, i, tar, port);
		}
	}

	// look for patch:writable's
	mod->writs = lilv_world_find_nodes(ui->world,
		plugin_uri, ui->regs.patch.writable.node, NULL);
	if(mod->writs)
	{
		LILV_FOREACH(nodes, i, mod->writs)
		{
			const LilvNode *writable = lilv_nodes_get(mod->writs, i);
			property_t *prop = _sp_ui_mod_static_prop_add(ui, mod, writable, 1);

			if(prop)
				mod->static_properties = eina_list_sorted_insert(mod->static_properties, _urid_cmp, prop);
		}
	}

	// look for patch:readable's
	mod->reads = lilv_world_find_nodes(ui->world,
		plugin_uri, ui->regs.patch.readable.node, NULL);
	if(mod->reads)
	{
		LILV_FOREACH(nodes, i, mod->reads)
		{
			const LilvNode *readable = lilv_nodes_get(mod->reads, i);
			property_t *prop = _sp_ui_mod_static_prop_add(ui, mod, readable, 0);

			if(prop)
				mod->static_properties = eina_list_sorted_insert(mod->static_properties, _urid_cmp, prop);
		}
	}

	// ui
	mod->all_uis = lilv_plugin_get_uis(mod->plug);
	if(mod->all_uis)
	{
		LILV_FOREACH(uis, ptr, mod->all_uis)
		{
			const LilvUI *lui = lilv_uis_get(mod->all_uis, ptr);
			if(!lui)
				continue;
			const LilvNode *ui_uri_node = lilv_ui_get_uri(lui);
			if(!ui_uri_node)
				continue;

			// check for missing features
			int missing_required_feature = 0;
			LilvNodes *required_features = lilv_world_find_nodes(ui->world,
				ui_uri_node, ui->regs.core.required_feature.node, NULL);
			if(required_features)
			{
				LILV_FOREACH(nodes, i, required_features)
				{
					const LilvNode* required_feature = lilv_nodes_get(required_features, i);
					const char *required_feature_uri = lilv_node_as_uri(required_feature);
					missing_required_feature = 1;

					for(int f=0; f<nfeatures; f++)
					{
						if(!strcmp(mod->feature_list[f].URI, required_feature_uri))
						{
							missing_required_feature = 0;
							break;
						}
					}

					if(missing_required_feature)
					{
						fprintf(stderr, "UI '%s' requires non-supported feature: %s\n",
							lilv_node_as_uri(ui_uri_node), required_feature_uri);
						break;
					}
				}
				lilv_nodes_free(required_features);
			}
			if(missing_required_feature)
				continue; // plugin requires a feature we do not support

			mod_ui_t *mod_ui = calloc(1, sizeof(mod_ui_t));
			if(!mod_ui)
				continue;

			mod->mod_uis = eina_list_append(mod->mod_uis, mod_ui);
			mod_ui->ui = lui;
			mod_ui->urid = ui->driver->map->map(ui->driver->map->handle,
				lilv_node_as_string(lilv_ui_get_uri(lui)));
			mod_ui->type = MOD_UI_TYPE_UNSUPPORTED;

			// test for EoUI
			if(mod_ui->type == MOD_UI_TYPE_UNSUPPORTED)
			{
				if(lilv_ui_is_a(lui, ui->regs.ui.eo.node))
				{
					//printf("has EoUI\n");
					mod_ui->type = MOD_UI_TYPE_EO;
				}
			}

			// test for X11UI
			if(mod_ui->type == MOD_UI_TYPE_UNSUPPORTED)
			{
				if(lilv_ui_is_a(lui, ui->regs.ui.x11.node))
				{
					//printf("has x11-ui\n");
					mod_ui->type = MOD_UI_TYPE_X11;
				}
			}

			// test for show UI
			if(mod_ui->type == MOD_UI_TYPE_UNSUPPORTED)
			{ //TODO add to reg_t
				bool has_idle_iface = lilv_world_ask(ui->world, ui_uri_node,
					ui->regs.core.extension_data.node, ui->regs.ui.idle_interface.node);
				bool has_show_iface = lilv_world_ask(ui->world, ui_uri_node,
					ui->regs.core.extension_data.node, ui->regs.ui.show_interface.node);

				if(has_show_iface)
				{
					//printf("has show UI\n");
					mod_ui->type = MOD_UI_TYPE_SHOW;
				}
			}

			// test for kxstudio kx_ui
			if(mod_ui->type == MOD_UI_TYPE_UNSUPPORTED)
			{
				if(  lilv_ui_is_a(lui, ui->regs.ui.kx_widget.node)
					|| lilv_ui_is_a(lui, ui->regs.ui.external.node) )
				{
					//printf("has kx-ui\n");
					mod_ui->type = MOD_UI_TYPE_KX;
				}
			}
		}
	}

	Eina_List *l;
	mod_ui_t *mod_ui;
	EINA_LIST_FOREACH(mod->mod_uis, l, mod_ui)
	{
		if(mod_ui->ui && mod_ui->type)
			mod_ui->descriptor = _ui_dlopen(mod_ui->ui, &mod_ui->lib);
	}

	if(mod->system.source || mod->system.sink)
		mod->col = 0; // reserved color for system ports
	else
		mod->col = _sp_ui_next_col(ui);

	// load presets
	mod->presets = lilv_plugin_get_related(mod->plug, ui->regs.pset.preset.node);

	// load preset banks
	mod->banks = NULL;
	LILV_FOREACH(nodes, i, mod->presets)
	{
		const LilvNode* preset = lilv_nodes_get(mod->presets, i);
		if(!preset)
			continue;

		lilv_world_load_resource(ui->world, preset);

		LilvNodes *preset_banks = lilv_world_find_nodes(ui->world,
			preset, ui->regs.pset.preset_bank.node, NULL);

		LILV_FOREACH(nodes, j, preset_banks)
		{
			const LilvNode *bank = lilv_nodes_get(preset_banks, j);
			if(!bank)
				continue;

			LilvNode *bank_dup = eina_list_search_unsorted(mod->banks, _bank_cmp, bank);
			if(!bank_dup)
			{
				bank_dup = lilv_node_duplicate(bank); //TODO
				mod->banks = eina_list_append(mod->banks, bank_dup);
			}
		}
		lilv_nodes_free(preset_banks);
		
		//lilv_world_unload_resource(ui->world, preset); //FIXME
	}

	// request selected state
	_ui_mod_selected_request(mod);
	_ui_mod_visible_request(mod);
	_ui_mod_embedded_request(mod);

	//TODO save visibility in synthpod state?
	//if(!mod->eo.ui && mod->kx.ui)
	//	_kx_ui_show(mod);

	return mod;
}

static void
_sp_ui_mod_del(sp_ui_t *ui, mod_t *mod)
{
	ui->colors_vec[mod->col] -= 1; // decrease color count

	for(unsigned p=0; p<mod->num_ports; p++)
	{
		port_t *port = &mod->ports[p];

		if(port->points)
			lilv_scale_points_free(port->points);

		if(port->unit)
			free(port->unit);

		if(port->group)
			lilv_node_free(port->group);
	}
	if(mod->ports)
		free(mod->ports);

	LilvNode *bank;
	EINA_LIST_FREE(mod->banks, bank)
		lilv_node_free(bank);

	if(mod->presets)
		lilv_nodes_free(mod->presets);

	mod_ui_t *mod_ui;
	EINA_LIST_FREE(mod->mod_uis, mod_ui)
	{
		if(mod_ui->ui)
		{
			eina_module_unload(mod_ui->lib);
			eina_module_free(mod_ui->lib);
		}
		free(mod_ui);
	}

	if(mod->std.elmnt == ui->sink_itm)
		ui->sink_itm = 0;

	if(mod->static_properties)
	{
		property_t *prop;
		EINA_LIST_FREE(mod->static_properties, prop)
			_property_free(prop);
	}
	if(mod->dynamic_properties)
	{
		property_t *prop;
		EINA_LIST_FREE(mod->dynamic_properties, prop)
			_property_free(prop);
	}
	if(mod->writs)
		lilv_nodes_free(mod->writs);
	if(mod->reads)
		lilv_nodes_free(mod->reads);

	if(mod->all_uis)
		lilv_uis_free(mod->all_uis);

	if(mod->name)
		free(mod->name);

	if(mod->pset_label)
		free(mod->pset_label);

	if(mod->groups)
		eina_hash_free(mod->groups);

	free(mod);
}

#define INFO_PRE "<color=#bbb font=Mono>"
#define INFO_POST "</color>"

static char * 
_pluglist_label_get(void *data, Evas_Object *obj, const char *part)
{
	const plug_info_t *info = data;
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	if(!ui || !info)
		return NULL;

	switch(info->type)
	{
		case PLUG_INFO_TYPE_NAME:
		{
			LilvNode *node = lilv_plugin_get_name(info->plug);

			char *str = NULL;
			asprintf(&str, "%s", node
				? lilv_node_as_string(node)
				: "-");
			if(node)
				lilv_node_free(node);

			return str;
		}
		case PLUG_INFO_TYPE_URI:
		{
			const LilvNode *node = lilv_plugin_get_uri(info->plug);

			char *str = NULL;
			asprintf(&str, INFO_PRE"URI     "INFO_POST" %s", node
				? lilv_node_as_uri(node)
				: "-");

			return str;
		}
		case PLUG_INFO_TYPE_VERSION:
		{
			LilvNodes *nodes = lilv_plugin_get_value(info->plug,
				ui->regs.core.minor_version.node);
			LilvNode *node = nodes
				? lilv_nodes_get_first(nodes) //FIXME delete?
				: NULL;
			LilvNodes *nodes2 = lilv_plugin_get_value(info->plug,
				ui->regs.core.micro_version.node);
			LilvNode *node2 = nodes2
				? lilv_nodes_get_first(nodes2) //FIXME delete?
				: NULL;

			char *str = NULL;
			if(node && node2)
				asprintf(&str, INFO_PRE"Version "INFO_POST" 0.%i.%i",
					lilv_node_as_int(node), lilv_node_as_int(node2));
			else
				asprintf(&str, INFO_PRE"Version "INFO_POST" -");
			if(nodes)
				lilv_nodes_free(nodes);
			if(nodes2)
				lilv_nodes_free(nodes2);

			return str;
		}
		case PLUG_INFO_TYPE_LICENSE:
		{
			LilvNodes *nodes = lilv_plugin_get_value(info->plug,
				ui->regs.doap.license.node);
			LilvNode *node = nodes
				? lilv_nodes_get_first(nodes) //FIXME delete?
				: NULL;

			char *str = NULL;
			asprintf(&str, INFO_PRE"License "INFO_POST" %s", node
				? lilv_node_as_uri(node)
				: "-");
			if(nodes)
				lilv_nodes_free(nodes);

			return str;
		}
		case PLUG_INFO_TYPE_BUNDLE_URI:
		{
			const LilvNode *node = lilv_plugin_get_bundle_uri(info->plug);

			char *str = NULL;
			asprintf(&str, INFO_PRE"Bundle  "INFO_POST" %s", node
				? lilv_node_as_uri(node)
				: "-");

			return str;
		}
		case PLUG_INFO_TYPE_PROJECT:
		{
			LilvNode *node = lilv_plugin_get_project(info->plug);

			char *str = NULL;
			asprintf(&str, INFO_PRE"Project "INFO_POST" %s", node
				? lilv_node_as_string(node)
				: "-");
			if(node)
				lilv_node_free(node);

			return str;
		}
		case PLUG_INFO_TYPE_AUTHOR_NAME:
		{
			LilvNode *node = lilv_plugin_get_author_name(info->plug);

			char *str = NULL;
			asprintf(&str, INFO_PRE"Author  "INFO_POST" %s", node
				? lilv_node_as_string(node)
				: "-");
			if(node)
				lilv_node_free(node);

			return str;
		}
		case PLUG_INFO_TYPE_AUTHOR_EMAIL:
		{
			LilvNode *node = lilv_plugin_get_author_email(info->plug);

			char *str = NULL;
			asprintf(&str, INFO_PRE"Email   "INFO_POST" %s", node
				? lilv_node_as_string(node)
				: "-");
			if(node)
				lilv_node_free(node);

			return str;
		}
		case PLUG_INFO_TYPE_AUTHOR_HOMEPAGE:
		{
			LilvNode *node = lilv_plugin_get_author_homepage(info->plug);

			char *str = NULL;
			asprintf(&str, INFO_PRE"Homepage"INFO_POST" %s", node
				? lilv_node_as_string(node)
				: "-");
			if(node)
				lilv_node_free(node);

			return str;
		}
		case PLUG_INFO_TYPE_COMMENT:
		{
			LilvNodes *nodes = lilv_plugin_get_value(info->plug,
				ui->regs.rdfs.comment.node);
			LilvNode *node = nodes
				? lilv_nodes_get_first(nodes) //FIXME delete?
				: NULL;

			char *str = NULL;
			asprintf(&str, INFO_PRE"Comment "INFO_POST" %s", node
				? lilv_node_as_string(node)
				: "-");
			if(nodes)
				lilv_nodes_free(nodes);

			return str;
		}
		default:
			return NULL;
	}
}

static void
_pluglist_del(void *data, Evas_Object *obj)
{
	plug_info_t *info = data;

	if(info)
		free(info);
}

static void
_pluglist_activated(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;
	plug_info_t *info = elm_object_item_data_get(itm);
	if(!info)
		return;

	const LilvNode *uri_node = lilv_plugin_get_uri(info->plug);
	if(!uri_node)
		return;
	const char *uri_str = lilv_node_as_string(uri_node);

	size_t size = sizeof(transmit_module_add_t)
		+ lv2_atom_pad_size(strlen(uri_str) + 1);
	transmit_module_add_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_add_fill(&ui->regs, &ui->forge, trans, size, 0, uri_str,
			NULL, NULL);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_pluglist_expanded(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;
	plug_info_t *info = elm_object_item_data_get(itm);
	if(!info)
		return;

	plug_info_t *child;
	Elm_Object_Item *elmnt;

	for(int t=1; t<PLUG_INFO_TYPE_MAX; t++)
	{
		child = calloc(1, sizeof(plug_info_t));
		if(child)
		{
			//TODO check whether entry exists before adding
			child->type = t;
			child->plug = info->plug;
			Elm_Genlist_Item_Class *class = ui->plugitc; //FIXME
			elmnt = elm_genlist_item_append(ui->pluglist, class,
				child, itm, ELM_GENLIST_ITEM_NONE, NULL, NULL);
			if(elmnt)
				elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_NONE);
		}
	}
}

static void
_pluglist_contracted(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;

	// clear items
	elm_genlist_item_subitems_clear(itm);
}

static void
_list_expand_request(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;

	elm_genlist_item_expanded_set(itm, EINA_TRUE);
}

static void
_list_contract_request(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;

	elm_genlist_item_expanded_set(itm, EINA_FALSE);
}

static void
_patches_update(sp_ui_t *ui)
{
	if(!ui->modlist)
		return;

	int count [PORT_DIRECTION_NUM][PORT_TYPE_NUM];
	// clear counters
	memset(&count, 0, PORT_DIRECTION_NUM*PORT_TYPE_NUM*sizeof(int));

	// count input|output ports per type
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(ui->modlist);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != ui->listitc)
			continue; // ignore port items

		mod_t *mod = elm_object_item_data_get(itm);
		if(!mod || !mod->selected)
			continue; // ignore unselected mods

		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];
			if(!port->selected)
				continue; // ignore unselected ports

			if(  (port->type == PORT_TYPE_ATOM)
				&& (ui->matrix_atom_type != PORT_ATOM_TYPE_ALL)
				&& !(port->atom_type & ui->matrix_atom_type))
				continue;

			count[port->direction][port->type] += 1;
		}
	}

	// set dimension of patchers
	if(ui->matrix)
	{
		patcher_object_dimension_set(ui->matrix, 
			count[PORT_DIRECTION_OUTPUT][ui->matrix_type], // sources
			count[PORT_DIRECTION_INPUT][ui->matrix_type]); // sinks
	}

	// clear counters
	memset(&count, 0, PORT_DIRECTION_NUM*PORT_TYPE_NUM*sizeof(int));

	// populate patchers
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(ui->modlist);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != ui->listitc)
			continue; // ignore port items

		mod_t *mod = elm_object_item_data_get(itm);
		if(!mod || !mod->selected)
			continue; // ignore unselected mods

		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];
			if(!port->selected)
				continue; // ignore unselected ports
			if(port->type != ui->matrix_type)
				continue; // ignore unselected port types

			if(  (port->type == PORT_TYPE_ATOM)
				&& (ui->matrix_atom_type != PORT_ATOM_TYPE_ALL)
				&& !(port->atom_type & ui->matrix_atom_type))
				continue; // ignore unwanted atom types

			LilvNode *name_node = lilv_port_get_name(mod->plug, port->tar);
			const char *name_str = NULL;
			if(name_node)
				name_str = lilv_node_as_string(name_node);

			if(port->direction == PORT_DIRECTION_OUTPUT) // source
			{
				if(ui->matrix)
				{
					patcher_object_source_data_set(ui->matrix,
						count[port->direction][port->type], port);
					patcher_object_source_color_set(ui->matrix,
						count[port->direction][port->type], mod->col);
					patcher_object_source_label_set(ui->matrix,
						count[port->direction][port->type], name_str);
				}
			}
			else // sink
			{
				if(ui->matrix)
				{
					patcher_object_sink_data_set(ui->matrix,
						count[port->direction][port->type], port);
					patcher_object_sink_color_set(ui->matrix,
						count[port->direction][port->type], mod->col);
					patcher_object_sink_label_set(ui->matrix,
						count[port->direction][port->type], name_str);
				}
			}

			if(name_node)
				lilv_node_free(name_node);

			count[port->direction][port->type] += 1;
		}
	}

	if(ui->matrix)
		patcher_object_realize(ui->matrix);
}

static Eina_Bool
_groups_foreach(const Eina_Hash *hash, const void *key, void *data, void *fdata)
{
	Elm_Object_Item *itm = data;
	sp_ui_t *ui = fdata;
	Elm_Object_Item *elmnt;
	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);

	if(class == ui->grpitc) // is group
	{
		group_t *group = elm_object_item_data_get(itm);
		mod_t *mod = group->mod;
		
		if(group->type == GROUP_TYPE_PORT)
		{
			Eina_List *l;
			port_t *port;
			EINA_LIST_FOREACH(group->children, l, port)
			{
				elmnt = elm_genlist_item_sorted_insert(mod->std.list, ui->stditc, port, itm,
					ELM_GENLIST_ITEM_NONE, _stditc_cmp, NULL, NULL);
				if(elmnt)
				{
					_ui_port_tooltip_add(ui, elmnt, port);
					elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_NONE);
				}
			}
		}
		else if(group->type == GROUP_TYPE_PROPERTY)
		{
			Eina_List *l;
			property_t *prop;
			EINA_LIST_FOREACH(group->children, l, prop)
			{
				elmnt = elm_genlist_item_sorted_insert(mod->std.list, ui->propitc, prop, itm,
					ELM_GENLIST_ITEM_NONE, _propitc_cmp, NULL, NULL);
				if(elmnt)
				{
					int select_mode = prop->editable
						? ( (prop->type_urid == ui->forge.String) || (prop->type_urid == ui->forge.URI)
							? ELM_OBJECT_SELECT_MODE_DEFAULT
							: ELM_OBJECT_SELECT_MODE_NONE)
						: ELM_OBJECT_SELECT_MODE_NONE;
					elm_genlist_item_select_mode_set(elmnt, select_mode);
					_ui_property_tooltip_add(ui, elmnt, prop);
					prop->std.elmnt = elmnt;
				}
			}
		}
	}
	else
	{
		printf("is not a group, expanding\n"); //FIXME not needed
		elm_genlist_item_expanded_set(itm, EINA_TRUE);
	}

	return EINA_TRUE;
}

static int
_preset_label_cmp(mod_t *mod, const LilvNode *pset1, const LilvNode *pset2)
{
	if(!pset1 || !pset2 || !mod)
		return 1;

	sp_ui_t *ui = mod->ui;
	LilvNode *lbl1 = lilv_world_get(ui->world, pset1, ui->regs.rdfs.label.node, NULL);
	if(!lbl1)
		return 1;

	LilvNode *lbl2 = lilv_world_get(ui->world, pset2, ui->regs.rdfs.label.node, NULL);
	if(!lbl2)
	{
		lilv_node_free(lbl1);
		return 1;
	}

	const char *uri1 = lilv_node_as_string(lbl1);
	const char *uri2 = lilv_node_as_string(lbl2);

	int res = uri1 && uri2
		? strcasecmp(uri1, uri2)
		: 1;

	lilv_node_free(lbl1);
	lilv_node_free(lbl2);

	return res;
}

static int
_itmitc_cmp(const void *data1, const void *data2)
{
	const Elm_Object_Item *itm1 = data1;
	const Elm_Object_Item *itm2 = data2;
	if(!itm1 || !itm2)
		return 1;

	const Elm_Object_Item *par2 = elm_genlist_item_parent_get(itm1); // psetitc
	if(!par2)
		return 1;

	const Elm_Genlist_Item_Class *class1 = elm_genlist_item_item_class_get(itm1);
	const Elm_Genlist_Item_Class *class2 = elm_genlist_item_item_class_get(itm2);
	if(class1 != class2)
		return -1; // banks before presets

	const LilvNode *pset1 = elm_object_item_data_get(itm1);
	const LilvNode *pset2 = elm_object_item_data_get(itm2);
	mod_t *mod = elm_object_item_data_get(par2);

	return _preset_label_cmp(mod, pset1, pset2);
}

static int
_bnkitc_cmp(const void *data1, const void *data2)
{
	const Elm_Object_Item *itm1 = data1;
	const Elm_Object_Item *itm2 = data2;
	if(!itm1 || !itm2)
		return 1;

	const Elm_Object_Item *par1 = elm_genlist_item_parent_get(itm1); // bnkitc
	if(!par1)
		return 1;

	const Elm_Object_Item *par2 = elm_genlist_item_parent_get(par1); // psetitc
	if(!par2)
		return 1;

	const LilvNode *pset1 = elm_object_item_data_get(itm1);
	const LilvNode *pset2 = elm_object_item_data_get(itm2);
	mod_t *mod = elm_object_item_data_get(par2);

	return _preset_label_cmp(mod, pset1, pset2);
}

static void
_modgrid_expanded(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;
	Elm_Object_Item *elmnt;

	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);

	if(class == ui->psetitc) // is presets item
	{
		mod_t *mod = elm_object_item_data_get(itm);

		if(mod->banks)
		{
			Eina_List *l;
			LilvNode *bank;
			EINA_LIST_FOREACH(mod->banks, l, bank)
			{
				elmnt = elm_genlist_item_sorted_insert(mod->std.list, ui->psetbnkitc, bank, itm,
					ELM_GENLIST_ITEM_TREE, _itmitc_cmp, NULL, NULL);
				elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_DEFAULT);
			}
		}

		LILV_FOREACH(nodes, i, mod->presets)
		{
			const LilvNode* preset = lilv_nodes_get(mod->presets, i);
			if(!preset)
				continue;

			LilvNode *bank = lilv_world_get(ui->world, preset,
				ui->regs.pset.preset_bank.node, NULL);
			if(bank)
			{
				lilv_node_free(bank);
				continue; // ignore presets which are part of a bank
			}

			elmnt = elm_genlist_item_sorted_insert(mod->std.list, ui->psetitmitc, preset, itm,
				ELM_GENLIST_ITEM_NONE, _itmitc_cmp, NULL, NULL);
			elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_DEFAULT);
		}

		elmnt = elm_genlist_item_append(mod->std.list, ui->psetsaveitc, mod, itm,
			ELM_GENLIST_ITEM_NONE, NULL, NULL);
		elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_DEFAULT);
	}
	else if(class == ui->psetbnkitc) // is preset bank item
	{
		LilvNode *bank = elm_object_item_data_get(itm);
		Elm_Object_Item *parent = elm_genlist_item_parent_get(itm); // psetitc
		mod_t *mod = elm_object_item_data_get(parent);

		LilvNodes *presets = lilv_world_find_nodes(ui->world, NULL,
			ui->regs.pset.preset_bank.node, bank);
		LILV_FOREACH(nodes, i, presets)
		{
			const LilvNode *preset = lilv_nodes_get(presets, i);

			// lookup and reference corresponding preset in mod->presets
			const LilvNode *ref = NULL;
			LILV_FOREACH(nodes, j, mod->presets)
			{
				const LilvNode *_preset = lilv_nodes_get(mod->presets, j);
				if(lilv_node_equals(preset, _preset))
				{
					ref = _preset;
					break;
				}
			}

			if(ref)
			{
				elmnt = elm_genlist_item_sorted_insert(mod->std.list, ui->psetitmitc, ref, itm,
					ELM_GENLIST_ITEM_NONE, _bnkitc_cmp, NULL, NULL);
				elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_DEFAULT);
			}
		}
		lilv_nodes_free(presets);
	}
}

static void
_modgrid_contracted(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	// clear items
	elm_genlist_item_subitems_clear(itm);

	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);
	if(class == ui->moditc)
	{
		mod_t *mod = elm_object_item_data_get(itm);
		eina_hash_free(mod->groups);
		mod->groups = NULL;
	}
}

static void
_modlist_activated(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);

	if(class == ui->listitc)
	{
		mod_t *mod = elm_object_item_data_get(itm);
		printf("_modlist_activated: %p %p %u\n", ui, mod, mod->uid);

		if(mod->std.grid)
		{
			elm_object_item_del(mod->std.grid);
			_mod_embedded_set(mod, 0);
		}
		else
		{
			mod->std.grid = elm_gengrid_item_append(ui->modgrid, ui->griditc, mod,
				NULL, NULL);
			_mod_embedded_set(mod, 1);
		}
	}
}

static void
_modgrid_activated(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);

	if(class == ui->psetitmitc) // is presets item
	{
		// get parent item
		Elm_Object_Item *parent = elm_genlist_item_parent_get(itm); // psetbnkitc || psetitc
		if(!parent)
			return;

		const Elm_Genlist_Item_Class *parent_class = elm_genlist_item_item_class_get(parent);
		if(parent_class == ui->psetbnkitc)
		{
			parent = elm_genlist_item_parent_get(parent); // psetitc

			if(!parent)
				return;
		}

		mod_t *mod = elm_object_item_data_get(parent);
		if(!mod)
			return;

		const LilvNode* preset = elm_object_item_data_get(itm);
		if(!preset)
			return;

		const char *uri = lilv_node_as_uri(preset);
		if(!uri)
			return;

		// signal app
		size_t size = sizeof(transmit_module_preset_load_t)
			+ lv2_atom_pad_size(strlen(uri) + 1);
		transmit_module_preset_load_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transmit_module_preset_load_fill(&ui->regs, &ui->forge, trans, size, mod->uid, uri);
			_sp_ui_to_app_advance(ui, size);
		}

		// contract parent list item
		//evas_object_smart_callback_call(obj, "contract,request", parent);
	}

	//TODO toggle checkboxes on modules and ports
}

// only called upon user interaction
static void
_modlist_moved(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	Elm_Object_Item *first = elm_genlist_first_item_get(obj);
	Elm_Object_Item *last = elm_genlist_last_item_get(obj);

	if(!first || !last)
		return;

	// we must not move mod to top or end of list
	if(itm == first)
	{
		// promote system source to top of list
		Elm_Object_Item *source = elm_genlist_item_next_get(itm);
		if(source)
			elm_genlist_item_promote(source); // does not call _modlist_moved
	}
	else if(itm == last)
	{
		// demote system sink to end of list
		Elm_Object_Item *sink = elm_genlist_item_prev_get(itm);
		if(sink)
			elm_genlist_item_demote(sink); // does not call _modlist_moved
	}

	// get previous item
	Elm_Object_Item *prev = elm_genlist_item_prev_get(itm);
	if(!prev)
		return;

	mod_t *itm_mod = elm_object_item_data_get(itm);
	mod_t *prev_mod = elm_object_item_data_get(prev);

	if(!itm_mod || !prev_mod)
		return;

	// signal app
	size_t size = sizeof(transmit_module_move_t);
	transmit_module_move_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_move_fill(&ui->regs, &ui->forge, trans, size,
			itm_mod->uid, prev_mod->uid);
		_sp_ui_to_app_advance(ui, size);
	}

	_patches_update(ui);
}

static void
_modgrid_changed(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	int w, h;
	evas_object_geometry_get(ui->modgrid, NULL, NULL, &w, &h);

	const int iw = w / 3; //FIXME make this configurable
	const int ih = (h - 20) / 2; //FIXME make this configurable
	elm_gengrid_item_size_set(ui->modgrid, iw, ih);
}

static inline void
_mod_del_widgets(mod_t *mod)
{
	mod_ui_t *mod_ui = mod->mod_ui;

	if(mod_ui)
	{
		// close show ui
		if(mod_ui->type == MOD_UI_TYPE_SHOW)
			_show_ui_hide(mod);
		// close kx ui
		else if(mod_ui->type == MOD_UI_TYPE_KX)
			_kx_ui_hide(mod);
		// close x11 ui
		else if(mod_ui->type == MOD_UI_TYPE_X11)
			_x11_ui_hide(mod);
		else if(mod_ui->type == MOD_UI_TYPE_EO)
			_eo_ui_hide(mod);
	}
}

static inline void
_mod_del_propagate(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	size_t size = sizeof(transmit_module_del_t);
	transmit_module_del_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_del_fill(&ui->regs, &ui->forge, trans, size, mod->uid);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_mod_close_click(void *data, Evas_Object *lay, const char *emission, const char *source)
{
	mod_t *mod = data;

	_mod_del_widgets(mod);
	_mod_del_propagate(mod);
}

static void
_mod_ui_toggle_raw(mod_t *mod, mod_ui_t *mod_ui)
{
	mod->mod_ui = mod_ui;

	switch(mod_ui->type)
	{
		case MOD_UI_TYPE_EO:
			_eo_ui_show(mod);
			break;
		case MOD_UI_TYPE_SHOW:
			if(!mod_ui->show.visible)
				_show_ui_show(mod);
			break;
		case MOD_UI_TYPE_KX:
			if(!mod_ui->kx.widget)
				_kx_ui_show(mod);
			break;
		case MOD_UI_TYPE_X11:
			if(!mod_ui->x11.win)
				_x11_ui_show(mod);
			break;
	}
}

static void
_mod_ui_toggle_chosen(void *data, Evas_Object *obj, void *event_info)
{
	mod_ui_t *mod_ui = data;
	mod_t *mod = evas_object_data_get(obj, "module");
	sp_ui_t *ui = mod->ui;

	evas_object_hide(ui->selector);

	_mod_ui_toggle_raw(mod, mod_ui);
}

static void
_mod_ui_toggle(void *data, Evas_Object *lay, const char *emission, const char *source)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	// clear
	elm_object_content_set(ui->selector, NULL);
	evas_object_data_set(ui->selector, "module", mod);

	if(!mod->mod_ui) // show it!
	{
		Eina_List *l;
		mod_ui_t *mod_ui;
		if(eina_list_count(mod->mod_uis) == 1) // single UI
		{
			mod_ui = eina_list_data_get(mod->mod_uis);
			_mod_ui_toggle_chosen(mod_ui, ui->selector, NULL);
		}
		else if(eina_list_count(mod->mod_uis) > 1) // multiple UIs
		{
			EINA_LIST_FOREACH(mod->mod_uis, l, mod_ui)
			{
				const LilvNode *ui_uri = lilv_ui_get_uri(mod_ui->ui);
				const char *ui_uri_str = lilv_node_as_string(ui_uri);

				switch(mod_ui->type)
				{
					case MOD_UI_TYPE_EO:
					case MOD_UI_TYPE_SHOW:
					case MOD_UI_TYPE_KX:
					case MOD_UI_TYPE_X11:
						elm_popup_item_append(ui->selector, ui_uri_str, NULL, _mod_ui_toggle_chosen, mod_ui);
						break;
				}
			}
			evas_object_show(ui->selector);
		}
	}
	else // hide it!
	{
		mod_ui_t *mod_ui = mod->mod_ui;

		switch(mod_ui->type)
		{
			case MOD_UI_TYPE_EO:
				_eo_ui_hide(mod);
				break;
			case MOD_UI_TYPE_SHOW:
				if(mod_ui->show.visible)
					_show_ui_hide(mod);
				break;
			case MOD_UI_TYPE_KX:
				if(mod_ui->kx.widget)
					_kx_ui_hide(mod);
				break;
			case MOD_UI_TYPE_X11:
				if(mod_ui->x11.win)
					_x11_ui_hide(mod);
				break;
		}
	}
}

static void
_mod_link_toggle(void *data, Evas_Object *lay, const char *emission, const char *source)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	mod->selected ^= 1; // toggle
	elm_layout_signal_emit(lay, mod->selected ? "link,on" : "link,off", "");

	_patches_update(ui);

	// signal app
	size_t size = sizeof(transmit_module_selected_t);
	transmit_module_selected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_selected_fill(&ui->regs, &ui->forge, trans, size, mod->uid, mod->selected);
		_sp_ui_to_app_advance(ui, size);
	}
}

static Evas_Object *
_modlist_content_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;

	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	Evas_Object *lay = elm_layout_add(obj);
	if(lay)
	{
		elm_layout_file_set(lay, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/modlist/module");
		evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(lay);

		elm_layout_text_set(lay, "elm.text", mod->name);

		char col [7];
		sprintf(col, "col,%02i", mod->col);
		elm_layout_signal_emit(lay, col, MODLIST_UI);

		// link
		elm_layout_signal_callback_add(lay, "link,toggle", "", _mod_link_toggle, mod);
		elm_layout_signal_emit(lay, mod->selected ? "link,on" : "link,off", "");

		// close
		if(!mod->system.source && !mod->system.sink)
		{
			elm_layout_signal_callback_add(lay, "close,click", "", _mod_close_click, mod);
			elm_layout_signal_emit(lay, "close,show", "");
		}
		else
		{
			// system mods cannot be removed
			elm_layout_signal_emit(lay, "close,hide", "");
		}

		// window
		//if(mod->show.ui || mod->kx.ui || mod->eo.ui || mod->x11.ui) //TODO also check for descriptor
		if(eina_list_count(mod->mod_uis) > 0)
		{
			elm_layout_signal_callback_add(lay, "ui,toggle", "", _mod_ui_toggle, mod);
			elm_layout_signal_emit(lay, "ui,show", "");
		}
		else
		{
			elm_layout_signal_emit(lay, "ui,hide", "");
		}
	} // lay

	return lay;
}

static void
_smart_mouse_in(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	if(mod->std.list)
	{
		elm_scroller_movement_block_set(mod->std.list,
			ELM_SCROLLER_MOVEMENT_BLOCK_HORIZONTAL | ELM_SCROLLER_MOVEMENT_BLOCK_VERTICAL);
	}

	if(ui->modgrid)
	{
		elm_scroller_movement_block_set(ui->modgrid,
			ELM_SCROLLER_MOVEMENT_BLOCK_HORIZONTAL | ELM_SCROLLER_MOVEMENT_BLOCK_VERTICAL);
	}
}

static void
_smart_mouse_out(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	if(mod->std.list)
		elm_scroller_movement_block_set(mod->std.list, ELM_SCROLLER_MOVEMENT_NO_BLOCK);

	if(ui->modgrid)
		elm_scroller_movement_block_set(ui->modgrid, ELM_SCROLLER_MOVEMENT_NO_BLOCK);
}

static void
_property_path_chosen(void *data, Evas_Object *obj, void *event_info)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	const char *path = event_info;
	if(!path)
		return;

	//printf("_property_path_chosen: %s\n", path);

	size_t strsize = strlen(path) + 1;
	size_t len = sizeof(transfer_patch_set_obj_t) + lv2_atom_pad_size(strsize);

	for(unsigned index=0; index<mod->num_ports; index++)
	{
		port_t *port = &mod->ports[index];

		// only consider event ports which support patch:Message
		if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
			|| (port->direction != PORT_DIRECTION_INPUT)
			|| !port->patchable)
		{
			continue; // skip
		}

		transfer_patch_set_obj_t *trans = malloc(len);
		if(trans)
		{
			LV2_Atom *atom = _sp_transfer_patch_set_obj_fill(&ui->regs,
				&ui->forge, trans, strsize,
				mod->subject, prop->tar_urid, prop->type_urid);
			if(atom)
			{
				strcpy(LV2_ATOM_BODY(atom), path);

				_std_ui_write_function(mod, index, lv2_atom_total_size(&trans->obj.atom),
					ui->regs.port.event_transfer.urid, &trans->obj);
			}
			free(trans);
		}
	}
}

static void
_property_string_activated(void *data, Evas_Object *obj, void *event_info)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	const char *entered = elm_entry_entry_get(obj);
	if(!entered)
		return;

	//printf("_property_string_activated: %s\n", entered);

	size_t strsize = strlen(entered) + 1;
	size_t len = sizeof(transfer_patch_set_obj_t) + lv2_atom_pad_size(strsize);

	for(unsigned index=0; index<mod->num_ports; index++)
	{
		port_t *port = &mod->ports[index];

		// only consider event ports which support patch:Message
		if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
			|| (port->direction != PORT_DIRECTION_INPUT)
			|| !port->patchable)
		{
			continue; // skip
		}

		transfer_patch_set_obj_t *trans = malloc(len);
		if(trans)
		{
			LV2_Atom *atom = _sp_transfer_patch_set_obj_fill(&ui->regs,
				&ui->forge, trans, strsize,
				mod->subject, prop->tar_urid, prop->type_urid);
			if(atom) {
				strcpy(LV2_ATOM_BODY(atom), entered);

				_std_ui_write_function(mod, index, lv2_atom_total_size(&trans->obj.atom),
					ui->regs.port.event_transfer.urid, &trans->obj);
			}
			free(trans);
		}
	}
}

static void
_property_sldr_changed(void *data, Evas_Object *obj, void *event_info)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	double value = smart_slider_value_get(obj);

	size_t body_size = 0;
	if(  (prop->type_urid == ui->forge.Int)
		|| (prop->type_urid == ui->forge.Float)
		|| (prop->type_urid == ui->forge.URID) )
	{
		body_size = sizeof(int32_t);
	}
	else if(  (prop->type_urid == ui->forge.Long)
		|| (prop->type_urid == ui->forge.Double) )
	{
		body_size = sizeof(int64_t);
	}

	size_t len = sizeof(transfer_patch_set_obj_t) + lv2_atom_pad_size(body_size);

	for(unsigned index=0; index<mod->num_ports; index++)
	{
		port_t *port = &mod->ports[index];

		// only consider event ports which support patch:Message
		if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
			|| (port->direction != PORT_DIRECTION_INPUT)
			|| !port->patchable)
		{
			continue; // skip
		}

		transfer_patch_set_obj_t *trans = malloc(len);
		if(trans)
		{
			LV2_Atom *atom = _sp_transfer_patch_set_obj_fill(&ui->regs,
				&ui->forge, trans, body_size,
				mod->subject, prop->tar_urid, prop->type_urid);
			if(atom)
			{
				if(prop->type_urid == ui->forge.Int)
					((LV2_Atom_Int *)atom)->body = value;
				else if(prop->type_urid == ui->forge.Long)
					((LV2_Atom_Long *)atom)->body = value;
				else if(prop->type_urid == ui->forge.Float)
					((LV2_Atom_Float *)atom)->body = value;
				else if(prop->type_urid == ui->forge.Double)
					((LV2_Atom_Double *)atom)->body = value;
				else if(prop->type_urid == ui->forge.URID)
					((LV2_Atom_URID *)atom)->body = value;

				_std_ui_write_function(mod, index, lv2_atom_total_size(&trans->obj.atom),
					ui->regs.port.event_transfer.urid, &trans->obj);
			}
			free(trans);
		}
	}
}

static void
_property_check_changed(void *data, Evas_Object *obj, void *event_info)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	int value = smart_toggle_value_get(obj);

	size_t body_size = sizeof(int32_t);
	size_t len = sizeof(transfer_patch_set_obj_t) + lv2_atom_pad_size(body_size);

	for(unsigned index=0; index<mod->num_ports; index++)
	{
		port_t *port = &mod->ports[index];

		// only consider event ports which support patch:Message
		if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
			|| (port->direction != PORT_DIRECTION_INPUT)
			|| !port->patchable)
		{
			continue; // skip
		}

		transfer_patch_set_obj_t *trans = malloc(len);
		if(trans)
		{
			LV2_Atom *atom = _sp_transfer_patch_set_obj_fill(&ui->regs,
				&ui->forge, trans, body_size,
				mod->subject, prop->tar_urid, prop->type_urid);
			if(atom)
			{
				((LV2_Atom_Bool *)atom)->body = value;

				_std_ui_write_function(mod, index, lv2_atom_total_size(&trans->obj.atom),
					ui->regs.port.event_transfer.urid, &trans->obj);
			}
			free(trans);
		}
	}
}

static void
_property_spinner_changed(void *data, Evas_Object *obj, void *event_info)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	const char *key = NULL;
	float value = 0.f;

	if(prop->type_urid == ui->forge.String)
		key = smart_spinner_key_get(obj);
	else
		value = smart_spinner_value_get(obj);

	size_t body_size = 0;
	if(prop->type_urid == ui->forge.String)
		body_size = strlen(key) + 1;
	else if(prop->type_urid == ui->forge.Int)
		body_size = sizeof(int32_t);
	else if(prop->type_urid == ui->forge.Float)
		body_size = sizeof(float);
	else if(prop->type_urid == ui->forge.Long)
		body_size = sizeof(int64_t);
	else if(prop->type_urid == ui->forge.Double)
		body_size = sizeof(double);
	//TODO do other types
	size_t len = sizeof(transfer_patch_set_obj_t) + lv2_atom_pad_size(body_size);

	for(unsigned index=0; index<mod->num_ports; index++)
	{
		port_t *port = &mod->ports[index];

		// only consider event ports which support patch:Message
		if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
			|| (port->direction != PORT_DIRECTION_INPUT)
			|| !port->patchable)
		{
			continue; // skip
		}

		transfer_patch_set_obj_t *trans = malloc(len);
		if(trans)
		{
			LV2_Atom *atom = _sp_transfer_patch_set_obj_fill(&ui->regs,
				&ui->forge, trans, body_size,
				mod->subject, prop->tar_urid, prop->type_urid);
			if(atom)
			{
				if(prop->type_urid == ui->forge.String)
					strcpy(LV2_ATOM_BODY(atom), key);
				else if(prop->type_urid == ui->forge.Int)
					((LV2_Atom_Int *)atom)->body = value;
				else if(prop->type_urid == ui->forge.Float)
					((LV2_Atom_Float *)atom)->body = value;
				else if(prop->type_urid == ui->forge.Long)
					((LV2_Atom_Long *)atom)->body = value;
				else if(prop->type_urid == ui->forge.Double)
					((LV2_Atom_Double *)atom)->body = value;
				//TODO do other types

				_std_ui_write_function(mod, index, lv2_atom_total_size(&trans->obj.atom),
					ui->regs.port.event_transfer.urid, &trans->obj);
			}
			free(trans);
		}
	}
}

static Evas_Object *
_property_content_get(void *data, Evas_Object *obj, const char *part)
{
	property_t *prop = data;
	mod_t *mod = prop->mod;
	sp_ui_t *ui = mod->ui;

	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	if(!prop->type_urid) // type not yet set, e.g. for dynamic properties
		return NULL;

	Evas_Object *lay = elm_layout_add(obj);
	if(lay)
	{
		elm_layout_file_set(lay, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/modlist/port");
		evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(lay);

		// link
		elm_layout_signal_emit(lay, "link,hide", ""); //TODO or "link,on"

		// monitor
		elm_layout_signal_emit(lay, "monitor,hide", ""); //TODO or "monitor,on"

		char col [7];
		sprintf(col, "col,%02i", mod->col);

		// source/sink
		elm_layout_signal_emit(lay, col, MODLIST_UI);
		if(!prop->editable)
		{
			elm_layout_signal_emit(lay, "source,show", "");
			elm_layout_signal_emit(lay, "sink,hide", "");
		}
		else
		{
			elm_layout_signal_emit(lay, "source,hide", "");
			elm_layout_signal_emit(lay, "sink,show", "");
		}

		if(prop->label)
			elm_layout_text_set(lay, "elm.text", prop->label);

		Evas_Object *child = NULL;

		if(!prop->scale_points)
		{
			if(  (prop->type_urid == ui->forge.String)
				|| (prop->type_urid == ui->forge.URI) )
			{
				if(prop->editable)
				{
					child = elm_layout_add(lay);
					if(child)
					{
						elm_layout_file_set(child, SYNTHPOD_DATA_DIR"/synthpod.edj",
							"/synthpod/entry/theme");
						elm_layout_signal_emit(child, col, "/synthpod/entry/ui");

						prop->std.entry = elm_entry_add(child);
						if(prop->std.entry)
						{
							elm_entry_single_line_set(prop->std.entry, EINA_TRUE);
							evas_object_smart_callback_add(prop->std.entry, "activated",
								_property_string_activated, prop);
							evas_object_show(prop->std.entry);
							elm_layout_content_set(child, "elm.swallow.content", prop->std.entry);
						}
					}
				}
				else // !editable
				{
					child = elm_label_add(lay);
					if(child)
						evas_object_size_hint_align_set(child, 0.f, EVAS_HINT_FILL);
				}
			}
			else if(prop->type_urid == ui->forge.Path)
			{
				if(prop->editable)
				{
					child = elm_fileselector_button_add(lay);
					if(child)
					{
						elm_fileselector_button_inwin_mode_set(child, EINA_FALSE);
						elm_fileselector_button_window_title_set(child, "Select file");
						elm_fileselector_is_save_set(child, EINA_TRUE);
						elm_object_text_set(child, "Select file");
						evas_object_smart_callback_add(child, "file,chosen",
							_property_path_chosen, prop);
						//TODO MIME type
					}
				}
				else // !editable
				{
					child = elm_label_add(lay);
					if(child)
						evas_object_size_hint_align_set(child, 0.f, EVAS_HINT_FILL);
				}
			}
			else if( (prop->type_urid == ui->forge.Int)
				|| (prop->type_urid == ui->forge.URID)
				|| (prop->type_urid == ui->forge.Long)
				|| (prop->type_urid == ui->forge.Float)
				|| (prop->type_urid == ui->forge.Double) )
			{
				child = smart_slider_add(evas_object_evas_get(lay));
				if(child)
				{
					int integer = (prop->type_urid == ui->forge.Int)
						|| (prop->type_urid == ui->forge.URID)
						|| (prop->type_urid == ui->forge.Long);
					double min = prop->minimum;
					double max = prop->maximum;
					double dflt = prop->minimum; //FIXME

					smart_slider_range_set(child, min, max, dflt);
					smart_slider_color_set(child, mod->col);
					smart_slider_integer_set(child, integer);
					//smart_slider_logarithmic_set(child, logarithmic); //TODO
					smart_slider_format_set(child, integer ? "%.0f %s" : "%.4f %s"); //TODO handle MIDI notes
					smart_slider_disabled_set(child, !prop->editable);
					if(prop->unit)
						smart_slider_unit_set(child, prop->unit);
					if(prop->editable)
						evas_object_smart_callback_add(child, "changed", _property_sldr_changed, prop);
					evas_object_smart_callback_add(child, "cat,in", _smart_mouse_in, mod);
					evas_object_smart_callback_add(child, "cat,out", _smart_mouse_out, mod);
				}
			}
			else if(prop->type_urid == ui->forge.Bool)
			{
				child = smart_toggle_add(evas_object_evas_get(lay));
				if(child)
				{
					smart_toggle_color_set(child, mod->col);
					smart_toggle_disabled_set(child, !prop->editable);
					if(prop->editable)
						evas_object_smart_callback_add(child, "changed", _property_check_changed, prop);
					evas_object_smart_callback_add(child, "cat,in", _smart_mouse_in, mod);
					evas_object_smart_callback_add(child, "cat,out", _smart_mouse_out, mod);
				}
			}
		}
		else // scale_points
		{
			child = smart_spinner_add(evas_object_evas_get(lay));
			if(child)
			{
				smart_spinner_color_set(child, mod->col);
				smart_spinner_disabled_set(child, !prop->editable);
				Eina_List *l;
				point_t *p;
				EINA_LIST_FOREACH(prop->scale_points, l, p)
				{
					if(prop->type_urid == ui->forge.String)
						smart_spinner_key_add(child, p->s, p->label);
					else
						smart_spinner_value_add(child, *p->d, p->label);
				}
				if(prop->editable)
					evas_object_smart_callback_add(child, "changed", _property_spinner_changed, prop);
				evas_object_smart_callback_add(child, "cat,in", _smart_mouse_in, mod);
				evas_object_smart_callback_add(child, "cat,out", _smart_mouse_out, mod);
			}
		}

		// send patch:Get
		size_t len = sizeof(transfer_patch_get_t);
		for(unsigned index=0; index<mod->num_ports; index++)
		{
			port_t *port = &mod->ports[index];

			// only consider event ports which support patch:Message
			if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
				|| (port->direction != PORT_DIRECTION_INPUT)
				|| !port->patchable)
			{
				continue; // skip
			}

			transfer_patch_get_t *trans = _sp_ui_to_app_request(ui, len);
			if(trans)
			{
				_sp_transfer_patch_get_fill(&ui->regs,
					&ui->forge, trans, mod->uid, index,
					mod->subject, prop->tar_urid);
				_sp_ui_to_app_advance(ui, len);
			}
		}

		if(child)
		{
			evas_object_show(child);
			elm_layout_content_set(lay, "elm.swallow.content", child);
		}

		prop->std.widget = child; //FIXME reset to NULL + std.entry + std.elmnt
	} // lay

	return lay;
}

static void
_property_del(void *data, Evas_Object *obj)
{
	property_t *prop = data;

	if(prop)
		prop->std.elmnt = NULL;

	// we don't free it here, as this is only a reference from group->children
}

static Evas_Object *
_group_content_get(void *data, Evas_Object *obj, const char *part)
{
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	group_t *group = data;
	if(!group || !ui)
		return NULL;

	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	Evas_Object *lay = elm_layout_add(obj);
	if(lay)
	{
		elm_layout_file_set(lay, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/group/theme");
		char col [7];
		sprintf(col, "col,%02i", group->mod->col);
		elm_layout_signal_emit(lay, col, "/synthpod/group/ui");
		evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(lay);

		if(group->node)
		{
			LilvNode *label = lilv_world_get(ui->world, group->node,
				ui->regs.core.name.node, NULL);
			if(label)
			{
				const char *label_str = lilv_node_as_string(label);

				if(label_str)
					elm_object_part_text_set(lay, "elm.text", label_str);

				lilv_node_free(label);
			}
		}
		else
		{
			if(group->type == GROUP_TYPE_PORT)
				elm_object_part_text_set(lay, "elm.text", "Ports");
			else if(group->type == GROUP_TYPE_PROPERTY)
				elm_object_part_text_set(lay, "elm.text", "Properties");
		}
	}

	return lay;
}

static void
_group_del(void *data, Evas_Object *obj)
{
	group_t *group = data;

	if(group)
	{
		if(group->children)
			eina_list_free(group->children);
		free(group);
	}
}

static void
_port_link_toggle(void *data, Evas_Object *lay, const char *emission, const char *source)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	port->selected ^= 1; // toggle
	elm_layout_signal_emit(lay, port->selected ? "link,on" : "link,off", "");

	_patches_update(ui);

	size_t size = sizeof(transmit_port_selected_t);
	transmit_port_selected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_selected_fill(&ui->regs, &ui->forge, trans, size, mod->uid, port->index, port->selected);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_port_monitor_toggle(void *data, Evas_Object *lay, const char *emission, const char *source)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	port->std.monitored ^= 1; // toggle
	elm_layout_signal_emit(lay, port->std.monitored ? "monitor,on" : "monitor,off", "");

	// subsribe or unsubscribe, depending on monitored state
	{
		int32_t i = port->index;
		int32_t state = port->std.monitored;

		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, state);
		else if(port->type == PORT_TYPE_AUDIO)
			_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, state);
		else if(port->type == PORT_TYPE_CV)
			_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, state);
	}

	// signal monitored state to app
	{
		size_t size = sizeof(transmit_port_monitored_t);
		transmit_port_monitored_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transmit_port_monitored_fill(&ui->regs, &ui->forge, trans, size, mod->uid, port->index, port->std.monitored);
			_sp_ui_to_app_advance(ui, size);
		}
	}
}

static void
_check_changed(void *data, Evas_Object *obj, void *event)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	float val = smart_toggle_value_get(obj);

	_std_ui_write_function(mod, port->index, sizeof(float),
		ui->regs.port.float_protocol.urid, &val);
}

static void
_spinner_changed(void *data, Evas_Object *obj, void *event)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	float val = smart_spinner_value_get(obj);

	_std_ui_write_function(mod, port->index, sizeof(float),
		ui->regs.port.float_protocol.urid, &val);
}

static void
_sldr_changed(void *data, Evas_Object *obj, void *event)
{
	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	float val = smart_slider_value_get(obj);
	if(port->integer)
		val = floor(val);

	_std_ui_write_function(mod, port->index, sizeof(float),
		ui->regs.port.float_protocol.urid, &val);
}

static void
_modlist_std_del(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	if(!data)
		return;

	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	port->std.widget = NULL;

	// unsubscribe from port
	if(port->std.monitored)
	{
		const uint32_t i = port->index;
		if(port->type == PORT_TYPE_CONTROL)
			_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 0);
		else if(port->type == PORT_TYPE_AUDIO)
			_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, 0);
		else if(port->type == PORT_TYPE_CV)
			_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, 0);
	}
}

static Evas_Object * 
_modlist_std_content_get(void *data, Evas_Object *obj, const char *part)
{
	if(!data) // mepty item
		return NULL;

	port_t *port = data;
	mod_t *mod = port->mod;
	sp_ui_t *ui = mod->ui;

	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	Evas_Object *lay = elm_layout_add(obj);
	if(lay)
	{
		elm_layout_file_set(lay, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/modlist/port");
		evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_event_callback_add(lay, EVAS_CALLBACK_DEL, _modlist_std_del, port);
		evas_object_show(lay);

		// link
		elm_layout_signal_callback_add(lay, "link,toggle", "", _port_link_toggle, port);
		elm_layout_signal_emit(lay, port->selected ? "link,on" : "link,off", "");

		// monitor
		if( (port->type != PORT_TYPE_ATOM) && (port->type != PORT_TYPE_EVENT) )
		{
			elm_layout_signal_callback_add(lay, "monitor,toggle", "", _port_monitor_toggle, port);
			elm_layout_signal_emit(lay, port->std.monitored ? "monitor,on" : "monitor,off", "");
		}
		else
		{
			elm_layout_signal_emit(lay, "monitor,hide", "");
		}

		char col [7];
		sprintf(col, "col,%02i", mod->col);

		// source/sink
		elm_layout_signal_emit(lay, col, MODLIST_UI);
		if(port->direction == PORT_DIRECTION_OUTPUT)
		{
			elm_layout_signal_emit(lay, "source,show", "");
			elm_layout_signal_emit(lay, "sink,hide", "");
		}
		else
		{
			elm_layout_signal_emit(lay, "source,hide", "");
			elm_layout_signal_emit(lay, "sink,show", "");
		}

		LilvNode *name_node = lilv_port_get_name(mod->plug, port->tar);
		if(name_node)
		{
			const char *type_str = lilv_node_as_string(name_node);
			elm_layout_text_set(lay, "elm.text", type_str);
			lilv_node_free(name_node);
		}

		Evas_Object *child = NULL;
		if(port->type == PORT_TYPE_CONTROL)
		{
			if(port->toggled)
			{
				Evas_Object *check = smart_toggle_add(evas_object_evas_get(lay));
				if(check)
				{
					smart_toggle_color_set(check, mod->col);
					smart_toggle_disabled_set(check, port->direction == PORT_DIRECTION_OUTPUT);
					if(port->direction == PORT_DIRECTION_INPUT)
						evas_object_smart_callback_add(check, "changed", _check_changed, port);
					evas_object_smart_callback_add(check, "cat,in", _smart_mouse_in, mod);
					evas_object_smart_callback_add(check, "cat,out", _smart_mouse_out, mod);
				}

				child = check;
			}
			else if(port->points)
			{
				Evas_Object *spin = smart_spinner_add(evas_object_evas_get(lay));
				if(spin)
				{
					smart_spinner_color_set(spin, mod->col);
					smart_spinner_disabled_set(spin, port->direction == PORT_DIRECTION_OUTPUT);
					LILV_FOREACH(scale_points, itr, port->points)
					{
						const LilvScalePoint *point = lilv_scale_points_get(port->points, itr);
						const LilvNode *label_node = lilv_scale_point_get_label(point);
						const LilvNode *val_node = lilv_scale_point_get_value(point);

						smart_spinner_value_add(spin,
							lilv_node_as_float(val_node), lilv_node_as_string(label_node));
					}
					if(port->direction == PORT_DIRECTION_INPUT)
						evas_object_smart_callback_add(spin, "changed", _spinner_changed, port);
					evas_object_smart_callback_add(spin, "cat,in", _smart_mouse_in, mod);
					evas_object_smart_callback_add(spin, "cat,out", _smart_mouse_out, mod);
				}

				child = spin;
			}
			else // integer or float
			{
				Evas_Object *sldr = smart_slider_add(evas_object_evas_get(lay));
				if(sldr)
				{
					smart_slider_range_set(sldr, port->min, port->max, port->dflt);
					smart_slider_color_set(sldr, mod->col);
					smart_slider_integer_set(sldr, port->integer);
					smart_slider_logarithmic_set(sldr, port->logarithmic);
					smart_slider_format_set(sldr, port->integer ? "%.0f %s" : "%.4f %s"); //TODO handle MIDI notes
					if(port->unit)
						smart_slider_unit_set(sldr, port->unit);
					smart_slider_disabled_set(sldr, port->direction == PORT_DIRECTION_OUTPUT);
					if(port->direction == PORT_DIRECTION_INPUT)
						evas_object_smart_callback_add(sldr, "changed", _sldr_changed, port);
					evas_object_smart_callback_add(sldr, "cat,in", _smart_mouse_in, mod);
					evas_object_smart_callback_add(sldr, "cat,out", _smart_mouse_out, mod);
				}

				child = sldr;
			}
		}
		else if(port->type == PORT_TYPE_AUDIO
			|| port->type == PORT_TYPE_CV)
		{
			Evas_Object *sldr = smart_meter_add(evas_object_evas_get(lay));
			if(sldr)
				smart_meter_color_set(sldr, mod->col);

			child = sldr;
		}
		else if(port->type == PORT_TYPE_ATOM)
		{
			Evas_Object *lbl = elm_label_add(lay);
			if(lbl)
				elm_object_text_set(lbl, "Atom Port");

			child = lbl;
		}
		else if(port->type == PORT_TYPE_EVENT)
		{
			Evas_Object *lbl = elm_label_add(lay);
			if(lbl)
				elm_object_text_set(lbl, "Event Port");

			child = lbl;
		}

		if(child)
		{
			evas_object_show(child);
			elm_layout_content_set(lay, "elm.swallow.content", child);
		}

		if(port->std.monitored)
		{
			// subscribe to port
			const uint32_t i = port->index;
			if(port->type == PORT_TYPE_CONTROL)
				_port_subscription_set(mod, i, ui->regs.port.float_protocol.urid, 1);
			else if(port->type == PORT_TYPE_AUDIO)
				_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, 1);
			else if(port->type == PORT_TYPE_CV)
				_port_subscription_set(mod, i, ui->regs.port.peak_protocol.urid, 1);
		}

		port->std.widget = child;
	} // lay

	return lay;
}

static Evas_Object * 
_modlist_psets_content_get(void *data, Evas_Object *obj, const char *part)
{
	if(!data) // mepty item
		return NULL;

	mod_t *mod = data;

	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	Evas_Object *lay = elm_layout_add(obj);
	if(lay)
	{
		elm_layout_file_set(lay, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/group/theme");
		char col [7];
		sprintf(col, "col,%02i", mod->col);
		elm_layout_signal_emit(lay, col, "/synthpod/group/ui");
		elm_object_part_text_set(lay, "elm.text", "Presets");
		evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(lay);
	}

	return lay;
}

static char * 
_modlist_bank_label_get(void *data, Evas_Object *obj, const char *part)
{
	const LilvNode* bank = data;
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	if(!ui)
		return NULL;

	if(!strcmp(part, "elm.text"))
	{
		char *lbl = NULL;

		//lilv_world_load_resource(ui->world, bank); //FIXME
		LilvNode *label = lilv_world_get(ui->world, bank,
			ui->regs.rdfs.label.node, NULL);
		if(label)
		{
			const char *label_str = lilv_node_as_string(label);
			if(label_str)
				lbl = strdup(label_str);
			lilv_node_free(label);
		}
		//lilv_world_unload_resource(ui->world, bank); //FIXME

		return lbl;
	}

	return NULL;
}

static char * 
_modlist_pset_label_get(void *data, Evas_Object *obj, const char *part)
{
	const LilvNode* preset = data;
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	if(!ui)
		return NULL;

	if(!strcmp(part, "elm.text"))
	{
		char *lbl = NULL;

		//lilv_world_load_resource(ui->world, preset); //FIXME
		LilvNode *label = lilv_world_get(ui->world, preset,
			ui->regs.rdfs.label.node, NULL);
		if(label)
		{
			const char *label_str = lilv_node_as_string(label);
			if(label_str)
				lbl = strdup(label_str);
			lilv_node_free(label);
		}
		//lilv_world_unload_resource(ui->world, preset); //FIXME

		return lbl;
	}

	return NULL;
}

static void
_pset_markup(void *data, Evas_Object *obj, char **txt)
{
	// intercept enter
	if(!strcmp(*txt, "<tab/>") || !strcmp(*txt, " "))
	{
		free(*txt);
		*txt = strdup("_"); //TODO check
	}
}

static void
_pset_changed(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;

	const char *chunk = elm_entry_entry_get(obj);
	char *utf8 = elm_entry_markup_to_utf8(chunk);

	if(mod->pset_label)
		free(mod->pset_label);

	mod->pset_label = strdup(utf8); //TODO check
	free(utf8);
}

static void
_pset_clicked(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	if(!mod->pset_label)
		return;

	// signal app
	size_t size = sizeof(transmit_module_preset_save_t)
		+ lv2_atom_pad_size(strlen(mod->pset_label) + 1);
	transmit_module_preset_save_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_preset_save_fill(&ui->regs, &ui->forge, trans, size, mod->uid, mod->pset_label);
		_sp_ui_to_app_advance(ui, size);
	}

	// reset pset_label
	free(mod->pset_label);
	mod->pset_label = strdup("unknown"); //TODO check

	// contract parent list item
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(mod->std.list);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != ui->psetitc) // is not a parent preset item
			continue; // skip 

		if(elm_object_item_data_get(itm) != mod) // does not belong to this module
			continue; // skip

		evas_object_smart_callback_call(mod->std.list, "contract,request", itm);
		break;
	}
}

static Evas_Object * 
_modlist_pset_content_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	if(!ui)
		return NULL;

	if(!strcmp(part, "elm.swallow.content"))
	{
		Evas_Object *hbox = elm_box_add(obj);
		if(hbox)
		{
			elm_box_horizontal_set(hbox, EINA_TRUE);
			elm_box_homogeneous_set(hbox, EINA_FALSE);
			elm_box_padding_set(hbox, 5, 0);
			evas_object_show(hbox);

			Evas_Object *entry = elm_entry_add(hbox);
			if(entry)
			{
				elm_entry_single_line_set(entry, EINA_TRUE);
				elm_entry_entry_set(entry, mod->pset_label);
				elm_entry_editable_set(entry, EINA_TRUE);
				elm_entry_markup_filter_append(entry, _pset_markup, mod);
				evas_object_smart_callback_add(entry, "changed,user", _pset_changed, mod);
				evas_object_size_hint_weight_set(entry, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(entry, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(entry);
				elm_box_pack_end(hbox, entry);
			}

			Evas_Object *but = elm_button_add(hbox);
			if(but)
			{
				elm_object_text_set(but, "+");
				evas_object_smart_callback_add(but, "clicked", _pset_clicked, mod);
				evas_object_size_hint_align_set(but, 0.f, EVAS_HINT_FILL);
				evas_object_show(but);
				elm_box_pack_start(hbox, but);
			}
		}

		return hbox;
	}

	return NULL;
}

static void
_modlist_del(void *data, Evas_Object *obj)
{
	mod_t *mod = data;
	mod_ui_t *mod_ui = mod->mod_ui;
	sp_ui_t *ui = mod->ui;

	if(mod_ui)
	{
		// close show ui
		if( (mod_ui->type == MOD_UI_TYPE_SHOW) && mod_ui->descriptor)
			_show_ui_hide(mod);
		// close kx ui
		else if( (mod_ui->type == MOD_UI_TYPE_KX) && mod_ui->descriptor)
			_kx_ui_hide(mod);
		// close x11 ui
		else if( (mod_ui->type == MOD_UI_TYPE_X11) && mod_ui->descriptor)
			_x11_ui_hide(mod);
		else if( (mod_ui->type == MOD_UI_TYPE_EO) && mod_ui->eo.win && mod_ui->descriptor)
			evas_object_del(mod_ui->eo.win);
	}

	_sp_ui_mod_del(ui, mod);
}

static void
_modgrid_mouse_in(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(ui->modgrid)
		elm_scroller_movement_block_set(ui->modgrid,
			ELM_SCROLLER_MOVEMENT_BLOCK_HORIZONTAL | ELM_SCROLLER_MOVEMENT_BLOCK_VERTICAL);
}

static void
_modgrid_mouse_out(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(ui->modgrid)
		elm_scroller_movement_block_set(ui->modgrid, ELM_SCROLLER_MOVEMENT_NO_BLOCK);
}

static char *
_modgrid_label_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;

	if(strcmp(part, "elm.text"))
		return NULL;

	return strdup(mod->name);
}

static Evas_Object *
_modgrid_content_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	Evas_Object *modlist = elm_genlist_add(obj);
	if(modlist)
	{
		elm_genlist_homogeneous_set(modlist, EINA_TRUE); // needef for lazy-loading
		elm_genlist_mode_set(modlist, ELM_LIST_LIMIT);
		elm_genlist_block_count_set(modlist, 64); // needef for lazy-loading
		//elm_genlist_select_mode_set(modlist, ELM_OBJECT_SELECT_MODE_NONE);
		elm_genlist_reorder_mode_set(modlist, EINA_FALSE);
		evas_object_smart_callback_add(modlist, "expand,request",
			_list_expand_request, ui);
		evas_object_smart_callback_add(modlist, "contract,request",
			_list_contract_request, ui);
		evas_object_smart_callback_add(modlist, "expanded",
			_modgrid_expanded, ui);
		evas_object_smart_callback_add(modlist, "contracted",
			_modgrid_contracted, ui);
		evas_object_smart_callback_add(modlist, "activated",
			_modgrid_activated, ui);
		evas_object_data_set(modlist, "ui", ui);
		evas_object_size_hint_weight_set(modlist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(modlist, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(modlist);
		mod->std.list = modlist;

		// port groups
		mod->groups = eina_hash_string_superfast_new(NULL); //TODO check

		// port entries
		for(unsigned i=0; i<mod->num_ports; i++)
		{
			port_t *port = &mod->ports[i];

			Elm_Object_Item *parent;
			group_t *group;

			if(port->group)
			{
				const char *group_lbl = lilv_node_as_string(port->group);
				group = _mod_group_get(mod, group_lbl, GROUP_TYPE_PORT, port->group, &parent, false);
			}
			else
			{
				const char *group_lbl = "*Ungrouped*";
				group = _mod_group_get(mod, group_lbl, GROUP_TYPE_PORT, NULL, &parent, false);
			}

			// append port to corresponding group
			if(group)
				group->children = eina_list_append(group->children, port);
		}

		{
			const char *group_lbl = "*Properties*";
			Elm_Object_Item *parent;
			group_t *group = _mod_group_get(mod, group_lbl, GROUP_TYPE_PROPERTY, NULL, &parent, false);

			Eina_List *l;
			property_t *prop;
			EINA_LIST_FOREACH(mod->static_properties, l, prop)
			{
				// append property to corresponding group
				if(group)
					group->children = eina_list_append(group->children, prop);
			}
		}

		// presets //FIXME put in vbox 
		Elm_Object_Item *elmnt = elm_genlist_item_sorted_insert(mod->std.list, ui->psetitc, mod, NULL,
			ELM_GENLIST_ITEM_TREE, _grpitc_cmp, NULL, NULL);
		elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_DEFAULT);

		// expand all groups by default
		eina_hash_foreach(mod->groups, _groups_foreach, ui);

		// request all properties
		size_t len = sizeof(transfer_patch_get_t);
		for(unsigned index=0; index<mod->num_ports; index++)
		{
			port_t *port = &mod->ports[index];

			// only consider event ports which support patch:Message
			if(  (port->buffer_type != PORT_BUFFER_TYPE_SEQUENCE)
				|| (port->direction != PORT_DIRECTION_INPUT)
				|| !port->patchable)
			{
				continue; // skip
			}

			transfer_patch_get_all_t *trans = _sp_ui_to_app_request(ui, len);
			if(trans)
			{
				_sp_transfer_patch_get_all_fill(&ui->regs,
					&ui->forge, trans, mod->uid, index,
					mod->subject);
				_sp_ui_to_app_advance(ui, len);
			}
		}
	} // modlist

	return modlist;
}

static void
_modgrid_del(void *data, Evas_Object *obj)
{
	mod_t *mod = data;

	if(mod)
	{
		mod->std.grid = NULL;
		mod->std.list = NULL;
	}
}

static void
_matrix_connect_request(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	patcher_event_t *ev = event_info;
	if(!ui || !ev)
		return;

	patcher_event_t *source = &ev[0];
	patcher_event_t *sink = &ev[1];
	if(!source || !sink)
		return;

	port_t *source_port = source->ptr;
	port_t *sink_port = sink->ptr;
	if(!source_port || !sink_port)
		return;

	size_t size = sizeof(transmit_port_connected_t);
	transmit_port_connected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_connected_fill(&ui->regs, &ui->forge, trans, size,
			source_port->mod->uid, source_port->index,
			sink_port->mod->uid, sink_port->index, 1, -999);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_matrix_disconnect_request(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	patcher_event_t *ev = event_info;
	if(!ui || !ev)
		return;

	patcher_event_t *source = &ev[0];
	patcher_event_t *sink = &ev[1];
	if(!source || !sink)
		return;

	port_t *source_port = source->ptr;
	port_t *sink_port = sink->ptr;
	if(!source_port || !sink_port)
		return;

	size_t size = sizeof(transmit_port_connected_t);
	transmit_port_connected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_connected_fill(&ui->regs, &ui->forge, trans, size,
			source_port->mod->uid, source_port->index,
			sink_port->mod->uid, sink_port->index, 0, -999);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_matrix_realize_request(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	patcher_event_t *ev = event_info;
	if(!ui || !ev)
		return;

	patcher_event_t *source = &ev[0];
	patcher_event_t *sink = &ev[1];
	if(!source || !sink)
		return;

	port_t *source_port = source->ptr;
	port_t *sink_port = sink->ptr;
	if(!source_port || !sink_port)
		return;

	size_t size = sizeof(transmit_port_connected_t);
	transmit_port_connected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_port_connected_fill(&ui->regs, &ui->forge, trans, size,
			source_port->mod->uid, source_port->index,
			sink_port->mod->uid, sink_port->index, -1, -999);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_pluglist_populate(sp_ui_t *ui, const char *match)
{
	if(!ui || !ui->plugs || !ui->pluglist || !ui->plugitc)
		return;

	LILV_FOREACH(plugins, itr, ui->plugs)
	{
		const LilvPlugin *plug = lilv_plugins_get(ui->plugs, itr);
		if(!plug)
			continue;

		LilvNode *name_node = lilv_plugin_get_name(plug);
		if(name_node)
		{
			const char *name_str = lilv_node_as_string(name_node);

			if(strcasestr(name_str, match))
			{
				plug_info_t *info = calloc(1, sizeof(plug_info_t));
				if(info)
				{
					info->type = PLUG_INFO_TYPE_NAME;
					info->plug = plug;
					Elm_Object_Item *elmnt = elm_genlist_item_append(ui->pluglist, ui->plugitc, info, NULL,
						ELM_GENLIST_ITEM_TREE, NULL, NULL);
				}
			}

			lilv_node_free(name_node);
		}
	}
}

static void
_modlist_refresh(sp_ui_t *ui)
{
	size_t size = sizeof(transmit_module_list_t);
	transmit_module_list_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_list_fill(&ui->regs, &ui->forge, trans, size);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_plugentry_changed(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	if(!ui || !ui->pluglist)
		return;

	const char *chunk = elm_entry_entry_get(obj);
	char *match = elm_entry_markup_to_utf8(chunk);

	elm_genlist_clear(ui->pluglist);
	_pluglist_populate(ui, match); // populate with matching plugins
	free(match);
}

static void
_modlist_clear(sp_ui_t *ui, bool clear_system_ports, bool propagate)
{
	if(!ui || !ui->modlist)
		return;

	// iterate over all registered modules
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(ui->modlist);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != ui->listitc) // is not a parent mod item 
			continue; // skip 

		mod_t *mod = elm_object_item_data_get(itm);

		if(!clear_system_ports && (mod->system.source || mod->system.sink) )
			continue; // skip

		_mod_del_widgets(mod);
		if(propagate)
			_mod_del_propagate(mod);
	}
}

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

static void
_menu_new(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	if(!ui)
		return;

	sp_ui_bundle_new(ui);
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
		sp_ui_bundle_save(ui, ui->bundle_path, 0);
}

static void
_menu_close(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(ui && ui->driver->close)
		ui->driver->close(ui->data);
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

static void
_patchbar_restore(sp_ui_t *ui)
{
	switch(ui->matrix_type)
	{
		case PORT_TYPE_AUDIO:
			elm_toolbar_item_selected_set(ui->matrix_audio, EINA_TRUE);
			break;
		case PORT_TYPE_CONTROL:
			elm_toolbar_item_selected_set(ui->matrix_control, EINA_TRUE);
			break;
		case PORT_TYPE_CV:
			elm_toolbar_item_selected_set(ui->matrix_cv, EINA_TRUE);
			break;
		case PORT_TYPE_EVENT:
			elm_toolbar_item_selected_set(ui->matrix_event, EINA_TRUE);
			break;
		case PORT_TYPE_ATOM:
			switch(ui->matrix_atom_type)
			{
				case PORT_ATOM_TYPE_ALL:
					elm_toolbar_item_selected_set(ui->matrix_atom, EINA_TRUE);
					break;
				case PORT_ATOM_TYPE_MIDI:
					elm_toolbar_item_selected_set(ui->matrix_atom_midi, EINA_TRUE);
					break;
				case PORT_ATOM_TYPE_OSC:
					elm_toolbar_item_selected_set(ui->matrix_atom_osc, EINA_TRUE);
					break;
				case PORT_ATOM_TYPE_TIME:
					elm_toolbar_item_selected_set(ui->matrix_atom_time, EINA_TRUE);
					break;
				case PORT_ATOM_TYPE_PATCH:
					elm_toolbar_item_selected_set(ui->matrix_atom_patch, EINA_TRUE);
					break;
			}
			break;
		default:
			break;
	}
}

static void
_patchbar_selected(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;
	Elm_Object_Item *itm = event_info;

	if(itm == ui->matrix_audio)
	{
		ui->matrix_type = PORT_TYPE_AUDIO;
	}
	else if(itm == ui->matrix_control)
	{
		ui->matrix_type = PORT_TYPE_CONTROL;
	}
	else if(itm == ui->matrix_cv)
	{
		ui->matrix_type = PORT_TYPE_CV;
	}
	else if(itm == ui->matrix_event)
	{
		ui->matrix_type = PORT_TYPE_EVENT;
	}
	else if(itm == ui->matrix_atom)
	{
		ui->matrix_type = PORT_TYPE_ATOM;
		ui->matrix_atom_type = PORT_ATOM_TYPE_ALL;
	}

	else if(itm == ui->matrix_atom_midi)
	{
		ui->matrix_type = PORT_TYPE_ATOM;
		ui->matrix_atom_type = PORT_ATOM_TYPE_MIDI;
	}
	else if(itm == ui->matrix_atom_osc)
	{
		ui->matrix_type = PORT_TYPE_ATOM;
		ui->matrix_atom_type = PORT_ATOM_TYPE_OSC;
	}
	else if(itm == ui->matrix_atom_time)
	{
		ui->matrix_type = PORT_TYPE_ATOM;
		ui->matrix_atom_type = PORT_ATOM_TYPE_TIME;
	}
	else if(itm == ui->matrix_atom_patch)
	{
		ui->matrix_type = PORT_TYPE_ATOM;
		ui->matrix_atom_type = PORT_ATOM_TYPE_PATCH;
	}

	else
	{
		return;
	}
	
	_patches_update(ui);
}

static void
_menu_matrix_del(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	ui->patchwin = NULL;
	ui->matrix_audio = NULL;
	ui->matrix_control = NULL;
	ui->matrix_cv = NULL;
	ui->matrix_event = NULL;
	ui->matrix_atom = NULL;
	ui->matrix_atom_midi = NULL;
	ui->matrix_atom_osc = NULL;
	ui->matrix_atom_time = NULL;
	ui->matrix_atom_patch = NULL;
	ui->matrix = NULL;
}

static inline Evas_Object *
_menu_matrix_new(sp_ui_t *ui)
{
	const char *title = "Matrix";
	Evas_Object *win = elm_win_add(ui->win, title, ELM_WIN_BASIC);
	if(win)
	{
		elm_win_title_set(win, title);
		elm_win_autodel_set(win, EINA_TRUE);
		evas_object_smart_callback_add(win, "delete,request", _menu_matrix_del, ui);
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

		Evas_Object *patchbox = elm_box_add(win);
		if(patchbox)
		{
			elm_box_horizontal_set(patchbox, EINA_FALSE);
			elm_box_homogeneous_set(patchbox, EINA_FALSE);
			evas_object_data_set(patchbox, "ui", ui);
			evas_object_size_hint_weight_set(patchbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(patchbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(patchbox);
			elm_win_resize_object_add(win, patchbox);

			Evas_Object *patchbar = elm_toolbar_add(patchbox);
			if(patchbar)
			{
				elm_toolbar_horizontal_set(patchbar, EINA_TRUE);
				elm_toolbar_homogeneous_set(patchbar, EINA_TRUE);
				elm_toolbar_align_set(patchbar, 0.f);
				elm_toolbar_select_mode_set(patchbar, ELM_OBJECT_SELECT_MODE_ALWAYS);
				elm_toolbar_shrink_mode_set(patchbar, ELM_TOOLBAR_SHRINK_SCROLL);
				evas_object_smart_callback_add(patchbar, "selected", _patchbar_selected, ui);
				evas_object_size_hint_weight_set(patchbar, EVAS_HINT_EXPAND, 0.f);
				evas_object_size_hint_align_set(patchbar, EVAS_HINT_FILL, 0.f);
				evas_object_show(patchbar);
				elm_box_pack_end(patchbox, patchbar);

				ui->matrix_audio = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/audio.png", "Audio", NULL, NULL);
				elm_toolbar_item_selected_set(ui->matrix_audio, EINA_TRUE);
				ui->matrix_control = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/control.png", "Control", NULL, NULL);
				ui->matrix_cv = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/cv.png", "CV", NULL, NULL);
				ui->matrix_event = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/atom.png", "Event", NULL, NULL); //FIXME event.png
				ui->matrix_atom = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/atom.png", "Atom", NULL, NULL);

				Elm_Object_Item *sep = elm_toolbar_item_append(patchbar,
					NULL, NULL, NULL, NULL);
				elm_toolbar_item_separator_set(sep, EINA_TRUE);

				ui->matrix_atom_midi = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/midi.png", "MIDI", NULL, NULL);
				ui->matrix_atom_osc = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/osc.png", "OSC", NULL, NULL);
				ui->matrix_atom_time = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/time.png", "Time", NULL, NULL);
				ui->matrix_atom_patch = elm_toolbar_item_append(patchbar,
					SYNTHPOD_DATA_DIR"/patch.png", "Patch", NULL, NULL);
			} // patchbar

			ui->matrix = patcher_object_add(patchbox);
			if(ui->matrix)
			{
				evas_object_smart_callback_add(ui->matrix, "connect,request",
					_matrix_connect_request, ui);
				evas_object_smart_callback_add(ui->matrix, "disconnect,request",
					_matrix_disconnect_request, ui);
				evas_object_smart_callback_add(ui->matrix, "realize,request",
					_matrix_realize_request, ui);
				evas_object_size_hint_weight_set(ui->matrix, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(ui->matrix, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(ui->matrix);
				elm_box_pack_end(patchbox, ui->matrix);

				_patchbar_restore(ui);
				_patches_update(ui);
			} // matrix
		} // patchbox

		return win;
	}

	return NULL;
}

static void
_menu_matrix(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(!ui->patchwin)
		ui->patchwin = _menu_matrix_new(ui);
}

static void
_menu_plugin_del(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	ui->plugwin = NULL;
	ui->pluglist = NULL;
}

static inline Evas_Object *
_menu_plugin_new(sp_ui_t *ui)
{
	const char *title = "Plugin";
	Evas_Object *win = elm_win_add(ui->win, title, ELM_WIN_BASIC);
	if(win)
	{
		elm_win_title_set(win, title);
		elm_win_autodel_set(win, EINA_TRUE);
		evas_object_smart_callback_add(win, "delete,request", _menu_plugin_del, ui);
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

		Evas_Object *plugbox = elm_box_add(win);
		if(plugbox)
		{
			elm_box_horizontal_set(plugbox, EINA_FALSE);
			elm_box_homogeneous_set(plugbox, EINA_FALSE);
			evas_object_data_set(plugbox, "ui", ui);
			evas_object_size_hint_weight_set(plugbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(plugbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(plugbox);
			elm_win_resize_object_add(win, plugbox);

			Evas_Object *plugentry = elm_entry_add(plugbox);
			if(plugentry)
			{
				elm_entry_entry_set(plugentry, "");
				elm_entry_editable_set(plugentry, EINA_TRUE);
				elm_entry_single_line_set(plugentry, EINA_TRUE);
				elm_entry_scrollable_set(plugentry, EINA_TRUE);
				evas_object_smart_callback_add(plugentry, "changed,user", _plugentry_changed, ui);
				evas_object_data_set(plugentry, "ui", ui);
				//evas_object_size_hint_weight_set(plugentry, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(plugentry, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(plugentry);
				elm_box_pack_end(plugbox, plugentry);
			} // plugentry

			ui->pluglist = elm_genlist_add(plugbox);
			if(ui->pluglist)
			{
				elm_genlist_homogeneous_set(ui->pluglist, EINA_TRUE); // needef for lazy-loading
				elm_genlist_mode_set(ui->pluglist, ELM_LIST_SCROLL);
				elm_genlist_block_count_set(ui->pluglist, 64); // needef for lazy-loading
				evas_object_smart_callback_add(ui->pluglist, "activated",
					_pluglist_activated, ui);
				evas_object_smart_callback_add(ui->pluglist, "expand,request",
					_list_expand_request, ui);
				evas_object_smart_callback_add(ui->pluglist, "contract,request",
					_list_contract_request, ui);
				evas_object_smart_callback_add(ui->pluglist, "expanded",
					_pluglist_expanded, ui);
				evas_object_smart_callback_add(ui->pluglist, "contracted",
					_pluglist_contracted, ui);
				evas_object_data_set(ui->pluglist, "ui", ui);
				evas_object_size_hint_weight_set(ui->pluglist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(ui->pluglist, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(ui->pluglist);
				elm_box_pack_end(plugbox, ui->pluglist);
			} // pluglist

			_pluglist_populate(ui, ""); // populate with everything
		} // plugbox

		return win;
	}

	return NULL;
}

static void
_menu_plugin(void *data, Evas_Object *obj, void *event_info)
{
	sp_ui_t *ui = data;

	if(!ui->plugwin)
		ui->plugwin = _menu_plugin_new(ui);
}

static void
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

Evas_Object *
sp_ui_widget_get(sp_ui_t *ui)
{
	return ui->vbox;
}

static inline mod_t *
_sp_ui_mod_get(sp_ui_t *ui, u_id_t uid)
{
	if(!ui || !ui->modlist)
		return NULL;

	for(Elm_Object_Item *itm = elm_genlist_first_item_get(ui->modlist);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		mod_t *mod = elm_object_item_data_get(itm);
		if(mod && (mod->uid == uid))
			return mod;
	}

	return NULL;
}

static inline port_t *
_sp_ui_port_get(sp_ui_t *ui, u_id_t uid, uint32_t index)
{
	mod_t *mod = _sp_ui_mod_get(ui, uid);
	if(mod && (index < mod->num_ports) )
		return &mod->ports[index];

	return NULL;
}

static void
_sp_ui_from_app_module_add(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_add_t *trans = (const transmit_module_add_t *)atom;

	mod_t *mod = _sp_ui_mod_add(ui, trans->uri_str, trans->uid.body,
		(void *)(uintptr_t)trans->inst.body, (data_access_t)(uintptr_t)trans->data.body);
	if(!mod)
		return; //TODO report

	if(mod->system.source || mod->system.sink || !ui->sink_itm)
	{
		if(ui->modlist)
		{
			mod->std.elmnt = elm_genlist_item_append(ui->modlist, ui->listitc, mod,
				NULL, ELM_GENLIST_ITEM_NONE, NULL, NULL);
		}

		if(mod->system.sink)
			ui->sink_itm = mod->std.elmnt;
	}
	else // no sink and no source
	{
		if(ui->modlist)
		{
			mod->std.elmnt = elm_genlist_item_insert_before(ui->modlist, ui->listitc, mod,
				NULL, ui->sink_itm, ELM_GENLIST_ITEM_NONE, NULL, NULL);
		}
	}
}

static void
_sp_ui_from_app_module_del(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_del_t *trans = (const transmit_module_del_t *)atom;
	mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
	if(!mod)
		return;

	mod_ui_t *mod_ui = mod->mod_ui;

	if(mod_ui)
		_mod_del_widgets(mod);

	// remove StdUI list item
	if(mod->std.list)
	{
		elm_genlist_clear(mod->std.list);
		elm_object_item_del(mod->std.list);
		mod->std.list = NULL;
	}
	if(mod->std.elmnt)
	{
		elm_object_item_del(mod->std.elmnt);
		mod->std.elmnt = NULL;
	}
	if(mod->std.grid)
	{
		elm_object_item_del(mod->std.grid);
	}

	_patches_update(ui);
}

static void
_sp_ui_from_app_module_preset_save(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_preset_save_t *trans = (const transmit_module_preset_save_t *)atom;
	mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
	if(!mod)
		return;

	// reload presets for this module
	mod->presets = _preset_reload(ui->world, &ui->regs, mod->plug, mod->presets,
		trans->label_str);
}

static void
_sp_ui_from_app_module_selected(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_selected_t *trans = (const transmit_module_selected_t *)atom;
	mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
	if(!mod)
		return;

	if(mod->selected != trans->state.body)
	{
		mod->selected = trans->state.body;
		if(mod->std.elmnt)
			elm_genlist_item_update(mod->std.elmnt);

		_patches_update(ui);
	}
}

static void
_sp_ui_from_app_module_visible(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_visible_t *trans = (const transmit_module_visible_t *)atom;
	mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
	if(!mod)
		return;

	if(trans->state.body == 1)
	{
		Eina_List *l;
		mod_ui_t *mod_ui;
		EINA_LIST_FOREACH(mod->mod_uis, l, mod_ui)
		{
			if(mod_ui->urid == trans->urid.body)
			{
				_mod_ui_toggle_raw(mod, mod_ui);
				break;
			}
		}
	}
}

static void
_sp_ui_from_app_module_embedded(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_module_embedded_t *trans = (const transmit_module_embedded_t *)atom;
	mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
	if(!mod)
		return;

	if(mod->std.grid && !trans->state.body)
		elm_object_item_del(mod->std.grid);
	else if(!mod->std.grid && trans->state.body)
		mod->std.grid = elm_gengrid_item_append(ui->modgrid, ui->griditc, mod,
			NULL, NULL);
}

static void
_sp_ui_from_app_port_connected(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_port_connected_t *trans = (const transmit_port_connected_t *)atom;
	port_t *src = _sp_ui_port_get(ui, trans->src_uid.body, trans->src_port.body);
	port_t *snk = _sp_ui_port_get(ui, trans->snk_uid.body, trans->snk_port.body);
	if(!src || !snk)
		return;

	if(ui->matrix && (src->type == ui->matrix_type))
	{
		patcher_object_connected_set(ui->matrix, src, snk,
			trans->state.body ? EINA_TRUE : EINA_FALSE,
			trans->indirect.body);
	}
}

static void
_sp_ui_from_app_float_protocol(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transfer_float_t *trans = (const transfer_float_t *)atom;
	uint32_t port_index = trans->transfer.port.body;
	const float value = trans->value.body;
	mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
	if(!mod)
		return;

	_ui_port_event(mod, port_index, sizeof(float),
		ui->regs.port.float_protocol.urid, &value);
}

static void
_sp_ui_from_app_peak_protocol(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transfer_peak_t *trans = (const transfer_peak_t *)atom;
	uint32_t port_index = trans->transfer.port.body;
	const LV2UI_Peak_Data data = {
		.period_start = trans->period_start.body,
		.period_size = trans->period_size.body,
		.peak = trans->peak.body
	};
	mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
	if(!mod)
		return;

	_ui_port_event(mod, port_index, sizeof(LV2UI_Peak_Data),
		ui->regs.port.peak_protocol.urid, &data);
}

static void
_sp_ui_from_app_atom_transfer(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transfer_atom_t *trans = (const transfer_atom_t *)atom;
	uint32_t port_index = trans->transfer.port.body;
	const LV2_Atom *subatom = trans->atom;
	uint32_t size = sizeof(LV2_Atom) + subatom->size;
	mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
	if(!mod)
		return;

	_ui_port_event(mod, port_index, size,
		ui->regs.port.atom_transfer.urid, subatom);
}

static void
_sp_ui_from_app_event_transfer(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transfer_atom_t *trans = (const transfer_atom_t *)atom;
	uint32_t port_index = trans->transfer.port.body;
	const LV2_Atom *subatom = trans->atom;
	uint32_t size = sizeof(LV2_Atom) + subatom->size;
	mod_t *mod = _sp_ui_mod_get(ui, trans->transfer.uid.body);
	if(!mod)
		return;

	_ui_port_event(mod, port_index, size,
		ui->regs.port.event_transfer.urid, subatom);
}

static void
_sp_ui_from_app_port_selected(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_port_selected_t *trans = (const transmit_port_selected_t *)atom;
	port_t *port = _sp_ui_port_get(ui, trans->uid.body, trans->port.body);
	if(!port)
		return;

	if(port->selected != trans->state.body)
	{
		port->selected = trans->state.body;

		// FIXME update port itm
		mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
		/* FIXME
		if(mod && mod->std.elmnt)
			elm_genlist_item_update(mod->std.elmnt);
		*/

		_patches_update(ui);
	}
}

static void
_sp_ui_from_app_port_monitored(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_port_monitored_t *trans = (const transmit_port_monitored_t *)atom;
	port_t *port = _sp_ui_port_get(ui, trans->uid.body, trans->port.body);
	if(!port)
		return;

	if(port->std.monitored != trans->state.body)
	{
		port->std.monitored = trans->state.body;

		// FIXME update port itm
		mod_t *mod = _sp_ui_mod_get(ui, trans->uid.body);
		/* FIXME
		if(mod && mod->std.elmnt)
			elm_genlist_item_update(mod->std.elmnt);
		*/
	}
}

static void
_sp_ui_from_app_module_list(sp_ui_t *ui, const LV2_Atom *atom)
{
	if(ui->modlist)
	{
		ui->dirty = 1; // disable ui -> app communication
		elm_genlist_clear(ui->modlist);
		ui->dirty = 0; // enable ui -> app communication

		_modlist_refresh(ui);
	}
}

static void
_sp_ui_from_app_bundle_load(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_bundle_load_t *trans = (const transmit_bundle_load_t *)atom;

	if(ui->driver->opened)
		ui->driver->opened(ui->data, trans->status.body);

	if(ui->popup && evas_object_visible_get(ui->popup))
	{
		elm_popup_timeout_set(ui->popup, 1.f);
		evas_object_show(ui->popup);
	}
}

static void
_sp_ui_from_app_bundle_save(sp_ui_t *ui, const LV2_Atom *atom)
{
	atom = ASSUME_ALIGNED(atom);

	const transmit_bundle_save_t *trans = (const transmit_bundle_save_t *)atom;

	if(ui->driver->saved)
		ui->driver->saved(ui->data, trans->status.body);
}

void
sp_ui_from_app(sp_ui_t *ui, const LV2_Atom *atom)
{
	if(!ui || !atom)
		return;

	atom = ASSUME_ALIGNED(atom);
	const transmit_t *transmit = (const transmit_t *)atom;

	// check for atom object type
	if(!lv2_atom_forge_is_object_type(&ui->forge, transmit->obj.atom.type))
		return;

	// what we want to search for
	const from_app_t cmp = {
		.protocol = transmit->obj.body.otype,
		.cb = NULL
	};

	// search for corresponding callback
	const from_app_t *from_app = bsearch(&cmp, from_apps, FROM_APP_NUM, sizeof(from_app_t), _from_app_cmp);

	// run callback if found
	if(from_app)
		from_app->cb(ui, atom);
}

static uint32_t
_uri_to_id(LV2_URI_Map_Callback_Data handle, const char *_, const char *uri)
{
	sp_ui_t *ui = handle;

	LV2_URID_Map *map = ui->driver->map;

	return map->map(map->handle, uri);
}

sp_ui_t *
sp_ui_new(Evas_Object *win, const LilvWorld *world, sp_ui_driver_t *driver,
	void *data, int show_splash)
{
	if(!driver || !data)
		return NULL;

	if(  !driver->map || !driver->unmap
		|| !driver->to_app_request || !driver->to_app_advance)
		return NULL;

#if defined(ELM_1_10)
	elm_config_focus_autoscroll_mode_set(ELM_FOCUS_AUTOSCROLL_MODE_NONE);
	elm_config_focus_move_policy_set(ELM_FOCUS_MOVE_POLICY_CLICK);
	elm_config_first_item_focus_on_first_focusin_set(EINA_TRUE);
#endif

	Elm_Theme *default_theme = elm_theme_default_get();
	elm_theme_extension_add(default_theme, SYNTHPOD_DATA_DIR"/synthpod.edj");

	sp_ui_t *ui = calloc(1, sizeof(sp_ui_t));
	if(!ui)
		return NULL;

	ui->win = win;
	ui->driver = driver;
	ui->data = data;

	lv2_atom_forge_init(&ui->forge, ui->driver->map);

	if(world)
	{
		ui->world = (LilvWorld *)world;
		ui->embedded = 1;
	}
	else
	{
		ui->world = lilv_world_new();
		if(!ui->world)
		{
			free(ui);
			return NULL;
		}
		LilvNode *node_false = lilv_new_bool(ui->world, false);
		if(node_false)
		{
			lilv_world_set_option(ui->world, LILV_OPTION_DYN_MANIFEST, node_false);
			lilv_node_free(node_false);
		}
		lilv_world_load_all(ui->world);
		LilvNode *synthpod_bundle = lilv_new_uri(ui->world, "file://"SYNTHPOD_BUNDLE_DIR"/");
		if(synthpod_bundle)
		{
			lilv_world_load_bundle(ui->world, synthpod_bundle);
			lilv_node_free(synthpod_bundle);
		}
	}

	if(ui->win)
	{
		ui->plugitc = elm_genlist_item_class_new();
		if(ui->plugitc)
		{
			ui->plugitc->item_style = "default_style";
			ui->plugitc->func.text_get = _pluglist_label_get;
			ui->plugitc->func.content_get = NULL;
			ui->plugitc->func.state_get = NULL;
			ui->plugitc->func.del = _pluglist_del;
		}

		ui->propitc = elm_genlist_item_class_new();
		if(ui->propitc)
		{
			ui->propitc->item_style = "full";
			ui->propitc->func.text_get = NULL;
			ui->propitc->func.content_get = _property_content_get;
			ui->propitc->func.state_get = NULL;
			ui->propitc->func.del = _property_del;
		}

		ui->moditc = elm_genlist_item_class_new();
		if(ui->moditc)
		{
			ui->moditc->item_style = "full";
			ui->moditc->func.text_get = NULL;
			ui->moditc->func.content_get = _modlist_content_get;
			ui->moditc->func.state_get = NULL;
			ui->moditc->func.del = NULL;
		}

		ui->psetitc = elm_genlist_item_class_new();
		if(ui->psetitc)
		{
			ui->psetitc->item_style = "full";
			ui->psetitc->func.text_get = NULL;
			ui->psetitc->func.content_get = _modlist_psets_content_get;
			ui->psetitc->func.state_get = NULL;
			ui->psetitc->func.del = NULL;

			elm_genlist_item_class_ref(ui->psetitc);
		}

		ui->grpitc = elm_genlist_item_class_new();
		if(ui->grpitc)
		{
			ui->grpitc->item_style = "full";
			ui->grpitc->func.text_get = NULL;
			ui->grpitc->func.content_get = _group_content_get;
			ui->grpitc->func.state_get = NULL;
			ui->grpitc->func.del = _group_del;

			elm_genlist_item_class_ref(ui->grpitc); // used for genlist ordering
			elm_genlist_item_class_ref(ui->grpitc); // used for genlist ordering
		}

		ui->listitc = elm_genlist_item_class_new();
		if(ui->listitc)
		{
			ui->listitc->item_style = "full";
			ui->listitc->func.text_get = NULL;
			ui->listitc->func.content_get = _modlist_content_get;
			ui->listitc->func.state_get = NULL;
			ui->listitc->func.del = _modlist_del;
		}

		ui->stditc = elm_genlist_item_class_new();
		if(ui->stditc)
		{
			ui->stditc->item_style = "full";
			ui->stditc->func.text_get = NULL;
			ui->stditc->func.content_get = _modlist_std_content_get;
			ui->stditc->func.state_get = NULL;
			ui->stditc->func.del = NULL;
		}

		ui->psetbnkitc = elm_genlist_item_class_new();
		if(ui->psetbnkitc)
		{
			ui->psetbnkitc->item_style = "default";
			ui->psetbnkitc->func.text_get = _modlist_bank_label_get;
			ui->psetbnkitc->func.content_get = NULL;
			ui->psetbnkitc->func.state_get = NULL;
			ui->psetbnkitc->func.del = NULL;
		}

		ui->psetitmitc = elm_genlist_item_class_new();
		if(ui->psetitmitc)
		{
			ui->psetitmitc->item_style = "default";
			ui->psetitmitc->func.text_get = _modlist_pset_label_get;
			ui->psetitmitc->func.content_get = NULL;
			ui->psetitmitc->func.state_get = NULL;
			ui->psetitmitc->func.del = NULL;
		}

		ui->psetsaveitc = elm_genlist_item_class_new();
		if(ui->psetsaveitc)
		{
			ui->psetsaveitc->item_style = "full";
			ui->psetsaveitc->func.text_get = NULL;
			ui->psetsaveitc->func.content_get = _modlist_pset_content_get;
			ui->psetsaveitc->func.state_get = NULL;
			ui->psetsaveitc->func.del = NULL;
		}

		ui->griditc = elm_gengrid_item_class_new();
		if(ui->griditc)
		{
			ui->griditc->item_style = "synthpod";
			ui->griditc->func.text_get = _modgrid_label_get;
			ui->griditc->func.content_get = _modgrid_content_get;
			ui->griditc->func.state_get = NULL;
			ui->griditc->func.del = _modgrid_del;
		}

		ui->vbox = elm_box_add(ui->win);
		if(ui->vbox)
		{
			elm_box_homogeneous_set(ui->vbox, EINA_FALSE);
			elm_box_padding_set(ui->vbox, 0, 0);
			evas_object_size_hint_weight_set(ui->vbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(ui->vbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(ui->vbox);

			// get theme data items
			Evas_Object *theme = elm_layout_add(ui->vbox);
			if(theme)
			{
				elm_layout_file_set(theme, SYNTHPOD_DATA_DIR"/synthpod.edj",
					"/synthpod/theme");

				const char *colors_max = elm_layout_data_get(theme, "colors_max");
				ui->colors_max = colors_max ? atoi(colors_max) : 20;
				ui->colors_vec = calloc(ui->colors_max, sizeof(int));

				evas_object_del(theme);
			}
			else
			{
				ui->colors_max = 20;
			}

			elm_win_resize_object_add(ui->win, ui->vbox);
			evas_object_event_callback_add(ui->win, EVAS_CALLBACK_KEY_DOWN, _theme_key_down, ui);

			const Eina_Bool exclusive = EINA_FALSE;
			const Evas_Modifier_Mask ctrl_mask = evas_key_modifier_mask_get(
				evas_object_evas_get(ui->win), "Control");
			const Evas_Modifier_Mask shift_mask = evas_key_modifier_mask_get(
				evas_object_evas_get(ui->win), "Shift");
			// new
			if(!evas_object_key_grab(ui->win, "n", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'n' key\n");
			// open
			if(!evas_object_key_grab(ui->win, "o", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'o' key\n");
			// save and save-as
			if(!evas_object_key_grab(ui->win, "s", ctrl_mask | shift_mask, 0, exclusive))
				fprintf(stderr, "could not grab 's' key\n");
			// import
			if(!evas_object_key_grab(ui->win, "i", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'i' key\n");
			// export
			if(!evas_object_key_grab(ui->win, "e", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'e' key\n");
			// quit
			if(!evas_object_key_grab(ui->win, "q", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'q' key\n");
			// about
			if(!evas_object_key_grab(ui->win, "h", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'h' key\n");
			// matrix
			if(!evas_object_key_grab(ui->win, "m", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'm' key\n");
			// plugin
			if(!evas_object_key_grab(ui->win, "p", ctrl_mask, 0, exclusive))
				fprintf(stderr, "could not grab 'p' key\n");

			ui->mainmenu = elm_win_main_menu_get(ui->win);
			if(ui->mainmenu)
			{
				evas_object_show(ui->mainmenu);

				Elm_Object_Item *elmnt;

				if(ui->driver->features & SP_UI_FEATURE_NEW)
				{
					elmnt = elm_menu_item_add(ui->mainmenu, NULL, "document-new", "New", _menu_new, ui);
					elm_object_item_tooltip_text_set(elmnt, "Ctrl+N");
				}

				if(ui->driver->features & (SP_UI_FEATURE_OPEN | SP_UI_FEATURE_IMPORT_FROM) )
				{
					if(ui->driver->features & SP_UI_FEATURE_OPEN)
					{
						elmnt = elm_menu_item_add(ui->mainmenu, NULL, "document-new", "Open", _menu_open_fileselector, ui);
						elm_object_item_tooltip_text_set(elmnt, "Ctrl+O");
					}
					else if(ui->driver->features & SP_UI_FEATURE_IMPORT_FROM)
					{
						elmnt = elm_menu_item_add(ui->mainmenu, NULL, "document-import", "Import", _menu_open_fileselector, ui);
						elm_object_item_tooltip_text_set(elmnt, "Ctrl+I");
					}
				}

				if(ui->driver->features & SP_UI_FEATURE_SAVE)
				{
					elmnt = elm_menu_item_add(ui->mainmenu, NULL, "document-save", "Save", _menu_save, ui);
					elm_object_item_tooltip_text_set(elmnt, "Ctrl+S");
				}

				if(ui->driver->features & (SP_UI_FEATURE_SAVE_AS | SP_UI_FEATURE_EXPORT_TO) )
				{
					if(ui->driver->features & SP_UI_FEATURE_SAVE_AS)
					{
						elmnt = elm_menu_item_add(ui->mainmenu, NULL, "document-save-as", "Save as", _menu_save_as_fileselector, ui);
						elm_object_item_tooltip_text_set(elmnt, "Ctrl+Shift+S");
					}
					else if(ui->driver->features & SP_UI_FEATURE_EXPORT_TO)
					{
						elmnt = elm_menu_item_add(ui->mainmenu, NULL, "document-export", "Export", _menu_save_as_fileselector, ui);
						elm_object_item_tooltip_text_set(elmnt, "Ctrl+E");
					}
				}

				elmnt = elm_menu_item_add(ui->mainmenu, NULL, "list-add", "Plugin", _menu_plugin, ui);
				elm_object_item_tooltip_text_set(elmnt, "Ctrl+P");

				elmnt = elm_menu_item_add(ui->mainmenu, NULL, "applications-system", "Matrix", _menu_matrix, ui);
				elm_object_item_tooltip_text_set(elmnt, "Ctrl+M");

				if(ui->driver->features & SP_UI_FEATURE_CLOSE)
				{
					elmnt = elm_menu_item_add(ui->mainmenu, NULL, "application-exit", "Quit", _menu_close, ui);
					elm_object_item_tooltip_text_set(elmnt, "Ctrl+Q");
				}

				elmnt = elm_menu_item_add(ui->mainmenu, NULL, "help-about", "About", _menu_about, ui);
				elm_object_item_tooltip_text_set(elmnt, "Ctrl+H");
			}

			ui->popup = elm_popup_add(ui->vbox);
			if(ui->popup)
			{
				elm_popup_allow_events_set(ui->popup, EINA_TRUE);
				if(show_splash)
					evas_object_show(ui->popup);

				Evas_Object *hbox = elm_box_add(ui->popup);
				if(hbox)
				{
					elm_box_horizontal_set(hbox, EINA_TRUE);
					elm_box_homogeneous_set(hbox, EINA_FALSE);
					elm_box_padding_set(hbox, 10, 0);
					evas_object_show(hbox);
					elm_object_content_set(ui->popup, hbox);

					Evas_Object *icon = elm_icon_add(hbox);
					if(icon)
					{
						elm_image_file_set(icon, SYNTHPOD_DATA_DIR"/synthpod.edj",
							"/omk/logo");
						evas_object_size_hint_min_set(icon, 128, 128);
						evas_object_size_hint_max_set(icon, 256, 256);
						evas_object_size_hint_aspect_set(icon, EVAS_ASPECT_CONTROL_BOTH, 1, 1);
						evas_object_show(icon);
						elm_box_pack_end(hbox, icon);
					}

					Evas_Object *label = elm_label_add(hbox);
					if(label)
					{
						elm_object_text_set(label,
							"<color=#b00 shadow_color=#fff font_size=20>"
							"Synthpod - Plugin Container"
							"</color></br><align=left>"
							"Version "SYNTHPOD_VERSION"</br></br>"
							"Copyright (c) 2015 Hanspeter Portner</br></br>"
							"This is free and libre software</br>"
							"Released under Artistic License 2.0</br>"
							"By Open Music Kontrollers</br></br>"
							"<color=#bbb>"
							"http://open-music-kontrollers.ch/lv2/synthpod</br>"
							"dev@open-music-kontrollers.ch"
							"</color></align>");

						evas_object_show(label);
						elm_box_pack_end(hbox, label);
					}
				}
			}

			ui->selector = elm_popup_add(ui->vbox);
			if(ui->selector)
			{
				elm_popup_allow_events_set(ui->selector, EINA_FALSE);
			}

			ui->mainpane = elm_panes_add(ui->vbox);
			if(ui->mainpane)
			{
				elm_panes_horizontal_set(ui->mainpane, EINA_FALSE);
				elm_panes_content_left_size_set(ui->mainpane, 0.2);
				evas_object_size_hint_weight_set(ui->mainpane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(ui->mainpane, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(ui->mainpane);
				elm_box_pack_end(ui->vbox, ui->mainpane);

				ui->modlist = elm_genlist_add(ui->mainpane);
				if(ui->modlist)
				{
					elm_genlist_homogeneous_set(ui->modlist, EINA_TRUE); // needef for lazy-loading
					elm_genlist_mode_set(ui->modlist, ELM_LIST_LIMIT);
					elm_genlist_block_count_set(ui->modlist, 64); // needef for lazy-loading
					//elm_genlist_select_mode_set(ui->modlist, ELM_OBJECT_SELECT_MODE_NONE);
					elm_genlist_reorder_mode_set(ui->modlist, EINA_TRUE);
					evas_object_smart_callback_add(ui->modlist, "activated",
						_modlist_activated, ui);
					evas_object_smart_callback_add(ui->modlist, "moved",
						_modlist_moved, ui);
					evas_object_data_set(ui->modlist, "ui", ui);
					evas_object_size_hint_weight_set(ui->modlist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
					evas_object_size_hint_align_set(ui->modlist, EVAS_HINT_FILL, EVAS_HINT_FILL);
					evas_object_show(ui->modlist);
					elm_object_part_content_set(ui->mainpane, "left", ui->modlist);
				} // modlist

				ui->modgrid = elm_gengrid_add(ui->mainpane);
				if(ui->modgrid)
				{
					elm_gengrid_select_mode_set(ui->modgrid, ELM_OBJECT_SELECT_MODE_NONE);
					elm_gengrid_reorder_mode_set(ui->modgrid, EINA_TRUE);
					elm_gengrid_horizontal_set(ui->modgrid, EINA_TRUE);
					elm_scroller_policy_set(ui->modgrid, ELM_SCROLLER_POLICY_AUTO, ELM_SCROLLER_POLICY_OFF);
					elm_scroller_single_direction_set(ui->modgrid, ELM_SCROLLER_SINGLE_DIRECTION_HARD);
					elm_gengrid_item_size_set(ui->modgrid, 200, 200);
					evas_object_smart_callback_add(ui->modgrid, "changed",
						_modgrid_changed, ui);
					evas_object_data_set(ui->modgrid, "ui", ui);
					evas_object_size_hint_weight_set(ui->modgrid, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
					evas_object_size_hint_align_set(ui->modgrid, EVAS_HINT_FILL, EVAS_HINT_FILL);
					evas_object_show(ui->modgrid);
					elm_object_part_content_set(ui->mainpane, "right", ui->modgrid);
				} // modgrid
			} // mainpane
			
			ui->statusline = elm_label_add(ui->vbox);
			if(ui->statusline)
			{
				//TODO use
				elm_object_text_set(ui->statusline, "");
				evas_object_size_hint_weight_set(ui->statusline, EVAS_HINT_EXPAND, 0.f);
				evas_object_size_hint_align_set(ui->statusline, 0.f, 1.f);
				evas_object_show(ui->statusline);
				elm_box_pack_end(ui->vbox, ui->statusline);
			} // statusline
		} // theme

		// listen for elm config changes
		ecore_event_handler_add(ELM_EVENT_CONFIG_ALL_CHANGED, _elm_config_changed, ui);
	}

	// initialzie registry
	sp_regs_init(&ui->regs, ui->world, ui->driver->map);

	// fill from_app binary callback tree
	{
		unsigned ptr = 0;

		from_apps[ptr].protocol = ui->regs.synthpod.module_add.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_module_add;

		from_apps[ptr].protocol = ui->regs.synthpod.module_del.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_module_del;

		from_apps[ptr].protocol = ui->regs.synthpod.module_preset_save.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_module_preset_save;

		from_apps[ptr].protocol = ui->regs.synthpod.module_selected.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_module_selected;

		from_apps[ptr].protocol = ui->regs.synthpod.module_visible.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_module_visible;

		from_apps[ptr].protocol = ui->regs.synthpod.module_embedded.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_module_embedded;

		from_apps[ptr].protocol = ui->regs.synthpod.port_connected.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_port_connected;

		from_apps[ptr].protocol = ui->regs.port.float_protocol.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_float_protocol;

		from_apps[ptr].protocol = ui->regs.port.peak_protocol.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_peak_protocol;

		from_apps[ptr].protocol = ui->regs.port.atom_transfer.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_atom_transfer;

		from_apps[ptr].protocol = ui->regs.port.event_transfer.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_event_transfer;

		from_apps[ptr].protocol = ui->regs.synthpod.port_selected.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_port_selected;

		from_apps[ptr].protocol = ui->regs.synthpod.port_monitored.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_port_monitored;

		from_apps[ptr].protocol = ui->regs.synthpod.module_list.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_module_list;

		from_apps[ptr].protocol = ui->regs.synthpod.bundle_load.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_bundle_load;

		from_apps[ptr].protocol = ui->regs.synthpod.bundle_save.urid;
		from_apps[ptr++].cb = _sp_ui_from_app_bundle_save;

		assert(ptr == FROM_APP_NUM);
		// sort according to URID
		qsort(from_apps, FROM_APP_NUM, sizeof(from_app_t), _from_app_cmp);
	}

	// walk plugin directories
	ui->plugs = lilv_world_get_all_plugins(ui->world);

	// populate uri_to_id
	ui->uri_to_id.callback_data = ui;
	ui->uri_to_id.uri_to_id = _uri_to_id;

	return ui;
}

void
sp_ui_resize(sp_ui_t *ui, int w, int h)
{
	if(!ui)
		return;

	if(ui->vbox)
		evas_object_resize(ui->vbox, w, h);
}

void
sp_ui_iterate(sp_ui_t *ui)
{
	ecore_main_loop_iterate();
}

void
sp_ui_refresh(sp_ui_t *ui)
{
	if(!ui)
		return;

	/*
	ui->dirty = 1; // disable ui -> app communication
	elm_genlist_clear(ui->modlist);
	ui->dirty = 0; // enable ui -> app communication
	*/

	_modlist_refresh(ui);
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

	if(ui->colors_vec)
		free(ui->colors_vec);

	if(ui->bundle_path)
		free(ui->bundle_path);

	evas_object_event_callback_del(ui->win, EVAS_CALLBACK_KEY_DOWN, _theme_key_down);

	if(ui->plugitc)
		elm_genlist_item_class_free(ui->plugitc);
	if(ui->griditc)
		elm_gengrid_item_class_free(ui->griditc);
	if(ui->listitc)
		elm_genlist_item_class_free(ui->listitc);
	if(ui->moditc)
		elm_genlist_item_class_free(ui->moditc);
	if(ui->grpitc)
	{
		elm_genlist_item_class_unref(ui->grpitc);
		elm_genlist_item_class_unref(ui->grpitc);
		elm_genlist_item_class_free(ui->grpitc);
	}
	if(ui->psetitc)
	{
		elm_genlist_item_class_unref(ui->psetitc);
		elm_genlist_item_class_free(ui->psetitc);
	}
	if(ui->stditc)
		elm_genlist_item_class_free(ui->stditc);
	if(ui->psetbnkitc)
		elm_genlist_item_class_free(ui->psetbnkitc);
	if(ui->psetitmitc)
		elm_genlist_item_class_free(ui->psetitmitc);
	if(ui->psetsaveitc)
		elm_genlist_item_class_free(ui->psetsaveitc);
	if(ui->propitc)
		elm_genlist_item_class_free(ui->propitc);

	sp_regs_deinit(&ui->regs);

	if(!ui->embedded)
		lilv_world_free(ui->world);

	free(ui);
}

void
sp_ui_del(sp_ui_t *ui, bool delete_self)
{
	if(ui->modgrid)
	{
		elm_gengrid_clear(ui->modgrid);
		evas_object_del(ui->modgrid);
	}

	if(ui->modlist)
	{
		elm_genlist_clear(ui->modlist);
		evas_object_del(ui->modlist);
	}

	if(ui->plugwin)
		evas_object_del(ui->plugwin);

	if(ui->patchwin)
		evas_object_del(ui->patchwin);

	if(ui->mainpane)
		evas_object_del(ui->mainpane);
	if(ui->popup)
		evas_object_del(ui->popup);
	if(ui->selector)
		evas_object_del(ui->selector);
	if(ui->vbox)
	{
		elm_box_clear(ui->vbox);
		if(delete_self)
			evas_object_del(ui->vbox);
	}
}

void
sp_ui_bundle_load(sp_ui_t *ui, const char *bundle_path, int update_path)
{
	if(!ui || !bundle_path)
		return;

	// update internal bundle_path for one-click-save
	if(update_path)
	{
		if(ui->bundle_path)
			free(ui->bundle_path);
		ui->bundle_path = strdup(bundle_path);
	}

	// signal to app
	size_t size = sizeof(transmit_bundle_load_t)
		+ lv2_atom_pad_size(strlen(bundle_path) + 1);
	transmit_bundle_load_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_bundle_load_fill(&ui->regs, &ui->forge, trans, size,
			-1, bundle_path);
		_sp_ui_to_app_advance(ui, size);
	}
}

void
sp_ui_bundle_new(sp_ui_t *ui)
{
	if(!ui)
		return;

	_modlist_clear(ui, false, true); // do not clear system ports
}

void
sp_ui_bundle_save(sp_ui_t *ui, const char *bundle_path, int update_path)
{
	if(!ui || !bundle_path)
		return;

	// update internal bundle_path for one-click-save
	if(update_path)
	{
		if(ui->bundle_path)
			free(ui->bundle_path);
		ui->bundle_path = strdup(bundle_path);
	}

	// signal to app
	size_t size = sizeof(transmit_bundle_save_t)
		+ lv2_atom_pad_size(strlen(bundle_path) + 1);
	transmit_bundle_save_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_bundle_save_fill(&ui->regs, &ui->forge, trans, size,
			-1, bundle_path);
		_sp_ui_to_app_advance(ui, size);
	}
}
