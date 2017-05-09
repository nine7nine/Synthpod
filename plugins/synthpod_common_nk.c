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

#include <synthpod_lv2.h>
#include <synthpod_patcher.h>

#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/options/options.h"
#include "lv2/lv2plug.in/ns/ext/port-groups/port-groups.h"
#include "lv2/lv2plug.in/ns/ext/presets/presets.h"
#include "lv2/lv2plug.in/ns/ext/patch/patch.h"

#include <osc.lv2/osc.h>
#include <xpress.lv2/xpress.h>

#include <sandbox_master.h>

#include <math.h>
#include <unistd.h> // fork
#include <sys/wait.h> // waitpid
#include <errno.h> // waitpid

#define NK_PUGL_API
#include <nk_pugl/nk_pugl.h>

#include <lilv/lilv.h>

#define SEARCH_BUF_MAX 128
#define ATOM_BUF_MAX 0x100000 // 1M
#define CONTROL 14 //FIXME

#ifdef Bool
#	undef Bool // interferes with atom forge
#endif

typedef enum _property_type_t property_type_t;
typedef enum _selector_search_t selector_search_t;

typedef union _param_union_t param_union_t;

typedef struct _hash_t hash_t;
typedef struct _chunk_t chunk_t;
typedef struct _scale_point_t scale_point_t;
typedef struct _control_port_t control_port_t;
typedef struct _audio_port_t audio_port_t;
typedef struct _port_t port_t;
typedef struct _param_t param_t;
typedef struct _mod_conn_t mod_conn_t;
typedef struct _mod_ui_t mod_ui_t;
typedef struct _port_conn_t port_conn_t;
typedef struct _mod_t mod_t;
typedef struct _plughandle_t plughandle_t;
typedef struct _prof_t prof_t;
typedef enum  _auto_type_t auto_type_t;
typedef struct _midi_auto_t midi_auto_t;
typedef struct _osc_auto_t osc_auto_t;
typedef struct _auto_t auto_t;

enum _property_type_t {
	PROPERTY_TYPE_NONE				= 0,

	PROPERTY_TYPE_CONTROL			= (1 << 0),
	PROPERTY_TYPE_PARAM				= (1 << 1),

	PROPERTY_TYPE_AUDIO				= (1 << 2),
	PROPERTY_TYPE_CV					= (1 << 3),
	PROPERTY_TYPE_ATOM				= (1 << 4),

	PROPERTY_TYPE_MIDI				= (1 << 8),
	PROPERTY_TYPE_OSC					= (1 << 9),
	PROPERTY_TYPE_TIME				= (1 << 10),
	PROPERTY_TYPE_PATCH				= (1 << 11),
	PROPERTY_TYPE_XPRESS			= (1 << 12),
	PROPERTY_TYPE_AUTOMATION	= (1 << 13),

	PROPERTY_TYPE_MAX
};

enum _selector_search_t {
	SELECTOR_SEARCH_NAME = 0,
	SELECTOR_SEARCH_COMMENT,
	SELECTOR_SEARCH_AUTHOR,
	SELECTOR_SEARCH_CLASS,
	SELECTOR_SEARCH_PROJECT,

	SELECTOR_SEARCH_MAX
};

struct _hash_t {
	void **nodes;
	unsigned size;
};

struct _chunk_t {
	uint32_t size;
	uint8_t *body;
};

union _param_union_t {
	int32_t b;
	int32_t i;
	int64_t h;
	float f;
	double d;
	double u;
	struct nk_text_edit editor;
	chunk_t chunk;
};

struct _scale_point_t {
	char *label;
	param_union_t val;
};

enum _auto_type_t {
	AUTO_NONE = 0,
	AUTO_MIDI,
	AUTO_OSC,

	AUTO_MAX,
};

struct _midi_auto_t {
	int channel;
	int controller;
	int a;
	int b;
};

struct _osc_auto_t {
	char *path;
	float min;
	float max;
};

struct _auto_t {
	auto_type_t type;

	double c;
	double d;

	union {
		midi_auto_t midi;
		osc_auto_t osc;
	};
};

struct _control_port_t {
	hash_t points;
	scale_point_t *points_ref;
	param_union_t min;
	param_union_t max;
	param_union_t span;
	param_union_t val;
	bool is_int;
	bool is_bool;
	bool is_readonly;
	auto_t automation;
};

struct _audio_port_t {
	float peak;
	float gain;
};

struct _port_t {
	property_type_t type;
	uint32_t index;
	char *name;
	const char *symbol;
	mod_t *mod;
	const LilvPort *port;
	LilvNodes *groups;

	union {
		control_port_t control;
		audio_port_t audio;
	};
};

struct _param_t {
	bool is_readonly;
	LV2_URID property;
	LV2_URID range;
	mod_t *mod;

	param_union_t min;
	param_union_t max;
	param_union_t span;
	param_union_t val;

	char *label;
	char *comment;
	LV2_URID units_unit;
	char *units_symbol;
	auto_t automation;
};

struct _prof_t {
	float min;
	float avg;
	float max;
};

struct _mod_t {
	plughandle_t *handle;

	LV2_URID urn;
	const LilvPlugin *plug;
	LilvUIs *ui_nodes;
	hash_t uis;

	hash_t ports;
	hash_t groups;
	hash_t banks;
	hash_t params;
	hash_t dynams;

	LilvNodes *readables;
	LilvNodes *writables;
	LilvNodes *presets;

	struct nk_vec2 pos;
	struct nk_vec2 dim;
	bool moving;
	bool hovered;
	bool hilighted;

	hash_t sources;
	hash_t sinks;

	property_type_t source_type;
	property_type_t sink_type;

	prof_t prof;
};

struct _port_conn_t {
	port_t *source_port;
	port_t *sink_port;
};

struct _mod_conn_t {
	mod_t *source_mod;
	mod_t *sink_mod;
	property_type_t source_type;
	property_type_t sink_type;
	hash_t conns;

	struct nk_vec2 pos;
	bool moving;
};

struct _mod_ui_t {
	mod_t *mod;
	const LilvUI *ui;
	const char *uri;

	pid_t pid;
	struct {
		sandbox_master_driver_t driver;
		sandbox_master_t *sb;
		char *socket_uri;
		char *bundle_path;
		char *window_name;
		char *update_rate;
	} sbox;
};

struct _plughandle_t {
	LilvWorld *world;

	LV2_Atom_Forge forge;

	LV2_URID atom_eventTransfer;
	LV2_URID bundle_urn;
	LV2_URID self_urn;

	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	LV2UI_Write_Function writer;
	LV2UI_Controller controller;

	nk_pugl_window_t win;

	mod_t *module_selector;
	port_t *port_selector;
	param_t *param_selector;

	hash_t mods;
	hash_t conns;

	struct {
		LilvNode *pg_group;
		LilvNode *lv2_integer;
		LilvNode *lv2_toggled;
		LilvNode *lv2_minimum;
		LilvNode *lv2_maximum;
		LilvNode *lv2_default;
		LilvNode *pset_Preset;
		LilvNode *pset_bank;
		LilvNode *rdfs_comment;
		LilvNode *rdfs_range;
		LilvNode *doap_name;
		LilvNode *lv2_minorVersion;
		LilvNode *lv2_microVersion;
		LilvNode *doap_license;
		LilvNode *rdfs_label;
		LilvNode *lv2_name;
		LilvNode *lv2_OutputPort;
		LilvNode *lv2_AudioPort;
		LilvNode *lv2_CVPort;
		LilvNode *lv2_ControlPort;
		LilvNode *atom_AtomPort;
		LilvNode *patch_readable;
		LilvNode *patch_writable;
		LilvNode *rdf_type;
		LilvNode *lv2_Plugin;
		LilvNode *midi_MidiEvent;
		LilvNode *osc_Message;
		LilvNode *time_Position;
		LilvNode *patch_Message;
		LilvNode *xpress_Message;
	} node;

	float dy;
	float dy2;

	enum nk_collapse_states plugin_collapse_states;
	enum nk_collapse_states preset_import_collapse_states;
	enum nk_collapse_states preset_export_collapse_states;
	enum nk_collapse_states plugin_info_collapse_states;
	enum nk_collapse_states preset_info_collapse_states;

	selector_search_t plugin_search_selector;
	selector_search_t preset_search_selector;
	selector_search_t port_search_selector;

	hash_t plugin_matches;
	hash_t preset_matches;
	hash_t port_matches;
	hash_t param_matches;
	hash_t dynam_matches;

	char plugin_search_buf [SEARCH_BUF_MAX];
	char preset_search_buf [SEARCH_BUF_MAX];
	char port_search_buf [SEARCH_BUF_MAX];

	struct nk_text_edit plugin_search_edit;
	struct nk_text_edit preset_search_edit;
	struct nk_text_edit port_search_edit;

	bool first;

	reg_t regs;
	union {
		LV2_Atom atom;
		uint8_t buf [ATOM_BUF_MAX];
	};

	bool has_control_a;

	struct nk_vec2 scrolling;
	struct nk_vec2 nxt;
	float scale;

	bool plugin_find_matches;
	bool preset_find_matches;
	bool prop_find_matches;

	struct {
		bool active;
		mod_t *source_mod;
	} linking;

	property_type_t type;

	bool done;

	prof_t prof;

	float sample_rate;
	float update_rate;
};

static const char *search_labels [SELECTOR_SEARCH_MAX] = {
	[SELECTOR_SEARCH_NAME] = "Name",
	[SELECTOR_SEARCH_COMMENT] = "Comment",
	[SELECTOR_SEARCH_AUTHOR] = "Author",
	[SELECTOR_SEARCH_CLASS] = "Class",
	[SELECTOR_SEARCH_PROJECT] = "Project"
};

static const struct nk_color grid_line_color = {40, 40, 40, 255};
static const struct nk_color grid_background_color = {30, 30, 30, 255};
static const struct nk_color hilight_color = {200, 100, 0, 255};
static const struct nk_color button_border_color = {100, 100, 100, 255};
static const struct nk_color grab_handle_color = {100, 100, 100, 255};
static const struct nk_color toggle_color = {150, 150, 150, 255};
static const struct nk_color head_color = {12, 12, 12, 255};
static const struct nk_color group_color = {24, 24, 24, 255};

static const char *auto_labels [] = {
	[AUTO_NONE] = "None",
	[AUTO_MIDI] = "MIDI",
	[AUTO_OSC] = "OSC"
};

static inline bool
_message_request(plughandle_t *handle)
{
	lv2_atom_forge_set_buffer(&handle->forge, handle->buf, ATOM_BUF_MAX);
	return true;
}

static inline void
_message_write(plughandle_t *handle)
{
	handle->writer(handle->controller, CONTROL, lv2_atom_total_size(&handle->atom),
		handle->regs.port.event_transfer.urid, &handle->atom);
}

static size_t
_textedit_len(struct nk_text_edit *edit)
{
	return nk_str_len(&edit->string);
}

static const char *
_textedit_const(struct nk_text_edit *edit)
{
	return nk_str_get_const(&edit->string);
}

static void
_textedit_zero_terminate(struct nk_text_edit *edit)
{
	char *str = nk_str_get(&edit->string);
	if(str)
		str[nk_str_len(&edit->string)] = '\0';
}

#define HASH_FOREACH(hash, itr) \
	for(void **(itr) = (hash)->nodes; (itr) - (hash)->nodes < (hash)->size; (itr)++)

#define HASH_FREE(hash, ptr) \
	for(void *(ptr) = _hash_pop((hash)); (ptr); (ptr) = _hash_pop((hash)))

static bool
_hash_empty(hash_t *hash)
{
	return hash->size == 0;
}

static size_t
_hash_size(hash_t *hash)
{
	return hash->size;
}

static void
_hash_add(hash_t *hash, void *node)
{
	hash->nodes = realloc(hash->nodes, (hash->size + 1)*sizeof(void *));
	if(hash->nodes)
	{
		hash->nodes[hash->size] = node;
		hash->size++;
	}
}

static void
_hash_remove(hash_t *hash, void *node)
{
	void **nodes = NULL;
	size_t size = 0;

	HASH_FOREACH(hash, node_itr)
	{
		void *node_ptr = *node_itr;

		if(node_ptr != node)
		{
			nodes = realloc(nodes, (size + 1)*sizeof(void *));
			if(nodes)
			{
				nodes[size] = node_ptr;
				size++;
			}
		}
	}

	free(hash->nodes);
	hash->nodes = nodes;
	hash->size = size;
}

static void
_hash_remove_cb(hash_t *hash, bool (*cb)(void *node, void *data), void *data)
{
	void **nodes = NULL;
	size_t size = 0;

	HASH_FOREACH(hash, node_itr)
	{
		void *node_ptr = *node_itr;

		if(cb(node_ptr, data))
		{
			nodes = realloc(nodes, (size + 1)*sizeof(void *));
			if(nodes)
			{
				nodes[size] = node_ptr;
				size++;
			}
		}
	}

	free(hash->nodes);
	hash->nodes = nodes;
	hash->size = size;
}

static void
_hash_free(hash_t *hash)
{
	free(hash->nodes);
	hash->nodes = NULL;
	hash->size = 0;
}

static void *
_hash_pop(hash_t *hash)
{
	if(hash->size)
	{
		void *node = hash->nodes[--hash->size];

		if(!hash->size)
			_hash_free(hash);

		return node;
	}

	return NULL;
}

static void
_hash_sort(hash_t *hash, int (*cmp)(const void *a, const void *b))
{
	if(hash->size)
		qsort(hash->nodes, hash->size, sizeof(void *), cmp);
}

static void
_hash_sort_r(hash_t *hash, int (*cmp)(const void *a, const void *b, void *data),
	void *data)
{
	if(hash->size)
		qsort_r(hash->nodes, hash->size, sizeof(void *), cmp, data);
}

static int
_node_as_int(const LilvNode *node, int dflt)
{
	if(lilv_node_is_int(node))
		return lilv_node_as_int(node);
	else if(lilv_node_is_float(node))
		return floorf(lilv_node_as_float(node));
	else if(lilv_node_is_bool(node))
		return lilv_node_as_bool(node) ? 1 : 0;
	else
		return dflt;
}

static float
_node_as_float(const LilvNode *node, float dflt)
{
	if(lilv_node_is_int(node))
		return lilv_node_as_int(node);
	else if(lilv_node_is_float(node))
		return lilv_node_as_float(node);
	else if(lilv_node_is_bool(node))
		return lilv_node_as_bool(node) ? 1.f : 0.f;
	else
		return dflt;
}

static int32_t
_node_as_bool(const LilvNode *node, int32_t dflt)
{
	if(lilv_node_is_int(node))
		return lilv_node_as_int(node) != 0;
	else if(lilv_node_is_float(node))
		return lilv_node_as_float(node) != 0.f;
	else if(lilv_node_is_bool(node))
		return lilv_node_as_bool(node) ? 1 : 0;
	else
		return dflt;
}

static LV2_Atom_Forge_Ref
_patch_connection_internal(plughandle_t *handle, port_t *source_port, port_t *sink_port)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.source_module.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&handle->forge, source_port->mod->urn);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.source_symbol.urid);
	if(ref)
		ref = lv2_atom_forge_string(&handle->forge, source_port->symbol, strlen(source_port->symbol));

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_module.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&handle->forge, sink_port->mod->urn);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_symbol.urid);
	if(ref)
		ref = lv2_atom_forge_string(&handle->forge, sink_port->symbol, strlen(sink_port->symbol));

	return ref;
}

static void
_patch_connection_add(plughandle_t *handle, port_t *source_port, port_t *sink_port)
{
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_add_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.connection_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, 0)
		&& _patch_connection_internal(handle, source_port, sink_port) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}
}

static void
_patch_connection_remove(plughandle_t *handle, port_t *source_port, port_t *sink_port)
{
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_remove_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.connection_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, 0)
		&& _patch_connection_internal(handle, source_port, sink_port) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}
}

static LV2_Atom_Forge_Ref
_patch_subscription_internal(plughandle_t *handle, port_t *source_port)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_module.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&handle->forge, source_port->mod->urn);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_symbol.urid);
	if(ref)
		ref = lv2_atom_forge_string(&handle->forge, source_port->symbol, strlen(source_port->symbol));

	return ref;
}

static void
_patch_subscription_add(plughandle_t *handle, port_t *source_port)
{
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_add_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.subscription_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, 0)
		&& _patch_subscription_internal(handle, source_port) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}
}

static void
_patch_subscription_remove(plughandle_t *handle, port_t *source_port)
{
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_remove_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.subscription_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, 0)
		&& _patch_subscription_internal(handle, source_port) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}
}

static void
_mod_unsubscribe_all(plughandle_t *handle, mod_t *mod)
{
	HASH_FOREACH(&mod->ports, port_itr)
	{
		port_t *port = *port_itr;

		_patch_subscription_remove(handle, port);
	}
}

static void
_mod_subscribe_all(plughandle_t *handle, mod_t *mod)
{
	HASH_FOREACH(&mod->ports, port_itr)
	{
		port_t *port = *port_itr;

		_patch_subscription_add(handle, port);
	}
}

static LV2_Atom_Forge_Ref
_patch_notification_internal(plughandle_t *handle, port_t *source_port,
	uint32_t size, LV2_URID type, const void *body)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_module.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&handle->forge, source_port->mod->urn);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_symbol.urid);
	if(ref)
		ref = lv2_atom_forge_string(&handle->forge, source_port->symbol, strlen(source_port->symbol));

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.rdf.value.urid);
	if(ref)
		ref = lv2_atom_forge_atom(&handle->forge, size, type);
	if(ref)
		ref = lv2_atom_forge_write(&handle->forge, body, size);

	return ref;
}

static void
_patch_notification_add(plughandle_t *handle, port_t *source_port,
	LV2_URID proto, uint32_t size, LV2_URID type, const void *body)
{
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_add_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.notification_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, proto)
		&& _patch_notification_internal(handle, source_port, size, type, body) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}
}

static LV2_Atom_Forge_Ref
_patch_notification_patch_set_internal(plughandle_t *handle, port_t *source_port,
	LV2_URID subject, int32_t seqn, LV2_URID property,
	uint32_t size, LV2_URID type, const void *body)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_module.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&handle->forge, source_port->mod->urn);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_symbol.urid);
	if(ref)
		ref = lv2_atom_forge_string(&handle->forge, source_port->symbol, strlen(source_port->symbol));

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.rdf.value.urid);
	if(ref)
		ref = synthpod_patcher_set(&handle->regs, &handle->forge,
			subject, seqn, property, size, type, body);

	return ref;
}

static void
_patch_notification_add_patch_set(plughandle_t *handle, mod_t *mod,
	LV2_URID proto, LV2_URID subject, int32_t seqn, LV2_URID property,
	uint32_t size, LV2_URID type, const void *body)
{
	LV2_Atom_Forge_Frame frame [3];

	HASH_FOREACH(&mod->ports, port_itr)
	{
		port_t *port = *port_itr;

		if(!(port->type & PROPERTY_TYPE_PATCH))
			continue;

		//FIXME set patch:destination to handle->regs.core.plugin.urid to omit feedback
		if(  _message_request(handle)
			&& synthpod_patcher_add_object(&handle->regs, &handle->forge, &frame[0],
				0, 0, handle->regs.synthpod.notification_list.urid) //TODO subject
			&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, proto)
			&& _patch_notification_patch_set_internal(handle, port,
				subject, seqn, property, size, type, body) )
		{
			synthpod_patcher_pop(&handle->forge, frame, 3);
			_message_write(handle);
		}
	}
}

static LV2_Atom_Forge_Ref
_patch_notification_patch_get_internal(plughandle_t *handle, port_t *source_port,
	LV2_URID subject, int32_t seqn, LV2_URID property)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_module.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&handle->forge, source_port->mod->urn);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_symbol.urid);
	if(ref)
		ref = lv2_atom_forge_string(&handle->forge, source_port->symbol, strlen(source_port->symbol));

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.rdf.value.urid);
	if(ref)
		ref = synthpod_patcher_get(&handle->regs, &handle->forge,
			subject, seqn, property);

	return ref;
}

static void
_patch_notification_add_patch_get(plughandle_t *handle, mod_t *mod,
	LV2_URID proto, LV2_URID subject, int32_t seqn, LV2_URID property)
{
	LV2_Atom_Forge_Frame frame [3];

	HASH_FOREACH(&mod->ports, port_itr)
	{
		port_t *port = *port_itr;

		if(!(port->type & PROPERTY_TYPE_PATCH))
			continue;

		//FIXME set patch:destination to handle->regs.core.plugin.urid to omit feedback
		if(  _message_request(handle)
			&& synthpod_patcher_add_object(&handle->regs, &handle->forge, &frame[0],
				0, 0, handle->regs.synthpod.notification_list.urid) //TODO subject
			&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, proto)
			&& _patch_notification_patch_get_internal(handle, port,
				subject, seqn, property) ) // property == 0 -> get all
		{
			synthpod_patcher_pop(&handle->forge, frame, 3);
			_message_write(handle);
		}
	}
}

static LV2_Atom_Forge_Ref
_patch_port_automation_internal(plughandle_t *handle, port_t *source_port)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_module.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&handle->forge, source_port->mod->urn);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_symbol.urid);
	if(ref)
		ref = lv2_atom_forge_string(&handle->forge, source_port->symbol, strlen(source_port->symbol));

	return ref;
}

static LV2_Atom_Forge_Ref
_patch_param_automation_internal(plughandle_t *handle, param_t *source_param)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_module.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&handle->forge, source_param->mod->urn);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.patch.property.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&handle->forge, source_param->property);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.rdfs.range.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&handle->forge, source_param->range);

	return ref;
}

static LV2_Atom_Forge_Ref
_patch_midi_automation_internal(plughandle_t *handle, auto_t *automation)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(&handle->forge, handle->regs.midi.channel.urid);
	if(ref)
		ref = lv2_atom_forge_int(&handle->forge, automation->midi.channel);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.midi.controller_number.urid);
	if(ref)
		ref = lv2_atom_forge_int(&handle->forge, automation->midi.controller);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.source_min.urid);
	if(ref)
		ref = lv2_atom_forge_double(&handle->forge, automation->midi.a);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.source_max.urid);
	if(ref)
		ref = lv2_atom_forge_double(&handle->forge, automation->midi.b);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_min.urid);
	if(ref)
		ref = lv2_atom_forge_double(&handle->forge, automation->c);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_max.urid);
	if(ref)
		ref = lv2_atom_forge_double(&handle->forge, automation->d);

	return ref;
}

static void
_patch_port_midi_automation_add(plughandle_t *handle, port_t *source_port,
	auto_t *automation)
{
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_add_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.automation_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, handle->regs.midi.Controller.urid)
		&& _patch_port_automation_internal(handle, source_port)
		&& _patch_midi_automation_internal(handle, automation) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}
}

static void
_patch_port_automation_remove(plughandle_t *handle, port_t *source_port)
{
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_remove_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.automation_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, 0)
		&& _patch_port_automation_internal(handle, source_port) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}
}

static inline double
_param_union_as_double(LV2_Atom_Forge *forge, LV2_URID range, param_union_t *pu)
{
	if(range == forge->Bool)
		return pu->b;
	else if(range == forge->Int)
		return pu->i;
	else if(range == forge->Long)
		return pu->h;
	else if(range == forge->Float)
		return pu->f;
	else if(range == forge->Double)
		return pu->d;
	else if(range == forge->URID)
		return pu->u;

	return 0.0;
}

static void
_patch_param_midi_automation_add(plughandle_t *handle, param_t *source_param,
	auto_t *automation)
{
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_add_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.automation_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, handle->regs.midi.Controller.urid)
		&& _patch_param_automation_internal(handle, source_param)
		&& _patch_midi_automation_internal(handle, automation) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}
}

static void
_patch_param_automation_remove(plughandle_t *handle, param_t *source_param)
{
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_remove_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.automation_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, 0)
		&& _patch_param_automation_internal(handle, source_param) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}
}

static void
_port_conn_free(port_conn_t *port_conn);

static mod_conn_t *
_mod_conn_find(plughandle_t *handle, mod_t *source_mod, mod_t *sink_mod)
{
	HASH_FOREACH(&handle->conns, mod_conn_itr)
	{
		mod_conn_t *mod_conn = *mod_conn_itr;

		if( (mod_conn->source_mod == source_mod) && (mod_conn->sink_mod == sink_mod) )
			return mod_conn;
	}

	return NULL;
}

static mod_conn_t *
_mod_conn_add(plughandle_t *handle, mod_t *source_mod, mod_t *sink_mod)
{
	mod_conn_t *mod_conn = calloc(1, sizeof(mod_conn_t));
	if(mod_conn)
	{
		mod_conn->source_mod = source_mod;
		mod_conn->sink_mod = sink_mod;
		mod_conn->pos = nk_vec2(
			(source_mod->pos.x + sink_mod->pos.x)/2,
			(source_mod->pos.y + sink_mod->pos.y)/2);
		mod_conn->source_type = PROPERTY_TYPE_NONE;
		mod_conn->sink_type = PROPERTY_TYPE_NONE;
		_hash_add(&handle->conns, mod_conn);
	}

	return mod_conn;
}

static void
_mod_conn_free(plughandle_t *handle, mod_conn_t *mod_conn)
{
	HASH_FREE(&mod_conn->conns, port_conn_ptr)
	{
		port_conn_t *port_conn = port_conn_ptr;

		_port_conn_free(port_conn);
	}

	free(mod_conn);
}

static void
_mod_conn_remove(plughandle_t *handle, mod_conn_t *mod_conn)
{
	_hash_remove(&handle->conns, mod_conn);
	_mod_conn_free(handle, mod_conn);
}

static void
_mod_conn_refresh_type(mod_conn_t *mod_conn)
{
	mod_conn->source_type = PROPERTY_TYPE_NONE;
	mod_conn->sink_type = PROPERTY_TYPE_NONE;

	HASH_FOREACH(&mod_conn->conns, port_conn_itr)
	{
		port_conn_t *port_conn = *port_conn_itr;

		mod_conn->source_type |= port_conn->source_port->type;
		mod_conn->sink_type |= port_conn->sink_port->type;
	}
}

static port_t *
_mod_port_find_by_symbol(mod_t *mod, const char *symbol)
{
	HASH_FOREACH(&mod->ports, port_itr)
	{
		port_t *port = *port_itr;

		if(!strcmp(port->symbol, symbol))
			return port;
	}

	return NULL;
}

static port_t *
_mod_port_find_by_index(mod_t *mod, uint32_t index)
{
	HASH_FOREACH(&mod->ports, port_itr)
	{
		port_t *port = *port_itr;

		if(port->index == index)
			return port;
	}

	return NULL;
}

static param_t *
_mod_param_find_by_property(mod_t *mod, LV2_URID property)
{
	HASH_FOREACH(&mod->params, param_itr)
	{
		param_t *param = *param_itr;

		if(param->property == property)
			return param;
	}
	HASH_FOREACH(&mod->dynams, param_itr)
	{
		param_t *param = *param_itr;

		if(param->property == property)
			return param;
	}

	return NULL;
}

static param_t *
_mod_dynam_find_by_property(mod_t *mod, LV2_URID property)
{
	HASH_FOREACH(&mod->dynams, param_itr)
	{
		param_t *param = *param_itr;

		if(param->property == property)
			return param;
	}

	return NULL;
}

static mod_t *
_mod_find_by_urn(plughandle_t *handle, LV2_URID urn)
{
	HASH_FOREACH(&handle->mods, mod_itr)
	{
		mod_t *mod = *mod_itr;

		if(mod->urn == urn)
			return mod;
	}

	return NULL;
}

static mod_ui_t *
_mod_ui_get_first(mod_t *mod)
{
	HASH_FOREACH(&mod->uis, mod_ui_itr)
	{
		mod_ui_t *mod_ui = *mod_ui_itr;

		return mod_ui;
	}

	return NULL;
}

static bool
_mod_ui_is_running(mod_ui_t *mod_ui)
{
	return (mod_ui->pid != 0) && mod_ui->sbox.sb;
}

static void
_mod_uis_send(mod_t *mod, uint32_t index, uint32_t size, uint32_t format,
	const void *buf)
{
	HASH_FOREACH(&mod->uis, mod_ui_itr)
	{
		mod_ui_t *mod_ui = *mod_ui_itr;

		if(!_mod_ui_is_running(mod_ui))
			continue;

		const int status = sandbox_master_send(mod_ui->sbox.sb, index, size, format, buf);
		(void)status; //FIXME
	}
}

static void
_param_update_span(plughandle_t *handle, param_t *param)
{
	if(param->range == handle->forge.Int)
		param->span.i = param->max.i - param->min.i;
	else if(param->range == handle->forge.Bool)
		param->span.i = param->max.i - param->min.i;
	else if(param->range == handle->forge.Long)
		param->span.h = param->max.h - param->min.h;
	else if(param->range == handle->forge.Float)
		param->span.f = param->max.f - param->min.f;
	else if(param->range == handle->forge.Double)
		param->span.d = param->max.d - param->min.d;
	//FIXME more types
}

static void
_param_fill(plughandle_t *handle, param_t *param, const LilvNode *param_node)
{
	param->property = handle->map->map(handle->map->handle, lilv_node_as_uri(param_node));

	LilvNode *range = lilv_world_get(handle->world, param_node, handle->node.rdfs_range, NULL);
	if(range)
	{
		param->range = handle->map->map(handle->map->handle, lilv_node_as_uri(range));
		if(param->range == handle->forge.String)
			nk_textedit_init_default(&param->val.editor);
		lilv_node_free(range);
	}

	if(param->range)
	{
		if(param->range == handle->forge.Bool)
		{
			param->min.b = 0;
			param->max.b = 1;
		}

		LilvNode *min = lilv_world_get(handle->world, param_node, handle->node.lv2_minimum, NULL);
		if(min)
		{
			if(param->range == handle->forge.Int)
				param->min.i = _node_as_int(min, 0);
			else if(param->range == handle->forge.Bool)
				param->min.i = _node_as_bool(min, false);
			else if(param->range == handle->forge.Long)
				param->min.h = _node_as_int(min, 0);
			else if(param->range == handle->forge.Float)
				param->min.f = _node_as_float(min, 0.f);
			else if(param->range == handle->forge.Double)
				param->min.d = _node_as_float(min, 0.f);
			//FIXME
			lilv_node_free(min);
		}

		LilvNode *max = lilv_world_get(handle->world, param_node, handle->node.lv2_maximum, NULL);
		if(max)
		{
			if(param->range == handle->forge.Int)
				param->max.i = _node_as_int(max, 1);
			else if(param->range == handle->forge.Bool)
				param->max.i = _node_as_bool(max, true);
			else if(param->range == handle->forge.Long)
				param->max.h = _node_as_int(max, 1);
			else if(param->range == handle->forge.Float)
				param->max.f = _node_as_float(max, 1.f);
			else if(param->range == handle->forge.Double)
				param->max.d = _node_as_float(max, 1.f);
			//FIXME
			lilv_node_free(max);
		}

		LilvNode *val = lilv_world_get(handle->world, param_node, handle->node.lv2_default, NULL);
		if(val)
		{
			if(param->range == handle->forge.Int)
				param->val.i = _node_as_int(val, 0);
			else if(param->range == handle->forge.Bool)
				param->val.i = _node_as_bool(min, false);
			else if(param->range == handle->forge.Long)
				param->val.h = _node_as_int(min, 0);
			else if(param->range == handle->forge.Float)
				param->val.f = _node_as_float(min, 0.f);
			else if(param->range == handle->forge.Double)
				param->val.d = _node_as_float(min, 0.f);
			//FIXME
			lilv_node_free(val);
		}

		_param_update_span(handle, param);
	}

	LilvNode *label = lilv_world_get(handle->world, param_node, handle->regs.rdfs.label.node, NULL);
	if(label)
	{
		if(lilv_node_is_string(label))
			param->label = strdup(lilv_node_as_string(label));
		lilv_node_free(label);
	}

	LilvNode *comment = lilv_world_get(handle->world, param_node, handle->regs.rdfs.comment.node, NULL);
	if(comment)
	{
		if(lilv_node_is_string(comment))
			param->comment = strdup(lilv_node_as_string(comment));
		lilv_node_free(comment);
	}

	LilvNode *units_unit = lilv_world_get(handle->world, param_node, handle->regs.units.unit.node, NULL);
	if(units_unit)
	{
		if(lilv_node_is_uri(units_unit))
			param->units_unit = handle->map->map(handle->map->handle, lilv_node_as_uri(units_unit));
		lilv_node_free(units_unit);
	}

	//FIXME units_symbol
}

static param_t *
_param_add(mod_t *mod, hash_t *hash, bool is_readonly)
{
	param_t *param = calloc(1, sizeof(param_t));
	if(param)
	{
		param->is_readonly = is_readonly;
		param->range = 0;
		param->mod = mod;

		_hash_add(hash, param);
	}

	return param;
}

static void
_param_free(plughandle_t *handle, param_t *param)
{
	if(param->range == handle->forge.String)
	{
		nk_textedit_free(&param->val.editor);
	}
	else if(param->range == handle->forge.Chunk)
	{
		free(param->val.chunk.body);
	}

	free(param->label);
	free(param->comment);
	free(param->units_symbol);
	free(param);
}

static void
_set_string(struct nk_str *str, uint32_t size, const char *body)
{
	nk_str_clear(str);

	// replace tab with 2 spaces
	const char *end = body + size - 1;
	const char *from = body;
	for(const char *to = strchr(from, '\t');
		to && (to < end);
		from = to + 1, to = strchr(from, '\t'))
	{
		nk_str_append_text_utf8(str, from, to-from);
		nk_str_append_text_utf8(str, "  ", 2);
	}
	nk_str_append_text_utf8(str, from, end-from);
}

static void
_param_set_value(plughandle_t *handle, param_t *param, const LV2_Atom *value)
{
	if(param->range == handle->forge.Int)
	{
		param->val.i = ((const LV2_Atom_Int *)value)->body;
	}
	else if(param->range == handle->forge.Bool)
	{
		param->val.i = ((const LV2_Atom_Bool *)value)->body;
	}
	else if(param->range == handle->forge.Long)
	{
		param->val.h = ((const LV2_Atom_Long *)value)->body;
	}
	else if(param->range == handle->forge.Float)
	{
		param->val.f = ((const LV2_Atom_Float *)value)->body;
	}
	else if(param->range == handle->forge.Double)
	{
		param->val.d = ((const LV2_Atom_Double *)value)->body;
	}
	else if(param->range == handle->forge.String)
	{
		struct nk_str *str = &param->val.editor.string;
		_set_string(str, value->size, LV2_ATOM_BODY_CONST(value));
	}
	else if(param->range == handle->forge.Chunk)
	{
		param->val.chunk.size = value->size;
		param->val.chunk.body = realloc(param->val.chunk.body, value->size);
		if(param->val.chunk.body)
			memcpy(param->val.chunk.body, LV2_ATOM_BODY_CONST(value), value->size);
	}
	//FIXME handle remaining types
}

static void
_refresh_main_dynam_list(plughandle_t *handle, mod_t *mod)
{
	_hash_free(&handle->dynam_matches);

	bool search = _textedit_len(&handle->port_search_edit) != 0;

	HASH_FOREACH(&mod->dynams, itr)
	{
		param_t *param = *itr;

		bool visible = true;
		if(search)
		{
			if(param->label)
			{
				if(!strcasestr(param->label, _textedit_const(&handle->port_search_edit)))
					visible = false;
			}
		}

		if(visible)
			_hash_add(&handle->dynam_matches, param);
	}
}

static int
_sort_param_name(const void *a, const void *b)
{
	const param_t *param_a = *(const param_t **)a;
	const param_t *param_b = *(const param_t **)b;

	const char *name_a = param_a->label;
	const char *name_b = param_b->label;

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	return ret;
}

static void
_mod_nk_write_function(plughandle_t *handle, mod_t *src_mod, port_t *src_port,
	uint32_t src_proto, const LV2_Atom *src_value, bool route_to_ui)
{
	if(  (src_proto == handle->regs.port.float_protocol.urid)
		&& (src_value->type == handle->forge.Float)
		&& (src_port->type & PROPERTY_TYPE_CONTROL) )
	{
		control_port_t *control = &src_port->control;
		const float f32 = ((const LV2_Atom_Float *)src_value)->body;

		if(control->is_bool || control->is_int)
			control->val.i = f32;
		else // float
			control->val.f = f32;

		if(route_to_ui)
		{
			_mod_uis_send(src_mod, src_port->index, sizeof(float), 0, &f32);
		}
	}
	else if( (src_proto == handle->regs.port.peak_protocol.urid)
		&& (src_value->type == handle->forge.Tuple)
		&& (src_port->type & PROPERTY_TYPE_AUDIO || src_port->type & PROPERTY_TYPE_CV) )
	{
		const LV2_Atom_Tuple *tup = (const LV2_Atom_Tuple *)src_value;
		const LV2_Atom_Int *period_start = (const LV2_Atom_Int *)lv2_atom_tuple_begin(tup);
		const LV2_Atom_Int *period_size = (const LV2_Atom_Int *)lv2_atom_tuple_next(&period_start->atom);
		const LV2_Atom_Float *peak = (const LV2_Atom_Float *)lv2_atom_tuple_next(&period_size->atom);;

		audio_port_t *audio = &src_port->audio;

		audio->peak = peak ? peak->body : 0;

		if(route_to_ui)
		{
			const LV2UI_Peak_Data pdata = {
				.period_start = period_start ? period_start->body : 0,
				.period_size = period_size ? period_size->body : 0,
				.peak = audio->peak
			};

			_mod_uis_send(src_mod, src_port->index, sizeof(LV2UI_Peak_Data),
				handle->regs.port.peak_protocol.urid, &pdata);
		}
	}
	else if( (src_proto == handle->regs.port.event_transfer.urid)
		&& (src_port->type & PROPERTY_TYPE_ATOM) )
	{
		const LV2_Atom_Object *pobj = (const LV2_Atom_Object *)src_value;

		if(pobj->atom.type == handle->forge.Object)
		{
			if(pobj->body.otype == handle->regs.patch.set.urid)
			{
				//printf("got patch:Set notification\n");
				const LV2_Atom_URID *subject = NULL;
				const LV2_Atom_URID *property = NULL;
				const LV2_Atom *value = NULL;

				lv2_atom_object_get(pobj,
					handle->regs.patch.subject.urid, &subject,
					handle->regs.patch.property.urid, &property,
					handle->regs.patch.value.urid, &value,
					0);

				const LV2_URID subj = subject && (subject->atom.type == handle->forge.URID)
					? subject->body
					: 0;
				const LV2_URID prop = property && (property->atom.type == handle->forge.URID)
					? property->body
					: 0;

				if(prop && value)
				{
					param_t *param = _mod_param_find_by_property(src_mod, prop);
					if(param && (param->range == value->type) )
					{
						_param_set_value(handle, param, value);
					}
				}
			}
			else if(pobj->body.otype == handle->regs.patch.put.urid)
			{
				//printf("got patch:Put notification\n");
				const LV2_Atom_URID *subject = NULL;
				const LV2_Atom_Object *body = NULL;

				lv2_atom_object_get(pobj,
					handle->regs.patch.subject.urid, &subject,
					handle->regs.patch.body.urid, &body,
					0);

				const LV2_URID subj = subject && (subject->atom.type == handle->forge.URID)
					? subject->body
					: 0;

				LV2_ATOM_OBJECT_FOREACH(body, prop)
				{
					param_t *param = _mod_param_find_by_property(src_mod, prop->key);
					if(param && (param->range == prop->value.type) )
					{
						_param_set_value(handle, param, &prop->value);
					}
				}
			}
			else if(pobj->body.otype == handle->regs.patch.patch.urid)
			{
				//printf("got patch:Patch notification\n");
				const LV2_Atom_URID *subject = NULL;
				const LV2_Atom_Object *rem = NULL;
				const LV2_Atom_Object *add = NULL;

				lv2_atom_object_get(pobj,
					handle->regs.patch.subject.urid, &subject,
					handle->regs.patch.remove.urid, &rem,
					handle->regs.patch.add.urid, &add,
					0);

				const LV2_URID subj = subject && (subject->atom.type == handle->forge.URID)
					? subject->body
					: 0;

				if(  rem && (rem->atom.type == handle->forge.Object)
					&& add && (add->atom.type == handle->forge.Object) )
				{
					LV2_ATOM_OBJECT_FOREACH(rem, prop)
					{
						if(  (prop->key == handle->regs.patch.writable.urid)
							&& (prop->value.type == handle->forge.URID) )
						{
							const LV2_URID property = ((const LV2_Atom_URID *)&prop->value)->body;

							if(property == handle->regs.patch.wildcard.urid)
							{
								HASH_FREE(&src_mod->dynams, param_ptr)
								{
									param_t *param = param_ptr;
									_param_free(handle, param);
								} //FIXME only delete writables
							}
							else if(subj)
							{
								param_t *param = _mod_dynam_find_by_property(src_mod, subj);
								if(param)
								{
									_hash_remove(&src_mod->dynams, param);
									_param_free(handle, param);
								}
							}
						}
						else if( (prop->key == handle->regs.patch.readable.urid)
							&& (prop->value.type == handle->forge.URID) )
						{
							const LV2_URID property = ((const LV2_Atom_URID *)&prop->value)->body;

							if(property == handle->regs.patch.wildcard.urid)
							{
								HASH_FREE(&src_mod->dynams, param_ptr)
								{
									param_t *param = param_ptr;
									_param_free(handle, param);
								} //FIXME only delete readables
							}
							else if(subj)
							{
								param_t *param = _mod_dynam_find_by_property(src_mod, subj);
								if(param)
								{
									_hash_remove(&src_mod->dynams, param);
									_param_free(handle, param);
								}
							}
						}
						else if(subj)
						{
							param_t *param = _mod_dynam_find_by_property(src_mod, subj);
							if(param)
							{
								if(prop->key == handle->regs.rdfs.range.urid)
								{
									param->range = 0;
								}
								else if(prop->key == handle->regs.rdfs.label.urid)
								{
									free(param->label);
									param->label = NULL;
								}
								else if(prop->key == handle->regs.rdfs.comment.urid)
								{
									free(param->comment);
									param->comment= NULL;
								}
								else if(prop->key == handle->regs.core.minimum.urid)
								{
									param->min.i = 0;
								}
								else if(prop->key == handle->regs.core.maximum.urid)
								{
									param->max.i = 0;
								}
								else if(prop->key == handle->regs.units.unit.urid)
								{
									param->units_unit = 0;
								}
								else if(prop->key == handle->regs.units.symbol.urid)
								{
									free(param->units_symbol);
									param->units_symbol = NULL;
								}
							}
						}
					}

					LV2_ATOM_OBJECT_FOREACH(add, prop)
					{
						if(  (prop->key == handle->regs.patch.writable.urid)
							&& (prop->value.type == handle->forge.URID) )
						{
							const LV2_URID property = ((const LV2_Atom_URID *)&prop->value)->body;

							param_t *param = _mod_dynam_find_by_property(src_mod, property);
							if(!param)
								param = _param_add(src_mod, &src_mod->dynams, false);
							if(param)
							{
								param->property = property;

								// patch:Get [patch:property property]
								_patch_notification_add_patch_get(handle, src_mod,
									handle->regs.port.event_transfer.urid, 0, 0, property);
							}
						}
						else if( (prop->key == handle->regs.patch.readable.urid)
							&& (prop->value.type == handle->forge.URID) )
						{
							const LV2_URID property = ((const LV2_Atom_URID *)&prop->value)->body;

							param_t *param = _mod_dynam_find_by_property(src_mod, property);
							if(!param)
								param = _param_add(src_mod, &src_mod->dynams, true);
							if(param)
							{
								param->property = property;

								// patch:Get [patch:property property]
								_patch_notification_add_patch_get(handle, src_mod,
									handle->regs.port.event_transfer.urid, 0, 0, property);
							}
						}
						else if(subj)
						{
							param_t *param = _mod_dynam_find_by_property(src_mod, subj);
							if(param)
							{
								if(  (prop->key == handle->regs.rdfs.range.urid)
									&& (prop->value.type == handle->forge.URID) )
								{
									param->range = ((const LV2_Atom_URID *)&prop->value)->body;
									if(param->range == handle->forge.String)
										nk_textedit_init_default(&param->val.editor);
									else if(param->range == handle->forge.Bool)
									{
										param->min.b = 0;
										param->min.b = 1;
									}
								}
								else if( (prop->key == handle->regs.rdfs.label.urid)
									&& (prop->value.type == handle->forge.String) )
								{
									free(param->label);
									param->label = strdup(LV2_ATOM_BODY_CONST(&prop->value));
									_refresh_main_dynam_list(handle, src_mod);
									_hash_sort(&src_mod->dynams, _sort_param_name);
								}
								else if( (prop->key == handle->regs.rdfs.comment.urid)
									&& (prop->value.type == handle->forge.String) )
								{
									free(param->comment);
									param->comment = strdup(LV2_ATOM_BODY_CONST(&prop->value));
								}
								else if( (prop->key == handle->regs.core.minimum.urid)
									&& (prop->value.type == param->range) )
								{
									if(param->range == handle->forge.Int)
										param->min.i = ((const LV2_Atom_Int *)&prop->value)->body;
									else if(param->range == handle->forge.Bool)
										param->min.i = ((const LV2_Atom_Bool *)&prop->value)->body;
									else if(param->range == handle->forge.Long)
										param->min.h = ((const LV2_Atom_Long *)&prop->value)->body;
									else if(param->range == handle->forge.Float)
										param->min.f = ((const LV2_Atom_Float *)&prop->value)->body;
									else if(param->range == handle->forge.Double)
										param->min.d = ((const LV2_Atom_Double *)&prop->value)->body;
									//FIXME support more types

									_param_update_span(handle, param);
								}
								else if( (prop->key == handle->regs.core.maximum.urid)
									&& (prop->value.type == param->range) )
								{
									if(param->range == handle->forge.Int)
										param->max.i = ((const LV2_Atom_Int *)&prop->value)->body;
									else if(param->range == handle->forge.Bool)
										param->max.i = ((const LV2_Atom_Bool *)&prop->value)->body;
									else if(param->range == handle->forge.Long)
										param->max.h = ((const LV2_Atom_Long *)&prop->value)->body;
									else if(param->range == handle->forge.Float)
										param->max.f = ((const LV2_Atom_Float *)&prop->value)->body;
									else if(param->range == handle->forge.Double)
										param->max.d = ((const LV2_Atom_Double *)&prop->value)->body;
									//FIXME support more types

									_param_update_span(handle, param);
								}
								else if( (prop->key == handle->regs.units.unit.urid)
									&& (prop->value.type == handle->forge.URID) )
								{
									param->units_unit = ((const LV2_Atom_URID *)&prop->value)->body;
								}
								else if( (prop->key == handle->regs.units.symbol.urid)
									&& (prop->value.type == handle->forge.String) )
								{
									free(param->units_symbol);
									param->units_symbol = strdup(LV2_ATOM_BODY_CONST(&prop->value));
								}
							}
						}
					}
				}
			}
		}

		if(route_to_ui)
		{
			_mod_uis_send(src_mod, src_port->index, lv2_atom_total_size(src_value),
				handle->regs.port.event_transfer.urid, src_value);
		}
	}
	else if( (src_proto == handle->regs.port.atom_transfer.urid)
		&& (src_port->type & PROPERTY_TYPE_ATOM) )
	{
		//FIXME rarely (never) sent by any plugin

		if(route_to_ui)
		{
			_mod_uis_send(src_mod, src_port->index, lv2_atom_total_size(src_value),
				handle->regs.port.atom_transfer.urid, src_value);
		}
	}

	nk_pugl_post_redisplay(&handle->win);
}

static void
_mod_ui_write_function(LV2UI_Controller controller, uint32_t index,
	uint32_t size, uint32_t protocol, const void *buffer)
{
	mod_ui_t *mod_ui = controller;
	mod_t *mod = mod_ui->mod;
	plughandle_t *handle = mod->handle;

	if(protocol == 0)
		protocol = handle->regs.port.float_protocol.urid;

	//printf("_mod_ui_write_function: %u %u %u\n", index, size, protocol);

	port_t *port = _mod_port_find_by_index(mod, index);
	if(port)
	{
		if(protocol == handle->regs.port.float_protocol.urid)
		{
			// route to dsp
			_patch_notification_add(handle, port, protocol,
				sizeof(float), handle->forge.Float, buffer);

			// route to nk
			const LV2_Atom_Float flt = {
				.atom = {
					.size = sizeof(float),
					.type = handle->forge.Float
				},
				.body = *(const float *)buffer
			};
			_mod_nk_write_function(handle, mod, port, protocol, &flt.atom, false);
		}
		else if( (protocol == handle->regs.port.event_transfer.urid)
			|| (protocol == handle->regs.port.atom_transfer.urid) )
		{
			const LV2_Atom *atom = buffer;

			// route to dsp
			_patch_notification_add(handle, port, protocol,
				atom->size, atom->type, LV2_ATOM_BODY_CONST(atom));

			// FIXME routing to nk not needed, will be fed back via dsp 
		}
	}
}

static void
_mod_ui_subscribe_function(LV2UI_Controller controller, uint32_t index,
	uint32_t protocol, bool state)
{
	mod_ui_t *mod_ui = controller;
	mod_t *mod = mod_ui->mod;
	plughandle_t *handle = mod->handle;

	printf("_mod_ui_subscribe_function: %u %u %i\n", index, protocol, state);

	// route to dsp
	port_t *port = _mod_port_find_by_index(mod, index);
	if(port)
		_patch_subscription_add(handle, port);
}

static mod_ui_t *
_mod_ui_add(plughandle_t *handle, mod_t *mod, const LilvUI *ui)
{
	bool supported = false;
#if defined(SANDBOX_X11)
	if(lilv_ui_is_a(ui, handle->regs.ui.x11.node))
		supported = true;
#endif
#if defined(SANDBOX_GTK2)
	if(lilv_ui_is_a(ui, handle->regs.ui.gtk2.node))
		supported = true;
#endif
#if defined(SANDBOX_GTK3)
	if(lilv_ui_is_a(ui, handle->regs.ui.gtk3.node))
		supported = true;
#endif
#if defined(SANDBOX_QT4)
	if(lilv_ui_is_a(ui, handle->regs.ui.qt4.node))
		supported = true;
#endif
#if defined(SANDBOX_QT5)
	if(lilv_ui_is_a(ui, handle->regs.ui.qt5.node))
		supported = true;
#endif
#if defined(SANDBOX_SHOW)
	if(lilv_ui_is_a(ui, handle->regs.ui.show_interface.node))
		supported = true;
#endif
#if defined(SANDBOX_KX)
	if(lilv_ui_is_a(ui, handle->regs.ui.kx_widget.node))
		supported = true;
#endif
	if(!supported)
		return NULL;

	mod_ui_t *mod_ui = calloc(1, sizeof(mod_ui_t));
	if(mod_ui)
	{
		const LilvNode *ui_node = lilv_ui_get_uri(ui);

		mod_ui->mod = mod;
		mod_ui->ui = ui;
		mod_ui->uri = lilv_node_as_uri(ui_node);
		const LV2_URID ui_urn = handle->map->map(handle->map->handle, mod_ui->uri);

		const LilvNode *bundle_node = lilv_plugin_get_bundle_uri(mod->plug);
		const char *bundle_uri = bundle_node ? lilv_node_as_uri(bundle_node) : NULL;
		mod_ui->sbox.bundle_path = lilv_file_uri_parse(bundle_uri, NULL);

		if(asprintf(&mod_ui->sbox.socket_uri, "shm:///%"PRIu32"-%"PRIu32, mod->urn, ui_urn) == -1)
			mod_ui->sbox.socket_uri = NULL;

		if(asprintf(&mod_ui->sbox.window_name, "%s", mod_ui->uri) == -1)
			mod_ui->sbox.window_name = NULL;

		//FIXME sample_rate

		if(asprintf(&mod_ui->sbox.update_rate, "%f", handle->update_rate) == -1)
			mod_ui->sbox.update_rate = NULL;

		mod_ui->sbox.driver.socket_path = mod_ui->sbox.socket_uri;
		mod_ui->sbox.driver.map = handle->map;
		mod_ui->sbox.driver.unmap = handle->unmap;
		mod_ui->sbox.driver.recv_cb = _mod_ui_write_function;
		mod_ui->sbox.driver.subscribe_cb = _mod_ui_subscribe_function;

		_hash_add(&mod->uis, mod_ui);
	}

	return mod_ui;
}

static void
_mod_ui_run(mod_ui_t *mod_ui)
{
	const LilvUI *ui = mod_ui->ui;
	mod_t *mod = mod_ui->mod;
	plughandle_t *handle = mod->handle;

	const LilvNode *plugin_node = lilv_plugin_get_uri(mod->plug);
	const char *plugin_uri = plugin_node ? lilv_node_as_uri(plugin_node) : NULL;

	const char *exec_uri = NULL;
#if defined(SANDBOX_X11)
	if(lilv_ui_is_a(ui, handle->regs.ui.x11.node))
		exec_uri = "/usr/local/bin/synthpod_sandbox_x11"; //FIXME prefix
#endif
#if defined(SANDBOX_GTK2)
	if(lilv_ui_is_a(ui, handle->regs.ui.gtk2.node))
		exec_uri = "/usr/local/bin/synthpod_sandbox_gtk2"; //FIXME prefix
#endif
#if defined(SANDBOX_GTK3)
	if(lilv_ui_is_a(ui, handle->regs.ui.gtk3.node))
		exec_uri = "/usr/local/bin/synthpod_sandbox_gtk3"; //FIXME prefix
#endif
#if defined(SANDBOX_QT4)
	if(lilv_ui_is_a(ui, handle->regs.ui.qt4.node))
		exec_uri = "/usr/local/bin/synthpod_sandbox_qt4"; //FIXME prefix
#endif
#if defined(SANDBOX_QT5)
	if(lilv_ui_is_a(ui, handle->regs.ui.qt5.node))
		exec_uri = "/usr/local/bin/synthpod_sandbox_qt5"; //FIXME prefix
#endif
#if defined(SANDBOX_SHOW)
	if(lilv_ui_is_a(ui, handle->regs.ui.show_interface.node))
		exec_uri = "/usr/local/bin/synthpod_sandbox_show"; //FIXME prefix
#endif
#if defined(SANDBOX_KX)
	if(lilv_ui_is_a(ui, handle->regs.ui.kx_widget.node))
		exec_uri = "/usr/local/bin/synthpod_sandbox_kx"; //FIXME prefix
#endif

	mod_ui->sbox.sb = sandbox_master_new(&mod_ui->sbox.driver, mod_ui);

	if(exec_uri && plugin_uri && mod_ui->sbox.bundle_path && mod_ui->uri
		&& mod_ui->sbox.socket_uri && mod_ui->sbox.window_name
		&& mod_ui->sbox.update_rate && mod_ui->sbox.sb)
	{
		_mod_subscribe_all(handle, mod);

		const pid_t pid = fork();
		if(pid == 0) // child
		{
			char *const args [] = {
				(char *)exec_uri,
				"-p", (char *)plugin_uri,
				"-b", mod_ui->sbox.bundle_path,
				"-u", (char *)mod_ui->uri,
				"-s", mod_ui->sbox.socket_uri,
				"-w", mod_ui->sbox.window_name,
				"-r", mod_ui->sbox.update_rate,
				NULL
			};

			execvp(args[0], args);
		}

		// parent
		mod_ui->pid = pid;
	}
}

static void
_mod_ui_stop(mod_ui_t *mod_ui)
{
	mod_t *mod = mod_ui->mod;
	plughandle_t *handle = mod->handle;

	if(mod_ui->pid)
	{
		kill(mod_ui->pid, SIGTERM);
		mod_ui->pid = 0;
	}

	if(mod_ui->sbox.sb)
	{
		_mod_unsubscribe_all(handle, mod);
		sandbox_master_free(mod_ui->sbox.sb);
		mod_ui->sbox.sb = NULL;
	}
}

static void
_mod_ui_free(mod_ui_t *mod_ui)
{
	if(_mod_ui_is_running(mod_ui))
		_mod_ui_stop(mod_ui);

	lilv_free(mod_ui->sbox.bundle_path);
	free(mod_ui->sbox.socket_uri);
	free(mod_ui);
}

static port_conn_t *
_port_conn_find(mod_conn_t *mod_conn, port_t *source_port, port_t *sink_port)
{
	HASH_FOREACH(&mod_conn->conns, port_conn_itr)
	{
		port_conn_t *port_conn = *port_conn_itr;

		if( (port_conn->source_port == source_port) && (port_conn->sink_port == sink_port) )
			return port_conn;
	}

	return NULL;
}

static port_conn_t *
_port_conn_add(mod_conn_t *mod_conn, port_t *source_port, port_t *sink_port)
{
	port_conn_t *port_conn = calloc(1, sizeof(port_conn_t));
	if(port_conn)
	{
		port_conn->source_port = source_port;
		port_conn->sink_port = sink_port;

		mod_conn->source_type |= source_port->type;
		mod_conn->sink_type |= sink_port->type;
		_hash_add(&mod_conn->conns, port_conn);
	}

	return port_conn;
}

static void
_port_conn_free(port_conn_t *port_conn)
{
	free(port_conn);
}

static void
_port_conn_remove(plughandle_t *handle, mod_conn_t *mod_conn, port_conn_t *port_conn)
{
	_hash_remove(&mod_conn->conns, port_conn);
	_port_conn_free(port_conn);

	if(_hash_size(&mod_conn->conns) == 0)
		_mod_conn_remove(handle, mod_conn);
	else
		_mod_conn_refresh_type(mod_conn);
}

static inline float
dBFSp6(float peak)
{
	const float d = 6.f + 20.f * log10f(fabsf(peak) / 2.f);
	const float e = (d + 64.f) / 70.f;
	return NK_CLAMP(0.f, e, 1.f);
}

#if 0
static inline float
dBFS(float peak)
{
	const float d = 20.f * log10f(fabsf(peak));
	const float e = (d + 70.f) / 70.f;
	return NK_CLAMP(0.f, e, 1.f);
}
#endif

static int
_sort_rdfs_label(const void *a, const void *b, void *data)
{
	plughandle_t *handle = data;

	const LilvNode *group_a = *(const LilvNode **)a;
	const LilvNode *group_b = *(const LilvNode **)b;

	const char *name_a = NULL;
	const char *name_b = NULL;

	LilvNode *node_a = lilv_world_get(handle->world, group_a, handle->node.rdfs_label, NULL);
	if(!node_a)
		node_a = lilv_world_get(handle->world, group_a, handle->node.lv2_name, NULL);

	LilvNode *node_b = lilv_world_get(handle->world, group_b, handle->node.rdfs_label, NULL);
	if(!node_b)
		node_b = lilv_world_get(handle->world, group_b, handle->node.lv2_name, NULL);

	if(node_a)
		name_a = lilv_node_as_string(node_a);
	if(node_b)
		name_b = lilv_node_as_string(node_b);

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	if(node_a)
		lilv_node_free(node_a);
	if(node_b)
		lilv_node_free(node_b);

	return ret;
}

static int
_sort_port_name(const void *a, const void *b)
{
	const port_t *port_a = *(const port_t **)a;
	const port_t *port_b = *(const port_t **)b;

	const char *name_a = port_a->name;
	const char *name_b = port_b->name;

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	return ret;
}

static int
_sort_scale_point_name(const void *a, const void *b)
{
	const scale_point_t *scale_point_a = *(const scale_point_t **)a;
	const scale_point_t *scale_point_b = *(const scale_point_t **)b;

	const char *name_a = scale_point_a->label;
	const char *name_b = scale_point_b->label;

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	return ret;
}

static void
_patch_mod_add(plughandle_t *handle, const LilvPlugin *plug)
{
	const LilvNode *node= lilv_plugin_get_uri(plug);
	const char *uri = lilv_node_as_string(node);
	const LV2_URID urid = handle->map->map(handle->map->handle, uri);

	if(  _message_request(handle)
		&& synthpod_patcher_add(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.module_list.urid, //TODO subject
			sizeof(uint32_t), handle->forge.URID, &urid) )
	{
		_message_write(handle);
	}
}

static void
_patch_mod_remove(plughandle_t *handle, mod_t *mod)
{
	if(  _message_request(handle)
		&& synthpod_patcher_remove(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.module_list.urid, //TODO subject
			sizeof(uint32_t), handle->forge.URID, &mod->urn) )
	{
		_message_write(handle);
	}
}

static void
_patch_mod_preset_set(plughandle_t *handle, mod_t *mod, const LilvNode *preset)
{
	const char *preset_uri = lilv_node_as_uri(preset);
	const LV2_URID preset_urid = handle->map->map(handle->map->handle, preset_uri);;

	if(  _message_request(handle)
		&&  synthpod_patcher_set(&handle->regs, &handle->forge,
			mod->urn, 0, handle->regs.pset.preset.urid,
			sizeof(uint32_t), handle->forge.URID, &preset_urid) )
	{
		_message_write(handle);
	}
}

static mod_t *
_mod_find_by_subject(plughandle_t *handle, LV2_URID subj)
{
	HASH_FOREACH(&handle->mods, itr)
	{
		mod_t *mod = *itr;

		if(mod->urn == subj)
			return mod;
	}

	return NULL;
}

#define nxt_x0 115.f
#define nxt_y0  50.f
#define nxt_xm (nxt_x0 + 640.f)
#define nxt_ym (nxt_y0 + 360.f)
#define nxt_xd 50.f
#define nxt_yd 50.f

static void
_mod_add(plughandle_t *handle, LV2_URID urn)
{
	mod_t *mod = calloc(1, sizeof(mod_t));
	if(!mod)
		return;

	mod->handle = handle;
	mod->urn = urn;
	mod->pos = nk_vec2(handle->nxt.x, handle->nxt.y);
	_hash_add(&handle->mods, mod);

	// derive initial position
	const float xmax = nxt_xm * handle->scale;
	const float ymax = nxt_ym * handle->scale;
	const float xd = nxt_xd * handle->scale;
	const float yd = nxt_yd * handle->scale;

	handle->nxt.y += yd;
	handle->nxt.x += xd;

	if(handle->nxt.y > ymax)
		handle->nxt.y = nxt_y0 * handle->scale;

	if(handle->nxt.x > xmax)
		handle->nxt.x = nxt_x0 * handle->scale;
}

static void
_mod_init(plughandle_t *handle, mod_t *mod, const LilvPlugin *plug)
{
	mod->plug = plug;
	const unsigned num_ports = lilv_plugin_get_num_ports(plug) + 1; // + automation port

	for(unsigned p=0; p<num_ports-1; p++) // - automation port
	{
		port_t *port = calloc(1, sizeof(port_t));
		if(!port)
			continue;

		_hash_add(&mod->ports, port);

		port->mod = mod;
		port->index = p;
		port->port = lilv_plugin_get_port_by_index(plug, p);
		port->symbol = lilv_node_as_string(lilv_port_get_symbol(plug, port->port));
		port->groups = lilv_port_get_value(plug, port->port, handle->node.pg_group);

		LilvNode *port_name = lilv_port_get_name(plug, port->port);
		if(port_name)
		{
			port->name = strdup(lilv_node_as_string(port_name));
			lilv_node_free(port_name);
		}

		LILV_FOREACH(nodes, i, port->groups)
		{
			const LilvNode *port_group = lilv_nodes_get(port->groups, i);

			bool match = false;
			HASH_FOREACH(&mod->groups, itr)
			{
				const LilvNode *mod_group = *itr;

				if(lilv_node_equals(mod_group, port_group))
				{
					match = true;
					break;
				}
			}

			if(!match)
				_hash_add(&mod->groups, (void *)port_group);
		}

		const bool is_audio = lilv_port_is_a(plug, port->port, handle->node.lv2_AudioPort);
		const bool is_cv = lilv_port_is_a(plug, port->port, handle->node.lv2_CVPort);
		const bool is_control = lilv_port_is_a(plug, port->port, handle->node.lv2_ControlPort);
		const bool is_atom = lilv_port_is_a(plug, port->port, handle->node.atom_AtomPort);
		const bool is_output = lilv_port_is_a(plug, port->port, handle->node.lv2_OutputPort);

		if(is_audio)
		{
			port->type = PROPERTY_TYPE_AUDIO;
			audio_port_t *audio = &port->audio;

			audio->peak = dBFSp6(2.f * rand() / RAND_MAX);
			audio->gain = (float)rand() / RAND_MAX;
			//TODO
		}
		else if(is_cv)
		{
			port->type = PROPERTY_TYPE_CV;
			audio_port_t *audio = &port->audio;

			audio->peak = dBFSp6(2.f * rand() / RAND_MAX);
			audio->gain = (float)rand() / RAND_MAX;
			//TODO
		}
		else if(is_control)
		{
			port->type = PROPERTY_TYPE_CONTROL;
			control_port_t *control = &port->control;

			control->is_readonly = is_output;
			control->is_int = lilv_port_has_property(plug, port->port, handle->node.lv2_integer);
			control->is_bool = lilv_port_has_property(plug, port->port, handle->node.lv2_toggled);

			LilvNode *val = NULL;
			LilvNode *min = NULL;
			LilvNode *max = NULL;
			lilv_port_get_range(plug, port->port, &val, &min, &max);

			if(val)
			{
				if(control->is_int)
					control->val.i = _node_as_int(val, 0);
				else if(control->is_bool)
					control->val.i = _node_as_bool(val, false);
				else
					control->val.f = _node_as_float(val, 0.f);

				lilv_node_free(val);
			}
			else
				control->val.f = 0.f;

			if(min)
			{
				if(control->is_int)
					control->min.i = _node_as_int(min, 0);
				else if(control->is_bool)
					control->min.i = _node_as_bool(min, false);
				else
					control->min.f = _node_as_float(min, 0.f);

				lilv_node_free(min);
			}
			else
				control->min.f = 0.f;

			if(max)
			{
				if(control->is_int)
					control->max.i = _node_as_int(max, 1);
				else if(control->is_bool)
					control->max.i = _node_as_bool(max, true);
				else
					control->max.f = _node_as_float(max, 1.f);

				lilv_node_free(max);
			}
			else
				control->max.f = 1.f;

			if(control->is_int)
				control->span.i = control->max.i - control->min.i;
			else if(control->is_bool)
				control->span.i = control->max.i - control->min.i;
			else
				control->span.f = control->max.f - control->min.f;

			if(control->is_int && (control->min.i == 0) && (control->max.i == 1) )
			{
				control->is_int = false;
				control->is_bool = true;
			}

			LilvScalePoints *port_points = lilv_port_get_scale_points(plug, port->port);
			if(port_points)
			{
				LILV_FOREACH(scale_points, i, port_points)
				{
					const LilvScalePoint *port_point = lilv_scale_points_get(port_points, i);
					const LilvNode *label_node = lilv_scale_point_get_label(port_point);
					const LilvNode *value_node = lilv_scale_point_get_value(port_point);

					if(label_node && value_node)
					{
						scale_point_t *point = calloc(1, sizeof(scale_point_t));
						if(!point)
							continue;

						_hash_add(&port->control.points, point);

						point->label = strdup(lilv_node_as_string(label_node));

						if(control->is_int)
						{
							point->val.i = _node_as_int(value_node, 0);
						}
						else if(control->is_bool)
						{
							point->val.i = _node_as_bool(value_node, false);
						}
						else // is_float
						{
							point->val.f = _node_as_float(value_node, 0.f);
						}
					}
				}

				int32_t diff1 = INT32_MAX;
				HASH_FOREACH(&port->control.points, itr)
				{
					scale_point_t *point = *itr;

					const int32_t diff2 = abs(point->val.i - control->val.i); //FIXME

					if(diff2 < diff1)
					{
						control->points_ref = point;
						diff1 = diff2;
					}
				}

				_hash_sort(&port->control.points, _sort_scale_point_name);

				lilv_scale_points_free(port_points);
			}
		}
		else if(is_atom)
		{
			port->type = PROPERTY_TYPE_ATOM;

			if(lilv_port_supports_event(plug, port->port, handle->node.midi_MidiEvent))
				port->type |= PROPERTY_TYPE_MIDI;
			if(lilv_port_supports_event(plug, port->port, handle->node.osc_Message))
				port->type |= PROPERTY_TYPE_OSC;
			if(lilv_port_supports_event(plug, port->port, handle->node.time_Position))
				port->type |= PROPERTY_TYPE_TIME;
			if(lilv_port_supports_event(plug, port->port, handle->node.patch_Message))
				port->type |= PROPERTY_TYPE_PATCH;
			if(lilv_port_supports_event(plug, port->port, handle->node.xpress_Message))
				port->type |= PROPERTY_TYPE_XPRESS;

			//TODO
		}

		if(is_audio || is_cv || is_atom)
		{
			if(is_output)
				_hash_add(&mod->sources, port);
			else
				_hash_add(&mod->sinks, port);
		}

		if(is_output)
			mod->source_type |= port->type;
		else
			mod->sink_type |= port->type;
	}

	{
		const unsigned p = num_ports - 1;

		port_t *port = calloc(1, sizeof(port_t));
		if(port)
		{
			_hash_add(&mod->ports, port);

			port->mod = mod;
			port->index = p;
			port->port = NULL;
			port->symbol = "automation";
			port->groups = NULL;
			port->name = strdup("Automation");

			port->type = PROPERTY_TYPE_ATOM | PROPERTY_TYPE_AUTOMATION;

			_hash_add(&mod->sinks, port);
			mod->sink_type |= port->type;
		}
	}

	_hash_sort(&mod->ports, _sort_port_name);
	_hash_sort_r(&mod->groups, _sort_rdfs_label, handle);

	mod->presets = lilv_plugin_get_related(plug, handle->node.pset_Preset);
	if(mod->presets)
	{
		LILV_FOREACH(nodes, i, mod->presets)
		{
			const LilvNode *preset = lilv_nodes_get(mod->presets, i);
			lilv_world_load_resource(handle->world, preset);
		}

		LILV_FOREACH(nodes, i, mod->presets)
		{
			const LilvNode *preset = lilv_nodes_get(mod->presets, i);

			LilvNodes *banks = lilv_world_find_nodes(handle->world, preset, handle->node.pset_bank, NULL);
			if(banks)
			{
				const LilvNode *bank = lilv_nodes_size(banks)
					? lilv_nodes_get_first(banks) : NULL;

				if(bank)
				{
					bool match = false;
					HASH_FOREACH(&mod->banks, itr)
					{
						const LilvNode *mod_bank = *itr;

						if(lilv_node_equals(mod_bank, bank))
						{
							match = true;
							break;
						}
					}

					if(!match)
					{
						_hash_add(&mod->banks, lilv_node_duplicate(bank));
					}
				}
				lilv_nodes_free(banks);
			}
		}

		_hash_sort_r(&mod->banks, _sort_rdfs_label, handle);
	}

	mod->readables = lilv_plugin_get_value(plug, handle->node.patch_readable);
	mod->writables = lilv_plugin_get_value(plug, handle->node.patch_writable);

	LILV_FOREACH(nodes, i, mod->readables)
	{
		const LilvNode *param_node= lilv_nodes_get(mod->readables, i);

		param_t *param = _param_add(mod, &mod->params, true);
		if(param)
			_param_fill(handle, param, param_node);
	}
	LILV_FOREACH(nodes, i, mod->writables)
	{
		const LilvNode *param_node = lilv_nodes_get(mod->readables, i);

		param_t *param = _param_add(mod, &mod->params, false);
		if(param)
			_param_fill(handle, param, param_node);
	}

	_hash_sort(&mod->params, _sort_param_name);

	// add UIs
	mod->ui_nodes = lilv_plugin_get_uis(mod->plug);
	LILV_FOREACH(uis, itr, mod->ui_nodes)
	{
		const LilvUI *ui = lilv_uis_get(mod->ui_nodes, itr);

		_mod_ui_add(handle, mod, ui);
	}

	nk_pugl_post_redisplay(&handle->win); //FIXME
}

static void
_port_free(port_t *port)
{
	free(port->name);
	free(port);
}

static void
_mod_free(plughandle_t *handle, mod_t *mod)
{
	HASH_FREE(&mod->ports, ptr)
	{
		port_t *port = ptr;

		if(port->groups)
			lilv_nodes_free(port->groups);

		if(port->type == PROPERTY_TYPE_CONTROL)
		{
			HASH_FREE(&port->control.points, ptr2)
			{
				scale_point_t *point = ptr2;

				if(point->label)
					free(point->label);

				free(point);
			}
		}

		_port_free(port);
	}
	_hash_free(&mod->sources);
	_hash_free(&mod->sinks);

	HASH_FREE(&mod->banks, ptr)
	{
		LilvNode *node = ptr;
		lilv_node_free(node);
	}

	_hash_free(&mod->groups);

	HASH_FREE(&mod->params, ptr)
	{
		param_t *param = ptr;
		_param_free(handle, param);
	}

	if(mod->presets)
	{
		LILV_FOREACH(nodes, i, mod->presets)
		{
			const LilvNode *preset = lilv_nodes_get(mod->presets, i);
			lilv_world_unload_resource(handle->world, preset);
		}

		lilv_nodes_free(mod->presets);
	}

	if(mod->readables)
		lilv_nodes_free(mod->readables);

	if(mod->writables)
		lilv_nodes_free(mod->writables);

	HASH_FREE(&mod->uis, ptr)
	{
		mod_ui_t *mod_ui = ptr;
		_mod_ui_free(mod_ui);
	}

	lilv_uis_free(mod->ui_nodes);
}

static bool
_mod_remove_cb(void *node, void *data)
{
	mod_conn_t *mod_conn = node;
	mod_t *mod = data;
	plughandle_t *handle = mod->handle;

	if(  (mod_conn->source_mod == mod)
		|| (mod_conn->sink_mod == mod) )
	{
		_mod_conn_free(handle, mod_conn);

		return false;
	}

	return true;
}

static void
_mod_remove(plughandle_t *handle, mod_t *mod)
{
	_hash_remove(&handle->mods, mod);
	_hash_remove_cb(&handle->conns, _mod_remove_cb, mod);
	_mod_free(handle, mod);
}

static bool
_tooltip_visible(struct nk_context *ctx)
{
	return nk_widget_has_mouse_click_down(ctx, NK_BUTTON_RIGHT, nk_true)
		|| (nk_widget_is_hovered(ctx) && nk_input_is_key_down(&ctx->input, NK_KEY_CTRL));
}

static void
_expose_main_header(plughandle_t *handle, struct nk_context *ctx, float dy)
{
	nk_menubar_begin(ctx);
	{
		nk_layout_row_dynamic(ctx, dy, 5);
		{
			if(_tooltip_visible(ctx))
				nk_tooltip(ctx, "Ctrl-N");
			if(nk_button_label(ctx, "New") && handle->self_urn)
			{
				if(  _message_request(handle)
					&&  synthpod_patcher_copy(&handle->regs, &handle->forge,
						handle->self_urn, 0, 0) )
				{
					_message_write(handle);
				}
			}

			if(_tooltip_visible(ctx))
				nk_tooltip(ctx, "Ctrl-O");
			if(nk_button_label(ctx, "Open"))
			{
				//FIXME open file dialog
			}

			if(_tooltip_visible(ctx))
				nk_tooltip(ctx, "Ctrl-S");
			if(nk_button_label(ctx, "Save") && handle->bundle_urn)
			{
				if(  _message_request(handle)
					&&  synthpod_patcher_copy(&handle->regs, &handle->forge,
						0, 0, handle->bundle_urn) )
				{
					_message_write(handle);
				}
			}

			if(_tooltip_visible(ctx))
				nk_tooltip(ctx, "Ctrl-Shift-S");
			if(nk_button_label(ctx, "Save As"))
			{
				//FIXME open file dialog
			}

			if(_tooltip_visible(ctx))
				nk_tooltip(ctx, "Ctrl-Q");
			if(nk_button_label(ctx, "Quit"))
			{
				handle->done = true;
			}
		}
		nk_menubar_end(ctx);
	}
}

static int
_sort_plugin_name(const void *a, const void *b)
{
	const LilvPlugin *plug_a = *(const LilvPlugin **)a;
	const LilvPlugin *plug_b = *(const LilvPlugin **)b;

	const char *name_a = NULL;
	const char *name_b = NULL;

	LilvNode *node_a = lilv_plugin_get_name(plug_a);
	LilvNode *node_b = lilv_plugin_get_name(plug_b);

	if(node_a)
		name_a = lilv_node_as_string(node_a);
	if(node_b)
		name_b = lilv_node_as_string(node_b);

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	if(node_a)
		lilv_node_free(node_a);
	if(node_b)
		lilv_node_free(node_b);

	return ret;
}

static void
_refresh_main_plugin_list(plughandle_t *handle)
{
	_hash_free(&handle->plugin_matches);

	const LilvPlugins *plugs = lilv_world_get_all_plugins(handle->world);

	LilvNode *p = NULL;
	if(handle->plugin_search_selector == SELECTOR_SEARCH_COMMENT)
		p = handle->node.rdfs_comment;
	else if(handle->plugin_search_selector == SELECTOR_SEARCH_PROJECT)
		p = handle->node.doap_name;

	bool selector_visible = false;
	LILV_FOREACH(plugins, i, plugs)
	{
		const LilvPlugin *plug = lilv_plugins_get(plugs, i);

		LilvNode *name_node = lilv_plugin_get_name(plug);
		if(name_node)
		{
			const char *name_str = lilv_node_as_string(name_node);
			bool visible = _textedit_len(&handle->plugin_search_edit) == 0;

			if(!visible)
			{
				switch(handle->plugin_search_selector)
				{
					case SELECTOR_SEARCH_NAME:
					{
						if(strcasestr(name_str, _textedit_const(&handle->plugin_search_edit)))
							visible = true;
					} break;
					case SELECTOR_SEARCH_COMMENT:
					{
						LilvNodes *label_nodes = p ? lilv_plugin_get_value(plug, p) : NULL;
						if(label_nodes)
						{
							const LilvNode *label_node = lilv_nodes_size(label_nodes)
								? lilv_nodes_get_first(label_nodes) : NULL;
							if(label_node)
							{
								if(strcasestr(lilv_node_as_string(label_node), _textedit_const(&handle->plugin_search_edit)))
									visible = true;
							}
							lilv_nodes_free(label_nodes);
						}
					} break;
					case SELECTOR_SEARCH_AUTHOR:
					{
						LilvNode *author_node = lilv_plugin_get_author_name(plug);
						if(author_node)
						{
							if(strcasestr(lilv_node_as_string(author_node), _textedit_const(&handle->plugin_search_edit)))
								visible = true;
							lilv_node_free(author_node);
						}
					} break;
					case SELECTOR_SEARCH_CLASS:
					{
						const LilvPluginClass *class = lilv_plugin_get_class(plug);
						if(class)
						{
							const LilvNode *label_node = lilv_plugin_class_get_label(class);
							if(label_node)
							{
								if(strcasestr(lilv_node_as_string(label_node), _textedit_const(&handle->plugin_search_edit)))
									visible = true;
							}
						}
					} break;
					case SELECTOR_SEARCH_PROJECT:
					{
						LilvNode *project = lilv_plugin_get_project(plug);
						if(project)
						{
							LilvNode *label_node = p ? lilv_world_get(handle->world, lilv_plugin_get_uri(plug), p, NULL) : NULL;
							if(label_node)
							{
								if(strcasestr(lilv_node_as_string(label_node), _textedit_const(&handle->plugin_search_edit)))
									visible = true;
								lilv_node_free(label_node);
							}
							lilv_node_free(project);
						}
					} break;

					default: break;
				}
			}

			if(visible)
			{
				_hash_add(&handle->plugin_matches, (void *)plug);
			}

			lilv_node_free(name_node);
		}
	}

	_hash_sort(&handle->plugin_matches, _sort_plugin_name);
}

static void
_expose_main_plugin_list(plughandle_t *handle, struct nk_context *ctx,
	bool find_matches)
{
	if(_hash_empty(&handle->plugin_matches) || find_matches)
		_refresh_main_plugin_list(handle);

	const LilvPlugins *plugs = lilv_world_get_all_plugins(handle->world);

	int count = 0;
	HASH_FOREACH(&handle->plugin_matches, itr)
	{
		const LilvPlugin *plug = *itr;
		if(plug)
		{
			LilvNode *name_node = lilv_plugin_get_name(plug);
			if(name_node)
			{
				const char *name_str = lilv_node_as_string(name_node);

				nk_style_push_style_item(ctx, &ctx->style.selectable.normal, (count++ % 2)
					? nk_style_item_color(nk_rgb(40, 40, 40))
					: nk_style_item_color(nk_rgb(45, 45, 45))); // NK_COLOR_WINDOW

				if(nk_select_label(ctx, name_str, NK_TEXT_LEFT, nk_false))
				{
					_patch_mod_add(handle, plug);
				}

				nk_style_pop_style_item(ctx);

				lilv_node_free(name_node);
			}
		}
	}
}

#if 0
static void
_expose_main_plugin_info(plughandle_t *handle, struct nk_context *ctx)
{
	const LilvPlugin *plug = handle->plugin_selector;

	if(!plug)
		return;

	LilvNode *name_node = lilv_plugin_get_name(plug);
	const LilvNode *uri_node = lilv_plugin_get_uri(plug);
	const LilvNode *bundle_node = lilv_plugin_get_bundle_uri(plug);
	LilvNode *author_name_node = lilv_plugin_get_author_name(plug);
	LilvNode *author_email_node = lilv_plugin_get_author_email(plug);
	LilvNode *author_homepage_node = lilv_plugin_get_author_homepage(plug);
	LilvNodes *minor_nodes = lilv_plugin_get_value(plug, handle->node.lv2_minorVersion);
	LilvNodes *micro_nodes = lilv_plugin_get_value(plug, handle->node.lv2_microVersion);
	LilvNodes *license_nodes = lilv_plugin_get_value(plug, handle->node.doap_license);

	if(name_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Name:    %s", lilv_node_as_string(name_node));
	if(uri_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "URI:     %s", lilv_node_as_uri(uri_node));
	if(bundle_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Bundle:  %s", lilv_node_as_uri(bundle_node));
	if(author_name_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Author:  %s", lilv_node_as_string(author_name_node));
	if(author_email_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Email:   %s", lilv_node_as_string(author_email_node));
	if(author_homepage_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Web:     %s", lilv_node_as_string(author_homepage_node));
	if(lilv_nodes_size(minor_nodes) && lilv_nodes_size(micro_nodes))
		nk_labelf(ctx, NK_TEXT_LEFT, "Version: %i.%i",
			lilv_node_as_int(lilv_nodes_get_first(minor_nodes)),
			lilv_node_as_int(lilv_nodes_get_first(micro_nodes)) );
	if(lilv_nodes_size(license_nodes))
		nk_labelf(ctx, NK_TEXT_LEFT, "License: %s",
			lilv_node_as_uri(lilv_nodes_get_first(license_nodes)) );
	//TODO project

	if(name_node)
		lilv_node_free(name_node);
	if(author_name_node)
		lilv_node_free(author_name_node);
	if(author_email_node)
		lilv_node_free(author_email_node);
	if(author_homepage_node)
		lilv_node_free(author_homepage_node);
	if(minor_nodes)
		lilv_nodes_free(minor_nodes);
	if(micro_nodes)
		lilv_nodes_free(micro_nodes);
	if(license_nodes)
		lilv_nodes_free(license_nodes);
}
#endif

static void
_refresh_main_preset_list_for_bank(plughandle_t *handle,
	LilvNodes *presets, const LilvNode *preset_bank)
{
	bool search = _textedit_len(&handle->preset_search_edit) != 0;

	LILV_FOREACH(nodes, i, presets)
	{
		const LilvNode *preset = lilv_nodes_get(presets, i);

		bool visible = false;

		LilvNode *bank = lilv_world_get(handle->world, preset, handle->node.pset_bank, NULL);
		if(bank)
		{
			if(lilv_node_equals(preset_bank, bank))
				visible = true;

			lilv_node_free(bank);
		}
		else if(!preset_bank)
			visible = true;

		if(visible)
		{
			//FIXME support other search criteria
			LilvNode *label_node = lilv_world_get(handle->world, preset, handle->node.rdfs_label, NULL);
			if(!label_node)
				label_node = lilv_world_get(handle->world, preset, handle->node.lv2_name, NULL);
			if(label_node)
			{
				const char *label_str = lilv_node_as_string(label_node);

				if(!search || strcasestr(label_str, _textedit_const(&handle->preset_search_edit)))
				{
					_hash_add(&handle->preset_matches, (void *)preset);
				}

				lilv_node_free(label_node);
			}
		}
	}
}

static void
_refresh_main_preset_list(plughandle_t *handle, mod_t *mod)
{
	_hash_free(&handle->preset_matches);

	HASH_FOREACH(&mod->banks, itr)
	{
		const LilvNode *bank = *itr;

		_refresh_main_preset_list_for_bank(handle, mod->presets, bank);
	}

	_refresh_main_preset_list_for_bank(handle, mod->presets, NULL);

	_hash_sort_r(&handle->preset_matches, _sort_rdfs_label, handle);
}

static void
_tab_label(struct nk_context *ctx, const char *label)
{
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	struct nk_rect bounds = nk_widget_bounds(ctx);

	nk_fill_rect(canvas, bounds, 0.f, group_color);
	nk_label(ctx, label, NK_TEXT_CENTERED);
}

static void
_expose_main_preset_list_for_bank(plughandle_t *handle, struct nk_context *ctx,
	const LilvNode *preset_bank)
{
	bool first = true;
	int count = 0;
	HASH_FOREACH(&handle->preset_matches, itr)
	{
		const LilvNode *preset = *itr;

		bool visible = false;

		LilvNode *bank = lilv_world_get(handle->world, preset, handle->node.pset_bank, NULL);
		if(bank)
		{
			if(lilv_node_equals(preset_bank, bank))
				visible = true;

			lilv_node_free(bank);
		}
		else if(!preset_bank)
			visible = true;

		if(visible)
		{
			LilvNode *label_node = lilv_world_get(handle->world, preset, handle->node.rdfs_label, NULL);
			if(!label_node)
				label_node = lilv_world_get(handle->world, preset, handle->node.lv2_name, NULL);
			if(label_node)
			{
				if(first)
				{
					LilvNode *bank_label_node = NULL;
					if(preset_bank)
					{
						bank_label_node = lilv_world_get(handle->world, preset_bank, handle->node.rdfs_label, NULL);
						if(!bank_label_node)
							bank_label_node = lilv_world_get(handle->world, preset_bank, handle->node.lv2_name, NULL);
					}
					const char *bank_label = bank_label_node
						? lilv_node_as_string(bank_label_node)
						: "Unbanked";

					nk_layout_row_dynamic(ctx, handle->dy2, 1);
					_tab_label(ctx, bank_label);

					nk_layout_row_dynamic(ctx, handle->dy2, 1);

					if(bank_label_node)
						lilv_node_free(bank_label_node);

					first = false;
				}

				const char *label_str = lilv_node_as_string(label_node);

				nk_style_push_style_item(ctx, &ctx->style.selectable.normal, (count++ % 2)
					? nk_style_item_color(nk_rgb(40, 40, 40))
					: nk_style_item_color(nk_rgb(45, 45, 45))); // NK_COLOR_WINDOW

				if(nk_select_label(ctx, label_str, NK_TEXT_LEFT, nk_false))
				{
					_patch_mod_preset_set(handle, handle->module_selector, preset);
				}

				nk_style_pop_style_item(ctx);

				lilv_node_free(label_node);
			}
		}
	}
}

static void
_expose_main_preset_list(plughandle_t *handle, struct nk_context *ctx,
	bool find_matches)
{
	mod_t *mod = handle->module_selector;

	if(mod && mod->presets)
	{
		if(_hash_empty(&handle->preset_matches) || find_matches)
			_refresh_main_preset_list(handle, mod);

		HASH_FOREACH(&mod->banks, itr)
		{
			const LilvNode *bank = *itr;

			_expose_main_preset_list_for_bank(handle, ctx, bank);
		}

		_expose_main_preset_list_for_bank(handle, ctx, NULL);
	}
}

#if 0
static void
_expose_main_preset_info(plughandle_t *handle, struct nk_context *ctx)
{
	const LilvNode *preset = handle->preset_selector;

	if(!preset)
		return;

	//FIXME
	LilvNode *name_node = lilv_world_get(handle->world, preset, handle->node.rdfs_label, NULL);
	if(!name_node)
		name_node = lilv_world_get(handle->world, preset, handle->node.lv2_name, NULL);
	LilvNode *comment_node = lilv_world_get(handle->world, preset, handle->node.rdfs_comment, NULL);
	LilvNode *bank_node = lilv_world_get(handle->world, preset, handle->node.pset_bank, NULL);
	LilvNode *minor_node = lilv_world_get(handle->world, preset, handle->node.lv2_minorVersion, NULL);
	LilvNode *micro_node = lilv_world_get(handle->world, preset, handle->node.lv2_microVersion, NULL);
	LilvNode *license_node = lilv_world_get(handle->world, preset, handle->node.doap_license, NULL);

	if(name_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Name:    %s", lilv_node_as_string(name_node));
	if(preset)
		nk_labelf(ctx, NK_TEXT_LEFT, "URI:     %s", lilv_node_as_uri(preset));
	if(comment_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Comment: %s", lilv_node_as_string(comment_node));
	if(bank_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Bank:    %s", lilv_node_as_uri(bank_node));
	if(minor_node && micro_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Version: %i.%i",
			lilv_node_as_int(minor_node), lilv_node_as_int(micro_node) );
	if(license_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "License: %s", lilv_node_as_uri(license_node));
	//TODO author, project

	if(name_node)
		lilv_node_free(name_node);
	if(comment_node)
		lilv_node_free(comment_node);
	if(bank_node)
		lilv_node_free(bank_node);
	if(minor_node)
		lilv_node_free(minor_node);
	if(micro_node)
		lilv_node_free(micro_node);
	if(license_node)
		lilv_node_free(license_node);
}
#endif

static int
_dial_bool(struct nk_context *ctx, int32_t *val, struct nk_color color, bool editable)
{
	const int32_t tmp = *val;
	struct nk_rect bounds = nk_layout_space_bounds(ctx);
	const bool left_mouse_click_in_cursor = nk_widget_is_mouse_clicked(ctx, NK_BUTTON_LEFT);
	const enum nk_widget_layout_states layout_states = nk_widget(&bounds, ctx);

	if(layout_states != NK_WIDGET_INVALID)
	{
		enum nk_widget_states states = NK_WIDGET_STATE_INACTIVE;
		struct nk_input *in = ( (layout_states == NK_WIDGET_ROM)
			|| (ctx->current->layout->flags & NK_WINDOW_ROM) ) ? 0 : &ctx->input;

		if(in && editable && (layout_states == NK_WIDGET_VALID) )
		{
			bool mouse_has_scrolled = false;

			if(left_mouse_click_in_cursor)
			{
				states = NK_WIDGET_STATE_ACTIVED;
			}
			else if(nk_input_is_mouse_hovering_rect(in, bounds))
			{
				if(in->mouse.scroll_delta != 0.f) // has scrolling
				{
					mouse_has_scrolled = true;
					in->mouse.scroll_delta = 0.f;
				}

				states = NK_WIDGET_STATE_HOVER;
			}

			if(left_mouse_click_in_cursor || mouse_has_scrolled)
			{
				*val = !*val;
			}
		}

		const struct nk_style_item *fg = NULL;
		const struct nk_style_item *bg = NULL;

		switch(states)
		{
			case NK_WIDGET_STATE_HOVER:
			{
				bg = &ctx->style.progress.hover;
				fg = &ctx->style.progress.cursor_hover;
			}	break;
			case NK_WIDGET_STATE_ACTIVED:
			{
				bg = &ctx->style.progress.active;
				fg = &ctx->style.progress.cursor_active;
			}	break;
			default:
			{
				bg = &ctx->style.progress.normal;
				fg = &ctx->style.progress.cursor_normal;
			}	break;
		}

		const struct nk_color bg_color = bg->data.color;
		struct nk_color fg_color = fg->data.color;

		fg_color.r = (int)fg_color.r * color.r / 0xff;
		fg_color.g = (int)fg_color.g * color.g / 0xff;
		fg_color.b = (int)fg_color.b * color.b / 0xff;
		fg_color.a = (int)fg_color.a * color.a / 0xff;

		struct nk_command_buffer *canv= nk_window_get_canvas(ctx);
		const float w2 = bounds.w/2;
		const float h2 = bounds.h/2;
		const float r1 = NK_MIN(w2, h2);
		const float r2 = r1 / 2;
		const float cx = bounds.x + w2;
		const float cy = bounds.y + h2;

		nk_fill_arc(canv, cx, cy, r2, 0.f, 2*M_PI, fg_color);
		nk_fill_arc(canv, cx, cy, r2 - 2, 0.f, 2*M_PI, ctx->style.window.background);
		nk_fill_arc(canv, cx, cy, r2 - 4, 0.f, 2*M_PI,
			*val ? fg_color : bg_color);
	}

	return tmp != *val;
}

static float
_dial_numeric_behavior(struct nk_context *ctx, struct nk_rect bounds,
	enum nk_widget_states *states, int *divider, struct nk_input *in)
{
	const struct nk_mouse_button *btn = &in->mouse.buttons[NK_BUTTON_LEFT];;
	const bool left_mouse_down = btn->down;
	const bool left_mouse_click_in_cursor = nk_input_has_mouse_click_down_in_rect(in,
		NK_BUTTON_LEFT, bounds, nk_true);

	float dd = 0.f;
	if(left_mouse_down && left_mouse_click_in_cursor)
	{
		const float dx = in->mouse.delta.x;
		const float dy = in->mouse.delta.y;
		dd = fabs(dx) > fabs(dy) ? dx : -dy;

		*states = NK_WIDGET_STATE_ACTIVED;
	}
	else if(nk_input_is_mouse_hovering_rect(in, bounds))
	{
		if(in->mouse.scroll_delta != 0.f) // has scrolling
		{
			dd = in->mouse.scroll_delta;
			in->mouse.scroll_delta = 0.f;
		}

		*states = NK_WIDGET_STATE_HOVER;
	}

	if(nk_input_is_key_down(in, NK_KEY_CTRL))
		*divider *= 4;
	if(nk_input_is_key_down(in, NK_KEY_SHIFT))
		*divider *= 4;

	return dd;
}

static void
_dial_numeric_draw(struct nk_context *ctx, struct nk_rect bounds,
	enum nk_widget_states states, float perc, struct nk_color color)
{
	struct nk_command_buffer *canv= nk_window_get_canvas(ctx);
	const struct nk_style_item *bg = NULL;
	const struct nk_style_item *fg = NULL;

	switch(states)
	{
		case NK_WIDGET_STATE_HOVER:
		{
			bg = &ctx->style.progress.hover;
			fg = &ctx->style.progress.cursor_hover;
		}	break;
		case NK_WIDGET_STATE_ACTIVED:
		{
			bg = &ctx->style.progress.active;
			fg = &ctx->style.progress.cursor_active;
		}	break;
		default:
		{
			bg = &ctx->style.progress.normal;
			fg = &ctx->style.progress.cursor_normal;
		}	break;
	}

	const struct nk_color bg_color = bg->data.color;
	struct nk_color fg_color = fg->data.color;

	fg_color.r = (int)fg_color.r * color.r / 0xff;
	fg_color.g = (int)fg_color.g * color.g / 0xff;
	fg_color.b = (int)fg_color.b * color.b / 0xff;
	fg_color.a = (int)fg_color.a * color.a / 0xff;

	const float w2 = bounds.w/2;
	const float h2 = bounds.h/2;
	const float r1 = NK_MIN(w2, h2);
	const float r2 = r1 / 2;
	const float cx = bounds.x + w2;
	const float cy = bounds.y + h2;
	const float aa = M_PI/6;
	const float a1 = M_PI/2 + aa;
	const float a2 = 2*M_PI + M_PI/2 - aa;
	const float a3 = a1 + (a2 - a1)*perc;

	nk_fill_arc(canv, cx, cy, r1, a1, a2, bg_color);
	nk_fill_arc(canv, cx, cy, r1, a1, a3, fg_color);
	nk_fill_arc(canv, cx, cy, r2, 0.f, 2*M_PI, ctx->style.window.background);
}

static int
_dial_double(struct nk_context *ctx, double min, double *val, double max, float mul,
	struct nk_color color, bool editable)
{
	const double tmp = *val;
	struct nk_rect bounds = nk_layout_space_bounds(ctx);
	const enum nk_widget_layout_states layout_states = nk_widget(&bounds, ctx);

	if(layout_states != NK_WIDGET_INVALID)
	{
		enum nk_widget_states states = NK_WIDGET_STATE_INACTIVE;
		const double range = max - min;
		struct nk_input *in = ( (layout_states == NK_WIDGET_ROM)
			|| (ctx->current->layout->flags & NK_WINDOW_ROM) ) ? 0 : &ctx->input;

		if(in && editable && (layout_states == NK_WIDGET_VALID) )
		{
			int divider = 1;
			const float dd = _dial_numeric_behavior(ctx, bounds, &states, &divider, in);

			if(dd != 0.f) // update value
			{
				const double per_pixel_inc = mul * range / bounds.w / divider;

				*val += dd * per_pixel_inc;
				*val = NK_CLAMP(min, *val, max);
			}
		}

		const float perc = (*val - min) / range;
		_dial_numeric_draw(ctx, bounds, states, perc, color);
	}

	return tmp != *val;
}

static int
_dial_long(struct nk_context *ctx, int64_t min, int64_t *val, int64_t max, float mul,
	struct nk_color color, bool editable)
{
	const int64_t tmp = *val;
	struct nk_rect bounds = nk_layout_space_bounds(ctx);
	const enum nk_widget_layout_states layout_states = nk_widget(&bounds, ctx);

	if(layout_states != NK_WIDGET_INVALID)
	{
		enum nk_widget_states states = NK_WIDGET_STATE_INACTIVE;
		const int64_t range = max - min;
		struct nk_input *in = ( (layout_states == NK_WIDGET_ROM)
			|| (ctx->current->layout->flags & NK_WINDOW_ROM) ) ? 0 : &ctx->input;

		if(in && editable && (layout_states == NK_WIDGET_VALID) )
		{
			int divider = 1;
			const float dd = _dial_numeric_behavior(ctx, bounds, &states, &divider, in);

			if(dd != 0.f) // update value
			{
				const double per_pixel_inc = mul * range / bounds.w / divider;

				const double diff = dd * per_pixel_inc;
				*val += diff < 0.0 ? floor(diff) : ceil(diff);
				*val = NK_CLAMP(min, *val, max);
			}
		}

		const float perc = (float)(*val - min) / range;
		_dial_numeric_draw(ctx, bounds, states, perc, color);
	}

	return tmp != *val;
}

static int
_dial_float(struct nk_context *ctx, float min, float *val, float max, float mul,
	struct nk_color color, bool editable)
{
	double tmp = *val;
	const int res = _dial_double(ctx, min, &tmp, max, mul, color, editable);
	*val = tmp;

	return res;
}

static int
_dial_int(struct nk_context *ctx, int32_t min, int32_t *val, int32_t max, float mul,
	struct nk_color color, bool editable)
{
	int64_t tmp = *val;
	const int res = _dial_long(ctx, min, &tmp, max, mul, color, editable);
	*val = tmp;

	return res;
}

static void
_expose_atom_port(struct nk_context *ctx, mod_t *mod, audio_port_t *audio,
	float dy, const char *name_str)
{
	const float DY = nk_window_get_content_region(ctx).h
		- 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);
		nk_label(ctx, name_str, NK_TEXT_LEFT);

		//FIXME

		nk_group_end(ctx);
	}

	//FIXME
}

static void
_expose_audio_port(struct nk_context *ctx, mod_t *mod, audio_port_t *audio,
	float dy, const char *name_str, bool is_cv)
{
	const float DY = nk_window_get_content_region(ctx).h
		- 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);
		nk_label(ctx, name_str, NK_TEXT_LEFT);

		struct nk_rect bounds;
		const enum nk_widget_layout_states states = nk_widget(&bounds, ctx);
		if(states != NK_WIDGET_INVALID)
		{
			struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

			const struct nk_color bg = ctx->style.property.normal.data.color;
			nk_fill_rect(canvas, bounds, ctx->style.property.rounding, bg);
			nk_stroke_rect(canvas, bounds, ctx->style.property.rounding, ctx->style.property.border, bg);

			const struct nk_rect orig = bounds;
			struct nk_rect outline;
			const float mx1 = 58.f / 70.f;
			const float mx2 = 12.f / 70.f;
			const uint8_t alph = 0x7f;

			// <= -6dBFS
			{
				const float dbfs = NK_MIN(audio->peak, mx1);
				const uint8_t dcol = 0xff * dbfs / mx1;
				const struct nk_color left = is_cv
					? nk_rgba(0xff, 0x00, 0xff, alph)
					: nk_rgba(0x00, 0xff, 0xff, alph);
				const struct nk_color bottom = left;
				const struct nk_color right = is_cv
					? nk_rgba(0xff, dcol, 0xff-dcol, alph)
					: nk_rgba(dcol, 0xff, 0xff-dcol, alph);
				const struct nk_color top = right;

				const float ox = ctx->style.font->height/2 + ctx->style.property.border + ctx->style.property.padding.x;
				const float oy = ctx->style.property.border + ctx->style.property.padding.y;
				bounds.x += ox;
				bounds.y += oy;
				bounds.w -= 2*ox;
				bounds.h -= 2*oy;
				outline = bounds;
				bounds.w *= dbfs;

				nk_fill_rect_multi_color(canvas, bounds, left, top, right, bottom);
			}

			// > 6dBFS
			if(audio->peak > mx1)
			{
				const float dbfs = audio->peak - mx1;
				const uint8_t dcol = 0xff * dbfs / mx2;
				const struct nk_color left = nk_rgba(0xff, 0xff, 0x00, alph);
				const struct nk_color bottom = left;
				const struct nk_color right = nk_rgba(0xff, 0xff - dcol, 0x00, alph);
				const struct nk_color top = right;

				bounds = outline;
				bounds.x += bounds.w * mx1;
				bounds.w *= dbfs;
				nk_fill_rect_multi_color(canvas, bounds, left, top, right, bottom);
			}

			// draw 6dBFS lines from -60 to +6
			for(unsigned i = 4; i <= 70; i += 6)
			{
				const bool is_zero = (i == 64);
				const float dx = outline.w * i / 70.f;

				const float x0 = outline.x + dx;
				const float y0 = is_zero ? orig.y + 2.f : outline.y;

				const float border = (is_zero ? 2.f : 1.f) * ctx->style.window.group_border;

				const float x1 = x0;
				const float y1 = is_zero ? orig.y + orig.h - 2.f : outline.y + outline.h;

				nk_stroke_line(canvas, x0, y0, x1, y1, border, ctx->style.window.group_border_color);
			}

			nk_stroke_rect(canvas, outline, 0.f, ctx->style.window.group_border, ctx->style.window.group_border_color);
		}

		nk_group_end(ctx);
	}

	if(_dial_float(ctx, 0.f, &audio->gain, 1.f, 1.f, nk_rgb(0xff, 0xff, 0xff), true))
	{
		//FIXME
	}
}

const char *lab = "#"; //FIXME

static bool
_expose_control_port(struct nk_context *ctx, mod_t *mod, control_port_t *control,
	float dy, const char *name_str)
{
	bool changed = false;

	const float DY = nk_window_get_content_region(ctx).h
		- 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);
		nk_label(ctx, name_str, NK_TEXT_LEFT);

		if(!_hash_empty(&control->points))
		{
			scale_point_t *ref = control->points_ref;
			if(nk_combo_begin_label(ctx, ref->label, nk_vec2(nk_widget_width(ctx), 7*dy)))
			{
				nk_layout_row_dynamic(ctx, dy, 1);
				HASH_FOREACH(&control->points, itr)
				{
					scale_point_t *point = *itr;

					if(nk_combo_item_label(ctx, point->label, NK_TEXT_LEFT) && !control->is_readonly)
					{
						control->points_ref = point;
						control->val = point->val;
						changed = true;
					}
				}

				nk_combo_end(ctx);
			}
		}
		else if(control->is_int)
		{
			if(control->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%"PRIi32, control->val.i);
			}
			else // !readonly
			{
				const float inc = control->span.i / nk_widget_width(ctx);
				int val = control->val.i;
				nk_property_int(ctx, lab, control->min.i, &val, control->max.i, 1.f, inc);
				if(val != control->val.i)
					changed = true;
				control->val.i = val;
			}
		}
		else if(control->is_bool)
		{
			nk_spacing(ctx, 1);
		}
		else // is_float
		{
			if(control->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%f", control->val.f);
			}
			else // !readonly
			{
				const float step = control->span.f / 100.f;
				const float inc = control->span.f / nk_widget_width(ctx);
				const float val = control->val.f;
				nk_property_float(ctx, lab, control->min.f, &control->val.f, control->max.f, step, inc);
				if(val != control->val.f)
					changed = true;
			}
		}

		nk_group_end(ctx);
	}

	if(!_hash_empty(&control->points))
	{
		nk_spacing(ctx, 1);
	}
	else if(control->is_int)
	{
		if(_dial_int(ctx, control->min.i, &control->val.i, control->max.i, 1.f, nk_rgb(0xff, 0xff, 0xff), !control->is_readonly))
		{
			changed = true;
		}
	}
	else if(control->is_bool)
	{
		if(_dial_bool(ctx, &control->val.i, nk_rgb(0xff, 0xff, 0xff), !control->is_readonly))
		{
			changed = true;
		}
	}
	else // is_float
	{
		if(_dial_float(ctx, control->min.f, &control->val.f, control->max.f, 1.f, nk_rgb(0xff, 0xff, 0xff), !control->is_readonly))
		{
			changed = true;
		}
	}

	return changed;
}

static void
_expose_port(struct nk_context *ctx, mod_t *mod, port_t *port, float dy)
{
	plughandle_t *handle = mod->handle;

	if(nk_widget_has_mouse_click_down(ctx, NK_BUTTON_LEFT, nk_true))
	{
		handle->port_selector = port;
		handle->param_selector = NULL;
	}

	bool is_hilighted = false;
	if(handle->port_selector == port)
	{
		nk_style_push_color(ctx, &ctx->style.window.group_border_color, hilight_color);
		is_hilighted = true;
	}
	else if( (port->type == PROPERTY_TYPE_CONTROL) && (port->control.automation.type != AUTO_NONE) )
	{
		nk_style_push_color(ctx, &ctx->style.window.group_border_color, toggle_color);
		is_hilighted = true;
	}

	if(nk_group_begin(ctx, port->name, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
	{
		switch(port->type)
		{
			case PROPERTY_TYPE_AUDIO:
			{
				_expose_audio_port(ctx, mod, &port->audio, dy, port->name, false);
			} break;
			case PROPERTY_TYPE_CV:
			{
				_expose_audio_port(ctx, mod, &port->audio, dy, port->name, true);
				//FIXME notification
			} break;
			case PROPERTY_TYPE_CONTROL:
			{
				if(_expose_control_port(ctx, mod, &port->control, dy, port->name))
				{
					const float val = port->control.is_bool || port->control.is_int
						? port->control.val.i
						: port->control.val.f;

					_patch_notification_add(handle, port, handle->regs.port.float_protocol.urid,
						sizeof(float), handle->forge.Float, &val);

					// route to ui
					_mod_uis_send(mod, port->index, sizeof(float), 0, &val);
				}
			} break;

			default:
			{
				if(port->type & PROPERTY_TYPE_ATOM)
				{
					_expose_atom_port(ctx, mod, &port->audio, dy, port->name);
				}
			} break;
		}

		nk_group_end(ctx);
	}

	if(is_hilighted)
		nk_style_pop_color(ctx);
}

static bool
_widget_string(plughandle_t *handle, struct nk_context *ctx,
	struct nk_text_edit *editor, bool editable)
{
	bool commited = false;

	const int old_len = nk_str_len_char(&editor->string);
	nk_flags flags = NK_EDIT_BOX;
	if(!editable)
		flags |= NK_EDIT_READ_ONLY;
#if 0 //FIXME
	if(has_shift_enter)
#endif
		flags |= NK_EDIT_SIG_ENTER;
	const nk_flags state = nk_edit_buffer(ctx, flags,
		editor, nk_filter_default);
	if(state & NK_EDIT_COMMITED)
		commited = true;
#if 0 //FIXME
	if( (state & NK_EDIT_ACTIVE) && (old_len != nk_str_len_char(editor.string)) )
		param->dirty = true;
	if( (state & NK_EDIT_ACTIVE) && handle->has_control_a)
		nk_textedit_select_all(editor);
#endif

	return commited;
}

static bool
_expose_param_inner(struct nk_context *ctx, param_t *param, plughandle_t *handle,
	float dy, const char *name_str)
{
	const float DY = nk_window_get_content_region(ctx).h
		- 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	bool changed = false;
	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);
		nk_label(ctx, name_str, NK_TEXT_LEFT);

		if(param->range == handle->forge.Int)
		{
			if(param->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%"PRIi32, param->val.i);
			}
			else // !readonly
			{
				const float inc = param->span.i / nk_widget_width(ctx);
				int val = param->val.i;
				nk_property_int(ctx, lab, param->min.i, &val, param->max.i, 1.f, inc);
				if(val != param->val.i)
					changed = true;
				param->val.i = val;
			}
		}
		else if(param->range == handle->forge.Long)
		{
			if(param->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%"PRIi64, param->val.h);
			}
			else // !readonly
			{
				const float inc = param->span.h / nk_widget_width(ctx);
				int val = param->val.h;
				nk_property_int(ctx, lab, param->min.h, &val, param->max.h, 1.f, inc);
				if(val != param->val.h)
					changed = true;
				param->val.h = val;
			}
		}
		else if(param->range == handle->forge.Bool)
		{
			nk_spacing(ctx, 1);
		}
		else if(param->range == handle->forge.Float)
		{
			if(param->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%f", param->val.f);
			}
			else // !readonly
			{
				const float step = param->span.f / 100.f;
				const float inc = param->span.f / nk_widget_width(ctx);
				const float val = param->val.f;
				nk_property_float(ctx, lab, param->min.f, &param->val.f, param->max.f, step, inc);
				if(val != param->val.f)
					changed = true;
			}
		}
		else if(param->range == handle->forge.Double)
		{
			if(param->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%lf", param->val.d);
			}
			else // !readonly
			{
				const double step = param->span.d / 100.0;
				const float inc = param->span.d / nk_widget_width(ctx);
				const double val = param->val.d;
				nk_property_double(ctx, lab, param->min.d, &param->val.d, param->max.d, step, inc);
				if(val != param->val.d)
					changed = true;
			}
		}
		else if(param->range == handle->forge.String)
		{
			if(_widget_string(handle, ctx, &param->val.editor, !param->is_readonly))
				changed = true;
		}
		else if(param->range == handle->forge.Chunk)
		{
			nk_labelf(ctx, NK_TEXT_RIGHT, "%"PRIu32" bytes", param->val.chunk.size);
			//FIXME file dialog
		}
		else
		{
			nk_spacing(ctx, 1);
		}
		//FIXME handle remaining types

		nk_group_end(ctx);
	}

	if(param->range == handle->forge.Int)
	{
		if(_dial_int(ctx, param->min.i, &param->val.i, param->max.i, 1.f, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			changed = true;
		}
	}
	else if(param->range == handle->forge.Long)
	{
		if(_dial_long(ctx, param->min.h, &param->val.h, param->max.h, 1.f, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			changed = true;
		}
	}
	else if(param->range == handle->forge.Bool)
	{
		if(_dial_bool(ctx, &param->val.i, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			changed = true;
		}
	}
	else if(param->range == handle->forge.Float)
	{
		if(_dial_float(ctx, param->min.f, &param->val.f, param->max.f, 1.f, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			changed = true;
		}
	}
	else if(param->range == handle->forge.Double)
	{
		if(_dial_double(ctx, param->min.d, &param->val.d, param->max.d, 1.f, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			changed = true;
		}
	}
	else
	{
		nk_spacing(ctx, 1);
	}
	//FIXME handle remaining types
	
	return changed;
}

static void
_expose_param(plughandle_t *handle, mod_t *mod, struct nk_context *ctx, param_t *param, float dy)
{
	const char *name_str = param->label ? param->label : "Unknown";

	if(nk_widget_has_mouse_click_down(ctx, NK_BUTTON_LEFT, nk_true))
	{
		handle->port_selector = NULL;
		handle->param_selector = param;
	}

	bool is_hilighted = false;
	if(handle->param_selector == param)
	{
		nk_style_push_color(ctx, &ctx->style.window.group_border_color, hilight_color);
		is_hilighted = true;
	}
	else if(param->automation.type != AUTO_NONE)
	{
		nk_style_push_color(ctx, &ctx->style.window.group_border_color, toggle_color);
		is_hilighted = true;
	}

	if(nk_group_begin(ctx, name_str, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
	{
		if(_expose_param_inner(ctx, param, handle, dy, name_str))
		{
			//FIXME sandbox_master_send is not necessary, as messages should be fed back via dsp to nk
			if(param->range == handle->forge.Int)
			{
				_patch_notification_add_patch_set(handle, mod,
					handle->regs.port.event_transfer.urid, 0, 0, param->property,
					sizeof(int32_t), handle->forge.Int, &param->val.i);
			}
			else if(param->range == handle->forge.Bool)
			{
				_patch_notification_add_patch_set(handle, mod,
					handle->regs.port.event_transfer.urid, 0, 0, param->property,
					sizeof(int32_t), handle->forge.Bool, &param->val.i);
			}
			else if(param->range == handle->forge.Long)
			{
				_patch_notification_add_patch_set(handle, mod,
					handle->regs.port.event_transfer.urid, 0, 0, param->property,
					sizeof(int64_t), handle->forge.Long, &param->val.h);
			}
			else if(param->range == handle->forge.Float)
			{
				_patch_notification_add_patch_set(handle, mod,
					handle->regs.port.event_transfer.urid, 0, 0, param->property,
					sizeof(float), handle->forge.Float, &param->val.f);
			}
			else if(param->range == handle->forge.Double)
			{
				_patch_notification_add_patch_set(handle, mod,
					handle->regs.port.event_transfer.urid, 0, 0, param->property,
					sizeof(double), handle->forge.Double, &param->val.d);
			}
			else if(param->range == handle->forge.String)
			{
				const char *str = nk_str_get_const(&param->val.editor.string);
				const uint32_t sz= nk_str_len_char(&param->val.editor.string) + 1;

				_patch_notification_add_patch_set(handle, mod,
					handle->regs.port.event_transfer.urid, 0, 0, param->property,
					sz, handle->forge.String, str);
			}
			else if(param->range == handle->forge.Chunk)
			{
				chunk_t *chunk = &param->val.chunk;

				_patch_notification_add_patch_set(handle, mod,
					handle->regs.port.event_transfer.urid, 0, 0, param->property,
					chunk->size, handle->forge.Chunk, chunk->body);
			}
			//FIXME handle remaining types
		}

		nk_group_end(ctx);
	}

	if(is_hilighted)
		nk_style_pop_color(ctx);
}

static void
_refresh_main_port_list(plughandle_t *handle, mod_t *mod)
{
	_hash_free(&handle->port_matches);

	bool search = _textedit_len(&handle->port_search_edit) != 0;

	HASH_FOREACH(&mod->ports, itr)
	{
		port_t *port = *itr;

		bool visible = true;
		if(search)
		{
			if(port->name)
			{
				if(!strcasestr(port->name, _textedit_const(&handle->port_search_edit)))
					visible = false;
			}
		}

		if(visible)
			_hash_add(&handle->port_matches, port);
	}
}

static void
_refresh_main_param_list(plughandle_t *handle, mod_t *mod)
{
	_hash_free(&handle->param_matches);

	bool search = _textedit_len(&handle->port_search_edit) != 0;

	HASH_FOREACH(&mod->params, itr)
	{
		param_t *param = *itr;

		bool visible = true;
		if(search)
		{
			if(param->label)
			{
				if(!strcasestr(param->label, _textedit_const(&handle->port_search_edit)))
					visible = false;
			}
		}

		if(visible)
			_hash_add(&handle->param_matches, param);
	}
}

static void
_expose_control_list(plughandle_t *handle, mod_t *mod, struct nk_context *ctx,
	float DY, float dy, bool find_matches)
{
	if(_hash_empty(&handle->port_matches) || find_matches)
	{
		_refresh_main_port_list(handle, mod);
		_refresh_main_param_list(handle, mod);
		_refresh_main_dynam_list(handle, mod);
	}

	HASH_FOREACH(&mod->groups, itr)
	{
		const LilvNode *mod_group = *itr;

		LilvNode *group_label_node = lilv_world_get(handle->world, mod_group, handle->node.rdfs_label, NULL);
		if(!group_label_node)
			group_label_node = lilv_world_get(handle->world, mod_group, handle->node.lv2_name, NULL);
		if(group_label_node)
		{
			bool first = true;
			HASH_FOREACH(&handle->port_matches, port_itr)
			{
				port_t *port = *port_itr;
				if(!lilv_nodes_contains(port->groups, mod_group))
					continue;

				if(first)
				{
					nk_layout_row_dynamic(ctx, handle->dy2, 1);
					_tab_label(ctx, lilv_node_as_string(group_label_node));

					nk_layout_row_dynamic(ctx, DY, 4);
					first = false;
				}

				_expose_port(ctx, mod, port, dy);
			}

			lilv_node_free(group_label_node);
		}
	}

	{
		bool first = true;
		HASH_FOREACH(&handle->port_matches, itr)
		{
			port_t *port = *itr;
			if(lilv_nodes_size(port->groups))
				continue;

			if(first)
			{
				nk_layout_row_dynamic(ctx, handle->dy2, 1);
				_tab_label(ctx, "Ungrouped");

				nk_layout_row_dynamic(ctx, DY, 4);
				first = false;
			}

			_expose_port(ctx, mod, port, dy);
		}
	}

	{
		bool first = true;
		HASH_FOREACH(&handle->param_matches, itr)
		{
			param_t *param = *itr;

			if(first)
			{
				nk_layout_row_dynamic(ctx, handle->dy2, 1);
				_tab_label(ctx, "Parameters");

				nk_layout_row_dynamic(ctx, DY, 4);
				first = false;
			}

			_expose_param(handle, mod, ctx, param, dy);
		}
	}

	{
		bool first = true;
		HASH_FOREACH(&handle->dynam_matches, itr)
		{
			param_t *param = *itr;

			if(first)
			{
				nk_layout_row_dynamic(ctx, handle->dy2, 1);
				_tab_label(ctx, "Dynameters");

				nk_layout_row_dynamic(ctx, DY, 4);
				first = false;
			}

			_expose_param(handle, mod, ctx, param, dy);
		}
	}
}

static bool
_mod_moveable(plughandle_t *handle, struct nk_context *ctx, mod_t *mod,
	struct nk_rect *bounds)
{
	struct nk_input *in = &ctx->input;

	const bool is_hovering = nk_input_is_mouse_hovering_rect(in, *bounds);

	if(mod->moving)
	{
		if(nk_input_is_mouse_released(in, NK_BUTTON_LEFT))
		{
			mod->moving = false;

			if(  _message_request(handle)
				&&  synthpod_patcher_set(&handle->regs, &handle->forge,
					mod->urn, 0, handle->regs.synthpod.module_position_x.urid,
					sizeof(float), handle->forge.Float, &mod->pos.x) )
			{
				_message_write(handle);
			}

			if(  _message_request(handle)
				&& synthpod_patcher_set(&handle->regs, &handle->forge,
					mod->urn, 0, handle->regs.synthpod.module_position_y.urid,
					sizeof(float), handle->forge.Float, &mod->pos.y) )
			{
				_message_write(handle);
			}
		}
		else
		{
			mod->pos.x += in->mouse.delta.x;
			mod->pos.y += in->mouse.delta.y;
			bounds->x += in->mouse.delta.x;
			bounds->y += in->mouse.delta.y;

			// move connections together with mod
			HASH_FOREACH(&handle->conns, mod_conn_itr)
			{
				mod_conn_t *mod_conn = *mod_conn_itr;

				if(mod_conn->source_mod == mod)
				{
					mod_conn->pos.x += in->mouse.delta.x/2;
					mod_conn->pos.y += in->mouse.delta.y/2;
				}

				if(mod_conn->sink_mod == mod)
				{
					mod_conn->pos.x += in->mouse.delta.x/2;
					mod_conn->pos.y += in->mouse.delta.y/2;
				}
			}
		}
	}
	else if(is_hovering
		&& nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT)
		&& nk_input_is_key_down(in, NK_KEY_CTRL) )
	{
		mod->moving = true;
	}

	if  (is_hovering
		&& nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT) )
	{
		// consume mouse event
		in->mouse.buttons[NK_BUTTON_RIGHT].down = nk_false;
		in->mouse.buttons[NK_BUTTON_RIGHT].clicked = nk_false;

		return true;
	}

	return false;
}

static bool
_source_type_match(plughandle_t *handle, property_type_t source_type)
{
	if(handle->type == PROPERTY_TYPE_AUTOMATION)
		return (PROPERTY_TYPE_MIDI | PROPERTY_TYPE_OSC) & source_type;

	return handle->type & source_type;
}

static bool
_sink_type_match(plughandle_t *handle, property_type_t sink_type)
{
	return handle->type & sink_type;
}

static void
_mod_connectors(plughandle_t *handle, struct nk_context *ctx, mod_t *mod,
	struct nk_vec2 dim, bool is_hilighted)
{
	const struct nk_input *in = &ctx->input;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_vec2 scrolling = handle->scrolling;

	const float cw = 4.f * handle->scale;

	const struct nk_rect bounds = nk_rect(
		mod->pos.x - dim.x/2, mod->pos.y - dim.y/2,
		dim.x, dim.y
	);

	// output connector
	if(_source_type_match(handle, mod->source_type))
	{
		const float cx = mod->pos.x - scrolling.x + dim.x/2 + 2*cw;
		const float cy = mod->pos.y - scrolling.y;
		const struct nk_rect outer = nk_rect(
			cx - cw, cy - cw,
			4*cw, 4*cw
		);

		// start linking process
		const bool has_click_body = nk_input_has_mouse_click_down_in_rect(in, NK_BUTTON_LEFT, bounds, nk_true);
		const bool has_click_handle = nk_input_has_mouse_click_down_in_rect(in, NK_BUTTON_LEFT, outer, nk_true);
		if(  (has_click_body || has_click_handle)
			&& !nk_input_is_key_down(in, NK_KEY_CTRL))
		{
			handle->linking.active = true;
			handle->linking.source_mod = mod;
		}

		const bool is_hovering_body = nk_input_is_mouse_hovering_rect(in, bounds);
		const bool is_hovering_handle= nk_input_is_mouse_hovering_rect(in, outer);
		nk_fill_arc(canvas, cx, cy, cw, 0.f, 2*NK_PI,
			is_hilighted ? hilight_color : grab_handle_color);
		if(  (is_hovering_handle && !handle->linking.active)
			|| (handle->linking.active && (handle->linking.source_mod == mod)) )
		{
			nk_stroke_arc(canvas, cx, cy, 2*cw, 0.f, 2*NK_PI, 1.f, hilight_color);
		}

		// draw ilne from linked node slot to mouse position
		if(  handle->linking.active
			&& (handle->linking.source_mod == mod) )
		{
			struct nk_vec2 m = in->mouse.pos;

			nk_stroke_line(canvas, cx, cy, m.x, m.y, 1.f, hilight_color);
		}
	}

	// input connector
	if(_sink_type_match(handle, mod->sink_type))
	{
		const float cx = mod->pos.x - scrolling.x - dim.x/2 - 2*cw;
		const float cy = mod->pos.y - scrolling.y;
		const struct nk_rect outer = nk_rect(
			cx - cw, cy - cw,
			4*cw, 4*cw
		);

		// only allow connecting via grab handle on self
		const bool is_hovering_body = nk_input_is_mouse_hovering_rect(in, bounds)
			&& (!handle->linking.active || (mod != handle->linking.source_mod));
		const bool is_hovering_handle = nk_input_is_mouse_hovering_rect(in, outer);
		nk_fill_arc(canvas, cx, cy, cw, 0.f, 2*NK_PI,
			is_hilighted ? hilight_color : grab_handle_color);
		if(  (is_hovering_handle || is_hovering_body)
			&& handle->linking.active)
		{
			nk_stroke_arc(canvas, cx, cy, 2*cw, 0.f, 2*NK_PI, 1.f, hilight_color);
		}

		if(  nk_input_is_mouse_released(in, NK_BUTTON_LEFT)
			&& (is_hovering_handle || is_hovering_body)
			&& handle->linking.active)
		{
			handle->linking.active = nk_false;

			mod_t *src = handle->linking.source_mod;
			if(src)
			{
				mod_conn_t *mod_conn = _mod_conn_find(handle, src, mod);
				if(!mod_conn) // does not yet exist
					mod_conn = _mod_conn_add(handle, src, mod);
				if(mod_conn)
				{
					mod_conn->source_type |= handle->type;
					mod_conn->sink_type |= handle->type;

					if(nk_input_is_key_down(in, NK_KEY_CTRL)) // automatic connection
					{
						unsigned i = 0;
						HASH_FOREACH(&src->sources, source_port_itr)
						{
							port_t *source_port = *source_port_itr;

							if(!_source_type_match(handle, source_port->type))
								continue;

							unsigned j = 0;
							HASH_FOREACH(&mod->sinks, sink_port_itr)
							{
								port_t *sink_port = *sink_port_itr;

								if(!_sink_type_match(handle, sink_port->type))
									continue;

								if(i == j)
								{
									_patch_connection_add(handle, source_port, sink_port);
								}

								j++;
							}

							i++;
						}
					}
				}
			}
		}
	}
}

static inline unsigned
_mod_num_sources(mod_t *mod, property_type_t type)
{
	if(mod->source_type & type)
	{
		unsigned num = 0;

		HASH_FOREACH(&mod->sources, port_itr)
		{
			port_t *port = *port_itr;

			if(port->type & type)
				num += 1;
		}

		return num;
	}

	return 0;
}

static inline unsigned
_mod_num_sinks(mod_t *mod, property_type_t type)
{
	if(mod->sink_type & type)
	{
		unsigned num = 0;

		HASH_FOREACH(&mod->sinks, port_itr)
		{
			port_t *port = *port_itr;

			if(port->type & type)
				num += 1;
		}

		return num;
	}

	return 0;
}

static void
_set_module_selector(plughandle_t *handle, mod_t *mod)
{
	if(handle->module_selector)
		_mod_unsubscribe_all(handle, handle->module_selector);

	if(mod)
	{
		_mod_subscribe_all(handle, mod);

		_patch_notification_add_patch_get(handle, mod,
			handle->regs.port.event_transfer.urid, 0, 0, 0); // patch:Get []
	}

	handle->module_selector = mod;
	handle->port_selector = NULL;
	handle->param_selector = NULL;
	handle->preset_find_matches = true;
	handle->prop_find_matches = true;
}

static void
_expose_mod(plughandle_t *handle, struct nk_context *ctx, mod_t *mod, float dy)
{
	if(!_source_type_match(handle, mod->source_type) && !_sink_type_match(handle, mod->sink_type))
		return;

	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_input *in = &ctx->input;

	const LilvPlugin *plug = mod->plug;
	if(!plug)
		return;

	LilvNode *name_node = lilv_plugin_get_name(plug);
	if(!name_node)
		return;

	mod->dim.x = 200.f * handle->scale;
	mod->dim.y = handle->dy;

	const struct nk_vec2 scrolling = handle->scrolling;

	struct nk_rect bounds = nk_rect(
		mod->pos.x - mod->dim.x/2 - scrolling.x,
		mod->pos.y - mod->dim.y/2 - scrolling.y,
		mod->dim.x, mod->dim.y);

	if(_mod_moveable(handle, ctx, mod, &bounds))
	{
		if(nk_input_is_key_down(in, NK_KEY_SHIFT)) //FIXME
		{
			mod_ui_t *mod_ui = _mod_ui_get_first(mod);
			if(mod_ui)
			{
				if(_mod_ui_is_running(mod_ui))
					_mod_ui_stop(mod_ui); // stop existing UI
				else
					_mod_ui_run(mod_ui); // run UI
			}
		}
		else
		{
			_patch_mod_remove(handle, mod);
		}
	}

	const bool is_hovering = nk_input_is_mouse_hovering_rect(in, bounds);
	if(  is_hovering
		&& nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT))
	{
		_set_module_selector(handle, mod);
	}

	mod->hovered = is_hovering;
	const bool is_hilighted = mod->hilighted || is_hovering || mod->moving
		|| (handle->module_selector == mod);

	nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, bounds));

	struct nk_rect body;
	const enum nk_widget_layout_states states = nk_widget(&body, ctx);
	if(states != NK_WIDGET_INVALID)
	{
		struct nk_style_button *style = &ctx->style.button;
		const struct nk_user_font *font = ctx->style.font;

		nk_fill_rect(canvas, body, style->rounding, style->hover.data.color);
		nk_stroke_rect(canvas, body, style->rounding, style->border,
			is_hilighted ? hilight_color : style->border_color);

		const float fh = font->height;
		const float fy = body.y + (body.h - fh)/2;
		{
			const char *mod_name = lilv_node_as_string(name_node);
			const size_t mod_name_len = strlen(mod_name);
			const float fw = font->width(font->userdata, font->height, mod_name, mod_name_len);
			const struct nk_rect body2 = {
				.x = body.x + (body.w - fw)/2,
				.y = fy,
				.w = fw,
				.h = fh
			};
			nk_draw_text(canvas, body2, mod_name, mod_name_len, font,
				style->normal.data.color, style->text_normal);
		}

		const unsigned nsources = _mod_num_sources(mod, handle->type);
		const unsigned nsinks = _mod_num_sinks(mod, handle->type);

		if(nsources)
		{
			char nums [32];
			snprintf(nums, 32, "%u", nsources);

			const size_t nums_len = strlen(nums);
			const float fw = font->width(font->userdata, font->height, nums, nums_len);
			const struct nk_rect body2 = {
				.x = body.x + body.w - fw - 4.f,
				.y = fy,
				.w = fw,
				.h = fh
			};
			nk_draw_text(canvas, body2, nums, nums_len, font,
				style->normal.data.color, style->text_normal);
		}

		if(nsinks)
		{
			char nums [32];
			snprintf(nums, 32, "%u", nsinks);

			const size_t nums_len = strlen(nums);
			const float fw = font->width(font->userdata, font->height, nums, nums_len);
			const struct nk_rect body2 = {
				.x = body.x + 4.f,
				.y = fy,
				.w = fw,
				.h = fh
			};
			nk_draw_text(canvas, body2, nums, nums_len, font,
				style->normal.data.color, style->text_normal);
		}

		//FIXME can this be solved more elegantly
		{
			char load [32];
			snprintf(load, 32, "%.1f | %.1f | %.1f %%",
				mod->prof.min, mod->prof.avg, mod->prof.max);

			const size_t load_len= strlen(load);
			const float fw = font->width(font->userdata, font->height, load, load_len);
			const struct nk_rect body2 = {
				.x = body.x + (body.w - fw)/2,
				.y = fy + 1.5*fh,
				.w = fw,
				.h = fh
			};
			nk_draw_text(canvas, body2, load, load_len, font,
				style->normal.data.color, style->text_normal);
		}
	}

	_mod_connectors(handle, ctx, mod, nk_vec2(bounds.w, bounds.h), is_hilighted);
}

static void
_expose_mod_conn(plughandle_t *handle, struct nk_context *ctx, mod_conn_t *mod_conn, float dy)
{
	if(!_source_type_match(handle, mod_conn->source_type) || !_sink_type_match(handle, mod_conn->sink_type))
		return;

	struct nk_input *in = &ctx->input;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_vec2 scrolling = handle->scrolling;

	mod_t *src = mod_conn->source_mod;
	mod_t *snk = mod_conn->sink_mod;

	if(!src || !snk)
		return;

	const unsigned nx = _mod_num_sources(mod_conn->source_mod, handle->type);
	const unsigned ny = _mod_num_sinks(mod_conn->sink_mod, handle->type);

	const float ps = 16.f * handle->scale;
	const float pw = nx * ps;
	const float ph = ny * ps;
	struct nk_rect bounds = nk_rect(
		mod_conn->pos.x - scrolling.x - pw/2,
		mod_conn->pos.y - scrolling.y - ph/2,
		pw, ph
	);

	const int is_hovering = nk_input_is_mouse_hovering_rect(in, bounds);

	if(mod_conn->moving)
	{
		if(nk_input_is_mouse_released(in, NK_BUTTON_LEFT))
		{
			mod_conn->moving = false;
		}
		else
		{
			mod_conn->pos.x += in->mouse.delta.x;
			mod_conn->pos.y += in->mouse.delta.y;
			bounds.x += in->mouse.delta.x;
			bounds.y += in->mouse.delta.y;
		}
	}
	else if(is_hovering
		&& nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT)
		&& nk_input_is_key_down(in, NK_KEY_CTRL) )
	{
		mod_conn->moving = true;
	}
	else if(is_hovering
		&& nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT) )
	{
		// consume mouse event
		in->mouse.buttons[NK_BUTTON_RIGHT].down = nk_false;
		in->mouse.buttons[NK_BUTTON_RIGHT].clicked = nk_false;

		unsigned count = 0;
		HASH_FOREACH(&mod_conn->conns, port_conn_itr)
		{
			port_conn_t *port_conn = *port_conn_itr;

			if( (port_conn->source_port->type & handle->type) && (port_conn->sink_port->type & handle->type) )
			{
				_patch_connection_remove(handle, port_conn->source_port, port_conn->sink_port);
				count += 1;
			}
		}

		if(count == 0) // is empty matrix, demask for current type
		{
			mod_conn->source_type &= ~(handle->type);
			mod_conn->sink_type &= ~(handle->type);
		}
	}

	const bool is_hilighted = mod_conn->source_mod->hovered
		|| mod_conn->sink_mod->hovered
		|| is_hovering || mod_conn->moving;

	if(is_hilighted)
	{
		mod_conn->source_mod->hilighted = true;
		mod_conn->sink_mod->hilighted = true;
	}

	const float cs = 4.f * handle->scale;

	{
		const float cx = mod_conn->pos.x - scrolling.x;
		const float cxr = cx + pw/2;
		const float cy = mod_conn->pos.y - scrolling.y;
		const float cyl = cy - ph/2;
		const struct nk_color col = is_hilighted ? hilight_color : grab_handle_color;

		const float l0x = src->pos.x - scrolling.x + src->dim.x/2 + cs*2;
		const float l0y = src->pos.y - scrolling.y;
		const float l1x = snk->pos.x - scrolling.x - snk->dim.x/2 - cs*2;
		const float l1y = snk->pos.y - scrolling.y;

		const float bend = 50.f * handle->scale;
		nk_stroke_curve(canvas,
			l0x, l0y,
			l0x + bend, l0y,
			cx, cyl - bend,
			cx, cyl,
			1.f, col);
		nk_stroke_curve(canvas,
			cxr, cy,
			cxr + bend, cy,
			l1x - bend, l1y,
			l1x, l1y,
			1.f, col);

		nk_fill_arc(canvas, cx, cyl, cs, 2*M_PI/2, 4*M_PI/2, col);
		nk_fill_arc(canvas, cxr, cy, cs, 3*M_PI/2, 5*M_PI/2, col);
	}

	nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, bounds));

	struct nk_rect body;
	const enum nk_widget_layout_states states = nk_widget(&body, ctx);
	if(states != NK_WIDGET_INVALID)
	{
		struct nk_style_button *style = &ctx->style.button;

		nk_fill_rect(canvas, body, style->rounding, style->normal.data.color);

		for(float x = ps; x < body.w; x += ps)
		{
			nk_stroke_line(canvas,
				body.x + x, body.y,
				body.x + x, body.y + body.h,
				style->border, style->border_color);
		}

		for(float y = ps; y < body.h; y += ps)
		{
			nk_stroke_line(canvas,
				body.x, body.y + y,
				body.x + body.w, body.y + y,
				style->border, style->border_color);
		}

		nk_stroke_rect(canvas, body, style->rounding, style->border,
			is_hilighted ? hilight_color : style->border_color);

		float x = body.x + ps/2;
		HASH_FOREACH(&mod_conn->source_mod->sources, source_port_itr)
		{
			port_t *source_port = *source_port_itr;

			if(!_source_type_match(handle, source_port->type))
				continue;

			float y = body.y + ps/2;
			HASH_FOREACH(&mod_conn->sink_mod->sinks, sink_port_itr)
			{
				port_t *sink_port = *sink_port_itr;

				if(!_sink_type_match(handle, sink_port->type))
					continue;

				port_conn_t *port_conn = _port_conn_find(mod_conn, source_port, sink_port);

				if(port_conn)
					nk_fill_arc(canvas, x, y, cs, 0.f, 2*NK_PI, toggle_color);

				const struct nk_rect tile = nk_rect(x - ps/2, y - ps/2, ps, ps);

				if(  nk_input_is_mouse_hovering_rect(in, tile)
					&& !mod_conn->moving)
				{
					const char *source_name = source_port->name;
					const char *sink_name = sink_port->name;

					if(source_name && sink_name)
					{
						char tmp [128];
						snprintf(tmp, 128, "%s || %s", source_name, sink_name);
						nk_tooltip(ctx, tmp);

						if(nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT))
						{
							if(port_conn)
							{
								_patch_connection_remove(handle, source_port, sink_port);
							}
							else
							{
								_patch_connection_add(handle, source_port, sink_port);
							}
						}
					}
				}

				y += ps;
			}

			x += ps;
		}
	}
}

static inline int
_group_begin(struct nk_context *ctx, const char *title, nk_flags flags, struct nk_rect *bb)
{
	*bb = nk_widget_bounds(ctx);

	return nk_group_begin(ctx, title, flags);
}

static inline void
_group_end(struct nk_context *ctx, struct nk_rect *bb)
{
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	struct nk_style *style = &ctx->style;

	nk_group_end(ctx);
	nk_stroke_rect(canvas, *bb, 0.f, style->window.group_border, style->window.group_border_color);
}

static void
_expose_main_body(plughandle_t *handle, struct nk_context *ctx, float dh, float dy)
{
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	struct nk_style *style = &ctx->style;
	const struct nk_input *in = &ctx->input;

	handle->plugin_find_matches = false;
	handle->preset_find_matches = false;
	handle->prop_find_matches = false;

	const struct nk_rect total_space = nk_window_get_content_region(ctx);
	const float vertical = total_space.h
		- handle->dy
		- 3*style->window.group_padding.y;
	const float upper_h = vertical * 0.6f;
	const float lower_h = vertical * 0.4f
		- handle->dy
		- 2*style->window.group_padding.y;

	nk_layout_space_begin(ctx, NK_STATIC, upper_h,
		_hash_size(&handle->mods) + _hash_size(&handle->conns));
	{
    const struct nk_rect old_clip = canvas->clip;
		const struct nk_rect space_bounds= nk_layout_space_bounds(ctx);
		nk_push_scissor(canvas, space_bounds);

		// graph content scrolling
		if(  nk_input_is_mouse_hovering_rect(in, space_bounds)
			&& nk_input_is_mouse_down(in, NK_BUTTON_MIDDLE))
		{
			handle->scrolling.x -= in->mouse.delta.x;
			handle->scrolling.y -= in->mouse.delta.y;
		}

		const struct nk_vec2 scrolling = handle->scrolling;

		// display grid
		{
			struct nk_rect ssize = nk_layout_space_bounds(ctx);
			ssize.h -= style->window.group_padding.y;
			const float grid_size = 28.0f * handle->scale;

			nk_fill_rect(canvas, ssize, 0.f, grid_background_color);

			for(float x = fmod(ssize.x - scrolling.x, grid_size);
				x < ssize.w;
				x += grid_size)
			{
				nk_stroke_line(canvas, x + ssize.x, ssize.y, x + ssize.x, ssize.y + ssize.h,
					1.0f, grid_line_color);
			}

			for(float y = fmod(ssize.y - scrolling.y, grid_size);
				y < ssize.h;
				y += grid_size)
			{
				nk_stroke_line(canvas, ssize.x, y + ssize.y, ssize.x + ssize.w, y + ssize.y,
					1.0f, grid_line_color);
			}
		}

		HASH_FOREACH(&handle->mods, mod_itr)
		{
			mod_t *mod = *mod_itr;

			_expose_mod(handle, ctx, mod, dy);

			mod->hilighted = false;
		}

		HASH_FOREACH(&handle->conns, mod_conn_itr)
		{
			mod_conn_t *mod_conn = *mod_conn_itr;

			_expose_mod_conn(handle, ctx, mod_conn, dy);
		}

		// reset linking connection
		if(  handle->linking.active
			&& nk_input_is_mouse_released(in, NK_BUTTON_LEFT))
		{
			handle->linking.active = false;
		}

    nk_push_scissor(canvas, old_clip);
	}
	nk_layout_space_end(ctx);

	{
		nk_layout_row_dynamic(ctx, dy, 11);

		const bool is_audio = handle->type == PROPERTY_TYPE_AUDIO;
		const bool is_cv = handle->type == PROPERTY_TYPE_CV;
		const bool is_atom = handle->type == PROPERTY_TYPE_ATOM;

		const bool is_midi = handle->type == PROPERTY_TYPE_MIDI;
		const bool is_osc = handle->type == PROPERTY_TYPE_OSC;
		const bool is_time = handle->type == PROPERTY_TYPE_TIME;
		const bool is_patch = handle->type == PROPERTY_TYPE_PATCH;
		const bool is_xpress = handle->type == PROPERTY_TYPE_XPRESS;

		const bool is_automation = handle->type == PROPERTY_TYPE_AUTOMATION;

		if(is_audio)
			nk_style_push_color(ctx, &style->button.border_color, hilight_color);
		if(nk_button_label(ctx, "Audio"))
			handle->type = PROPERTY_TYPE_AUDIO;
		if(is_audio)
			nk_style_pop_color(ctx);

		if(is_cv)
			nk_style_push_color(ctx, &style->button.border_color, hilight_color);
		if(nk_button_label(ctx, "CV"))
			handle->type = PROPERTY_TYPE_CV;
		if(is_cv)
			nk_style_pop_color(ctx);

		if(is_atom)
			nk_style_push_color(ctx, &style->button.border_color, hilight_color);
		if(nk_button_label(ctx, "Atom"))
			handle->type = PROPERTY_TYPE_ATOM;
		if(is_atom)
			nk_style_pop_color(ctx);

		nk_spacing(ctx, 1);

		if(is_midi)
			nk_style_push_color(ctx, &style->button.border_color, hilight_color);
		if(nk_button_label(ctx, "MIDI"))
			handle->type = PROPERTY_TYPE_MIDI;
		if(is_midi)
			nk_style_pop_color(ctx);

		if(is_osc)
			nk_style_push_color(ctx, &style->button.border_color, hilight_color);
		if(nk_button_label(ctx, "OSC"))
			handle->type = PROPERTY_TYPE_OSC;
		if(is_osc)
			nk_style_pop_color(ctx);

		if(is_time)
			nk_style_push_color(ctx, &style->button.border_color, hilight_color);
		if(nk_button_label(ctx, "Time"))
			handle->type = PROPERTY_TYPE_TIME;
		if(is_time)
			nk_style_pop_color(ctx);

		if(is_patch)
			nk_style_push_color(ctx, &style->button.border_color, hilight_color);
		if(nk_button_label(ctx, "Patch"))
			handle->type = PROPERTY_TYPE_PATCH;
		if(is_patch)
			nk_style_pop_color(ctx);

		if(is_xpress)
			nk_style_push_color(ctx, &style->button.border_color, hilight_color);
		if(nk_button_label(ctx, "XPression"))
			handle->type = PROPERTY_TYPE_XPRESS;
		if(is_xpress)
			nk_style_pop_color(ctx);

		nk_spacing(ctx, 1);

		if(is_automation)
			nk_style_push_color(ctx, &style->button.border_color, hilight_color);
		if(nk_button_label(ctx, "Automation"))
			handle->type = PROPERTY_TYPE_AUTOMATION;
		if(is_automation)
			nk_style_pop_color(ctx);
	}

	{
		struct nk_rect bb;
		const float lower_ratio [4] = {0.2, 0.2, 0.4, 0.2};
		nk_layout_row(ctx, NK_DYNAMIC, lower_h, 4, lower_ratio);

		if(_group_begin(ctx, "Plugins", NK_WINDOW_TITLE, &bb))
		{
			nk_menubar_begin(ctx);
			{
				const float dim [2] = {0.4, 0.6};
				nk_layout_row(ctx, NK_DYNAMIC, dy, 2, dim);
				const selector_search_t old_sel = handle->plugin_search_selector;
				handle->plugin_search_selector = nk_combo(ctx, search_labels, SELECTOR_SEARCH_MAX,
					handle->plugin_search_selector, dy, nk_vec2(nk_widget_width(ctx), 7*dy));
				if(old_sel != handle->plugin_search_selector)
					handle->plugin_find_matches = true;
				const size_t old_len = _textedit_len(&handle->plugin_search_edit);
				const nk_flags args = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT;
				const nk_flags flags = nk_edit_buffer(ctx, args, &handle->plugin_search_edit, nk_filter_default);
				_textedit_zero_terminate(&handle->plugin_search_edit);
				if( (flags & NK_EDIT_COMMITED) || (old_len != _textedit_len(&handle->plugin_search_edit)) )
					handle->plugin_find_matches = true;
				if( (flags & NK_EDIT_ACTIVE) && handle->has_control_a)
					nk_textedit_select_all(&handle->plugin_search_edit);
			}
			nk_menubar_end(ctx);

			nk_layout_row_dynamic(ctx, handle->dy2, 1);
			nk_spacing(ctx, 1); // fixes mouse-over bug
			_expose_main_plugin_list(handle, ctx, handle->plugin_find_matches);

#if 0
			_expose_main_plugin_info(handle, ctx);
#endif

			_group_end(ctx, &bb);
		}

		if(_group_begin(ctx, "Presets", NK_WINDOW_TITLE, &bb))
		{
			nk_menubar_begin(ctx);
			{
				const float dim [2] = {0.4, 0.6};
				nk_layout_row(ctx, NK_DYNAMIC, dy, 2, dim);
				const selector_search_t old_sel = handle->preset_search_selector;
				handle->preset_search_selector = nk_combo(ctx, search_labels, SELECTOR_SEARCH_MAX,
					handle->preset_search_selector, dy, nk_vec2(nk_widget_width(ctx), 7*dy));
				if(old_sel != handle->preset_search_selector)
					handle->preset_find_matches = true;
				const size_t old_len = _textedit_len(&handle->preset_search_edit);
				const nk_flags args = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT;
				const nk_flags flags = nk_edit_buffer(ctx, args, &handle->preset_search_edit, nk_filter_default);
				_textedit_zero_terminate(&handle->preset_search_edit);
				if( (flags & NK_EDIT_COMMITED) || (old_len != _textedit_len(&handle->preset_search_edit)) )
					handle->preset_find_matches = true;
				if( (flags & NK_EDIT_ACTIVE) && handle->has_control_a)
					nk_textedit_select_all(&handle->preset_search_edit);
			}
			nk_menubar_end(ctx);

			_expose_main_preset_list(handle, ctx, handle->preset_find_matches);

#if 0
			_expose_main_preset_info(handle, ctx);
#endif
			_group_end(ctx, &bb);
		}

		if(_group_begin(ctx, "Controls", NK_WINDOW_TITLE, &bb))
		{
			nk_menubar_begin(ctx);
			{
				const float dim [7] = {0.2, 0.3, 0.1, 0.1, 0.1, 0.1, 0.1};
				nk_layout_row(ctx, NK_DYNAMIC, dy, 7, dim);
				const selector_search_t old_sel = handle->port_search_selector;
				handle->port_search_selector = nk_combo(ctx, search_labels, SELECTOR_SEARCH_MAX,
					handle->port_search_selector, dy, nk_vec2(nk_widget_width(ctx), 7*dy));
				if(old_sel != handle->port_search_selector)
					handle->prop_find_matches = true;
				const size_t old_len = _textedit_len(&handle->port_search_edit);
				const nk_flags args = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT;
				const nk_flags flags = nk_edit_buffer(ctx, args, &handle->port_search_edit, nk_filter_default);
				_textedit_zero_terminate(&handle->port_search_edit);
				if( (flags & NK_EDIT_COMMITED) || (old_len != _textedit_len(&handle->port_search_edit)) )
					handle->prop_find_matches = true;
				if( (flags & NK_EDIT_ACTIVE) && handle->has_control_a)
					nk_textedit_select_all(&handle->port_search_edit);

				nk_check_label(ctx, "In", nk_true); //FIXME
				nk_check_label(ctx, "Out", nk_true); //FIXME
				nk_check_label(ctx, "Audio", nk_true); //FIXME
				nk_check_label(ctx, "Ctrl.", nk_true); //FIXME
				nk_check_label(ctx, "Event", nk_true); //FIXME
			}
			nk_menubar_end(ctx);

			mod_t *mod = handle->module_selector;
			if(mod)
			{
				const float DY = dy*2 + 6*style->window.group_padding.y + 2*style->window.group_border;

				_expose_control_list(handle, mod, ctx, DY, dy, handle->prop_find_matches);
			}

			_group_end(ctx, &bb);
		}

		if(_group_begin(ctx, "Automations", NK_WINDOW_TITLE, &bb))
		{
			mod_t *mod = handle->module_selector;
			if(mod)
			{
				port_t *port = handle->port_selector;
				param_t *param = handle->param_selector;

				double c = 0.0;
				double d = 0.0;

				auto_t *automation = NULL;
				if(port && (port->type == PROPERTY_TYPE_CONTROL) )
				{
					automation = &port->control.automation;
					control_port_t *control = &port->control;
					c = control->is_int || control->is_bool
						? control->min.i
						: control->min.f;
					d = control->is_int || control->is_bool
						? control->max.i
						: control->max.f;
				}
				else if(param)
				{
					automation = &param->automation;
					c = _param_union_as_double(&handle->forge, param->range, &param->min);
					d = _param_union_as_double(&handle->forge, param->range, &param->max);
				}

				if(automation)
				{
					auto_t old_auto = *automation;

					nk_menubar_begin(ctx);
					{
						nk_layout_row_dynamic(ctx, dy, 1);

						const auto_type_t auto_type = automation->type;
						automation->type = nk_combo(ctx, auto_labels, AUTO_MAX - 1, //FIXME enable OSC
							automation->type, dy, nk_vec2(nk_widget_width(ctx), dy*5));
						if(auto_type != automation->type)
						{
							if(automation->type == AUTO_MIDI)
							{
								// initialize
								automation->midi.channel = -1;
								automation->midi.controller = -1;
								automation->midi.a = 0x0;
								automation->midi.b = 0x7f;
								automation->c = c;
								automation->d = d;
							}
							else if(automation->type == AUTO_OSC)
							{
								//FIXME initialize
							}
						}
					}
					nk_menubar_end(ctx);

					if(automation->type == AUTO_MIDI)
					{
						nk_layout_row_dynamic(ctx, dy, 1);
						nk_spacing(ctx, 1);

						const double inc = 1.0; //FIXME
						const float ipp = 1.f; //FIXME

						nk_property_int(ctx, "MIDI Channel", -1, &automation->midi.channel, 0xf, 1, ipp);
						nk_property_int(ctx, "MIDI Controller", -1, &automation->midi.controller, 0x7f, 1, ipp);
						nk_property_int(ctx, "MIDI Minimum", 0, &automation->midi.a, 0x7f, 1, ipp);
						nk_property_int(ctx, "MIDI Maximum", 0, &automation->midi.b, 0x7f, 1, ipp);
						nk_spacing(ctx, 1);
						nk_property_double(ctx, "Target Minimum", c, &automation->c, d, inc, ipp);
						nk_property_double(ctx, "Target Maximum", c, &automation->d, d, inc, ipp);
					}
					else if(automation->type == AUTO_OSC)
					{
						//FIXME
					}

					if(memcmp(&old_auto, automation, sizeof(auto_t))) // needs sync
					{
						printf("automation needs sync\n");
						if(automation->type == AUTO_NONE)
						{
							if(port)
								_patch_port_automation_remove(handle, port);
							else if(param)
								_patch_param_automation_remove(handle, param);
						}
						else if(automation->type == AUTO_MIDI)
						{
							if(port)
								_patch_port_midi_automation_add(handle, port, automation);
							else if(param)
								_patch_param_midi_automation_add(handle, param, automation);
						}
						else if(automation->type == AUTO_OSC)
						{
							//FIXME
						}
					}
				}
			}

			_group_end(ctx, &bb);
		}
	}
}

static void
_expose_main_footer(plughandle_t *handle, struct nk_context *ctx, float dy)
{
	nk_layout_row_dynamic(ctx, dy, 2);
	{
		nk_labelf(ctx, NK_TEXT_LEFT, "%.1f | %.1f | %.1f %%",
			handle->prof.min, handle->prof.avg, handle->prof.max);
		nk_label(ctx, "Synthpod: "SYNTHPOD_VERSION, NK_TEXT_RIGHT);
	}
}

static void
_init(plughandle_t *handle)
{
	handle->world = lilv_world_new();
	if(!handle->world)
		return;

	LilvNode *node_false = lilv_new_bool(handle->world, false);
	if(node_false)
	{
		lilv_world_set_option(handle->world, LILV_OPTION_DYN_MANIFEST, node_false);
		lilv_node_free(node_false);
	}
	lilv_world_load_all(handle->world);
	LilvNode *synthpod_bundle = lilv_new_file_uri(handle->world, NULL, SYNTHPOD_BUNDLE_DIR"/");
	if(synthpod_bundle)
	{
		lilv_world_load_bundle(handle->world, synthpod_bundle);
		lilv_node_free(synthpod_bundle);
	}

	handle->node.pg_group = lilv_new_uri(handle->world, LV2_PORT_GROUPS__group);
	handle->node.lv2_integer = lilv_new_uri(handle->world, LV2_CORE__integer);
	handle->node.lv2_toggled = lilv_new_uri(handle->world, LV2_CORE__toggled);
	handle->node.lv2_minimum = lilv_new_uri(handle->world, LV2_CORE__minimum);
	handle->node.lv2_maximum = lilv_new_uri(handle->world, LV2_CORE__maximum);
	handle->node.lv2_default = lilv_new_uri(handle->world, LV2_CORE__default);
	handle->node.pset_Preset = lilv_new_uri(handle->world, LV2_PRESETS__Preset);
	handle->node.pset_bank = lilv_new_uri(handle->world, LV2_PRESETS__bank);
	handle->node.rdfs_comment = lilv_new_uri(handle->world, LILV_NS_RDFS"comment");
	handle->node.rdfs_range = lilv_new_uri(handle->world, LILV_NS_RDFS"range");
	handle->node.doap_name = lilv_new_uri(handle->world, LILV_NS_DOAP"name");
	handle->node.lv2_minorVersion = lilv_new_uri(handle->world, LV2_CORE__minorVersion);
	handle->node.lv2_microVersion = lilv_new_uri(handle->world, LV2_CORE__microVersion);
	handle->node.doap_license = lilv_new_uri(handle->world, LILV_NS_DOAP"license");
	handle->node.rdfs_label = lilv_new_uri(handle->world, LILV_NS_RDFS"label");
	handle->node.lv2_name = lilv_new_uri(handle->world, LV2_CORE__name);
	handle->node.lv2_OutputPort = lilv_new_uri(handle->world, LV2_CORE__OutputPort);
	handle->node.lv2_AudioPort = lilv_new_uri(handle->world, LV2_CORE__AudioPort);
	handle->node.lv2_CVPort = lilv_new_uri(handle->world, LV2_CORE__CVPort);
	handle->node.lv2_ControlPort = lilv_new_uri(handle->world, LV2_CORE__ControlPort);
	handle->node.atom_AtomPort = lilv_new_uri(handle->world, LV2_ATOM__AtomPort);
	handle->node.patch_readable = lilv_new_uri(handle->world, LV2_PATCH__readable);
	handle->node.patch_writable = lilv_new_uri(handle->world, LV2_PATCH__writable);
	handle->node.rdf_type = lilv_new_uri(handle->world, LILV_NS_RDF"type");
	handle->node.lv2_Plugin = lilv_new_uri(handle->world, LV2_CORE__Plugin);

	handle->node.midi_MidiEvent = lilv_new_uri(handle->world, LV2_MIDI__MidiEvent);
	handle->node.osc_Message = lilv_new_uri(handle->world, LV2_OSC__Message);
	handle->node.time_Position = lilv_new_uri(handle->world, LV2_TIME__Position);
	handle->node.patch_Message = lilv_new_uri(handle->world, LV2_PATCH__Message);
	handle->node.xpress_Message = lilv_new_uri(handle->world, XPRESS_PREFIX"Message");

	sp_regs_init(&handle->regs, handle->world, handle->map);

	// patch:Get [patch:property spod:moduleList]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.module_list.urid) )
	{
		_message_write(handle);
	}
}

static void
_deinit(plughandle_t *handle)
{
	if(handle->world)
	{
		sp_regs_deinit(&handle->regs);

		lilv_node_free(handle->node.pg_group);
		lilv_node_free(handle->node.lv2_integer);
		lilv_node_free(handle->node.lv2_toggled);
		lilv_node_free(handle->node.lv2_minimum);
		lilv_node_free(handle->node.lv2_maximum);
		lilv_node_free(handle->node.lv2_default);
		lilv_node_free(handle->node.pset_Preset);
		lilv_node_free(handle->node.pset_bank);
		lilv_node_free(handle->node.rdfs_comment);
		lilv_node_free(handle->node.rdfs_range);
		lilv_node_free(handle->node.doap_name);
		lilv_node_free(handle->node.lv2_minorVersion);
		lilv_node_free(handle->node.lv2_microVersion);
		lilv_node_free(handle->node.doap_license);
		lilv_node_free(handle->node.rdfs_label);
		lilv_node_free(handle->node.lv2_name);
		lilv_node_free(handle->node.lv2_OutputPort);
		lilv_node_free(handle->node.lv2_AudioPort);
		lilv_node_free(handle->node.lv2_CVPort);
		lilv_node_free(handle->node.lv2_ControlPort);
		lilv_node_free(handle->node.atom_AtomPort);
		lilv_node_free(handle->node.patch_readable);
		lilv_node_free(handle->node.patch_writable);
		lilv_node_free(handle->node.rdf_type);
		lilv_node_free(handle->node.lv2_Plugin);

		lilv_world_free(handle->world);
	}
}

static void
_expose(struct nk_context *ctx, struct nk_rect wbounds, void *data)
{
	plughandle_t *handle = data;
	const struct nk_user_font *font = ctx->style.font;
	const struct nk_style *style = &handle->win.ctx.style;

	handle->scale = nk_pugl_get_scale(&handle->win);
	handle->dy = 20.f * handle->scale;
	handle->dy2 = font->height + 2 * style->window.header.label_padding.y;

	handle->has_control_a = nk_pugl_is_shortcut_pressed(&ctx->input, 'a', true);

	if(nk_begin(ctx, "synthpod", wbounds, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_window_set_bounds(ctx, wbounds);

		// reduce group padding
		nk_style_push_vec2(ctx, &ctx->style.window.group_padding, nk_vec2(2.f, 2.f));

		if(handle->first)
		{
			nk_layout_row_dynamic(ctx, wbounds.h, 1);
			nk_label(ctx, "loading ...", NK_TEXT_CENTERED);

			_init(handle);

			nk_pugl_post_redisplay(&handle->win);
			handle->first = false;
		}
		else
		{
			const float w_padding = ctx->style.window.padding.y;
			const float dy = handle->dy;
			const unsigned n_paddings = 4;
			const float dh = nk_window_get_height(ctx)
				- n_paddings*w_padding - (n_paddings - 2)*dy;

			_expose_main_header(handle, ctx, dy);
			_expose_main_body(handle, ctx, dh, dy);
			_expose_main_footer(handle, ctx, dy);
		}

		nk_style_pop_vec2(ctx);
	}
	nk_end(ctx);
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor, const char *plugin_uri,
	const char *bundle_path, LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	void *parent = NULL;
	LV2UI_Resize *host_resize = NULL;
	LV2_Options_Option *opts = NULL;
	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_UI__parent))
			parent = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_UI__resize))
			host_resize = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->unmap = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_OPTIONS__options))
			opts = features[i]->data;
	}

	if(!parent)
	{
		fprintf(stderr,
			"%s: Host does not support ui:parent\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->map)
	{
		fprintf(stderr,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->unmap)
	{
		fprintf(stderr,
			"%s: Host does not support urid:unmap\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	lv2_atom_forge_init(&handle->forge, handle->map);

	handle->atom_eventTransfer = handle->map->map(handle->map->handle, LV2_ATOM__eventTransfer);
	handle->self_urn = handle->map->map(handle->map->handle, plugin_uri);

	const LV2_URID atom_float = handle->map->map(handle->map->handle,
		LV2_ATOM__Float);
	const LV2_URID params_sample_rate = handle->map->map(handle->map->handle,
		LV2_PARAMETERS__sampleRate);
	const LV2_URID ui_update_rate= handle->map->map(handle->map->handle,
		LV2_UI__updateRate);

	handle->sample_rate = 48000.f; // fall-back
	handle->update_rate = 60.f; // fall-back

	for(LV2_Options_Option *opt = opts;
		opt && (opt->key != 0) && (opt->value != NULL);
		opt++)
	{
		if( (opt->key == params_sample_rate) && (opt->type == atom_float) )
			handle->sample_rate = *(float*)opt->value;
		else if( (opt->key == ui_update_rate) && (opt->type == atom_float) )
			handle->update_rate = *(float*)opt->value;
		//TODO handle more options
	}

	handle->controller = controller;
	handle->writer = write_function;

	nk_pugl_config_t *cfg = &handle->win.cfg;
	cfg->width = 1280;
	cfg->height = 720;
	cfg->resizable = true;
	cfg->ignore = false;
	cfg->class = "synthpod";
	cfg->title = "Synthpod";
	cfg->parent = (intptr_t)parent;
	cfg->host_resize = host_resize;
	cfg->data = handle;
	cfg->expose = _expose;

	if(asprintf(&cfg->font.face, "%sCousine-Regular.ttf", bundle_path) == -1)
		cfg->font.face = NULL;
	cfg->font.size = 13;

	*(intptr_t *)widget = nk_pugl_init(&handle->win);
	nk_pugl_show(&handle->win);

	// adjust styling
	struct nk_style *style = &handle->win.ctx.style;
	style->button.border_color = button_border_color;

	style->window.header.label_padding = nk_vec2(4.f, 1.f);
	style->window.header.padding = nk_vec2(4.f, 1.f);
	style->window.header.normal.data.color = head_color;
	style->window.header.hover.data.color = head_color;
	style->window.header.active.data.color = head_color;

	style->selectable.hover.data.color = hilight_color;
	style->selectable.pressed.data.color = hilight_color;
	style->selectable.text_hover = nk_rgb(0, 0, 0);
	style->selectable.text_pressed = nk_rgb(0, 0, 0);
	//TODO more styling changes to come here

	handle->scale = nk_pugl_get_scale(&handle->win);

	handle->scrolling = nk_vec2(0.f, 0.f);

	handle->plugin_collapse_states = NK_MAXIMIZED;
	handle->preset_import_collapse_states = NK_MAXIMIZED;
	handle->preset_export_collapse_states = NK_MINIMIZED;
	handle->plugin_info_collapse_states = NK_MINIMIZED;
	handle->preset_info_collapse_states = NK_MINIMIZED;

	nk_textedit_init_fixed(&handle->plugin_search_edit, handle->plugin_search_buf, SEARCH_BUF_MAX);
	nk_textedit_init_fixed(&handle->preset_search_edit, handle->preset_search_buf, SEARCH_BUF_MAX);
	nk_textedit_init_fixed(&handle->port_search_edit, handle->port_search_buf, SEARCH_BUF_MAX);

	handle->first = true;

	handle->type = PROPERTY_TYPE_AUDIO; //FIXME make configurable

	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle->win.cfg.font.face)
		free(handle->win.cfg.font.face);
	nk_pugl_hide(&handle->win);
	nk_pugl_shutdown(&handle->win);

	_set_module_selector(handle, NULL);

	HASH_FREE(&handle->mods, ptr)
	{
		mod_t *mod = ptr;
		_mod_free(handle, mod);
	}

	HASH_FREE(&handle->conns, ptr)
	{
		mod_conn_t *mod_conn = ptr;
		_mod_conn_free(handle, mod_conn);
	}

	_hash_free(&handle->plugin_matches);
	_hash_free(&handle->preset_matches);
	_hash_free(&handle->port_matches);
	_hash_free(&handle->param_matches);
	_hash_free(&handle->dynam_matches);

	_deinit(handle);

	free(handle);
}

static void
_add_connection(plughandle_t *handle, const LV2_Atom_Object *obj)
{
	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom *src_symbol = NULL;
	const LV2_Atom_URID *snk_module = NULL;
	const LV2_Atom *snk_symbol = NULL;

	lv2_atom_object_get(obj,
		handle->regs.synthpod.source_module.urid, &src_module,
		handle->regs.synthpod.source_symbol.urid, &src_symbol,
		handle->regs.synthpod.sink_module.urid, &snk_module,
		handle->regs.synthpod.sink_symbol.urid, &snk_symbol,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const char *src_sym = src_symbol
		? LV2_ATOM_BODY_CONST(src_symbol) : NULL;
	const LV2_URID snk_urn = snk_module
		? snk_module->body : 0;
	const char *snk_sym = snk_symbol
		? LV2_ATOM_BODY_CONST(snk_symbol) : NULL;

	if(src_urn && src_sym && snk_urn && snk_sym)
	{
		mod_t *src_mod = _mod_find_by_urn(handle, src_urn);
		mod_t *snk_mod = _mod_find_by_urn(handle, snk_urn);

		if(src_mod && snk_mod)
		{
			port_t *src_port = _mod_port_find_by_symbol(src_mod, src_sym);
			port_t *snk_port = _mod_port_find_by_symbol(snk_mod, snk_sym);

			if(src_port && snk_port)
			{
				mod_conn_t *mod_conn = _mod_conn_find(handle, src_mod, snk_mod);
				if(!mod_conn) // does not yet exist
					mod_conn = _mod_conn_add(handle, src_mod, snk_mod);
				if(mod_conn)
					_port_conn_add(mod_conn, src_port, snk_port);
			}
		}
	}
}

static void
_rem_connection(plughandle_t *handle, const LV2_Atom_Object *obj)
{
	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom *src_symbol = NULL;
	const LV2_Atom_URID *snk_module = NULL;
	const LV2_Atom *snk_symbol = NULL;

	lv2_atom_object_get(obj,
		handle->regs.synthpod.source_module.urid, &src_module,
		handle->regs.synthpod.source_symbol.urid, &src_symbol,
		handle->regs.synthpod.sink_module.urid, &snk_module,
		handle->regs.synthpod.sink_symbol.urid, &snk_symbol,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const char *src_sym = src_symbol
		? LV2_ATOM_BODY_CONST(src_symbol) : NULL;
	const LV2_URID snk_urn = snk_module
		? snk_module->body : 0;
	const char *snk_sym = snk_symbol
		? LV2_ATOM_BODY_CONST(snk_symbol) : NULL;

	if(src_urn && src_sym && snk_urn && snk_sym)
	{
		mod_t *src_mod = _mod_find_by_urn(handle, src_urn);
		mod_t *snk_mod = _mod_find_by_urn(handle, snk_urn);

		if(src_mod && snk_mod)
		{
			port_t *src_port = _mod_port_find_by_symbol(src_mod, src_sym);
			port_t *snk_port = _mod_port_find_by_symbol(snk_mod, snk_sym);

			if(src_port && snk_port)
			{
				mod_conn_t *mod_conn = _mod_conn_find(handle, src_mod, snk_mod);
				if(mod_conn)
				{
					port_conn_t *port_conn = _port_conn_find(mod_conn, src_port, snk_port);
					if(port_conn)
						_port_conn_remove(handle, mod_conn, port_conn);
				}
			}
		}
	}
}

static void
_add_automation(plughandle_t *handle, const LV2_Atom_Object *obj)
{
	if(obj->body.otype == handle->regs.midi.Controller.urid)
	{
		const LV2_Atom_URID *src_module = NULL;
		const LV2_Atom *src_symbol = NULL;
		const LV2_Atom_URID *src_property = NULL;
		const LV2_Atom_Int *midi_channel = NULL;
		const LV2_Atom_Int *midi_controller = NULL;
		const LV2_Atom_Double *src_min = NULL;
		const LV2_Atom_Double *src_max = NULL;
		const LV2_Atom_Double *snk_min = NULL;
		const LV2_Atom_Double *snk_max = NULL;

		lv2_atom_object_get(obj,
			handle->regs.synthpod.sink_module.urid, &src_module,
			handle->regs.synthpod.sink_symbol.urid, &src_symbol,
			handle->regs.patch.property.urid, &src_property,
			handle->regs.midi.channel.urid, &midi_channel,
			handle->regs.midi.controller_number.urid, &midi_controller,
			handle->regs.synthpod.source_min.urid, &src_min,
			handle->regs.synthpod.source_max.urid, &src_max,
			handle->regs.synthpod.sink_min.urid, &snk_min,
			handle->regs.synthpod.sink_max.urid, &snk_max,
			0);

		const LV2_URID src_urn = src_module
			? src_module->body : 0;
		const char *src_sym = src_symbol
			? LV2_ATOM_BODY_CONST(src_symbol) : NULL;
		const LV2_URID src_prop = src_property 
			? src_property->body : 0;

		auto_t *automation = NULL;

		if(src_urn && src_sym)
		{
			mod_t *src_mod = _mod_find_by_urn(handle, src_urn);

			if(src_mod)
			{
				port_t *src_port = _mod_port_find_by_symbol(src_mod, src_sym);

				if(src_port && (src_port->type == PROPERTY_TYPE_CONTROL) )
				{
					automation = &src_port->control.automation;

					control_port_t *control = &src_port->control;
				}
			}
		}
		else if(src_urn && src_prop)
		{
			mod_t *src_mod = _mod_find_by_urn(handle, src_urn);

			if(src_mod)
			{
				param_t *src_param = _mod_param_find_by_property(src_mod, src_prop);

				if(src_param)
				{
					automation = &src_param->automation;
				}
			}
		}

		if(automation)
		{
			automation->type = AUTO_MIDI;

			automation->midi.a = src_min ? src_min->body : 0x0;
			automation->midi.b = src_max ? src_max->body : 0x7f;
			automation->c = snk_min ? snk_min->body : 0.0; //FIXME
			automation->d = snk_max ? snk_max->body : 0.0; //FIXME

			midi_auto_t *mauto = &automation->midi;
			mauto->channel = midi_channel ? midi_channel->body : -1;
			mauto->controller = midi_controller ? midi_controller->body : -1;
		}
	}
	//FIXME OSC
}

static void
_add_notification(plughandle_t *handle, const LV2_Atom_Object *obj)
{
	const LV2_URID src_proto = obj->body.otype;
	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom *src_symbol = NULL;
	const LV2_Atom *src_value = NULL;

	lv2_atom_object_get(obj,
		handle->regs.synthpod.sink_module.urid, &src_module,
		handle->regs.synthpod.sink_symbol.urid, &src_symbol,
		handle->regs.rdf.value.urid, &src_value,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const char *src_sym = src_symbol
		? LV2_ATOM_BODY_CONST(src_symbol) : NULL;

	if(src_urn && src_sym && src_value)
	{
		mod_t *src_mod = _mod_find_by_urn(handle, src_urn);

		if(src_mod)
		{
			port_t *src_port = _mod_port_find_by_symbol(src_mod, src_sym);

			if(src_port)
			{
				_mod_nk_write_function(handle, src_mod, src_port, src_proto, src_value, true);
			}
		}
	}
}

static void
_add_mod(plughandle_t *handle, const LV2_Atom_URID *urn)
{
	mod_t *mod = _mod_find_by_urn(handle, urn->body);
	if(!mod)
	{
		_mod_add(handle, urn->body);

		// get information for each of those, FIXME only if not already available
		if(  _message_request(handle)
			&& synthpod_patcher_get(&handle->regs, &handle->forge,
				urn->body, 0, 0) )
		{
			_message_write(handle);
		}
	}
}

static void
_rem_mod(plughandle_t *handle, const LV2_Atom_URID *urn)
{
	mod_t *mod = _mod_find_by_urn(handle, urn->body);
	if(mod)
	{
		if(handle->module_selector == mod)
			_set_module_selector(handle, NULL);

		_mod_remove(handle, mod);
	}
}

static void
port_event(LV2UI_Handle instance, uint32_t port_index, uint32_t size,
	uint32_t format, const void *buffer)
{
	plughandle_t *handle = instance;

	if(port_index == 15) // notify
	{
		if(format == handle->regs.port.event_transfer.urid)
		{
			const LV2_Atom_Object *obj = buffer;

			if(lv2_atom_forge_is_object_type(&handle->forge, obj->atom.type))
			{
				if(obj->body.otype == handle->regs.patch.set.urid)
				{
					const LV2_Atom_URID *subject = NULL;
					const LV2_Atom_URID *property = NULL;
					const LV2_Atom *value = NULL;

					lv2_atom_object_get(obj,
						handle->regs.patch.subject.urid, &subject,
						handle->regs.patch.property.urid, &property,
						handle->regs.patch.value.urid, &value,
						0);

					const LV2_URID subj = subject && (subject->atom.type == handle->forge.URID)
						? subject->body
						: 0;
					const LV2_URID prop = property && (property->atom.type == handle->forge.URID)
						? property->body
						: 0;

					if(prop && value)
					{
						//printf("got patch:Set: %s\n", handle->unmap->unmap(handle->unmap->handle, prop));

						if(  (prop == handle->regs.synthpod.module_list.urid)
							&& (value->type == handle->forge.Tuple) )
						{
							const LV2_Atom_Tuple *tup = (const LV2_Atom_Tuple *)value;

							handle->nxt = nk_vec2(nxt_x0 * handle->scale, nxt_y0 * handle->scale);

							HASH_FREE(&handle->mods, ptr)
							{
								mod_t *mod = ptr;
								_mod_free(handle, mod);
							}

							LV2_ATOM_TUPLE_FOREACH(tup, itm)
							{
								_add_mod(handle, (const LV2_Atom_URID *)itm);
							}

							// patch:Get [patch:property spod:connectionList]
							if(  _message_request(handle)
								&& synthpod_patcher_get(&handle->regs, &handle->forge,
									0, 0, handle->regs.synthpod.connection_list.urid) )
							{
								_message_write(handle);
							}

							// patch:Get [patch:property pset:preset]
							if(  _message_request(handle)
								&& synthpod_patcher_get(&handle->regs, &handle->forge,
									0, 0, handle->regs.pset.preset.urid) )
							{
								_message_write(handle);
							}

							// patch:Get [patch:property spod:automationList]
							if(  _message_request(handle)
								&& synthpod_patcher_get(&handle->regs, &handle->forge,
									0, 0, handle->regs.synthpod.automation_list.urid) )
							{
								_message_write(handle);
							}
						}
						else if( (prop == handle->regs.synthpod.connection_list.urid)
							&& (value->type == handle->forge.Tuple) )
						{
							const LV2_Atom_Tuple *tup = (const LV2_Atom_Tuple *)value;

							HASH_FREE(&handle->conns, ptr)
							{
								mod_conn_t *mod_conn = ptr;
								_mod_conn_free(handle, mod_conn);
							}

							LV2_ATOM_TUPLE_FOREACH(tup, itm)
							{
								_add_connection(handle, (const LV2_Atom_Object *)itm);
							}
						}
						else if( (prop == handle->regs.pset.preset.urid)
							&& (value->type == handle->forge.URID) )
						{
							const LV2_Atom_URID *urid = (const LV2_Atom_URID *)value;

							handle->bundle_urn = urid->body;
						}
						else if( (prop == handle->regs.synthpod.automation_list.urid)
							&& (value->type == handle->forge.Tuple) )
						{
							const LV2_Atom_Tuple *tup = (const LV2_Atom_Tuple *)value;

							LV2_ATOM_TUPLE_FOREACH(tup, itm)
							{
								_add_automation(handle, (const LV2_Atom_Object *)itm);
							}
						}
						else if( (prop == handle->regs.synthpod.dsp_profiling.urid)
							&& (value->type == handle->forge.Vector) )
						{
							const LV2_Atom_Vector *vec = (const LV2_Atom_Vector *)value;
							const float *f32 = LV2_ATOM_CONTENTS_CONST(LV2_Atom_Vector, value);

							handle->prof.min = f32[0];
							handle->prof.avg = f32[1];
							handle->prof.max= f32[2];

							nk_pugl_post_redisplay(&handle->win);
						}
						else if( (prop == handle->regs.synthpod.module_profiling.urid)
							&& (value->type == handle->forge.Vector)
							&& subj )
						{
							const LV2_Atom_Vector *vec = (const LV2_Atom_Vector *)value;
							const float *f32 = LV2_ATOM_CONTENTS_CONST(LV2_Atom_Vector, value);

							mod_t *mod = _mod_find_by_urn(handle, subj);
							if(mod)
							{
								mod->prof.min = f32[0];
								mod->prof.avg = f32[1];
								mod->prof.max= f32[2];

								nk_pugl_post_redisplay(&handle->win);
							}
						}
					}
				}
				else if(obj->body.otype == handle->regs.patch.put.urid)
				{
					const LV2_Atom_URID *subject = NULL;
					const LV2_Atom_Object *body = NULL;

					lv2_atom_object_get(obj,
						handle->regs.patch.subject.urid, &subject,
						handle->regs.patch.body.urid, &body,
						0);

					const LV2_URID subj = subject && (subject->atom.type == handle->forge.URID)
						? subject->body
						: 0;

					if(subj && body)
					{
						//printf("got patch:Put for %u\n", subj);

						const LV2_Atom_URID *plugin = NULL;
						const LV2_Atom_Float *mod_pos_x = NULL;
						const LV2_Atom_Float *mod_pos_y = NULL;

						lv2_atom_object_get(body,
							handle->regs.core.plugin.urid, &plugin,
							handle->regs.synthpod.module_position_x.urid, &mod_pos_x,
							handle->regs.synthpod.module_position_y.urid, &mod_pos_y,
							0); //FIXME query more

						const LV2_URID urid = plugin
							? plugin->body
							: 0;

						const char *uri = urid
							? handle->unmap->unmap(handle->unmap->handle, urid)
							: NULL;

						mod_t *mod = _mod_find_by_subject(handle, subj);
						if(mod && uri)
						{
							if(uri)
							{
								LilvNode *uri_node = lilv_new_uri(handle->world, uri);
								const LilvPlugin *plug = NULL;

								if(uri_node)
								{
									const LilvPlugins *plugs = lilv_world_get_all_plugins(handle->world);
									plug = lilv_plugins_get_by_uri(plugs, uri_node);
									lilv_node_free(uri_node);
								}

								if(plug)
									_mod_init(handle, mod, plug);
							}

							if(mod_pos_x && (mod_pos_x->atom.type == handle->forge.Float) && (mod_pos_x->body != 0.f) )
							{
								mod->pos.x = mod_pos_x->body;
							}
							else if(  _message_request(handle)
								&&  synthpod_patcher_set(&handle->regs, &handle->forge,
									mod->urn, 0, handle->regs.synthpod.module_position_x.urid,
									sizeof(float), handle->forge.Float, &mod->pos.x) )
							{
								_message_write(handle);
							}

							if(mod_pos_y && (mod_pos_y->atom.type == handle->forge.Float) && (mod_pos_y->body != 0.f) )
							{
								mod->pos.y = mod_pos_y->body;
							}
							else if(  _message_request(handle)
								&& synthpod_patcher_set(&handle->regs, &handle->forge,
									mod->urn, 0, handle->regs.synthpod.module_position_y.urid,
									sizeof(float), handle->forge.Float, &mod->pos.y) )
							{
								_message_write(handle);
							}
						}
					}
					//TODO
				}
				else if(obj->body.otype == handle->regs.patch.patch.urid)
				{
					const LV2_Atom_URID *subject = NULL;
					const LV2_Atom_Object *add = NULL;
					const LV2_Atom_Object *rem = NULL;

					lv2_atom_object_get(obj,
						handle->regs.patch.subject.urid, &subject,
						handle->regs.patch.add.urid, &add,
						handle->regs.patch.remove.urid, &rem,
						0);

					const LV2_URID subj = subject && (subject->atom.type == handle->forge.URID)
						? subject->body
						: 0; //FIXME check

					if(  add && (add->atom.type == handle->forge.Object)
						&& rem && (rem->atom.type == handle->forge.Object) )
					{
						LV2_ATOM_OBJECT_FOREACH(rem, prop)
						{
							//printf("got patch:remove for %u\n", subj);

							if(  (prop->key == handle->regs.synthpod.connection_list.urid)
								&& (prop->value.type == handle->forge.Object) )
							{
								_rem_connection(handle, (const LV2_Atom_Object *)&prop->value);
							}
							else if( (prop->key == handle->regs.synthpod.notification_list.urid)
								&& (prop->value.type == handle->forge.Object) )
							{
								//FIXME never reached
							}
							else if( (prop->key == handle->regs.synthpod.module_list.urid)
								&& (prop->value.type == handle->forge.URID) )
							{
								_rem_mod(handle, (const LV2_Atom_URID *)&prop->value);
							}
						}

						LV2_ATOM_OBJECT_FOREACH(add, prop)
						{
							//printf("got patch:remove for %u\n", subj);

							if(  (prop->key == handle->regs.synthpod.connection_list.urid)
								&& (prop->value.type == handle->forge.Object) )
							{
								_add_connection(handle, (const LV2_Atom_Object *)&prop->value);
							}
							else if( (prop->key == handle->regs.synthpod.notification_list.urid)
								&& (prop->value.type == handle->forge.Object) )
							{
								_add_notification(handle, (const LV2_Atom_Object *)&prop->value);
							}
							else if( (prop->key == handle->regs.synthpod.module_list.urid)
								&& (prop->value.type == handle->forge.URID) )
							{
								_add_mod(handle, (const LV2_Atom_URID *)&prop->value);
							}
						}
					}
				}
			}
		}
	}
}

static int
_idle(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	// handle communication with plugin UIs
	HASH_FOREACH(&handle->mods, mod_itr)
	{
		mod_t *mod = *mod_itr;

		HASH_FOREACH(&mod->uis, mod_ui_itr)
		{
			mod_ui_t *mod_ui = *mod_ui_itr;

			if(!_mod_ui_is_running(mod_ui))
				continue;

			bool rolling = true;

			int status;
			const int res = waitpid(mod_ui->pid, &status, WUNTRACED | WNOHANG);
			if(res < 0)
			{
				if(errno == ECHILD) // child not existing
					rolling = false;
			}
			else if(res == mod_ui->pid)
			{
				if(!WIFSTOPPED(status) && !WIFCONTINUED(status)) // child exited/crashed
					rolling = false;
			}

			if(!rolling || sandbox_master_recv(mod_ui->sbox.sb))
			{
				_mod_ui_stop(mod_ui);
			}
		}
	}

	if(nk_pugl_process_events(&handle->win) || handle->done)
		return 1;

	return 0;
}

static const LV2UI_Idle_Interface idle_ext = {
	.idle = _idle
};

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_UI__idleInterface))
		return &idle_ext;

	return NULL;
}

const LV2UI_Descriptor synthpod_common_4_nk = {
	.URI						= SYNTHPOD_COMMON_NK_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= extension_data
};

const LV2UI_Descriptor synthpod_root_4_nk = {
	.URI						= SYNTHPOD_ROOT_NK_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= extension_data
};
