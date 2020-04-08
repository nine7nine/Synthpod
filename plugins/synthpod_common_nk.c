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
#include <synthpod_common.h>

#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/log/log.h"
#include "lv2/lv2plug.in/ns/ext/log/logger.h"
#include "lv2/lv2plug.in/ns/ext/options/options.h"
#include "lv2/lv2plug.in/ns/ext/port-groups/port-groups.h"
#include "lv2/lv2plug.in/ns/ext/presets/presets.h"
#include "lv2/lv2plug.in/ns/ext/patch/patch.h"

#include <osc.lv2/osc.h>
#include <xpress.lv2/xpress.h>

#include <sandbox_master.h>

#include <math.h>
#include <unistd.h> // vfork
#include <sys/wait.h> // waitpid
#include <errno.h> // waitpid
#include <time.h>
#include <signal.h> // kill
#include <inttypes.h> // kill

#define NK_PUGL_API
#include <nk_pugl/nk_pugl.h>

#include <lilv/lilv.h>

#if defined(USE_CAIRO_CANVAS)
#	include <canvas.lv2/canvas.h>
#	include <canvas.lv2/forge.h>
#	include <canvas.lv2/render.h>
#endif

#define SEARCH_BUF_MAX 128
#define ATOM_BUF_MAX 0x100000 // 1M
#define CONTROL 14 //FIXME
#define SPLINE_BEND 25.f
#define ALIAS_MAX 32
#define DEFAULT_PSET_LABEL "DEFAULT"

#ifdef Bool
#	undef Bool // interferes with atom forge
#endif

typedef enum _property_type_t property_type_t;
typedef enum _bundle_selector_search_t bundle_selector_search_t;
typedef enum _plugin_selector_search_t plugin_selector_search_t;
typedef enum _preset_selector_search_t preset_selector_search_t;
typedef enum _property_selector_search_t property_selector_search_t;

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
typedef struct _pset_group_t pset_group_t;
typedef struct _pset_preset_t pset_preset_t;

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

	PROPERTY_TYPE_MAX
};

enum _bundle_selector_search_t {
	BUNDLE_SELECTOR_SEARCH_NAME = 0,

	BUNDLE_SELECTOR_SEARCH_MAX
};

enum _plugin_selector_search_t {
	PLUGIN_SELECTOR_SEARCH_NAME = 0,
	PLUGIN_SELECTOR_SEARCH_COMMENT,
	PLUGIN_SELECTOR_SEARCH_AUTHOR,
	PLUGIN_SELECTOR_SEARCH_CLASS,
	PLUGIN_SELECTOR_SEARCH_PROJECT,

	PLUGIN_SELECTOR_SEARCH_MAX
};

enum _preset_selector_search_t {
	PRESET_SELECTOR_SEARCH_NAME = 0,

	PRESET_SELECTOR_SEARCH_MAX
};

enum _property_selector_search_t {
	PROPERTY_SELECTOR_SEARCH_NAME = 0,

	PROPERTY_SELECTOR_SEARCH_MAX
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
	int a;
	int b;
	int channel;
	int controller;
};

struct _osc_auto_t {
	double a;
	double b;
	char path [128]; //TODO how big?
};

struct _auto_t {
	auto_type_t type;
	int src_enabled;
	int snk_enabled;

	double c;
	double d;

	int learning;

	union {
		midi_auto_t midi;
		osc_auto_t osc;
	};
};

struct _control_port_t {
	hash_t points;
	param_union_t min;
	param_union_t max;
	param_union_t span;
	param_union_t val;
	bool is_int;
	bool is_bool;
	bool is_bitmask; //FIXME
	bool is_logarithmic;
	bool is_readonly;
	auto_t automation;
	char *units_symbol;
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
	bool automation;

	union {
		control_port_t control;
		audio_port_t audio;
	};
};

struct _param_t {
	bool is_readonly;
	bool is_bitmask;
	bool is_logarithmic;
	LV2_URID property;
	LV2_URID range;
	mod_t *mod;

	param_union_t min;
	param_union_t max;
	param_union_t span;
	param_union_t val;

	char *label;
	char *comment;
	char *units_symbol;
	auto_t automation;

	hash_t points;
};

struct _prof_t {
	float min;
	float avg;
	float max;
};

struct _mod_t {
	plughandle_t *handle;

	LV2_URID urn;
	LV2_URID subj;
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
	bool selected;

	hash_t sources;
	hash_t sinks;

	property_type_t source_type;
	property_type_t sink_type;

	prof_t prof;

	struct {
		uint32_t w;
		uint32_t h;
		struct nk_image img;
	} idisp;
	char alias [ALIAS_MAX];

#if defined(USE_CAIRO_CANVAS)
	struct {
		LV2_Inline_Display_Image_Surface image_surface;
		cairo_surface_t *surface;
		cairo_t *ctx;
	} cairo;
#endif

	size_t minimum;
};

struct _port_conn_t {
	port_t *source_port;
	port_t *sink_port;
	float gain;
};

struct _mod_conn_t {
	mod_t *source_mod;
	mod_t *sink_mod;
	property_type_t source_type;
	property_type_t sink_type;
	hash_t conns;
	bool on_hold;

	struct nk_vec2 pos;
	bool moving;
	bool hovering;
	bool selected;
};

struct _mod_ui_t {
	mod_t *mod;
	const LilvUI *ui;
	const char *uri;
	LV2_URID urn;

	pid_t pid;
	struct {
		sandbox_master_driver_t driver;
		sandbox_master_t *sb;
		char *socket_uri;
		char *plugin_bundle_path;
		char *ui_bundle_path;
		char *window_name;
		char *minimum;
		char *sample_rate;
		char *update_rate;
	} sbox;
};

struct _pset_group_t {
	const LilvNode *node;
	hash_t presets;
}; //FIXME use this

struct _pset_preset_t {
	const LilvNode *node;
	bool selected;
	auto_t automation;
}; //FIXME use this

struct _plughandle_t {
	LilvWorld *world;
	LilvNodes *bundles;

	LV2_Atom_Forge forge;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

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
		LilvNode *osc_Event;
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

	bundle_selector_search_t bundle_search_selector;
	plugin_selector_search_t plugin_search_selector;
	preset_selector_search_t preset_search_selector;
	property_selector_search_t property_search_selector;

	hash_t bundle_matches;
	hash_t plugin_matches;
	hash_t preset_matches;
	hash_t port_matches;
	hash_t param_matches;
	hash_t dynam_matches;

	char bundle_search_buf [SEARCH_BUF_MAX];
	char plugin_search_buf [SEARCH_BUF_MAX];
	char preset_search_buf [SEARCH_BUF_MAX];
	char port_search_buf [SEARCH_BUF_MAX];
	char mod_alias_buf [ALIAS_MAX];

	struct nk_text_edit bundle_search_edit;
	struct nk_text_edit plugin_search_edit;
	struct nk_text_edit preset_search_edit;
	struct nk_text_edit port_search_edit;
	struct nk_text_edit mod_alias_edit;

	bool first;
	bool has_initial_focus;

	struct {
		bool flag;
		struct nk_vec2 from;
	} box;

	reg_t regs;
	union {
		LV2_Atom atom;
		uint8_t buf [ATOM_BUF_MAX];
	};

	bool has_control_a;

	struct nk_vec2 scrolling;
	float scale;

	bool bundle_find_matches;
	bool plugin_find_matches;
	bool preset_find_matches;
	bool prop_find_matches;

	struct {
		bool active;
		mod_t *source_mod;
	} linking;

	property_type_t type;
	bool show_automation;

	bool done;

	prof_t prof;
	int32_t cpus_available;
	int32_t cpus_used;
	int32_t period_size;
	int32_t num_periods;
	float sample_rate;
	float update_rate;

	struct {
		struct nk_image atom;
		struct nk_image audio;
		struct nk_image control;
		struct nk_image cv;
		struct nk_image midi;
		struct nk_image osc;
		struct nk_image patch;
		struct nk_image time;
		struct nk_image xpress;
		struct nk_image automaton;

		struct nk_image plus;
		struct nk_image download;
		struct nk_image cancel;

		struct nk_image house;
		struct nk_image layers;
		struct nk_image user;

		struct nk_image settings;
		struct nk_image menu;
	} icon;

	bool show_sidebar;
	bool show_bottombar;

	time_t t0;
	struct nk_rect space_bounds;

#if defined(USE_CAIRO_CANVAS)
	LV2_Canvas canvas;
#endif

	xpress_t xpress;

	unsigned mods_moving;

	bool supports_x11;
	bool supports_gtk2;
	bool supports_gtk3;
	bool supports_qt4;
	bool supports_qt5;
	bool supports_kx;
	bool supports_show;
};

static const char *bundle_search_labels [BUNDLE_SELECTOR_SEARCH_MAX] = {
	[BUNDLE_SELECTOR_SEARCH_NAME] = "Name"
};

static const char *plugin_search_labels [PLUGIN_SELECTOR_SEARCH_MAX] = {
	[PLUGIN_SELECTOR_SEARCH_NAME] = "Name",
	[PLUGIN_SELECTOR_SEARCH_COMMENT] = "Comment",
	[PLUGIN_SELECTOR_SEARCH_AUTHOR] = "Author",
	[PLUGIN_SELECTOR_SEARCH_CLASS] = "Class",
	[PLUGIN_SELECTOR_SEARCH_PROJECT] = "Project"
};

static const char *preset_search_labels [PRESET_SELECTOR_SEARCH_MAX] = {
	[PRESET_SELECTOR_SEARCH_NAME] = "Name"
};

static const char *property_search_labels [PROPERTY_SELECTOR_SEARCH_MAX] = {
	[PROPERTY_SELECTOR_SEARCH_NAME] = "Name"
};

static const struct nk_color grid_line_color = {40, 40, 40, 255};
static const struct nk_color grid_background_color = {30, 30, 30, 255};
static const struct nk_color hilight_color = {0, 200, 200, 255};
static const struct nk_color selection_color = {0, 50, 50, 127};
static const struct nk_color automation_color = {200, 0, 100, 255};
static const struct nk_color button_border_color = {100, 100, 100, 255};
static const struct nk_color grab_handle_color = {100, 100, 100, 255};
static const struct nk_color toggle_color = {150, 150, 150, 255};
static const struct nk_color head_color = {12, 12, 12, 255};
static const struct nk_color group_color = {24, 24, 24, 255};
static const struct nk_color invisible_color = {0, 0, 0, 0};

static const char *auto_labels [] = {
	[AUTO_NONE] = "None",
	[AUTO_MIDI] = "MIDI",
	[AUTO_OSC] = "OSC"
};

#if 0
#	define DBG fprintf(stderr, ":: %s\n", __func__)
#else
#	define DBG
#endif

static int
_log_vprintf(plughandle_t *handle, LV2_URID typ, const char *fmt, va_list args)
{
	DBG;
	return handle->log
		? lv2_log_vprintf(&handle->logger, typ, fmt, args)
		: vfprintf(stderr, fmt, args);
}

static int __attribute__((format(printf, 2, 3)))
_log_error(plughandle_t *handle, const char *fmt, ...)
{
	DBG;
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(handle, handle->logger.Error, fmt, args);
  va_end(args);

	return ret;
}

static int __attribute__((format(printf, 2, 3)))
_log_note(plughandle_t *handle, const char *fmt, ...)
{
	DBG;
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(handle, handle->logger.Note, fmt, args);
  va_end(args);

	return ret;
}

static int __attribute__((format(printf, 2, 3)))
_log_warning(plughandle_t *handle, const char *fmt, ...)
{
	DBG;
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(handle, handle->logger.Warning, fmt, args);
  va_end(args);

	return ret;
}

static int __attribute__((format(printf, 2, 3)))
_log_trace(plughandle_t *handle, const char *fmt, ...)
{
	DBG;
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(handle, handle->logger.Trace, fmt, args);
  va_end(args);

	return ret;
}

static struct nk_image
_image_new(plughandle_t *handle, unsigned w, unsigned h, const void *data)
{
	DBG;
	GLuint tex = 0;

#if 0
	if(h != 256)
	{
		static uint32_t counter = 0;
		char *path;
		if(asprintf(&path, "/tmp/surface_%08u.pnm", counter++) != -1)
		{
			FILE *f = fopen(path, "wb");
			if(f)
			{

				fprintf(f, "P6\n%u %u\n%u\n", w, h, 0xff);

				for(int y = 0; y < h; y++)
				{
					const uint8_t *row = &data[w*sizeof(uint32_t)* y];

					for(int x = 0; x < w; x++)
					{
						fwrite(&row[x*sizeof(uint32_t) + 2], sizeof(uint8_t), 1, f);
						fwrite(&row[x*sizeof(uint32_t) + 1], sizeof(uint8_t), 1, f);
						fwrite(&row[x*sizeof(uint32_t) + 0], sizeof(uint8_t), 1, f);
					}
				}

				fflush(f);
				fclose(f);
			}

			free(path);
		}
	}
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	puglEnterContext(handle->win.view, false);
#pragma GCC diagnostic pop
	{
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if(!handle->win.glGenerateMipmap) // for GL >= 1.4 && < 3.1
			glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data);
		if(handle->win.glGenerateMipmap)
			handle->win.glGenerateMipmap(GL_TEXTURE_2D);
	}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	puglLeaveContext(handle->win.view, false);
#pragma GCC diagnostic pop

	return nk_image_id(tex);
}

static void
_image_free(plughandle_t *handle, struct nk_image *img)
{
	DBG;
	if(img->handle.id)
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
		puglEnterContext(handle->win.view, false);
#pragma GCC diagnostic pop
		{
			glDeleteTextures(1, (const GLuint *)&img->handle.id);
			img->handle.id = 0;
		}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
		puglLeaveContext(handle->win.view, false);
#pragma GCC diagnostic pop
	}
}

static bool
_image_empty(struct nk_image *img)
{
	DBG;
	return (img->handle.id == 0);	
}

static inline bool
_message_request(plughandle_t *handle)
{
	DBG;
	lv2_atom_forge_set_buffer(&handle->forge, handle->buf, ATOM_BUF_MAX);
	return true;
}

static inline void
_message_write(plughandle_t *handle)
{
	DBG;
	handle->writer(handle->controller, CONTROL, lv2_atom_total_size(&handle->atom),
		handle->regs.port.event_transfer.urid, &handle->atom);
}

static size_t
_textedit_len(struct nk_text_edit *edit)
{
	DBG;
	return nk_str_len(&edit->string);
}

static const char *
_textedit_const(struct nk_text_edit *edit)
{
	DBG;
	return nk_str_get_const(&edit->string);
}

static void
_textedit_zero_terminate(struct nk_text_edit *edit)
{
	DBG;
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
	DBG;
	return hash->size == 0;
}

static size_t
_hash_size(hash_t *hash)
{
	DBG;
	return hash->size;
}

static void
_hash_add(hash_t *hash, void *node)
{
	DBG;
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
	DBG;
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
	DBG;
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
	DBG;
	free(hash->nodes);
	hash->nodes = NULL;
	hash->size = 0;
}

static void *
_hash_pop(hash_t *hash)
{
	DBG;
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
	DBG;
	if(hash->size)
		qsort(hash->nodes, hash->size, sizeof(void *), cmp);
}

static void
#if defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
_hash_sort_r(hash_t *hash, int (*cmp)(void *data, const void *a, const void *b),
	void *data)
{
	DBG;
	if(hash->size)
		qsort_r(hash->nodes, hash->size, sizeof(void *), data, cmp);
}
#else
_hash_sort_r(hash_t *hash, int (*cmp)(const void *a, const void *b, void *data),
	void *data)
{
	DBG;
	if(hash->size)
		qsort_r(hash->nodes, hash->size, sizeof(void *), cmp, data);
}
#endif

static int64_t
_node_as_long(const LilvNode *node, int64_t dflt)
{
	DBG;
	if(lilv_node_is_int(node))
		return lilv_node_as_int(node);
	else if(lilv_node_is_float(node))
		return floorf(lilv_node_as_float(node));
	else if(lilv_node_is_bool(node))
		return lilv_node_as_bool(node) ? 1 : 0;
	else
		return dflt;
}

static int32_t
_node_as_int(const LilvNode *node, int32_t dflt)
{
	return _node_as_long(node, dflt);
}

static double
_node_as_double(const LilvNode *node, double dflt)
{
	DBG;
	if(lilv_node_is_int(node))
		return lilv_node_as_int(node);
	else if(lilv_node_is_float(node))
		return lilv_node_as_float(node);
	else if(lilv_node_is_bool(node))
		return lilv_node_as_bool(node) ? 1.0 : 0.0;
	else
		return dflt;
}

static float
_node_as_float(const LilvNode *node, float dflt)
{
	return _node_as_double(node, dflt);
}

static int32_t
_node_as_bool(const LilvNode *node, int32_t dflt)
{
	DBG;
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
_patch_connection_internal(plughandle_t *handle, port_t *source_port, port_t *sink_port, float gain)
{
	DBG;
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

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.param.gain.urid);
	if(ref)
		ref = lv2_atom_forge_float(&handle->forge, gain);

	return ref;
}

static void
_patch_connection_add(plughandle_t *handle, port_t *source_port, port_t *sink_port, float gain)
{
	DBG;
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_add_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.connection_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, 0)
		&& _patch_connection_internal(handle, source_port, sink_port, gain) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}

	nk_pugl_post_redisplay(&handle->win);
}

static void
_patch_connection_remove(plughandle_t *handle, port_t *source_port, port_t *sink_port)
{
	DBG;
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_remove_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.connection_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, 0)
		&& _patch_connection_internal(handle, source_port, sink_port, 0.f) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}

	nk_pugl_post_redisplay(&handle->win);
}

static LV2_Atom_Forge_Ref
_patch_node_internal(plughandle_t *handle, mod_t *source_mod, mod_t *sink_mod,
	float x, float y)
{
	DBG;
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.source_module.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&handle->forge, source_mod->urn);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_module.urid);
	if(ref)
		ref = lv2_atom_forge_urid(&handle->forge, sink_mod->urn);

	if(x != 0.f)
	{
		if(ref)
			ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.node_position_x.urid);
		if(ref)
			ref = lv2_atom_forge_float(&handle->forge, x);
	}

	if(y != 0.f)
	{
		if(ref)
			ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.node_position_y.urid);
		if(ref)
			ref = lv2_atom_forge_float(&handle->forge, y);
	}

	return ref;
}

static void
_patch_node_add(plughandle_t *handle, mod_t *source_mod, mod_t *sink_mod,
	float x, float y)
{
	DBG;
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_add_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.node_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, 0)
		&& _patch_node_internal(handle, source_mod, sink_mod, x, y) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}
}

static void
_patch_node_remove(plughandle_t *handle, mod_t *source_mod, mod_t *sink_mod)
{
	DBG;
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_remove_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.node_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, 0)
		&& _patch_node_internal(handle, source_mod, sink_mod, 0.f, 0.f) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}
}

static LV2_Atom_Forge_Ref
_patch_subscription_internal(plughandle_t *handle, port_t *source_port)
{
	DBG;
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
	DBG;
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
	DBG;
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

static param_t *
_mod_param_find_by_property(mod_t *mod, LV2_URID property)
{
	DBG;
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

static bool
_mod_subscription_is_persistent(plughandle_t *handle, mod_t *mod, port_t *port)
{
	DBG;
	param_t *param = NULL;

	if(port->type & PROPERTY_TYPE_PATCH)
	{
		param = param
			? param
			: _mod_param_find_by_property(mod, handle->canvas.urid.Canvas_graph);

		if(param)
		{
			return true;
		}
	}

	return false;
}

static void
_mod_unsubscribe_all(plughandle_t *handle, mod_t *mod)
{
	DBG;
	HASH_FOREACH(&mod->ports, port_itr)
	{
		port_t *port = *port_itr;

		if(_mod_subscription_is_persistent(handle, mod, port))
		{
			continue; // do not subscribe
		}

		_patch_subscription_remove(handle, port);
	}
}

static void
_mod_subscribe_persistent(plughandle_t *handle, mod_t *mod)
{
	DBG;
	HASH_FOREACH(&mod->ports, port_itr)
	{
		port_t *port = *port_itr;

		if(!_mod_subscription_is_persistent(handle, mod, port))
		{
			continue; // do not subscribe
		}

		_patch_subscription_add(handle, port);
	}
}

static void
_mod_subscribe_all(plughandle_t *handle, mod_t *mod)
{
	DBG;
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
	DBG;
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
	DBG;
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
	DBG;
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
	DBG;
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
	DBG;
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
	DBG;
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
	DBG;
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
	DBG;
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
	DBG;
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

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.source_enabled.urid);
	if(ref)
		ref = lv2_atom_forge_bool(&handle->forge, automation->src_enabled);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_enabled.urid);
	if(ref)
		ref = lv2_atom_forge_bool(&handle->forge, automation->snk_enabled);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.learning.urid);
	if(ref)
		ref = lv2_atom_forge_bool(&handle->forge, automation->learning);

	return ref;
}

static LV2_Atom_Forge_Ref
_patch_osc_automation_internal(plughandle_t *handle, auto_t *automation)
{
	DBG;
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_key(&handle->forge, handle->regs.osc.path.urid);
	if(ref)
		ref = lv2_atom_forge_string(&handle->forge, automation->osc.path, strlen(automation->osc.path));

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.source_min.urid);
	if(ref)
		ref = lv2_atom_forge_double(&handle->forge, automation->osc.a);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.source_max.urid);
	if(ref)
		ref = lv2_atom_forge_double(&handle->forge, automation->osc.b);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_min.urid);
	if(ref)
		ref = lv2_atom_forge_double(&handle->forge, automation->c);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_max.urid);
	if(ref)
		ref = lv2_atom_forge_double(&handle->forge, automation->d);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.source_enabled.urid);
	if(ref)
		ref = lv2_atom_forge_bool(&handle->forge, automation->src_enabled);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.sink_enabled.urid);
	if(ref)
		ref = lv2_atom_forge_bool(&handle->forge, automation->snk_enabled);

	if(ref)
		ref = lv2_atom_forge_key(&handle->forge, handle->regs.synthpod.learning.urid);
	if(ref)
		ref = lv2_atom_forge_bool(&handle->forge, automation->learning);

	return ref;
}

static void
_patch_port_midi_automation_add(plughandle_t *handle, port_t *source_port,
	auto_t *automation)
{
	DBG;
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
	DBG;
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

static void
_patch_port_osc_automation_add(plughandle_t *handle, port_t *source_port,
	auto_t *automation)
{
	DBG;
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_add_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.automation_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, handle->regs.osc.message.urid)
		&& _patch_port_automation_internal(handle, source_port)
		&& _patch_osc_automation_internal(handle, automation) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}
}

static inline double
_param_union_as_double(LV2_Atom_Forge *forge, LV2_URID range, param_union_t *pu)
{
	DBG;
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
	DBG;
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
_patch_param_osc_automation_add(plughandle_t *handle, param_t *source_param,
	auto_t *automation)
{
	DBG;
	LV2_Atom_Forge_Frame frame [3];

	if(  _message_request(handle)
		&& synthpod_patcher_add_object(&handle->regs, &handle->forge, &frame[0],
			0, 0, handle->regs.synthpod.automation_list.urid) //TODO subject
		&& lv2_atom_forge_object(&handle->forge, &frame[2], 0, handle->regs.osc.message.urid)
		&& _patch_param_automation_internal(handle, source_param)
		&& _patch_osc_automation_internal(handle, automation) )
	{
		synthpod_patcher_pop(&handle->forge, frame, 3);
		_message_write(handle);
	}
}

static void
_patch_param_automation_remove(plughandle_t *handle, param_t *source_param)
{
	DBG;
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
	DBG;
	HASH_FOREACH(&handle->conns, mod_conn_itr)
	{
		mod_conn_t *mod_conn = *mod_conn_itr;

		if( (mod_conn->source_mod == source_mod) && (mod_conn->sink_mod == sink_mod) )
			return mod_conn;
	}

	return NULL;
}

static mod_conn_t *
_mod_conn_add(plughandle_t *handle, mod_t *source_mod, mod_t *sink_mod, bool sync)
{
	DBG;
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
		mod_conn->on_hold = false;
		_hash_add(&handle->conns, mod_conn);

		if(sync)
			_patch_node_add(handle, source_mod, sink_mod, mod_conn->pos.x, mod_conn->pos.y);
	}

	return mod_conn;
}

static void
_mod_conn_free(plughandle_t *handle, mod_conn_t *mod_conn)
{
	DBG;
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
	DBG;
	_hash_remove(&handle->conns, mod_conn);
	_mod_conn_free(handle, mod_conn);
}

static void
_mod_conn_refresh_type(mod_conn_t *mod_conn)
{
	DBG;
	mod_conn->source_type = PROPERTY_TYPE_NONE;
	mod_conn->sink_type = PROPERTY_TYPE_NONE;
	mod_conn->on_hold = false;

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
	DBG;
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
	DBG;
	HASH_FOREACH(&mod->ports, port_itr)
	{
		port_t *port = *port_itr;

		if(port->index == index)
			return port;
	}

	return NULL;
}

static param_t *
_mod_dynam_find_by_property(mod_t *mod, LV2_URID property)
{
	DBG;
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
	DBG;
	HASH_FOREACH(&handle->mods, mod_itr)
	{
		mod_t *mod = *mod_itr;

		if(mod->urn == urn)
			return mod;
	}

	return NULL;
}

static bool
_mod_ui_is_running(mod_ui_t *mod_ui)
{
	DBG;
	return (mod_ui->pid != 0) && mod_ui->sbox.sb;
}

static void
_mod_uis_send(mod_t *mod, uint32_t index, uint32_t size, uint32_t format,
	const void *buf)
{
	DBG;
	plughandle_t *handle = mod->handle;

	HASH_FOREACH(&mod->uis, mod_ui_itr)
	{
		mod_ui_t *mod_ui = *mod_ui_itr;

		if(!_mod_ui_is_running(mod_ui))
			continue;

		if(sandbox_master_send(mod_ui->sbox.sb, index, size, format, buf) == -1)
			_log_error(handle, "%s: buffer overflow\n", __func__);
		sandbox_master_signal_tx(mod_ui->sbox.sb);
	}
}

static void
_param_update_span(plughandle_t *handle, param_t *param)
{
	DBG;
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
	else if(param->range == handle->forge.URID)
		param->span.u = UINT32_MAX;
	//FIXME more types
}

static int
strcasenumcmp(const char *s1, const char *s2)
{
	static const char *digits = "1234567890";
	const char *d1 = strpbrk(s1, digits);
	const char *d2 = strpbrk(s2, digits);

	// do both s1 and s2 contain digits?
	if(d1 && d2)
	{
		const size_t l1 = d1 - s1;
		const size_t l2 = d2 - s2;

		// do both s1 and s2 match up to the first digit?
		if( (l1 == l2) && (strncmp(s1, s2, l1) == 0) )
		{
			char *e1 = NULL;
			char *e2 = NULL;

			const int n1 = strtol(d1, &e1, 10);
			const int n2 = strtol(d2, &e2, 10);

			// do both d1 and d2 contain a valid number?
			if(e1 && e2)
			{
				// are the numbers equal? do the same for the substring
				if(n1 == n2)
				{
					return strcasenumcmp(e1, e2);
				}

				// the numbers differ, e.g. return their ordering
				return (n1 < n2) ? -1 : 1;
			}
		}
	}

	// no digits in either s1 or s2, do normal comparison
	return strcasecmp(s1, s2);
}

static int
_sort_scale_point_name(const void *a, const void *b)
{
	DBG;
	const scale_point_t *scale_point_a = *(const scale_point_t **)a;
	const scale_point_t *scale_point_b = *(const scale_point_t **)b;

	const char *name_a = scale_point_a->label;
	const char *name_b = scale_point_b->label;

	const int ret = name_a && name_b
		? strcasenumcmp(name_a, name_b)
		: 0;

	return ret;
}

static char *
_unit_symbol_obj(plughandle_t *handle, LilvNode *units_unit)
{
	DBG;
	char *symbol = NULL;

	LilvNode *units_symbol = lilv_world_get(handle->world, units_unit, handle->regs.units.symbol.node, NULL);
	if(units_symbol)
	{
		if(lilv_node_is_string(units_symbol))
				symbol = strdup(lilv_node_as_string(units_symbol));

		lilv_node_free(units_symbol);
	}

	return symbol;
}

static char *
_unit_symbol(plughandle_t *handle, const char *uri)
{
	DBG;
	char *symbol = NULL;

	LilvNode *units_unit = lilv_new_uri(handle->world, uri);
	if(units_unit)
	{
		symbol = _unit_symbol_obj(handle, units_unit);

		lilv_node_free(units_unit);
	}

	return symbol;
}

static void
_param_fill(plughandle_t *handle, param_t *param, const LilvNode *param_node)
{
	DBG;
	param->property = handle->map->map(handle->map->handle, lilv_node_as_uri(param_node));

	LilvNode *range = lilv_world_get(handle->world, param_node, handle->node.rdfs_range, NULL);
	if(range)
	{
		param->range = handle->map->map(handle->map->handle, lilv_node_as_uri(range));
		if(  (param->range == handle->forge.String)
			|| (param->range == handle->forge.Path)
			|| (param->range == handle->forge.URI)
			|| (param->range == handle->forge.URID) )
		{
			nk_textedit_init_default(&param->val.editor);
		}
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
				param->min.h = _node_as_long(min, 0);
			else if(param->range == handle->forge.Float)
				param->min.f = _node_as_float(min, 0.f);
			else if(param->range == handle->forge.Double)
				param->min.d = _node_as_double(min, 0.0);
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
				param->max.h = _node_as_long(max, 1);
			else if(param->range == handle->forge.Float)
				param->max.f = _node_as_float(max, 1.f);
			else if(param->range == handle->forge.Double)
				param->max.d = _node_as_double(max, 1.0);
			//FIXME
			lilv_node_free(max);
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
		{
			param->units_symbol = _unit_symbol(handle, lilv_node_as_uri(units_unit));
		}
		else if(lilv_world_ask(handle->world, units_unit, handle->regs.rdf.type.node, handle->regs.units.Unit.node))
		{
			param->units_symbol = _unit_symbol_obj(handle, units_unit);
		}

		lilv_node_free(units_unit);
	}

	LilvNodes *param_properties = lilv_world_find_nodes(handle->world, param_node, handle->regs.parameter.property.node, NULL);
	if(param_properties)
	{
		LILV_FOREACH(nodes, i, param_properties)
		{
			const LilvNode *param_property = lilv_nodes_get(param_properties, i);

			if(lilv_node_equals(param_property, handle->regs.port.is_bitmask.node))
				param->is_bitmask = true;
			else if(lilv_node_equals(param_property, handle->regs.port.logarithmic.node))
				param->is_logarithmic = true;
		}

		lilv_nodes_free(param_properties);
	}

	LilvNodes *lv2_scale_points = lilv_world_find_nodes(handle->world, param_node, handle->regs.core.scale_point.node, NULL);
	if(lv2_scale_points)
	{
		LILV_FOREACH(nodes, i, lv2_scale_points)
		{
			const LilvNode *port_point = lilv_nodes_get(lv2_scale_points, i);
			LilvNode *label_node = lilv_world_get(handle->world, port_point, handle->regs.rdfs.label.node, NULL);
			LilvNode *value_node = lilv_world_get(handle->world, port_point, handle->regs.rdf.value.node, NULL);

			if(label_node && value_node)
			{
				scale_point_t *point = calloc(1, sizeof(scale_point_t));
				if(!point)
					continue;

				_hash_add(&param->points, point);

				point->label = strdup(lilv_node_as_string(label_node));

				if(param->range == handle->forge.Int)
				{
					point->val.i = _node_as_int(value_node, 0);
				}
				else if(param->range == handle->forge.Long)
				{
					point->val.h = _node_as_long(value_node, 0);
				}
				else if(param->range == handle->forge.Bool)
				{
					point->val.i = _node_as_bool(value_node, false);
				}
				else if(param->range == handle->forge.Float)
				{
					point->val.f = _node_as_float(value_node, 0.f);
				}
				else if(param->range == handle->forge.Double)
				{
					point->val.d = _node_as_double(value_node, 0.0);
				}
				//FIXME other types

				lilv_node_free(label_node);
				lilv_node_free(value_node);
			}
		}

		_hash_sort(&param->points, _sort_scale_point_name);

		lilv_nodes_free(lv2_scale_points);
	}

	//FIXME units_symbol
}

static param_t *
_param_add(mod_t *mod, hash_t *hash, bool is_readonly)
{
	DBG;
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
	DBG;
	if(  (param->range == handle->forge.String)
		|| (param->range == handle->forge.Path)
		|| (param->range == handle->forge.URI)
		|| (param->range == handle->forge.URID) )
	{
		nk_textedit_free(&param->val.editor);
	}
	else if(param->range == handle->forge.Chunk)
	{
		free(param->val.chunk.body);
	}
	else if(param->range == handle->forge.Tuple)
	{
		if(param->property == handle->canvas.urid.Canvas_graph)
		{
			_image_free(handle, &param->mod->idisp.img);
		}
	}

	HASH_FREE(&param->points, ptr2)
	{
		scale_point_t *point = ptr2;

		if(point->label)
			free(point->label);

		free(point);
	}

	free(param->label);
	free(param->comment);
	free(param->units_symbol);
	free(param);
}

static void
_set_string(struct nk_str *str, uint32_t size, const char *body)
{
	DBG;
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

static inline LV2_Inline_Display_Image_Surface *
_cairo_init(mod_t *mod, int w, int h)
{
	LV2_Inline_Display_Image_Surface *surf = &mod->cairo.image_surface;

	surf->width = w;
	surf->height = h;
	surf->stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, surf->width);
	surf->data = realloc(surf->data, surf->stride * surf->height);
	if(!surf->data)
		return NULL;

	mod->cairo.surface = cairo_image_surface_create_for_data(
		surf->data, CAIRO_FORMAT_ARGB32, surf->width, surf->height, surf->stride);

	if(mod->cairo.surface)
	{
		cairo_surface_set_device_scale(mod->cairo.surface, surf->width, surf->height);

		mod->cairo.ctx = cairo_create(mod->cairo.surface);
		if(mod->cairo.ctx)
		{
			cairo_select_font_face(mod->cairo.ctx, "cairo:monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		}
	}

	return surf;
}

static inline void
_cairo_deinit(mod_t *mod)
{
	LV2_Inline_Display_Image_Surface *surf = &mod->cairo.image_surface;

	if(mod->cairo.ctx)
	{
		cairo_destroy(mod->cairo.ctx);
		mod->cairo.ctx = NULL;
	}

	if(mod->cairo.surface)
	{
		cairo_surface_finish(mod->cairo.surface);
		cairo_surface_destroy(mod->cairo.surface);
		mod->cairo.surface = NULL;
	}

	if(surf->data)
	{
		free(surf->data);
		surf->data = NULL;
	}
}

static void
_render(plughandle_t *handle, mod_t *mod, uint32_t w, uint32_t h,
	const LV2_Atom_Tuple *graph)
{
	LV2_Inline_Display_Image_Surface *surf = &mod->cairo.image_surface;

	float aspect_ratio = 1.f;
	int W;
	int H;

	param_t *param = _mod_param_find_by_property(mod, handle->canvas.urid.Canvas_aspectRatio);
	if(param)
	{
		aspect_ratio = param->val.f;
	}

	if(aspect_ratio < 1.f)
	{
		W = w;
		H = w / aspect_ratio;
	}
	else if(aspect_ratio > 1.f)
	{
		W = w;
		H = w / aspect_ratio;
	}
	else // aspect_ratio == 1.f
	{
		W = w;
		H = h;
	}

	if( (surf->width != W) || (surf->height != H) || !surf->data)
	{
		_cairo_deinit(mod);
		surf = _cairo_init(mod, W, H);
	}

	if(!surf)
		return;

	lv2_canvas_render(&handle->canvas, mod->cairo.ctx, graph);

	// create OpenGl texture from image data
	const void *data = mod->cairo.image_surface.data;
	w = surf->width;
	h = surf->height;

	_image_free(handle, &mod->idisp.img);
	mod->idisp.img = _image_new(handle, w, h, data);
	mod->idisp.w = w;
	mod->idisp.h = h;

	nk_pugl_post_redisplay(&handle->win);
}

static inline bool
_param_matches_type(plughandle_t *handle, param_t *param, LV2_URID type)
{
	if(!param)
		return false;

	if( (param->range + type) == (handle->forge.Int + handle->forge.Bool) )
		return true;

	return (param->range == type);
}

static void
_param_set_value(plughandle_t *handle, mod_t *mod, param_t *param,
	const LV2_Atom *value)
{
	DBG;
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
	else if(param->range == handle->forge.Path)
	{
		struct nk_str *str = &param->val.editor.string;
		_set_string(str, value->size, LV2_ATOM_BODY_CONST(value));
	}
	else if(param->range == handle->forge.URI)
	{
		struct nk_str *str = &param->val.editor.string;
		_set_string(str, value->size, LV2_ATOM_BODY_CONST(value));
	}
	else if(param->range == handle->forge.URID)
	{
		struct nk_str *str = &param->val.editor.string;
		const LV2_URID urid = ((const LV2_Atom_URID *)value)->body;
		const char *uri = handle->unmap->unmap(handle->unmap->handle, urid);
		_set_string(str, strlen(uri) + 1, uri);
	}
	else if(param->range == handle->forge.Chunk)
	{
		param->val.chunk.size = value->size;
		param->val.chunk.body = realloc(param->val.chunk.body, value->size);
		if(param->val.chunk.body)
			memcpy(param->val.chunk.body, LV2_ATOM_BODY_CONST(value), value->size);
	}
	else if(param->range == handle->forge.Tuple)
	{
		if(param->property == handle->canvas.urid.Canvas_graph)
		{
			_render(handle, mod, 256, 256, (const LV2_Atom_Tuple *)value); //FIXME how big?
		}
	}
	else
	{
		_log_warning(handle, "parameter range unsupported: %s\n",
			handle->unmap->unmap(handle->unmap->handle, param->range));
	}
}

static void
_refresh_main_dynam_list(plughandle_t *handle, mod_t *mod)
{
	DBG;
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
	DBG;
	const param_t *param_a = *(const param_t **)a;
	const param_t *param_b = *(const param_t **)b;

	const char *name_a = param_a->label;
	const char *name_b = param_b->label;

	const int ret = name_a && name_b
		? strcasenumcmp(name_a, name_b)
		: 0;

	return ret;
}

static void
_mod_nk_write_function(plughandle_t *handle, mod_t *src_mod, port_t *src_port,
	uint32_t src_proto, const LV2_Atom *src_value, bool route_to_ui)
{
	DBG;
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
					if(param && _param_matches_type(handle, param, value->type))
					{
						_param_set_value(handle, src_mod, param, value);
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
					if(param && _param_matches_type(handle, param, prop->value.type))
					{
						_param_set_value(handle, src_mod, param, &prop->value);
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
									//FIXME
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
									handle->regs.port.event_transfer.urid, src_mod->subj, 0, property);
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
									handle->regs.port.event_transfer.urid, src_mod->subj, 0, property);
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
									if(  (param->range == handle->forge.String)
										|| (param->range == handle->forge.URI)
										|| (param->range == handle->forge.URID) )
									{
										nk_textedit_init_default(&param->val.editor);
									}
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
									if(src_mod == handle->module_selector)
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
									const LV2_URID units_unit = ((const LV2_Atom_URID *)&prop->value)->body;
									param->units_symbol = _unit_symbol(handle,
										handle->unmap->unmap(handle->unmap->handle, units_unit));
								}
								else if( (prop->key == handle->regs.units.symbol.urid)
									&& (prop->value.type == handle->forge.String) )
								{
									free(param->units_symbol);
									param->units_symbol = strdup(LV2_ATOM_BODY_CONST(&prop->value));
								}
								else if( (prop->key == handle->regs.core.scale_point.urid)
									&& (prop->value.type == handle->forge.Tuple) )
								{
									const LV2_Atom_Tuple *tup = (const LV2_Atom_Tuple *)&prop->value;

									LV2_ATOM_TUPLE_FOREACH(tup, atom)
									{
										if(!lv2_atom_forge_is_object_type(&handle->forge, atom->type))
											continue;

										const LV2_Atom_Object *obj = (const LV2_Atom_Object *)atom;
										const LV2_Atom *label = NULL;
										const LV2_Atom *value = NULL;

										lv2_atom_object_get(obj,
											handle->regs.rdfs.label.urid, &label,
											handle->regs.rdf.value.urid, &value,
											NULL);

										if(  !label || (label->type != handle->forge.String)
											|| !value || (value->type != param->range) )
											continue;

										scale_point_t *point = calloc(1, sizeof(scale_point_t));
										if(!point)
											continue;

										_hash_add(&param->points, point);

										point->label = strdup(LV2_ATOM_BODY_CONST(label));

										if(param->range == handle->forge.Bool)
											point->val.i = ((const LV2_Atom_Bool *)value)->body;
										else if(param->range == handle->forge.Int)
											point->val.i = ((const LV2_Atom_Int *)value)->body;
										else if(param->range == handle->forge.Float)
											point->val.f = ((const LV2_Atom_Float *)value)->body;
										else if(param->range == handle->forge.Double)
											point->val.d = ((const LV2_Atom_Double *)value)->body;
										//FIXME support more types
									}

									_hash_sort(&param->points, _sort_scale_point_name);
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

static bool
_mod_ui_write_function(LV2UI_Controller controller, uint32_t index,
	uint32_t size, uint32_t protocol, const void *buffer)
{
	DBG;
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

	return true; // continue handling messages
}

static void
_mod_ui_subscribe_function(LV2UI_Controller controller, uint32_t index,
	uint32_t protocol, bool state)
{
	DBG;
	mod_ui_t *mod_ui = controller;
	mod_t *mod = mod_ui->mod;
	plughandle_t *handle = mod->handle;

	//printf("_mod_ui_subscribe_function: %u %u %i\n", index, protocol, state);

	// route to dsp
	port_t *port = _mod_port_find_by_index(mod, index);
	if(port)
		_patch_subscription_add(handle, port);
}

static mod_ui_t *
_mod_ui_add(plughandle_t *handle, mod_t *mod, const LilvUI *ui)
{
	DBG;
	const LilvNode *ui_node = lilv_ui_get_uri(ui);
	const LilvNode *plugin_bundle_node = lilv_plugin_get_bundle_uri(mod->plug);
	const LilvNode *ui_bundle_node = lilv_ui_get_bundle_uri(ui);

	//lilv_world_load_bundle(handle->world, (LilvNode *)bundle_node);
	lilv_world_load_resource(handle->world, ui_node);

	bool supported = false;
	if(handle->supports_x11 && lilv_ui_is_a(ui, handle->regs.ui.x11.node))
		supported = true;
	else if(handle->supports_gtk2 && lilv_ui_is_a(ui, handle->regs.ui.gtk2.node))
		supported = true;
	else if(handle->supports_gtk3 && lilv_ui_is_a(ui, handle->regs.ui.gtk3.node))
		supported = true;
	else if(handle->supports_qt4 && lilv_ui_is_a(ui, handle->regs.ui.qt4.node))
		supported = true;
	else if(handle->supports_qt5 && lilv_ui_is_a(ui, handle->regs.ui.qt5.node))
		supported = true;
	else if(handle->supports_kx && lilv_ui_is_a(ui, handle->regs.ui.kx_widget.node))
		supported = true;
	else if(handle->supports_show && lilv_world_ask(handle->world, ui_node, handle->regs.core.extension_data.node, handle->regs.ui.show_interface.node))
		supported = true;

	if(!supported)
	{
		lilv_world_unload_resource(handle->world, ui_node);
		//lilv_world_unload_bundle(handle->world, (LilvNode *)bundle_node);

		return NULL;
	}

	mod_ui_t *mod_ui = calloc(1, sizeof(mod_ui_t));
	if(mod_ui)
	{
		mod_ui->mod = mod;
		mod_ui->ui = ui;
		mod_ui->uri = lilv_node_as_uri(ui_node);
		mod_ui->urn = handle->map->map(handle->map->handle, mod_ui->uri);

		const char *plugin_bundle_uri = plugin_bundle_node
			? lilv_node_as_uri(plugin_bundle_node)
			: NULL;
		mod_ui->sbox.plugin_bundle_path = lilv_file_uri_parse(plugin_bundle_uri, NULL);

		const char *ui_bundle_uri = ui_bundle_node
			? lilv_node_as_uri(ui_bundle_node)
			: NULL;
		mod_ui->sbox.ui_bundle_path = lilv_file_uri_parse(ui_bundle_uri, NULL);

		if(asprintf(&mod_ui->sbox.socket_uri, "shm:///synthpod-sandbox-%016"PRIx64, (uint64_t)mod_ui) == -1)
			mod_ui->sbox.socket_uri = NULL;

		if(asprintf(&mod_ui->sbox.window_name, "%s", mod_ui->uri) == -1)
			mod_ui->sbox.window_name = NULL;

		if(asprintf(&mod_ui->sbox.minimum, "%zu", mod->minimum) == -1)
			mod_ui->sbox.minimum= NULL;

		if(asprintf(&mod_ui->sbox.sample_rate, "%f", handle->sample_rate) == -1)
			mod_ui->sbox.sample_rate = NULL;

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

static bool
_check_support_for_ui(const char *exec_uri)
{
	char *cmd = NULL;
	if(asprintf(&cmd, "%s -v &>/dev/null", exec_uri) == -1)
	{
		return false;
	}

	const int res = system(cmd);
	free(cmd);

	return (res == 0);
}

static void
_mod_ui_run(mod_ui_t *mod_ui, bool sync)
{
	DBG;
	const LilvUI *ui = mod_ui->ui;
	const LilvNode *ui_node = lilv_ui_get_uri(ui);
	mod_t *mod = mod_ui->mod;
	plughandle_t *handle = mod->handle;

	const LilvNode *plugin_node = lilv_plugin_get_uri(mod->plug);
	const char *plugin_uri = plugin_node ? lilv_node_as_uri(plugin_node) : NULL;
	const char *plugin_urn = handle->unmap->unmap(handle->unmap->handle, mod_ui->mod->urn);

	const char *exec_uri = NULL;
	if(lilv_ui_is_a(ui, handle->regs.ui.x11.node))
		exec_uri = "synthpod_sandbox_x11";
	else if(lilv_ui_is_a(ui, handle->regs.ui.gtk2.node))
		exec_uri = "synthpod_sandbox_gtk2";
	else if(lilv_ui_is_a(ui, handle->regs.ui.gtk3.node))
		exec_uri = "synthpod_sandbox_gtk3";
	else if(lilv_ui_is_a(ui, handle->regs.ui.qt4.node))
		exec_uri = "synthpod_sandbox_qt4";
	else if(lilv_ui_is_a(ui, handle->regs.ui.qt5.node))
		exec_uri = "synthpod_sandbox_qt5";
	else if(lilv_ui_is_a(ui, handle->regs.ui.kx_widget.node))
		exec_uri = "synthpod_sandbox_kx";
	else if(lilv_world_ask(handle->world, ui_node, handle->regs.core.extension_data.node, handle->regs.ui.show_interface.node))
		exec_uri = "synthpod_sandbox_show";

	mod_ui->sbox.sb = sandbox_master_new(&mod_ui->sbox.driver, mod_ui, mod->minimum);

	//printf("exec_uri: %s\n", exec_uri);

	if(exec_uri && plugin_uri && plugin_urn && mod_ui->sbox.plugin_bundle_path
		&& mod_ui->sbox.ui_bundle_path && mod_ui->uri
		&& mod_ui->sbox.socket_uri && mod_ui->sbox.window_name && mod_ui->sbox.minimum
		&& mod_ui->sbox.sample_rate && mod_ui->sbox.update_rate && mod_ui->sbox.sb)
	{
		const pid_t pid = vfork();
		if(pid == 0) // child
		{
			char *const args [] = {
#if 0
				"gdb", "--args",
#endif
				(char *)exec_uri,
				"-n", (char *)plugin_urn,
				"-p", (char *)plugin_uri,
				"-P", mod_ui->sbox.plugin_bundle_path,
				"-u", (char *)mod_ui->uri,
				"-U", mod_ui->sbox.ui_bundle_path,
				"-s", mod_ui->sbox.socket_uri,
				"-w", mod_ui->sbox.window_name,
				"-m", mod_ui->sbox.minimum,
				"-r", mod_ui->sbox.sample_rate,
				"-f", mod_ui->sbox.update_rate,
				NULL
			};

#if 0
			fprintf(stderr, "%s \\\n%s %s \\\n%s %s \\\n%s %s \\\n%s %s \\\n%s %s \\\n%s %s \\\n%s %s \\\n%s %s \\\n%s %s \\\n%s %s\n",
				(char *)exec_uri,
				"-n", (char *)plugin_urn,
				"-p", (char *)plugin_uri,
				"-P", mod_ui->sbox.plugin_bundle_path,
				"-u", (char *)mod_ui->uri,
				"-U", mod_ui->sbox.ui_bundle_path,
				"-s", mod_ui->sbox.socket_uri,
				"-w", mod_ui->sbox.window_name,
				"-m", mod_ui->sbox.minimum,
				"-r", mod_ui->sbox.sample_rate,
				"-f", mod_ui->sbox.update_rate);
#endif

			execvp(args[0], args);
		}

		// parent
		mod_ui->pid = pid;

		bool connected = false;

		for(unsigned i = 0; i < 10; i++)
		{
			if(sandbox_master_connected_get(mod_ui->sbox.sb))
			{
				connected = true;
				break;
			}

			// wait for connection
			_log_note(handle, "waiting for UI IPC\n");
			usleep(100000);
		}

		if(!connected)
		{
			_log_error(handle, "UI IPC was not up after 1s\n");
		}

		_mod_subscribe_all(handle, mod);

		_patch_notification_add_patch_get(handle, mod,
			handle->regs.port.event_transfer.urid, mod->subj, 0, 0); // patch:Get []

		if(sync)
		{
			if(  _message_request(handle)
				&& synthpod_patcher_set(&handle->regs, &handle->forge,
					mod->urn, 0, handle->regs.ui.ui.urid,
					sizeof(uint32_t), handle->forge.URID, &mod_ui->urn) )
			{
				_message_write(handle);
			}
		}
	}
}

static void
_mod_ui_stop(mod_ui_t *mod_ui, bool sync)
{
	DBG;
	mod_t *mod = mod_ui->mod;
	plughandle_t *handle = mod->handle;

	if(mod_ui->pid)
	{
		int status;

		kill(mod_ui->pid, SIGINT);
		waitpid(mod_ui->pid, &status, WUNTRACED); // blocking waitpid
		mod_ui->pid = 0;
	}

	if(mod_ui->sbox.sb)
	{
		_mod_unsubscribe_all(handle, mod);
		sandbox_master_free(mod_ui->sbox.sb);
		mod_ui->sbox.sb = NULL;
	}

	if(sync)
	{
		const uint32_t dummy = 0;
		if(  _message_request(handle)
			&& synthpod_patcher_set(&handle->regs, &handle->forge, //FIXME or patcher_del ?
				mod->urn, 0, handle->regs.ui.ui.urid,
				sizeof(uint32_t), handle->forge.URID, &dummy) )
		{
			_message_write(handle);
		}
	}
}

static void
_mod_ui_free(mod_ui_t *mod_ui)
{
	DBG;
	mod_t *mod = mod_ui->mod;
	plughandle_t *handle = mod->handle;

	const LilvNode *ui_node = lilv_ui_get_uri(mod_ui->ui);
	const LilvNode *bundle_node = lilv_plugin_get_bundle_uri(mod->plug);

	if(_mod_ui_is_running(mod_ui))
		_mod_ui_stop(mod_ui, false);

	lilv_world_unload_resource(handle->world, ui_node);
	//lilv_world_unload_bundle(handle->world, (LilvNode *)bundle_node);

	lilv_free(mod_ui->sbox.plugin_bundle_path);
	lilv_free(mod_ui->sbox.ui_bundle_path);
	free(mod_ui->sbox.socket_uri);
	free(mod_ui->sbox.window_name);
	free(mod_ui->sbox.minimum);
	free(mod_ui->sbox.update_rate);
	free(mod_ui->sbox.sample_rate);
	free(mod_ui);
}

static port_conn_t *
_port_conn_find(mod_conn_t *mod_conn, port_t *source_port, port_t *sink_port)
{
	DBG;
	HASH_FOREACH(&mod_conn->conns, port_conn_itr)
	{
		port_conn_t *port_conn = *port_conn_itr;

		if( (port_conn->source_port == source_port) && (port_conn->sink_port == sink_port) )
			return port_conn;
	}

	return NULL;
}

static port_conn_t *
_port_conn_add(mod_conn_t *mod_conn, port_t *source_port, port_t *sink_port, float gain)
{
	DBG;
	port_conn_t *port_conn = calloc(1, sizeof(port_conn_t));
	if(port_conn)
	{
		port_conn->source_port = source_port;
		port_conn->sink_port = sink_port;
		port_conn->gain = gain;

		mod_conn->source_type |= source_port->type;
		mod_conn->sink_type |= sink_port->type;
		mod_conn->on_hold = false;
		_hash_add(&mod_conn->conns, port_conn);
	}

	return port_conn;
}

static void
_port_conn_free(port_conn_t *port_conn)
{
	DBG;
	free(port_conn);
}

static void
_port_conn_remove(plughandle_t *handle, mod_conn_t *mod_conn, port_conn_t *port_conn)
{
	DBG;
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
	DBG;
	const float d = 6.f + 20.f * log10f(fabsf(peak) / 2.f);
	const float e = (d + 64.f) / 70.f;
	return NK_CLAMP(0.f, e, 1.f);
}

#if 0
static inline float
dBFS(float peak)
{
	DBG;
	const float d = 20.f * log10f(fabsf(peak));
	const float e = (d + 70.f) / 70.f;
	return NK_CLAMP(0.f, e, 1.f);
}
#endif

static int
#if defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
_sort_rdfs_label(void *data, const void *a, const void *b)
#else
_sort_rdfs_label(const void *a, const void *b, void *data)
#endif
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
	else
		name_a = DEFAULT_PSET_LABEL; // for default pset

	if(node_b)
		name_b = lilv_node_as_string(node_b);
	else
		name_b = DEFAULT_PSET_LABEL; // for default pset

	const int ret = name_a && name_b
		? strcasenumcmp(name_a, name_b)
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
	DBG;
	const port_t *port_a = *(const port_t **)a;
	const port_t *port_b = *(const port_t **)b;

	const char *name_a = port_a->name;
	const char *name_b = port_b->name;

	const int ret = name_a && name_b
		? strcasenumcmp(name_a, name_b)
		: 0;

	return ret;
}

static void
_patch_mod_add(plughandle_t *handle, const LilvPlugin *plug)
{
	DBG;
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
	DBG;
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
	DBG;
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

static void
_patch_mod_reinstantiate_set(plughandle_t *handle, mod_t *mod, int32_t state)
{
	DBG;

	if(  _message_request(handle)
		&&  synthpod_patcher_set(&handle->regs, &handle->forge,
			mod->urn, 0, handle->regs.synthpod.module_reinstantiate.urid,
			sizeof(int32_t), handle->forge.Bool, &state) )
	{
		_message_write(handle);
	}
}

static void
_patch_mod_preset_save(plughandle_t *handle)
{
	DBG;
	mod_t *mod = handle->module_selector;
	if(!mod)
		return;

	LilvNode *name_node = lilv_plugin_get_name(mod->plug);
	if(!name_node)
	{
		_log_error(handle, "%s: lilv_plugin_get_name failed\n", __func__);
		return;
	}

	const char *home = getenv("HOME");
	if(!home)
	{
		_log_error(handle, "%s: failed to get HOME from environment\n", __func__);
		return;
	}

	const char *mod_label = lilv_node_as_string(name_node);
	const char *preset_label = _textedit_const(&handle->preset_search_edit);

	// create target URI
	char *preset_path;
	if(asprintf(&preset_path, "file://%s/.lv2/%s_%s.preset.lv2", home, mod_label, preset_label) == -1)
		preset_path = NULL;

	if(preset_path)
	{
		// replace white space with underline
		const char *whitespace = " \t\r\n";
		for(char *c = strpbrk(preset_path, whitespace); c; c = strpbrk(c, whitespace))
			*c = '_';

		const LV2_URID preset_urid = handle->map->map(handle->map->handle, preset_path);

		if(  _message_request(handle)
			&&  synthpod_patcher_copy(&handle->regs, &handle->forge,
				mod->urn, 0, preset_urid) )
		{
			_message_write(handle);
		}

		free(preset_path);
	}

	lilv_node_free(name_node);
}

static mod_t *
_mod_find_by_subject(plughandle_t *handle, LV2_URID subj)
{
	DBG;
	HASH_FOREACH(&handle->mods, itr)
	{
		mod_t *mod = *itr;

		if(mod->urn == subj)
			return mod;
	}

	return NULL;
}

static void
_mod_add(plughandle_t *handle, LV2_URID urn)
{
	DBG;
	const struct nk_vec2 scrolling = handle->scrolling;
	const float dx = 200.f; //FIXME
	const float dy = 50.f; //FIXME

	const float x0 = handle->space_bounds.x + dx + scrolling.x;
	const float y0 = handle->space_bounds.y + dy + scrolling.y;
	const float y1 = handle->space_bounds.y + handle->space_bounds.h + scrolling.y;
	float cx = x0;
	float cy = y0;

	while(true)
	{
		bool stable = true;

		HASH_FOREACH(&handle->mods, mod_itr)
		{
			mod_t *mod = *mod_itr;

			if(  (cy > mod->pos.y - dy) && (cy < mod->pos.y + dy)
				&& (cx > mod->pos.x - dx) && (cx < mod->pos.x + dx) )
			{
				cy += dy;

				if(cy > y1)
				{
					cy = y0;
					cx += dx;
				}

				stable = false;
			}

		}

		HASH_FOREACH(&handle->conns, mod_conn_itr)
		{
			mod_conn_t *mod_conn = *mod_conn_itr;

			if(  (cy > mod_conn->pos.y - dy) && (cy < mod_conn->pos.y + dy)
				&& (cx > mod_conn->pos.x - dx) && (cx < mod_conn->pos.x + dx) )
			{
				cy += dy;

				if(cy > y1)
				{
					cy = y0;
					cx += dx;
				}

				stable = false;
			}

		}

		if(stable)
		{
			break;
		}
	}

	mod_t *mod = calloc(1, sizeof(mod_t));
	if(!mod)
		return;

	mod->handle = handle;
	mod->urn = urn;
	mod->pos = nk_vec2(cx, cy);
	_hash_add(&handle->mods, mod);
}

static void
_set_module_idisp_subscription(plughandle_t *handle, mod_t *mod, int32_t state)
{
	DBG;
	if(  _message_request(handle)
		&&  synthpod_patcher_set(&handle->regs, &handle->forge,
			mod->urn, 0, handle->regs.idisp.surface.urid,
			sizeof(int32_t), handle->forge.Bool, &state) )
	{
		_message_write(handle);
	}
}

static void
_mod_init(plughandle_t *handle, mod_t *mod, const LilvPlugin *plug)
{
	DBG;
	if(mod->plug) // already initialized
		return;

	const LilvNode *uri_node = lilv_plugin_get_uri(plug);
	const char *mod_uri = lilv_node_as_string(uri_node);

	mod->plug = plug;
	mod->subj = handle->map->map(handle->map->handle, mod_uri);
	const unsigned num_ports = lilv_plugin_get_num_ports(plug) + 2; // + automation ports

	mod->minimum = sizeof(LV2_Atom_Object);

	for(unsigned p=0; p<num_ports - 2; p++) // - automation ports
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

			audio->peak = dBFSp6(0.f);
			audio->gain = 0.f;
			//TODO

			mod->minimum += 3*(sizeof(LV2_Atom_Property) + sizeof(LV2_Atom_Float));
		}
		else if(is_cv)
		{
			port->type = PROPERTY_TYPE_CV;
			audio_port_t *audio = &port->audio;

			audio->peak = dBFSp6(0.f);
			audio->gain = 0.f;
			//TODO

			mod->minimum += 3*(sizeof(LV2_Atom_Property) + sizeof(LV2_Atom_Float));
		}
		else if(is_control)
		{
			port->type = PROPERTY_TYPE_CONTROL;
			control_port_t *control = &port->control;

			control->is_readonly = is_output;
			control->is_int = lilv_port_has_property(plug, port->port, handle->node.lv2_integer);
			control->is_bool = lilv_port_has_property(plug, port->port, handle->node.lv2_toggled);
			control->is_logarithmic = lilv_port_has_property(plug, port->port, handle->regs.port.logarithmic.node);

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

			LilvNode *units_unit = lilv_port_get(plug, port->port, handle->regs.units.unit.node);
			if(units_unit)
			{
				if(lilv_node_is_uri(units_unit))
				{
					control->units_symbol = _unit_symbol(handle, lilv_node_as_uri(units_unit));
				}
				else if(lilv_world_ask(handle->world, units_unit, handle->regs.rdf.type.node, handle->regs.units.Unit.node))
				{
					control->units_symbol = _unit_symbol_obj(handle, units_unit);
				}

				lilv_node_free(units_unit);
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

				_hash_sort(&port->control.points, _sort_scale_point_name);

				lilv_scale_points_free(port_points);
			}

			mod->minimum += sizeof(LV2_Atom_Property) + sizeof(LV2_Atom_Float);
		}
		else if(is_atom)
		{
			port->type = PROPERTY_TYPE_ATOM;

			if(lilv_port_supports_event(plug, port->port, handle->node.midi_MidiEvent))
				port->type |= PROPERTY_TYPE_MIDI;
			if(lilv_port_supports_event(plug, port->port, handle->node.osc_Event))
				port->type |= PROPERTY_TYPE_OSC;
			if(lilv_port_supports_event(plug, port->port, handle->node.time_Position))
				port->type |= PROPERTY_TYPE_TIME;
			if(lilv_port_supports_event(plug, port->port, handle->node.patch_Message))
				port->type |= PROPERTY_TYPE_PATCH;
			if(lilv_port_supports_event(plug, port->port, handle->node.xpress_Message))
				port->type |= PROPERTY_TYPE_XPRESS;


			LilvNode *min_size= lilv_port_get(plug, port->port, handle->regs.port.minimum_size.node);
			if(min_size)
			{
				mod->minimum += sizeof(LV2_Atom_Property) + sizeof(LV2_Atom) + lilv_node_as_int(min_size);
				lilv_node_free(min_size);
			}
			else
			{
				mod->minimum += 0x10000; //FIXME use sequence size from dsp
			}
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

	mod->minimum *= 8; // to be on the safe side

	// automation input port
	{
		const unsigned p = num_ports - 2;

		port_t *port = calloc(1, sizeof(port_t));
		if(port)
		{
			_hash_add(&mod->ports, port);

			port->mod = mod;
			port->index = p;
			port->port = NULL;
			port->symbol = "__automation__in__";
			port->groups = NULL;
			port->name = strdup("Automation In");
			port->automation = true;

			port->type = PROPERTY_TYPE_ATOM | PROPERTY_TYPE_MIDI | PROPERTY_TYPE_OSC;

			_hash_add(&mod->sinks, port);
			mod->sink_type |= port->type;
		}
	}

	// automation output port
	{
		const unsigned p = num_ports - 1;

		port_t *port = calloc(1, sizeof(port_t));
		if(port)
		{
			_hash_add(&mod->ports, port);

			port->mod = mod;
			port->index = p;
			port->port = NULL;
			port->symbol = "__automation__out__";
			port->groups = NULL;
			port->name = strdup("Automation Out");
			port->automation = true;

			port->type = PROPERTY_TYPE_ATOM | PROPERTY_TYPE_MIDI | PROPERTY_TYPE_OSC;

			_hash_add(&mod->sources, port);
			mod->source_type |= port->type;
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
		const LilvNode *ui_uri = lilv_ui_get_uri(ui);

		const bool needs_instance_access = lilv_world_ask(handle->world, ui_uri,
			handle->regs.core.required_feature.node, handle->regs.ui.instance_access.node);
		if(needs_instance_access)
		{
			if(handle->log)
				_log_warning(handle, "<%s> instance-access extension not supported\n", lilv_node_as_uri(ui_uri));
			continue;
		}

		const bool needs_data_access = lilv_world_ask(handle->world, ui_uri,
			handle->regs.core.required_feature.node, handle->regs.ui.data_access.node);
		if(needs_data_access)
		{
			if(handle->log)
				_log_warning(handle, "<%s> data-access extension not supported\n", lilv_node_as_uri(ui_uri));
			continue;
		}

		//FIXME check for more unsupported features

		_mod_ui_add(handle, mod, ui);
	}

	_set_module_idisp_subscription(handle, mod, 1);

	_mod_subscribe_persistent(handle, mod); // e.g. canvas:graph

	_patch_notification_add_patch_get(handle, mod,
		handle->regs.port.event_transfer.urid, mod->subj, 0, 0); // patch:Get []

	nk_pugl_post_redisplay(&handle->win); //FIXME
}

static void
_port_free(port_t *port)
{
	DBG;
	if(port->type == PROPERTY_TYPE_CONTROL)
	{
		free(port->control.units_symbol);
	}

	free(port->name);
	free(port);
}

static void
_mod_free(plughandle_t *handle, mod_t *mod)
{
	DBG;
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

	_image_free(handle, &mod->idisp.img);
	_set_module_idisp_subscription(handle, mod, 0);

	_cairo_deinit(mod);
}

static bool
_mod_remove_cb(void *node, void *data)
{
	DBG;
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
	DBG;
	_hash_remove(&handle->mods, mod);
	_hash_remove_cb(&handle->conns, _mod_remove_cb, mod);
	_mod_free(handle, mod);
}

static bool
_tooltip_visible(struct nk_context *ctx)
{
	DBG;
	return nk_widget_has_mouse_click_down(ctx, NK_BUTTON_RIGHT, nk_true)
		|| (nk_widget_is_hovered(ctx) && nk_input_is_key_down(&ctx->input, NK_KEY_CTRL));
}

static bool
_toolbar_button(struct nk_context *ctx, char key, struct nk_image icon,
	const char *label)
{
	DBG;
	struct nk_style *style = &ctx->style;
	bool is_hovered = nk_widget_is_hovered(ctx);

	if(_tooltip_visible(ctx))
	{
		char tt [16];
		snprintf(tt, sizeof(tt), "Ctrl-%c", isalpha(key) ? toupper(key) : key);
		nk_tooltip(ctx, tt);
	}
	else if(is_hovered)
	{
		nk_tooltip(ctx, label);
	}

	const bool old_state = nk_pugl_is_shortcut_pressed(&ctx->input, key, true);

	if(old_state || is_hovered)
		nk_style_push_color(ctx, &style->button.border_color, toggle_color);

	const bool state = nk_button_image_label(ctx, icon, "", NK_TEXT_RIGHT)
		|| old_state;

	if(old_state || is_hovered)
		nk_style_pop_color(ctx);

	return state;
}

static void
_toolbar_toggle(struct nk_context *ctx, bool *state, char key, struct nk_image icon,
	const char *label)
{
	DBG;
	struct nk_style *style = &ctx->style;
	const bool is_hovered = nk_widget_is_hovered(ctx);

	if(_tooltip_visible(ctx))
	{
		char tt [16];
		snprintf(tt, sizeof(tt), "Ctrl-%c", isalpha(key) ? toupper(key) : key);
		nk_tooltip(ctx, tt);
	}
	else if(is_hovered)
	{
		nk_tooltip(ctx, label);
	}

	if(nk_pugl_is_shortcut_pressed(&ctx->input, key, true))
		*state = !*state;

	bool old_state = *state;
	if(old_state)
		nk_style_push_color(ctx, &style->button.border_color, hilight_color);
	else if(is_hovered)
		nk_style_push_color(ctx, &style->button.border_color, toggle_color);

	if(nk_button_image_label(ctx, icon, "", NK_TEXT_RIGHT))
		*state = !*state;

	if(old_state || is_hovered)
		nk_style_pop_color(ctx);
}

static bool
_toolbar_toggled(struct nk_context *ctx, bool state, char key, struct nk_image icon,
	const char *label)
{
	DBG;
	_toolbar_toggle(ctx, &state, key, icon, label);
	return state;
}

static void
_toolbar_labeld(struct nk_context *ctx, bool *state, char key, const char *label)
{
	DBG;
	struct nk_style *style = &ctx->style;
	const bool is_hovered = nk_widget_is_hovered(ctx);

	if(_tooltip_visible(ctx))
	{
		char tt [16];
		snprintf(tt, sizeof(tt), "Ctrl-%c", isalpha(key) ? toupper(key) : key);
		nk_tooltip(ctx, tt);
	}

	if(nk_pugl_is_shortcut_pressed(&ctx->input, key, true))
		*state = !*state;

	bool old_state = *state;
	if(old_state)
		nk_style_push_color(ctx, &style->button.border_color, hilight_color);
	else if(is_hovered)
		nk_style_push_color(ctx, &style->button.border_color, toggle_color);

	if(nk_button_label(ctx, label))
		*state = !*state;

	if(old_state || is_hovered)
		nk_style_pop_color(ctx);
}

static bool
_toolbar_label(struct nk_context *ctx, bool state, char key, const char *label)
{
	DBG;
	_toolbar_labeld(ctx, &state, key, label);
	return state ;
}

static void
_expose_main_header(plughandle_t *handle, struct nk_context *ctx, float dy)
{
	DBG;
	struct nk_style *style = &ctx->style;

	nk_menubar_begin(ctx);
	{
		bool is_hovered;
		nk_layout_row_static(ctx, dy, 1.2*dy, 18);

		{
			if(_toolbar_button(ctx, 'n', handle->icon.plus, "New"))
			{
				if(  _message_request(handle)
					&&  synthpod_patcher_copy(&handle->regs, &handle->forge,
						handle->self_urn, 0, 0) )
				{
					_message_write(handle);
				}
			}

			if(_toolbar_button(ctx, 's', handle->icon.download, "Save"))
			{
				if(  _message_request(handle)
					&&  synthpod_patcher_copy(&handle->regs, &handle->forge,
						0, 0, handle->bundle_urn) )
				{
					_message_write(handle);
				}
			}

			if(_toolbar_button(ctx, 'q', handle->icon.cancel, "Quit"))
			{
				handle->done = true;
			}
		}

		nk_spacing(ctx, 1);

		{
			if(_toolbar_toggled(ctx, handle->type == PROPERTY_TYPE_AUDIO, 'w', handle->icon.audio, "Audio"))
				handle->type = PROPERTY_TYPE_AUDIO;
			if(_toolbar_toggled(ctx, handle->type == PROPERTY_TYPE_CV, 'e', handle->icon.cv, "CV"))
				handle->type = PROPERTY_TYPE_CV;
			if(_toolbar_toggled(ctx, handle->type == PROPERTY_TYPE_ATOM, 'r', handle->icon.atom, "Atom"))
				handle->type = PROPERTY_TYPE_ATOM;

			nk_spacing(ctx, 1);

			_toolbar_toggle(ctx, &handle->show_automation, 'd', handle->icon.automaton, "Automation");

			nk_spacing(ctx, 1);

			if(_toolbar_toggled(ctx, handle->type == PROPERTY_TYPE_MIDI, 't', handle->icon.midi, "MIDI"))
				handle->type = PROPERTY_TYPE_MIDI;
			if(_toolbar_toggled(ctx, handle->type == PROPERTY_TYPE_OSC, 'y', handle->icon.osc, "OSC"))
				handle->type = PROPERTY_TYPE_OSC;
			if(_toolbar_toggled(ctx, handle->type == PROPERTY_TYPE_TIME, 'u', handle->icon.time, "Time"))
				handle->type = PROPERTY_TYPE_TIME;
			if(_toolbar_toggled(ctx, handle->type == PROPERTY_TYPE_PATCH, 'o', handle->icon.patch, "Patch"))
				handle->type = PROPERTY_TYPE_PATCH;
			if(_toolbar_toggled(ctx, handle->type == PROPERTY_TYPE_XPRESS, 'p', handle->icon.xpress, "Xpress"))
				handle->type = PROPERTY_TYPE_XPRESS;
		}

		nk_spacing(ctx, 1);

		{
			const bool show_bottombar = handle->show_bottombar;
			_toolbar_toggle(ctx, &handle->show_bottombar, 'k', handle->icon.settings, "Settings");
			if(show_bottombar != handle->show_bottombar)
			{
				const LV2_URID subj = 0; // aka host

				if(  _message_request(handle)
					&&  synthpod_patcher_set(&handle->regs, &handle->forge,
						subj, 0, handle->regs.synthpod.row_enabled.urid,
						sizeof(int32_t), handle->forge.Bool, &handle->show_bottombar) )
				{
					_message_write(handle);
				}
			}

			const bool show_sidebar = handle->show_sidebar;
			_toolbar_toggle(ctx, &handle->show_sidebar, 'l', handle->icon.menu, "Controls");

			if(show_sidebar != handle->show_sidebar)
			{
				const LV2_URID subj = 0; // aka host

				if(  _message_request(handle)
					&&  synthpod_patcher_set(&handle->regs, &handle->forge,
						subj, 0, handle->regs.synthpod.column_enabled.urid,
						sizeof(int32_t), handle->forge.Bool, &handle->show_sidebar) )
				{
					_message_write(handle);
				}
			}
		}

		nk_menubar_end(ctx);
	}
}

static int
_sort_plugin_name(const void *a, const void *b)
{
	DBG;
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
		? strcasenumcmp(name_a, name_b)
		: 0;

	if(node_a)
		lilv_node_free(node_a);
	if(node_b)
		lilv_node_free(node_b);

	return ret;
}

static void
_discover_bundles(plughandle_t *handle)
{
	DBG;
	const LilvPlugins *plugs = lilv_world_get_all_plugins(handle->world);
	if(!plugs)
		return;

	LilvNode *self_uri = lilv_new_uri(handle->world, "http://open-music-kontrollers.ch/lv2/synthpod#stereo");
	if(self_uri)
	{
		const LilvPlugin *plug = lilv_plugins_get_by_uri(plugs, self_uri);
		if(plug)
		{
			handle->bundles = lilv_plugin_get_related(plug, handle->node.pset_Preset);

			if(handle->bundles)
			{
				LILV_FOREACH(nodes, itr, handle->bundles)
				{
					const LilvNode *bundle = lilv_nodes_get(handle->bundles, itr);

					lilv_world_load_resource(handle->world, bundle);
				}
			}
		}

		lilv_node_free(self_uri);
	}
}

static void
_undiscover_bundles(plughandle_t *handle)
{
	DBG;
	if(handle->bundles)
	{
		LILV_FOREACH(nodes, itr, handle->bundles)
		{
			const LilvNode *bundle = lilv_nodes_get(handle->bundles, itr);

			lilv_world_unload_resource(handle->world, bundle);
		}

		lilv_nodes_free(handle->bundles);
	}
}

static void
_refresh_main_bundle_list(plughandle_t *handle)
{
	DBG;
	_hash_free(&handle->bundle_matches);

	bool search = _textedit_len(&handle->bundle_search_edit) != 0;

	LILV_FOREACH(nodes, itr, handle->bundles)
	{
		const LilvNode *bundle = lilv_nodes_get(handle->bundles, itr);

		//FIXME support other search criteria
		LilvNode *label_node = lilv_world_get(handle->world, bundle, handle->node.rdfs_label, NULL);
		if(!label_node)
			label_node = lilv_world_get(handle->world, bundle, handle->node.lv2_name, NULL);
		if(label_node)
		{
			const char *label_str = lilv_node_as_string(label_node);

			if(!search || strcasestr(label_str, _textedit_const(&handle->bundle_search_edit)))
			{
				_hash_add(&handle->bundle_matches, (void *)bundle);
			}

			lilv_node_free(label_node);
		}
	}

	_hash_sort_r(&handle->bundle_matches, _sort_rdfs_label, handle);
}

static void
_expose_main_bundle_list(plughandle_t *handle, struct nk_context *ctx,
	bool find_matches)
{
	DBG;
	if(_hash_empty(&handle->bundle_matches) || find_matches)
		_refresh_main_bundle_list(handle);

	int count = 0;
	HASH_FOREACH(&handle->bundle_matches, itr)
	{
		const LilvNode *bundle = *itr;
		if(bundle)
		{
			LilvNode *label_node = lilv_world_get(handle->world, bundle, handle->node.rdfs_label, NULL);
			if(!label_node)
				label_node = lilv_world_get(handle->world, bundle, handle->node.lv2_name, NULL);
			if(label_node)
			{
				const char *label_str = lilv_node_as_string(label_node);
				const char *offset = NULL;
				if( (offset = strstr(label_str, ".lv2/")))
					label_str = offset + 5; // skip path prefix

				nk_style_push_style_item(ctx, &ctx->style.selectable.normal, (count++ % 2)
					? nk_style_item_color(nk_rgb(40, 40, 40))
					: nk_style_item_color(nk_rgb(45, 45, 45))); // NK_COLOR_WINDOW

				if(nk_select_label(ctx, label_str, NK_TEXT_LEFT, nk_false))
				{
					char *bundle_path = lilv_node_get_path(bundle, NULL);
					if(bundle_path)
					{
						char *tmp = strdup(bundle_path);
						if(tmp)
						{
							char *end = strrchr(tmp, '/');
							if(end)
								*end = '\0';

							const LV2_URID bundle_urid = handle->map->map(handle->map->handle, tmp);
							if(  _message_request(handle)
								&&  synthpod_patcher_copy(&handle->regs, &handle->forge,
									bundle_urid, 0, 0) )
							{
								_message_write(handle);
							}

							free(tmp);
						}
						lilv_free(bundle_path);
					}
				}

				nk_style_pop_style_item(ctx);

				lilv_node_free(label_node);
			}
		}
	}
}

static void
_refresh_main_plugin_list(plughandle_t *handle)
{
	DBG;
	_hash_free(&handle->plugin_matches);

	const LilvPlugins *plugs = lilv_world_get_all_plugins(handle->world);

	LilvNode *p = NULL;
	if(handle->plugin_search_selector == PLUGIN_SELECTOR_SEARCH_COMMENT)
		p = handle->node.rdfs_comment;
	else if(handle->plugin_search_selector == PLUGIN_SELECTOR_SEARCH_PROJECT)
		p = handle->node.doap_name;

	bool selector_visible = false;
	LILV_FOREACH(plugins, i, plugs)
	{
		const LilvPlugin *plug = lilv_plugins_get(plugs, i);
		const char *plug_uri = lilv_node_as_uri(lilv_plugin_get_uri(plug));

		if(  !strcmp(plug_uri, SYNTHPOD_PREFIX"sink")
			|| !strcmp(plug_uri, SYNTHPOD_PREFIX"source") )
		{
			continue;
		}

		LilvNode *name_node = lilv_plugin_get_name(plug);
		if(name_node)
		{
			const char *name_str = lilv_node_as_string(name_node);
			bool visible = _textedit_len(&handle->plugin_search_edit) == 0;

			if(!visible)
			{
				switch(handle->plugin_search_selector)
				{
					case PLUGIN_SELECTOR_SEARCH_NAME:
					{
						if(strcasestr(name_str, _textedit_const(&handle->plugin_search_edit)))
							visible = true;
					} break;
					case PLUGIN_SELECTOR_SEARCH_COMMENT:
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
					case PLUGIN_SELECTOR_SEARCH_AUTHOR:
					{
						LilvNode *author_node = lilv_plugin_get_author_name(plug);
						if(author_node)
						{
							if(strcasestr(lilv_node_as_string(author_node), _textedit_const(&handle->plugin_search_edit)))
								visible = true;
							lilv_node_free(author_node);
						}
					} break;
					case PLUGIN_SELECTOR_SEARCH_CLASS:
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
					case PLUGIN_SELECTOR_SEARCH_PROJECT:
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

					case PLUGIN_SELECTOR_SEARCH_MAX:
						break;
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
	DBG;
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
	DBG;
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
	LilvNodes *presets, const LilvNode *preset_bank, bool search)
{
	DBG;
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
	DBG;
	bool search = _textedit_len(&handle->preset_search_edit) != 0;
	_hash_free(&handle->preset_matches);

	// handle banked presets
	HASH_FOREACH(&mod->banks, itr)
	{
		const LilvNode *bank = *itr;

		_refresh_main_preset_list_for_bank(handle, mod->presets, bank, search);
	}

	// handle unbanked presets
	_refresh_main_preset_list_for_bank(handle, mod->presets, NULL, search);

	// handle default preset
	if(!search || strcasestr(DEFAULT_PSET_LABEL, _textedit_const(&handle->preset_search_edit)))
	{
		_hash_add(&handle->preset_matches, (void *)lilv_plugin_get_uri(mod->plug));
	}

	_hash_sort_r(&handle->preset_matches, _sort_rdfs_label, handle);
}

static void
_tab_label(struct nk_context *ctx, const char *label)
{
	DBG;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	struct nk_rect bounds = nk_widget_bounds(ctx);

	nk_fill_rect(canvas, bounds, 0.f, group_color);
	nk_label(ctx, label, NK_TEXT_CENTERED);
}

static void
_expose_main_preset_list_wo_banks(plughandle_t *handle, mod_t *mod,
	struct nk_context *ctx)
{
	DBG;
	int count = 0;

	nk_layout_row_dynamic(ctx, handle->dy2, 1);

	HASH_FOREACH(&handle->preset_matches, itr)
	{
		const LilvNode *preset = *itr;

		const bool is_default_pset = lilv_node_equals(preset, lilv_plugin_get_uri(mod->plug));

		LilvNode *label_node = lilv_world_get(handle->world, preset, handle->node.rdfs_label, NULL);
		if(!label_node)
			label_node = lilv_world_get(handle->world, preset, handle->node.lv2_name, NULL);
		if(label_node || is_default_pset)
		{
			const char *label_str = is_default_pset
				? DEFAULT_PSET_LABEL
				: lilv_node_as_string(label_node);

			nk_style_push_style_item(ctx, &ctx->style.selectable.normal, (count++ % 2)
				? nk_style_item_color(nk_rgb(40, 40, 40))
				: nk_style_item_color(nk_rgb(45, 45, 45))); // NK_COLOR_WINDOW

			const bool is_user_preset = !strncmp(lilv_node_as_string(preset), "file://", 7);
			bool is_preset_selected;

			if(is_user_preset)
			{
				is_preset_selected = nk_select_image_label(ctx, handle->icon.house,
					label_str, NK_TEXT_LEFT, nk_false);
			}
			else
			{
				is_preset_selected = nk_select_label(ctx,
					label_str, NK_TEXT_LEFT, nk_false);
			}

			if(is_preset_selected)
				_patch_mod_preset_set(handle, handle->module_selector, preset);

			nk_style_pop_style_item(ctx);

			lilv_node_free(label_node);
		}
	}
}

static void
_expose_main_preset_list(plughandle_t *handle, struct nk_context *ctx,
	bool find_matches)
{
	DBG;
	mod_t *mod = handle->module_selector;

	if(mod && mod->presets)
	{
		if(_hash_empty(&handle->preset_matches) || find_matches)
			_refresh_main_preset_list(handle, mod);

		_expose_main_preset_list_wo_banks(handle, mod, ctx);
	}
}

#if 0
static void
_expose_main_preset_info(plughandle_t *handle, struct nk_context *ctx)
{
	DBG;
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
	DBG;
	const int32_t tmp = *val;
	struct nk_rect bounds;
	const bool left_mouse_click_in_cursor = nk_widget_is_mouse_clicked(ctx, NK_BUTTON_LEFT);
	const enum nk_widget_layout_states layout_states = nk_widget(&bounds, ctx);

	if(layout_states != NK_WIDGET_INVALID)
	{
		enum nk_widget_states states = NK_WIDGET_STATE_INACTIVE;
		struct nk_input *in = (ctx->current->layout->flags & NK_WINDOW_ROM) ? 0 : &ctx->input;

		if(in && editable)
		{
			bool mouse_has_scrolled = false;

			if(left_mouse_click_in_cursor)
			{
				states = NK_WIDGET_STATE_ACTIVED;
			}
			else if(nk_input_is_mouse_hovering_rect(in, bounds))
			{
				if(in->mouse.scroll_delta.y != 0.f) // has scrolling
				{
					mouse_has_scrolled = true;
					in->mouse.scroll_delta.y = 0.f;
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

		nk_stroke_arc(canv, cx, cy, r2 - 0, 0.f, 2*M_PI, 2.f, fg_color);
		if(*val)
			nk_fill_arc(canv, cx, cy, r2 - 2, 0.f, 2*M_PI, fg_color);
	}

	return tmp != *val;
}

static bool drag = false;

static float
_dial_numeric_behavior(struct nk_context *ctx, struct nk_rect bounds,
	enum nk_widget_states *states, int *divider, struct nk_input *in)
{
	DBG;
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
		if(in->mouse.scroll_delta.y != 0.f) // has scrolling
		{
			dd = in->mouse.scroll_delta.y;
			in->mouse.scroll_delta.y = 0.f;
		}

		*states = NK_WIDGET_STATE_HOVER;
	}

	if(nk_input_is_key_down(in, NK_KEY_SHIFT))
		*divider *= 10;

	return dd;
}

static void
_dial_numeric_draw(struct nk_context *ctx, struct nk_rect bounds,
	enum nk_widget_states states, float perc, struct nk_color color)
{
	DBG;
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

	nk_stroke_arc(canv, cx, cy, (r1+r2)/2, a1, a2, r1-r2, bg_color);
	nk_stroke_arc(canv, cx, cy, (r1+r2)/2, a1, a3, r1-r2, fg_color);
}

static const float exp_a = 1.0 / (M_E - 1.0);
static const float exp_b = -1.0 / (M_E - 1.0);

static int
_dial_double(struct nk_context *ctx, double min, double *val, double max, float mul,
	struct nk_color color, bool editable, bool logarithmic)
{
	DBG;
	const double tmp = *val;
	struct nk_rect bounds;
	const enum nk_widget_layout_states layout_states = nk_widget(&bounds, ctx);

	if(layout_states != NK_WIDGET_INVALID)
	{
		double value = *val;
		enum nk_widget_states states = NK_WIDGET_STATE_INACTIVE;
		if(logarithmic)
		{
			min = log(min);
			max = log(max);
			value = log(value);
		}
		const double range = max - min;
		struct nk_input *in = (ctx->current->layout->flags & NK_WINDOW_ROM) ? 0 : &ctx->input;

		if(in && editable)
		{
			int divider = 1;
			const float dd = _dial_numeric_behavior(ctx, bounds, &states, &divider, in);

			if(dd != 0.f) // update value
			{
				const double per_pixel_inc = mul * range / bounds.w / divider;
				const double diff = dd * per_pixel_inc;

				value += diff;
				value = NK_CLAMP(min, value, max);
			}
		}

		const float perc = (value - min) / range;
		_dial_numeric_draw(ctx, bounds, states, perc, color);

		*val = logarithmic
			? exp(value)
			: value;
	}

	return tmp != *val;
}

static int
_dial_long(struct nk_context *ctx, int64_t min, int64_t *val, int64_t max, float mul,
	struct nk_color color, bool editable, bool logarithmic)
{
	DBG;
	const int64_t tmp = *val;
	struct nk_rect bounds;
	const enum nk_widget_layout_states layout_states = nk_widget(&bounds, ctx);

	if(layout_states != NK_WIDGET_INVALID)
	{
		int64_t value = *val;
		enum nk_widget_states states = NK_WIDGET_STATE_INACTIVE;
		if(logarithmic)
		{
			min = log(min);
			max = log(max);
			value = log(value);
		}
		const int64_t range = max - min;
		struct nk_input *in = (ctx->current->layout->flags & NK_WINDOW_ROM) ? 0 : &ctx->input;

		if(in && editable)
		{
			int divider = 1;
			const float dd = _dial_numeric_behavior(ctx, bounds, &states, &divider, in);

			if(dd != 0.f) // update value
			{
				const double per_pixel_inc = mul * range / bounds.w / divider;
				const double diff = dd * per_pixel_inc;

				value += diff < 0.0 ? floor(diff) : ceil(diff);
				value = NK_CLAMP(min, value, max);
			}
		}

		const float perc = (float)(value - min) / range;
		_dial_numeric_draw(ctx, bounds, states, perc, color);

		*val = logarithmic
			? exp(value)
			: value;
	}

	return tmp != *val;
}

static int
_dial_float(struct nk_context *ctx, float min, float *val, float max, float mul,
	struct nk_color color, bool editable, bool logarithmic)
{
	DBG;
	double tmp = *val;
	const int res = _dial_double(ctx, min, &tmp, max, mul, color, editable, logarithmic);
	*val = tmp;

	return res;
}

static int
_dial_int(struct nk_context *ctx, int32_t min, int32_t *val, int32_t max, float mul,
	struct nk_color color, bool editable, bool logarithmic)
{
	DBG;
	int64_t tmp = *val;
	const int res = _dial_long(ctx, min, &tmp, max, mul, color, editable, logarithmic);
	*val = tmp;

	return res;
}

static void
_expose_atom_port(struct nk_context *ctx, mod_t *mod, port_t *port,
	float DY, float dy, const char *name_str)
{
	DBG;
	plughandle_t *handle = mod->handle;

	DY -= 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		const bool has_midi = port->type & PROPERTY_TYPE_MIDI;
		const bool has_osc = port->type & PROPERTY_TYPE_OSC;
		const bool has_time = port->type & PROPERTY_TYPE_TIME;
		const bool has_patch = port->type & PROPERTY_TYPE_PATCH;
		const bool has_xpress = port->type & PROPERTY_TYPE_XPRESS;

		const unsigned n = has_midi + has_osc + has_time + has_patch + has_xpress;

		nk_layout_row_dynamic(ctx, dy, 1);
		nk_label(ctx, name_str, NK_TEXT_LEFT);

		if(n)
		{
			nk_layout_row_static(ctx, dy, dy, n);

			if(has_midi)
				nk_image(ctx, handle->icon.midi);
			if(has_osc)
				nk_image(ctx, handle->icon.osc);
			if(has_time)
				nk_image(ctx, handle->icon.time);
			if(has_patch)
				nk_image(ctx, handle->icon.patch);
			if(has_xpress)
				nk_image(ctx, handle->icon.xpress);
		}

		nk_group_end(ctx);
	}
}

static void
_expose_audio_port(struct nk_context *ctx, mod_t *mod, audio_port_t *audio,
	float DY, float dy, const char *name_str, bool is_cv)
{
	DBG;
	DY -= 2*ctx->style.window.group_padding.y;
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

#if 0
	if(_dial_float(ctx, 0.f, &audio->gain, 1.f, 1.f, nk_rgb(0xff, 0xff, 0xff), true))
	{
		//FIXME
	}
#endif
}

const char *lab = "#"; //FIXME

static bool
_expose_control_port(struct nk_context *ctx, mod_t *mod, control_port_t *control,
	float DY, float dy, const char *name_str)
{
	DBG;
	bool changed = false;

	DY -= 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);

		if(control->units_symbol)
			nk_labelf(ctx, NK_TEXT_LEFT, "%s [%s]", name_str, control->units_symbol);
		else
			nk_label(ctx, name_str, NK_TEXT_LEFT);

		if(!_hash_empty(&control->points))
		{
			scale_point_t *ref = NULL;

			int32_t diff1 = INT32_MAX;
			HASH_FOREACH(&control->points, itr)
			{
				scale_point_t *point = *itr;

				const int32_t diff2 = abs(point->val.i - control->val.i); //FIXME

				if(diff2 < diff1)
				{
					ref = point;
					diff1 = diff2;
				}
			}

			if(nk_combo_begin_label(ctx, ref->label, nk_vec2(nk_widget_width(ctx), 7*dy)))
			{
				nk_layout_row_dynamic(ctx, dy, 1);
				HASH_FOREACH(&control->points, itr)
				{
					scale_point_t *point = *itr;

					if(nk_combo_item_label(ctx, point->label, NK_TEXT_LEFT) && !control->is_readonly)
					{
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
		if(_dial_int(ctx, control->min.i, &control->val.i, control->max.i, 1.f,
			nk_rgb(0xff, 0xff, 0xff), !control->is_readonly, control->is_logarithmic))
		{
			changed = true;
		}
	}
	else if(control->is_bool)
	{
		if(_dial_bool(ctx, &control->val.i,
			nk_rgb(0xff, 0xff, 0xff), !control->is_readonly))
		{
			changed = true;
		}
	}
	else // is_float
	{
		if(_dial_float(ctx, control->min.f, &control->val.f, control->max.f, 1.f,
			nk_rgb(0xff, 0xff, 0xff), !control->is_readonly, control->is_logarithmic))
		{
			changed = true;
		}
	}

	return changed;
}

static void
_expose_port(struct nk_context *ctx, mod_t *mod, port_t *port, float DY, float dy)
{
	DBG;
	plughandle_t *handle = mod->handle;
	const bool is_hovered = nk_widget_is_hovered(ctx);

	if(nk_widget_is_mouse_clicked(ctx, NK_BUTTON_RIGHT))
	{
		handle->port_selector = (handle->port_selector == port) ? NULL : port;
		handle->param_selector = NULL;
	}

	nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
		nk_style_item_color(invisible_color));

	const struct nk_rect bb = nk_widget_bounds(ctx);
	if(nk_group_begin(ctx, port->name, NK_WINDOW_NO_SCROLLBAR))
	{
		if(handle->port_selector == port) // mark focus
		{
			struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
			nk_fill_rect(canvas, bb, 0.f, selection_color);
		}

		switch(port->type)
		{
			case PROPERTY_TYPE_AUDIO:
			{
				_expose_audio_port(ctx, mod, &port->audio, DY, dy, port->name, false);
			} break;
			case PROPERTY_TYPE_CV:
			{
				_expose_audio_port(ctx, mod, &port->audio, DY, dy, port->name, true);
				//FIXME notification
			} break;
			case PROPERTY_TYPE_CONTROL:
			{
				if(_expose_control_port(ctx, mod, &port->control, DY, dy, port->name))
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
					_expose_atom_port(ctx, mod, port, DY, dy, port->name);
				}
			} break;
		}

		nk_group_end(ctx);

		if( (port->type == PROPERTY_TYPE_CONTROL) && (port->control.automation.type != AUTO_NONE) ) // mark automation state
		{
			struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
			nk_fill_rect(canvas, nk_rect(bb.x + bb.w - 4.f, bb.y, 4.f, bb.h), 0.f, automation_color);
		}

		if(is_hovered)
		{
			struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
			nk_stroke_rect(canvas, bb, 0.f, ctx->style.property.border, hilight_color);
		}
	}

	nk_style_pop_style_item(ctx);
}

static bool
_widget_string(plughandle_t *handle, struct nk_context *ctx,
	struct nk_text_edit *editor, bool editable)
{
	DBG;
	bool commited = false;

	const int old_len = nk_str_len_char(&editor->string);
	nk_flags flags = NK_EDIT_BOX;
	if(!editable)
		flags |= NK_EDIT_READ_ONLY;
#if 0 //FIXME
	if(has_shift_enter)
#endif
		flags |= NK_EDIT_SIG_ENTER;
	const nk_flags state = nk_edit_buffer(ctx, flags, editor, nk_filter_default);
	_textedit_zero_terminate(editor);
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
	float DY, float dy, const char *name_str)
{
	DBG;
	DY -= 2*ctx->style.window.group_padding.y;

	bool changed = false;
	if(  (param->range == handle->forge.String)
		|| (param->range == handle->forge.Path)
		|| (param->range == handle->forge.URI)
		|| (param->range == handle->forge.URID)
		|| param->is_bitmask)
	{
		nk_layout_row_dynamic(ctx, DY, 1);
	}
	else // !String
	{
		const float ratio [] = {0.7, 0.3};
		nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	}

	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);

		if(param->units_symbol)
			nk_labelf(ctx, NK_TEXT_LEFT, "%s [%s]", name_str, param->units_symbol);
		else
			nk_label(ctx, name_str, NK_TEXT_LEFT);

		if(!_hash_empty(&param->points))
		{
			scale_point_t *ref = NULL;

			int32_t diff1 = INT32_MAX;
			HASH_FOREACH(&param->points, itr)
			{
				scale_point_t *point = *itr;

				const int32_t diff2 = abs(point->val.i - param->val.i); //FIXME

				if(diff2 < diff1)
				{
					ref = point;
					diff1 = diff2;
				}
			}

			if(nk_combo_begin_label(ctx, ref->label, nk_vec2(nk_widget_width(ctx), 7*dy)))
			{
				nk_layout_row_dynamic(ctx, dy, 1);
				HASH_FOREACH(&param->points, itr)
				{
					scale_point_t *point = *itr;

					if(nk_combo_item_label(ctx, point->label, NK_TEXT_LEFT) && !param->is_readonly)
					{
						param->val = point->val;
						changed = true;
					}
				}

				nk_combo_end(ctx);
			}
		}
		else if(param->is_bitmask)
		{
			struct nk_style *style = &ctx->style;

			const unsigned nbits = log2(param->max.i + 1);
			nk_layout_row_static(ctx, dy, dy, nbits);
			for(unsigned i=0; i<nbits; i++)
			{
				const uint8_t mask = (1 << i);
				const uint8_t has_bit = param->val.i & mask;
				char lbl [11];
				snprintf(lbl, sizeof(lbl), "%2"PRIu8, i);

				if(has_bit)
					nk_style_push_color(ctx, &style->button.border_color, hilight_color);

				if(nk_button_label(ctx, lbl))
				{
					if(has_bit)
						param->val.i &= ~mask;
					else
						param->val.i |= mask;

					changed = true;
				}

				if(has_bit)
					nk_style_pop_color(ctx);
			}
		}
		else if(param->range == handle->forge.Int)
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
			nk_layout_row_dynamic(ctx, dy*1.2, 1); // editor field needs to be heigher
			if(_widget_string(handle, ctx, &param->val.editor, !param->is_readonly))
				changed = true;
		}
		else if(param->range == handle->forge.Path)
		{
			nk_layout_row_dynamic(ctx, dy*1.2, 1); // editor field needs to be heigher
			if(_widget_string(handle, ctx, &param->val.editor, !param->is_readonly))
				changed = true;
		}
		else if(param->range == handle->forge.URI)
		{
			nk_layout_row_dynamic(ctx, dy*1.2, 1); // editor field needs to be heigher
			if(_widget_string(handle, ctx, &param->val.editor, !param->is_readonly))
				changed = true;
		}
		else if(param->range == handle->forge.URID)
		{
			nk_layout_row_dynamic(ctx, dy*1.2, 1); // editor field needs to be heigher
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

	if( (param->range == handle->forge.Int) && !param->is_bitmask)
	{
		if(_dial_int(ctx, param->min.i, &param->val.i, param->max.i, 1.f,
			nk_rgb(0xff, 0xff, 0xff), !param->is_readonly, param->is_logarithmic))
		{
			changed = true;
		}
	}
	else if(param->range == handle->forge.Long)
	{
		if(_dial_long(ctx, param->min.h, &param->val.h, param->max.h, 1.f,
			nk_rgb(0xff, 0xff, 0xff), !param->is_readonly, param->is_logarithmic))
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
		if(_dial_float(ctx, param->min.f, &param->val.f, param->max.f, 1.f,
			nk_rgb(0xff, 0xff, 0xff), !param->is_readonly, param->is_logarithmic))
		{
			changed = true;
		}
	}
	else if(param->range == handle->forge.Double)
	{
		if(_dial_double(ctx, param->min.d, &param->val.d, param->max.d, 1.f,
			nk_rgb(0xff, 0xff, 0xff), !param->is_readonly, param->is_logarithmic))
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
_param_notification_add(plughandle_t *handle, mod_t *mod, param_t *param)
{
	DBG;
	//FIXME sandbox_master_send is not necessary, as messages should be fed back via dsp to nk
	if(param->range == handle->forge.Int)
	{
		_patch_notification_add_patch_set(handle, mod,
			handle->regs.port.event_transfer.urid, mod->subj, 0, param->property,
			sizeof(int32_t), handle->forge.Int, &param->val.i);
	}
	else if(param->range == handle->forge.Bool)
	{
		_patch_notification_add_patch_set(handle, mod,
			handle->regs.port.event_transfer.urid, mod->subj, 0, param->property,
			sizeof(int32_t), handle->forge.Bool, &param->val.i);
	}
	else if(param->range == handle->forge.Long)
	{
		_patch_notification_add_patch_set(handle, mod,
			handle->regs.port.event_transfer.urid, mod->subj, 0, param->property,
			sizeof(int64_t), handle->forge.Long, &param->val.h);
	}
	else if(param->range == handle->forge.Float)
	{
		_patch_notification_add_patch_set(handle, mod,
			handle->regs.port.event_transfer.urid, mod->subj, 0, param->property,
			sizeof(float), handle->forge.Float, &param->val.f);
	}
	else if(param->range == handle->forge.Double)
	{
		_patch_notification_add_patch_set(handle, mod,
			handle->regs.port.event_transfer.urid, mod->subj, 0, param->property,
			sizeof(double), handle->forge.Double, &param->val.d);
	}
	else if(param->range == handle->forge.String)
	{
		const char *str = nk_str_get_const(&param->val.editor.string);
		const uint32_t sz= nk_str_len_char(&param->val.editor.string) + 1;

		_patch_notification_add_patch_set(handle, mod,
			handle->regs.port.event_transfer.urid, mod->subj, 0, param->property,
			sz, handle->forge.String, str);
	}
	else if(param->range == handle->forge.Path)
	{
		const char *str = nk_str_get_const(&param->val.editor.string);
		const uint32_t sz= nk_str_len_char(&param->val.editor.string) + 1;

		_patch_notification_add_patch_set(handle, mod,
			handle->regs.port.event_transfer.urid, mod->subj, 0, param->property,
			sz, handle->forge.Path, str);
	}
	else if(param->range == handle->forge.URI)
	{
		const char *str = nk_str_get_const(&param->val.editor.string);
		const uint32_t sz= nk_str_len_char(&param->val.editor.string) + 1;

		_patch_notification_add_patch_set(handle, mod,
			handle->regs.port.event_transfer.urid, mod->subj, 0, param->property,
			sz, handle->forge.URI, str);
	}
	else if(param->range == handle->forge.URID)
	{
		const char *str = nk_str_get_const(&param->val.editor.string);
		const uint32_t sz= nk_str_len_char(&param->val.editor.string);
		char *uri = alloca(sz+1);
		strncpy(uri, str, sz);
		uri[sz] = '\0';
		const uint32_t urid = handle->map->map(handle->map->handle, uri);

		_patch_notification_add_patch_set(handle, mod,
			handle->regs.port.event_transfer.urid, mod->subj, 0, param->property,
			sizeof(uint32_t), handle->forge.URID, &urid);
	}
	else if(param->range == handle->forge.Chunk)
	{
		chunk_t *chunk = &param->val.chunk;

		_patch_notification_add_patch_set(handle, mod,
			handle->regs.port.event_transfer.urid, mod->subj, 0, param->property,
			chunk->size, handle->forge.Chunk, chunk->body);
	}
	//FIXME handle remaining types
}

static void
_expose_param(plughandle_t *handle, mod_t *mod, struct nk_context *ctx, param_t *param, float DY, float dy)
{
	DBG;
	const char *name_str = param->label ? param->label : "Unknown";
	const bool is_hovered = nk_widget_is_hovered(ctx);

	if(nk_widget_is_mouse_clicked(ctx, NK_BUTTON_RIGHT))
	{
		handle->port_selector = NULL;
		handle->param_selector = (handle->param_selector == param) ? NULL : param;
	}

	nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
		nk_style_item_color(invisible_color));

	const struct nk_rect bb = nk_widget_bounds(ctx);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		if(handle->param_selector == param) // mark focus
		{
			struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
			nk_fill_rect(canvas, bb, 0.f, selection_color);
		}

		if(_expose_param_inner(ctx, param, handle, DY, dy, name_str))
		{
			_param_notification_add(handle, mod, param);
		}

		nk_group_end(ctx);

		if(param->automation.type != AUTO_NONE) // mark automation state
		{
			struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
			nk_fill_rect(canvas, nk_rect(bb.x + bb.w - 4.f, bb.y, 4.f, bb.h), 0.f, automation_color);
		}

		if(is_hovered)
		{
			struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
			nk_stroke_rect(canvas, bb, 0.f, ctx->style.property.border, hilight_color);
		}
	}

	nk_style_pop_style_item(ctx);
}

static void
_refresh_main_port_list(plughandle_t *handle, mod_t *mod)
{
	DBG;
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
			//FIXME support other search criteria
		}

		if(visible)
			_hash_add(&handle->port_matches, port);
	}
}

static void
_refresh_main_param_list(plughandle_t *handle, mod_t *mod)
{
	DBG;
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
			//FIXME support other search criteria
		}

		if(visible)
			_hash_add(&handle->param_matches, param);
	}
}

static void
_ruler(struct nk_context *ctx)
{
	DBG;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

	struct nk_rect body;
	const enum nk_widget_layout_states states = nk_widget(&body, ctx);
	if(states != NK_WIDGET_INVALID)
	{
		struct nk_style_button *style = &ctx->style.button;
		const struct nk_user_font *font = ctx->style.font;

		nk_stroke_line(canvas, body.x, body.y, body.x + body.w, body.y + body.h,
			ctx->style.window.group_border, ctx->style.window.group_border_color);
	}
}

static void
_expose_control_list(plughandle_t *handle, mod_t *mod, struct nk_context *ctx,
	float DY, float dy, bool find_matches)
{
	DBG;
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
					first = false;
				}
				else
				{
					nk_layout_row_dynamic(ctx, 1.f, 1);
					_ruler(ctx);
				}

				nk_layout_row_dynamic(ctx, DY, 1);
				_expose_port(ctx, mod, port, DY, dy);
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
				first = false;
			}
			else
			{
				nk_layout_row_dynamic(ctx, 1.f, 1);
				_ruler(ctx);
			}

			nk_layout_row_dynamic(ctx, DY, 1);
			_expose_port(ctx, mod, port, DY, dy);
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
				first = false;
			}
			else {
				nk_layout_row_dynamic(ctx, 1.f, 1);
				_ruler(ctx);
			}

			nk_layout_row_dynamic(ctx, DY, 1);
			_expose_param(handle, mod, ctx, param, DY, dy);
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
				first = false;
			}
			else
			{
				nk_layout_row_dynamic(ctx, 1.f, 1);
				_ruler(ctx);
			}

			nk_layout_row_dynamic(ctx, DY, 1);
			_expose_param(handle, mod, ctx, param, DY, dy);
		}
	}
}

static void
_set_module_selector(plughandle_t *handle, mod_t *mod)
{
	DBG;
	if(handle->module_selector)
	{
		_mod_unsubscribe_all(handle, handle->module_selector);
	}

	if(mod)
	{
		_mod_subscribe_all(handle, mod);

		_patch_notification_add_patch_get(handle, mod,
			handle->regs.port.event_transfer.urid, mod->subj, 0, 0); // patch:Get []
	}

	handle->module_selector = mod;
	handle->port_selector = NULL;
	handle->param_selector = NULL;
	handle->preset_find_matches = true;
	handle->prop_find_matches = true;
}

static inline bool
_has_selected_nodes(plughandle_t *handle)
{
	DBG;
	// deselect all modules
	HASH_FOREACH(&handle->mods, mod_itr)
	{
		mod_t *mod = *mod_itr;

		if(mod->selected)
			return true;
	}

	// deselect all module connectors
	HASH_FOREACH(&handle->conns, mod_conn_itr)
	{
		mod_conn_t *mod_conn = *mod_conn_itr;

		if(mod_conn->selected)
			return true;
	}

	return false;
}

static inline void
_set_selected_nodes(plughandle_t *handle, bool selected)
{
	DBG;
	// deselect all modules
	HASH_FOREACH(&handle->mods, mod_itr)
	{
		mod_t *mod = *mod_itr;

		mod->selected = selected;
	}

	// deselect all module connectors
	HASH_FOREACH(&handle->conns, mod_conn_itr)
	{
		mod_conn_t *mod_conn = *mod_conn_itr;

		mod_conn->selected = selected;
	}
}

static inline void
_set_selected_box(plughandle_t *handle, struct nk_rect box, bool selected)
{
	DBG;
	// deselect all modules
	HASH_FOREACH(&handle->mods, mod_itr)
	{
		mod_t *mod = *mod_itr;

		if(  (mod->pos.x > box.x)
			&& (mod->pos.x < box.x + box.w)
			&& (mod->pos.y > box.y)
			&& (mod->pos.y < box.y + box.h) )
		{
			mod->selected = selected;
		}
	}

	// deselect all module connectors
	HASH_FOREACH(&handle->conns, mod_conn_itr)
	{
		mod_conn_t *mod_conn = *mod_conn_itr;

		if(  (mod_conn->pos.x > box.x)
			&& (mod_conn->pos.x < box.x + box.w)
			&& (mod_conn->pos.y > box.y)
			&& (mod_conn->pos.y < box.y + box.h) )
		{
			mod_conn->selected = selected;
		}
	}
}

static inline void
_set_moving_nodes(plughandle_t *handle, bool moving)
{
	DBG;
	// deselect all modules
	HASH_FOREACH(&handle->mods, mod_itr)
	{
		mod_t *mod = *mod_itr;

		if(mod->selected)
			mod->moving = moving;
	}

	// deselect all module connectors
	HASH_FOREACH(&handle->conns, mod_conn_itr)
	{
		mod_conn_t *mod_conn = *mod_conn_itr;

		if(mod_conn->selected)
			mod_conn->moving = moving;
	}
}

static bool
_type_match(plughandle_t *handle, port_t *port)
{
	DBG;
	if(!handle->show_automation && port->automation)
		return false;

	return handle->type & port->type;
}

static bool
_source_type_match(plughandle_t *handle, port_t *source_port)
{
	DBG;
	return _type_match(handle, source_port);
}

static bool
_sink_type_match(plughandle_t *handle, port_t *sink_port)
{
	DBG;
	return _type_match(handle, sink_port);
}

static inline unsigned
_mod_num_sources(mod_t *mod, property_type_t type)
{
	DBG;
	if(mod->source_type & type)
	{
		unsigned num = 0;

		HASH_FOREACH(&mod->sources, port_itr)
		{
			port_t *port = *port_itr;

			if(!mod->handle->show_automation && port->automation)
				continue;

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
	DBG;
	if(mod->sink_type & type)
	{
		unsigned num = 0;

		HASH_FOREACH(&mod->sinks, port_itr)
		{
			port_t *port = *port_itr;

			if(!mod->handle->show_automation && port->automation)
				continue;

			if(port->type & type)
				num += 1;
		}

		return num;
	}

	return 0;
}

static inline bool
_mod_conn_num_connections(plughandle_t *handle, mod_conn_t *mod_conn)
{
	if( (mod_conn->source_type & handle->type) && (mod_conn->sink_type & handle->type) )
	{
		unsigned num = 0;

		HASH_FOREACH(&mod_conn->conns, conn_itr)
		{
			port_conn_t *conn = *conn_itr;

			if(  _source_type_match(handle, conn->source_port)
				&& _sink_type_match(handle, conn->sink_port) )
			{
				num += 1;
			}
		}

		return (num > 0) || mod_conn->on_hold;
	}

	return false;
}

static inline void
_remove_visible_ports_from_mod_conn(plughandle_t *handle, mod_conn_t *mod_conn)
{
	DBG;
	unsigned count = 0;

	HASH_FOREACH(&mod_conn->conns, port_conn_itr)
	{
		port_conn_t *port_conn = *port_conn_itr;

		if(  _source_type_match(handle, port_conn->source_port)
			&& _sink_type_match(handle, port_conn->sink_port) )
		{
			_patch_connection_remove(handle, port_conn->source_port, port_conn->sink_port);
			count += 1;
		}
	}

	if(count == 0) // is empty matrix, demask for current type and deselect
	{
		mod_conn->source_type &= ~(handle->type);
		mod_conn->sink_type &= ~(handle->type);
	}

	mod_conn->on_hold = false;
	mod_conn->selected = false;
}

static inline void
_remove_selected_nodes(plughandle_t *handle)
{
	DBG;
	// deselect all module connectors
	HASH_FOREACH(&handle->conns, mod_conn_itr)
	{
		mod_conn_t *mod_conn = *mod_conn_itr;

		if(mod_conn->selected)
		{
			_remove_visible_ports_from_mod_conn(handle, mod_conn);
		}
	}

	// deselect all modules
	HASH_FOREACH(&handle->mods, mod_itr)
	{
		mod_t *mod = *mod_itr;

		if(mod->selected)
		{
			const LilvPlugin *plug = mod->plug;
			const LilvNode *uri_node = lilv_plugin_get_uri(plug);
			const char *mod_uri = lilv_node_as_string(uri_node);

			if(  strcmp(mod_uri, SYNTHPOD_PREFIX"source")
				&& strcmp(mod_uri, SYNTHPOD_PREFIX"sink") )
			{
				_patch_mod_remove(handle, mod);
			}
		}
	}
}

static inline void
_reinstantiate_selected_nodes(plughandle_t *handle)
{
	DBG;
	// reinstantiate all modules
	HASH_FOREACH(&handle->mods, mod_itr)
	{
		mod_t *mod = *mod_itr;

		if(mod->selected)
		{
			const LilvPlugin *plug = mod->plug;
			const LilvNode *uri_node = lilv_plugin_get_uri(plug);
			const char *mod_uri = lilv_node_as_string(uri_node);

			if(  strcmp(mod_uri, SYNTHPOD_PREFIX"source")
				&& strcmp(mod_uri, SYNTHPOD_PREFIX"sink") )
			{
				_patch_mod_reinstantiate_set(handle, mod, 1);
			}
		}
	}
}

static inline void
_show_selected_nodes(plughandle_t *handle)
{
	DBG;
	HASH_FOREACH(&handle->mods, mod_itr)
	{
		mod_t *mod = *mod_itr;

		if(mod->selected)
		{
			HASH_FOREACH(&mod->uis, mod_ui_itr)
			{
				mod_ui_t *mod_ui = *mod_ui_itr;

				if(_mod_ui_is_running(mod_ui))
					_mod_ui_stop(mod_ui, true); // stop existing UI
				else
					_mod_ui_run(mod_ui, true); // run UI

				break; //FIXME only consider first UI
			}
		}
	}
}

static void
_link_modules(plughandle_t *handle, struct nk_context *ctx, mod_t *src, mod_t *mod)
{
	DBG;
	const struct nk_input *in = &ctx->input;
	mod_conn_t *mod_conn = _mod_conn_find(handle, src, mod);
	if(!mod_conn) // does not yet exist
		mod_conn = _mod_conn_add(handle, src, mod, true);
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

				if(!_source_type_match(handle, source_port))
					continue;

				unsigned j = 0;
				HASH_FOREACH(&mod->sinks, sink_port_itr)
				{
					port_t *sink_port = *sink_port_itr;

					if(!_sink_type_match(handle, sink_port))
						continue;

					if(i == j)
					{
						_patch_connection_add(handle, source_port, sink_port, 1.f);
					}

					j++;
				}

				i++;
			}
		}
		else
		{
			mod_conn->on_hold = true;
		}
	}
}

static void
_mod_moveable(plughandle_t *handle, struct nk_context *ctx, mod_t *mod,
	struct nk_rect space_bounds, struct nk_rect *bounds)
{
	DBG;
	struct nk_input *in = &ctx->input;

	const bool is_hovering = nk_input_is_mouse_hovering_rect(in, *bounds);

	if(mod->moving)
	{
		handle->mods_moving += 1;

		if(nk_input_is_mouse_released(in, NK_BUTTON_LEFT))
		{
			mod->moving = false;

			HASH_FOREACH(&handle->conns, mod_conn_itr)
			{
				mod_conn_t *mod_conn = *mod_conn_itr;

				// if hovering over a mod_conn, insert it there, replacing the connection
				if(!mod_conn->moving && mod_conn->hovering)
				{
					mod_t *src_mod = mod_conn->source_mod;
					mod_t *snk_mod = mod_conn->sink_mod;

					_remove_visible_ports_from_mod_conn(handle, mod_conn);
					_link_modules(handle, ctx, src_mod, mod);
					_link_modules(handle, ctx, mod, snk_mod);
					break; // only consider first mod hovering over
				}
			}

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
		else if(!nk_input_is_mouse_down(in, NK_BUTTON_MIDDLE))
		{
			mod->pos.x += in->mouse.delta.x;
			mod->pos.y += in->mouse.delta.y;
			bounds->x += in->mouse.delta.x;
			bounds->y += in->mouse.delta.y;
		}
	}
	else if(is_hovering
		&& nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT) )
	{
		const bool selected = mod->selected;

		if(!nk_input_is_key_down(in, NK_KEY_SHIFT))
			_set_selected_nodes(handle, false);

		mod->selected = !selected;
		_set_module_selector(handle, mod->selected ? mod : NULL);
	}
}

static void
_mod_connectors(plughandle_t *handle, struct nk_context *ctx, mod_t *mod,
	struct nk_vec2 dim, bool is_hilighted)
{
	DBG;
	const struct nk_input *in = &ctx->input;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_vec2 scrolling = handle->scrolling;

	const float cw = 4.f * handle->scale;

	struct nk_rect bounds = nk_rect(
		mod->pos.x - mod->dim.x/2 - scrolling.x,
		mod->pos.y - mod->dim.y/2 - scrolling.y,
		mod->dim.x, mod->dim.y);

	// output connector
	if(_mod_num_sources(mod, handle->type))
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
	if(_mod_num_sinks(mod, handle->type))
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
				_link_modules(handle, ctx, src, mod);
			}
		}
	}
}

static void
_expose_mod(plughandle_t *handle, struct nk_context *ctx, struct nk_rect space_bounds,
	mod_t *mod, float dy)
{
	DBG;
	// we always show modules, even if port type does not match current view

	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	struct nk_input *in = &ctx->input;

	const LilvPlugin *plug = mod->plug;
	if(!plug)
		return;

	LilvNode *name_node = lilv_plugin_get_name(plug);
	if(!name_node)
		return;

	mod->dim.x = 150.f * handle->scale;
	mod->dim.y = handle->dy;

	const struct nk_vec2 scrolling = handle->scrolling;

	struct nk_rect bounds = nk_rect(
		mod->pos.x - mod->dim.x/2 - scrolling.x,
		mod->pos.y - mod->dim.y/2 - scrolling.y,
		mod->dim.x, mod->dim.y);

	const bool is_selectable = (bounds.x >= space_bounds.x)
		&& (bounds.y >= space_bounds.y)
		&& (bounds.x + bounds.w <= space_bounds.x + space_bounds.w)
		&& (bounds.y + bounds.h <= space_bounds.y + space_bounds.h);

	_mod_moveable(handle, ctx, mod, space_bounds, &bounds);

	const bool is_hovering = is_selectable && nk_input_is_mouse_hovering_rect(in, bounds);
	mod->hovered = is_hovering;
	const bool is_focused = handle->module_selector == mod;
	const bool is_hilighted = mod->hilighted || is_hovering;

	nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, bounds));

	struct nk_rect body;
	const enum nk_widget_layout_states states = nk_widget(&body, ctx);
	if(states != NK_WIDGET_INVALID)
	{
		struct nk_style_button *style = &ctx->style.button;
		const struct nk_user_font *font = ctx->style.font;

		struct nk_color hov = style->hover.data.color;
		struct nk_color brd = is_hilighted
			? hilight_color : style->border_color;

		if(!_mod_num_sources(mod, handle->type) && !_mod_num_sinks(mod, handle->type))
		{
			hov.a = 0x3f;
			brd.a = 0x5f;
		}

		nk_fill_rect(canvas, body, style->rounding, hov);
		if(mod->selected)
			nk_fill_rect(canvas, body, style->rounding, selection_color);
		nk_stroke_rect(canvas, body, style->rounding, style->border, brd);

		const float fh = font->height;
		const float fy = body.y + (body.h - fh)/2;
		{
			const char *mod_name = strlen(mod->alias)
				? mod->alias
				: lilv_node_as_string(name_node);
			const size_t mod_name_len = NK_MIN(strlen(mod_name), 24); //TODO limit to how many characters?
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
			snprintf(nums, sizeof(nums), "%u", nsources);

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
			snprintf(nums, sizeof(nums), "%u", nsinks);

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
			snprintf(load, sizeof(load), "%.1f | %.1f | %.1f %%",
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

		if(!_image_empty(&mod->idisp.img))
		{
			const float aspect = (float)mod->idisp.w / mod->idisp.h;
			float w;
			float h;

			if(aspect < 1.f)
			{
				w = mod->dim.x;
				h = w  / aspect;
			}
			else if(aspect > 1.f)
			{
				w = mod->dim.x;
				h = w  / aspect;
			}
			else // aspect == 1.f
			{
				w = mod->dim.x;
				h = mod->dim.x;
			}

			const struct nk_rect body2 = {
				.x = body.x + (body.w - w)/2,
				.y = fy - 0.5*fh - h,
				.w = w,
				.h = h
			};

			const bool is_prev_hov = nk_input_is_mouse_prev_hovering_rect(in, body2);
			const bool is_hov = nk_input_is_mouse_hovering_rect(in, body2);
			if(is_prev_hov != is_hov)
			{
				param_t *param = _mod_param_find_by_property(mod, handle->canvas.urid.Canvas_mouseFocus);
				if(param)
				{
					param->val.i = is_hov ? 1 : 0;
					_param_notification_add(handle, mod, param);
				}
			}

			if(is_hov)
			{
				if(nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT))
				{
					param_t *param = _mod_param_find_by_property(mod, handle->canvas.urid.Canvas_mouseButtonLeft);
					if(param)
					{
						param->val.i = 1;
						_param_notification_add(handle, mod, param);
					}
				}
				else if(nk_input_is_mouse_released(in, NK_BUTTON_LEFT))
				{
					param_t *param = _mod_param_find_by_property(mod, handle->canvas.urid.Canvas_mouseButtonLeft);
					if(param)
					{
						param->val.i = 0;
						_param_notification_add(handle, mod, param);
					}
				}

				if(nk_input_is_mouse_pressed(in, NK_BUTTON_MIDDLE))
				{
					param_t *param = _mod_param_find_by_property(mod, handle->canvas.urid.Canvas_mouseButtonMiddle);
					if(param)
					{
						param->val.i = 1;
						_param_notification_add(handle, mod, param);
					}
				}
				else if(nk_input_is_mouse_released(in, NK_BUTTON_MIDDLE))
				{
					param_t *param = _mod_param_find_by_property(mod, handle->canvas.urid.Canvas_mouseButtonMiddle);
					if(param)
					{
						param->val.i = 0;
						_param_notification_add(handle, mod, param);
					}
				}

				if(nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT))
				{
					param_t *param = _mod_param_find_by_property(mod, handle->canvas.urid.Canvas_mouseButtonRight);
					if(param)
					{
						param->val.i = 1;
						_param_notification_add(handle, mod, param);
					}
				}
				else if(nk_input_is_mouse_released(in, NK_BUTTON_RIGHT))
				{
					param_t *param = _mod_param_find_by_property(mod, handle->canvas.urid.Canvas_mouseButtonRight);
					if(param)
					{
						param->val.i = 0;
						_param_notification_add(handle, mod, param);
					}
				}

				if(in->mouse.delta.x != 0.f)
				{
					param_t *param = _mod_param_find_by_property(mod, handle->canvas.urid.Canvas_mousePositionX);
					if(param)
					{
						param->val.f = (in->mouse.pos.x - body2.x) / body2.w;
						_param_notification_add(handle, mod, param);
					}
				}

				if(in->mouse.delta.y != 0.f)
				{
					param_t *param = _mod_param_find_by_property(mod, handle->canvas.urid.Canvas_mousePositionY);
					if(param)
					{
						param->val.f = (in->mouse.pos.y - body2.y) / body2.h;
						_param_notification_add(handle, mod, param);
					}
				}

				if(in->mouse.scroll_delta.x != 0.f)
				{
					param_t *param = _mod_param_find_by_property(mod, handle->canvas.urid.Canvas_mouseWheelX);
					if(param)
					{
						param->val.f = in->mouse.scroll_delta.x;
						_param_notification_add(handle, mod, param);

						in->mouse.scroll_delta.x = 0.f; // eat event
					}
				}

				if(in->mouse.scroll_delta.y != 0.f)
				{
					param_t *param = _mod_param_find_by_property(mod, handle->canvas.urid.Canvas_mouseWheelY);
					if(param)
					{
						param->val.f = in->mouse.scroll_delta.y;
						_param_notification_add(handle, mod, param);

						in->mouse.scroll_delta.y = 0.f; // eat event
					}
				}
			}

			nk_draw_image(canvas, body2, &mod->idisp.img, nk_rgb(0xff, 0xff, 0xff));
			nk_stroke_rect(canvas, body2, style->rounding, style->border, style->border_color);
		}
	}

	_mod_connectors(handle, ctx, mod, nk_vec2(bounds.w, bounds.h), is_hilighted);
}

static void
_expose_mod_conn(plughandle_t *handle, struct nk_context *ctx, struct nk_rect space_bounds,
	mod_conn_t *mod_conn, float dy)
{
	DBG;
	mod_conn->hovering = false;

	if(!_mod_conn_num_connections(handle, mod_conn))
		return;

	const bool is_dial = (handle->type == PROPERTY_TYPE_AUDIO) || (handle->type == PROPERTY_TYPE_CV);

	struct nk_input *in = &ctx->input;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_vec2 scrolling = handle->scrolling;

	mod_t *src = mod_conn->source_mod;
	mod_t *snk = mod_conn->sink_mod;

	if(!src || !snk)
		return;

	const unsigned nx = _mod_num_sources(mod_conn->source_mod, handle->type);
	const unsigned ny = _mod_num_sinks(mod_conn->sink_mod, handle->type);

	const float ps = (is_dial ? 24.f : 16.f) * handle->scale;
	const float pw = nx * ps;
	const float ph = ny * ps;
	struct nk_rect bounds = nk_rect(
		mod_conn->pos.x - scrolling.x - pw/2,
		mod_conn->pos.y - scrolling.y - ph/2,
		pw, ph
	);

	const bool is_selectable = (bounds.x >= space_bounds.x)
		&& (bounds.y >= space_bounds.y)
		&& (bounds.x + bounds.w <= space_bounds.x + space_bounds.w)
		&& (bounds.y + bounds.h <= space_bounds.y + space_bounds.h);

	mod_conn->hovering = is_selectable && nk_input_is_mouse_hovering_rect(in, bounds);

	if(is_selectable && mod_conn->moving)
	{
		if(nk_input_is_mouse_released(in, NK_BUTTON_LEFT))
		{
			mod_conn->moving = false;
		}
		//XXX
		else if(!nk_input_is_mouse_down(in, NK_BUTTON_MIDDLE))
		{
			mod_conn->pos.x += in->mouse.delta.x;
			mod_conn->pos.y += in->mouse.delta.y;
			bounds.x += in->mouse.delta.x;
			bounds.y += in->mouse.delta.y;

			_patch_node_add(handle, mod_conn->source_mod, mod_conn->sink_mod, mod_conn->pos.x, mod_conn->pos.y);
		}
	}
	else if(mod_conn->hovering
		&& nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT) )
	{
		const bool selected = mod_conn->selected;

		if(!nk_input_is_key_down(in, NK_KEY_SHIFT))
			_set_selected_nodes(handle, false);

		mod_conn->selected = !selected;
		_set_module_selector(handle, NULL);
	}

	const bool is_hilighted = mod_conn->source_mod->hovered
		|| mod_conn->sink_mod->hovered || mod_conn->hovering;

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
		struct nk_color col = is_hilighted
			? hilight_color : grab_handle_color;

		const float l0x = src->pos.x - scrolling.x + src->dim.x/2 + cs*2;
		const float l0y = src->pos.y - scrolling.y;
		const float l1x = snk->pos.x - scrolling.x - snk->dim.x/2 - cs*2;
		const float l1y = snk->pos.y - scrolling.y;

		const float bend = SPLINE_BEND * handle->scale;
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

		const struct nk_color hov = style->normal.data.color;
		const struct nk_color brd = is_hilighted
			? hilight_color : style->border_color;

		nk_fill_rect(canvas, body, style->rounding, hov);
		if(mod_conn->selected)
			nk_fill_rect(canvas, body, style->rounding, selection_color);

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

		nk_stroke_rect(canvas, body, style->rounding, style->border, brd);

		float x = body.x + ps/2;
		HASH_FOREACH(&mod_conn->source_mod->sources, source_port_itr)
		{
			port_t *source_port = *source_port_itr;

			if(!_source_type_match(handle, source_port))
				continue;

			float y = body.y + ps/2;
			HASH_FOREACH(&mod_conn->sink_mod->sinks, sink_port_itr)
			{
				port_t *sink_port = *sink_port_itr;

				if(!_sink_type_match(handle, sink_port))
					continue;

				const struct nk_rect tile = nk_rect(x - ps/2, y - ps/2, ps, ps);
				port_conn_t *port_conn = _port_conn_find(mod_conn, source_port, sink_port);

				if(  is_selectable
					&& nk_input_is_mouse_hovering_rect(in, tile)
					&& !mod_conn->moving)
				{
					const char *source_name = source_port->name;
					const char *sink_name = sink_port->name;

					if(!handle->mods_moving && source_name && sink_name)
					{
						if(nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT))
						{
							if(port_conn)
							{
								_patch_connection_remove(handle, source_port, sink_port);
							}
							else
							{
								_patch_connection_add(handle, source_port, sink_port, 1.f);
							}
						}
						else if(in->mouse.scroll_delta.y != 0.f) // has scrolling
						{
							float multiplier = 100.f;
							if(nk_input_is_key_down(in, NK_KEY_SHIFT))
								multiplier *= 0.1f;

							const float dd = in->mouse.scroll_delta.y * multiplier;
							in->mouse.scroll_delta.y = 0.f;

							if(is_dial)
							{
								if(port_conn)
								{
									float dBFS = 20.f * log10f(port_conn->gain);
									int mBFS = dBFS * 100.f;
									mBFS = NK_CLAMP(-3600, mBFS + dd, 3600);
									dBFS = mBFS / 100.f;
									port_conn->gain = exp10f(dBFS / 20.f);

									if(dBFS <= -36.f)
									{
										_patch_connection_remove(handle, source_port, sink_port);
									}
									else
									{
										_patch_connection_add(handle, source_port, sink_port, port_conn->gain);
									}
								}
								else if(dd > 0.f)
								{
									const int mBFS = NK_CLAMP(-3600, -3600 + dd, 3600);
									const float dBFS = mBFS / 100.f;
									const float gain = exp10f(dBFS / 20.f);

									_patch_connection_add(handle, source_port, sink_port, gain);
								}
							}
							else // !is_dial
							{
								if(port_conn)
									_patch_connection_remove(handle, source_port, sink_port);
								else
									_patch_connection_add(handle, source_port, sink_port, 1.f);
							}
						}

						if(  !nk_input_is_mouse_down(in, NK_BUTTON_LEFT)
							&& !nk_input_is_mouse_down(in, NK_BUTTON_MIDDLE)
							&& !nk_input_is_mouse_down(in, NK_BUTTON_RIGHT) )
						{
							char tmp [128];
							if(is_dial && port_conn)
							{
								const float dBFS = 20.f * log10f(port_conn->gain); //FIXME remove duplication
								snprintf(tmp, sizeof(tmp), "%s || %s || %+02.1f dBFS", source_name, sink_name, dBFS);
							}
							else
							{
								snprintf(tmp, sizeof(tmp), "%s || %s", source_name, sink_name);
							}

							const size_t tmp_len = strlen(tmp);

							const struct nk_user_font *font = ctx->style.font;

							const float fh = font->height;
							const float fy = body.y + body.h + fh/2;
							const float fw = font->width(font->userdata, font->height, tmp, tmp_len);
							const struct nk_rect body2 = {
								.x = body.x + (body.w - fw)/2,
								.y = fy,
								.w = fw,
								.h = fh
							};
							nk_draw_text(canvas, body2, tmp, tmp_len, font,
								style->normal.data.color, style->text_normal);
						}
					}
				}

				if(port_conn)
				{
					if(is_dial)
					{
						const float dBFS = 20.f * log10f(port_conn->gain); //FIXME remove duplication
						const float alpha = (dBFS + 36.f) / 72.f;
						const float beta = NK_PI/2;

						nk_stroke_arc(canvas,
							x, y, 10.f * handle->scale,
							beta + 0.2f*NK_PI, beta + 1.8f*NK_PI,
							1.f,
							style->border_color);
						nk_stroke_arc(canvas,
							x, y, 7.f * handle->scale,
							beta + 0.2f*NK_PI, beta + (0.2f + alpha*1.6f)*NK_PI,
							2.f,
							toggle_color);
					}
					else // !is_dial
					{
						nk_fill_arc(canvas, x, y, cs, 0.f, 2*NK_PI, toggle_color);
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
	DBG;
	*bb = nk_widget_bounds(ctx);

	return nk_group_begin(ctx, title, flags);
}

static inline void
_group_end(struct nk_context *ctx, struct nk_rect *bb)
{
	DBG;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	struct nk_style *style = &ctx->style;

	nk_group_end(ctx);
	nk_stroke_rect(canvas, *bb, 0.f, style->window.group_border, style->window.group_border_color);
}

static inline int
_osc_path_filter(const struct nk_text_edit *box, nk_rune unicode)
{
	DBG;
	if(unicode == '#')
		return nk_false;
	else if(unicode == ' ')
		return nk_false;

	return nk_true;
}

static inline void
_control_randomize(plughandle_t *handle, mod_t *mod, control_port_t *control)
{
	DBG;
	const float rnd = (float)rand() / RAND_MAX;

	if(control->is_bool)
		control->val.i = rnd > 0.5f ? 1 : 0;
	else if(control->is_int)
		control->val.i = control->min.i + control->span.i*rnd;
	else
		control->val.f = control->min.f + control->span.f*rnd;
}

static inline void
_param_randomize(plughandle_t *handle, mod_t *mod, param_t *param)
{
	DBG;
	const float rnd = (float)rand() / RAND_MAX;

	if(param->range == handle->forge.Bool)
		param->val.i = rnd > 0.5f ? 1 : 0;
	else if(param->range == handle->forge.Int)
		param->val.i = param->min.i + param->span.i*rnd;
	else if(param->range == handle->forge.Long)
		param->val.h = param->min.h + param->span.h*rnd;
	else if(param->range == handle->forge.Float)
		param->val.f = param->min.f + param->span.f*rnd;
	else if(param->range == handle->forge.Double)
		param->val.d = param->min.d + param->span.d*rnd;
	//FIXME handle other types
}

static inline void
_mod_randomize(plughandle_t *handle, mod_t *mod)
{
	DBG;
	HASH_FOREACH(&mod->ports, port_itr)
	{
		port_t *port = *port_itr;

		if(port->type == PROPERTY_TYPE_CONTROL)
		{
			_control_randomize(handle, mod, &port->control);

			const float val = port->control.is_bool || port->control.is_int
				? port->control.val.i
				: port->control.val.f;
			_patch_notification_add(handle, port, handle->regs.port.float_protocol.urid,
				sizeof(float), handle->forge.Float, &val);
		}
	}

	HASH_FOREACH(&mod->params, param_itr)
	{
		param_t *param = *param_itr;

		_param_randomize(handle, mod, param);
		_param_notification_add(handle, mod, param);
	}

	HASH_FOREACH(&mod->dynams, param_itr)
	{
		param_t *param = *param_itr;

		_param_randomize(handle, mod, param);
		_param_notification_add(handle, mod, param);
	}
}

static void
_expose_main_body(plughandle_t *handle, struct nk_context *ctx, float dh, float dy)
{
	DBG;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	struct nk_style *style = &ctx->style;
	struct nk_input *in = &ctx->input;

	handle->bundle_find_matches = false;
	handle->plugin_find_matches = false;
	handle->preset_find_matches = false;
	handle->prop_find_matches = false;

	const struct nk_rect total_space = nk_window_get_content_region(ctx);
	const float vertical = total_space.h
		- handle->dy
		- 2*style->window.group_padding.y;
	const float upper_h = vertical * (handle->show_bottombar ? 0.65f : 1.f);
	const float lower_h = vertical * 0.35f
		- 4*style->window.group_padding.y;

	const int upper_ratio_n = handle->show_sidebar ? 2 : 1;
	const float upper_ratio [2] = {handle->show_sidebar ? 0.8f : 1.f, 0.2f};
	nk_layout_row(ctx, NK_DYNAMIC, vertical, upper_ratio_n, upper_ratio);

	struct nk_rect bb;
	if(nk_group_begin(ctx, "left", NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_space_begin(ctx, NK_STATIC, upper_h,
			_hash_size(&handle->mods) + _hash_size(&handle->conns));
		{
			const struct nk_rect old_clip = canvas->clip;
			handle->space_bounds= nk_layout_space_bounds(ctx);
			nk_push_scissor(canvas, handle->space_bounds);

			// graph content scrolling
			if(nk_input_has_mouse_click_down_in_rect(in, NK_BUTTON_MIDDLE, handle->space_bounds, nk_true))
			{
				handle->scrolling.x -= in->mouse.delta.x;
				handle->scrolling.y -= in->mouse.delta.y;

				const LV2_URID subj = 0; // aka host

				if(  _message_request(handle)
					&&  synthpod_patcher_set(&handle->regs, &handle->forge,
						subj, 0, handle->regs.synthpod.graph_position_x.urid,
						sizeof(float), handle->forge.Float, &handle->scrolling.x) )
				{
					_message_write(handle);
				}

				if(  _message_request(handle)
					&&  synthpod_patcher_set(&handle->regs, &handle->forge,
						subj, 0, handle->regs.synthpod.graph_position_y.urid,
						sizeof(float), handle->forge.Float, &handle->scrolling.y) )
				{
					_message_write(handle);
				}
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

			if(nk_input_is_mouse_hovering_rect(in, handle->space_bounds))
			{
				if(in->keyboard.text_len == 1)
				{
					switch(in->keyboard.text[0])
					{
						case 'a':
						{
							const bool has_selected_nodes = _has_selected_nodes(handle);
							_set_selected_nodes(handle, !has_selected_nodes);
							if(has_selected_nodes)
								_set_module_selector(handle, NULL);
						}	break;
						case 'b':
						{
							handle->box.flag = true;
						}	break;
						case 'g':
						{
							_set_moving_nodes(handle, true);
						} break;
						case 'v':
						{
							_show_selected_nodes(handle);
						} break;
						case 'x':
						{
							_remove_selected_nodes(handle);
						} break;
						case 'i':
						{
							_reinstantiate_selected_nodes(handle);
						} break;
					}

					in->keyboard.text_len = 0; // consume character if mouse over canvas
				}

				if(nk_input_is_key_pressed(in, NK_KEY_TEXT_RESET_MODE)) // Esape
				{
					handle->box.flag = false; // exit box drawing mode

					_set_moving_nodes(handle, false); // exit node moving mode
				}
			}

			if(  nk_input_is_mouse_hovering_rect(in, handle->space_bounds)
				&& handle->box.flag)
			{
				if(nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT))
				{
					handle->box.from = in->mouse.pos;
				}
				else if(nk_input_is_mouse_released(in, NK_BUTTON_LEFT))
				{
					const struct nk_vec2 to = in->mouse.pos;

					const struct nk_rect box = {
						handle->scrolling.x + NK_MIN(to.x, handle->box.from.x),
						handle->scrolling.y + NK_MIN(to.y, handle->box.from.y),
						fabsf(to.x - handle->box.from.x),
						fabsf(to.y - handle->box.from.y)
					};

					_set_selected_box(handle, box, true);

					handle->box.flag = false;
				}
				else if(nk_input_is_mouse_down(in, NK_BUTTON_LEFT))
				{
					const struct nk_vec2 to = in->mouse.pos;

					const struct nk_rect box = {
						NK_MIN(to.x, handle->box.from.x),
						NK_MIN(to.y, handle->box.from.y),
						fabsf(to.x - handle->box.from.x),
						fabsf(to.y - handle->box.from.y)
					};

					nk_stroke_rect(canvas, box, 0.f, ctx->style.property.border, hilight_color);
				}
				else
				{
					const float x0 = handle->space_bounds.x;
					const float x1 = in->mouse.pos.x;
					const float x2 = handle->space_bounds.x + handle->space_bounds.w;
					const float y0 = handle->space_bounds.y;
					const float y1 = in->mouse.pos.y;
					const float y2 = handle->space_bounds.y + handle->space_bounds.h;

					nk_stroke_line(canvas, x1, y0, x1, y2, ctx->style.property.border, hilight_color);
					nk_stroke_line(canvas, x0, y1, x2, y1, ctx->style.property.border, hilight_color);
				}
			}

			handle->mods_moving = 0;

			HASH_FOREACH(&handle->mods, mod_itr)
			{
				mod_t *mod = *mod_itr;

				_expose_mod(handle, ctx, handle->space_bounds, mod, dy);

				mod->hilighted = false;
			}

			HASH_FOREACH(&handle->conns, mod_conn_itr)
			{
				mod_conn_t *mod_conn = *mod_conn_itr;

				_expose_mod_conn(handle, ctx, handle->space_bounds, mod_conn, dy);
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

		if(handle->show_bottombar)
		{
			nk_layout_row_dynamic(ctx, lower_h, 4);

			if(_group_begin(ctx, "Bundles", NK_WINDOW_TITLE, &bb))
			{
				nk_menubar_begin(ctx);
				{
					const float dim [2] = {0.6, 0.4};
					nk_layout_row(ctx, NK_DYNAMIC, dy, 2, dim);

					const size_t old_len = _textedit_len(&handle->bundle_search_edit);
					const nk_flags args = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT;
					if(!handle->has_initial_focus)
					{
						nk_edit_focus(ctx, args);
						handle->has_initial_focus = true;
					}
					const nk_flags flags = nk_edit_buffer(ctx, args, &handle->bundle_search_edit, nk_filter_default);
					_textedit_zero_terminate(&handle->bundle_search_edit);
					if( (flags & NK_EDIT_COMMITED) || (old_len != _textedit_len(&handle->bundle_search_edit)) )
						handle->bundle_find_matches = true;
					if( (flags & NK_EDIT_ACTIVE) && handle->has_control_a)
						nk_textedit_select_all(&handle->bundle_search_edit);

					const bundle_selector_search_t old_sel = handle->bundle_search_selector;
					handle->bundle_search_selector = nk_combo(ctx, bundle_search_labels, BUNDLE_SELECTOR_SEARCH_MAX,
						handle->bundle_search_selector, dy, nk_vec2(nk_widget_width(ctx), 7*dy));
					if(old_sel != handle->bundle_search_selector)
						handle->bundle_find_matches = true;
				}
				nk_menubar_end(ctx);

				nk_layout_row_dynamic(ctx, handle->dy2, 1);
				nk_spacing(ctx, 1); // fixes mouse-over bug
				_expose_main_bundle_list(handle, ctx, handle->bundle_find_matches);

				_group_end(ctx, &bb);
			}

			if(_group_begin(ctx, "Plugins", NK_WINDOW_TITLE, &bb))
			{
				nk_menubar_begin(ctx);
				{
					const float dim [2] = {0.6, 0.4};
					nk_layout_row(ctx, NK_DYNAMIC, dy, 2, dim);

					const size_t old_len = _textedit_len(&handle->plugin_search_edit);
					const nk_flags args = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT;
					const nk_flags flags = nk_edit_buffer(ctx, args, &handle->plugin_search_edit, nk_filter_default);
					_textedit_zero_terminate(&handle->plugin_search_edit);
					if( (flags & NK_EDIT_COMMITED) || (old_len != _textedit_len(&handle->plugin_search_edit)) )
						handle->plugin_find_matches = true;
					if( (flags & NK_EDIT_ACTIVE) && handle->has_control_a)
						nk_textedit_select_all(&handle->plugin_search_edit);

					const plugin_selector_search_t old_sel = handle->plugin_search_selector;
					handle->plugin_search_selector = nk_combo(ctx, plugin_search_labels, PLUGIN_SELECTOR_SEARCH_MAX,
						handle->plugin_search_selector, dy, nk_vec2(nk_widget_width(ctx), 7*dy));
					if(old_sel != handle->plugin_search_selector)
						handle->plugin_find_matches = true;
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
					const float dim [2] = {0.6, 0.4};
					nk_layout_row(ctx, NK_DYNAMIC, dy, 2, dim);

					const size_t old_len = _textedit_len(&handle->preset_search_edit);
					const nk_flags args = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT;
					const nk_flags flags = nk_edit_buffer(ctx, args, &handle->preset_search_edit, nk_filter_default);
					_textedit_zero_terminate(&handle->preset_search_edit);
					if(flags & NK_EDIT_COMMITED)
						_patch_mod_preset_save(handle);
					if(old_len != _textedit_len(&handle->preset_search_edit))
						handle->preset_find_matches = true;
					if( (flags & NK_EDIT_ACTIVE) && handle->has_control_a)
						nk_textedit_select_all(&handle->preset_search_edit);

					const preset_selector_search_t old_sel = handle->preset_search_selector;
					handle->preset_search_selector = nk_combo(ctx, preset_search_labels, PRESET_SELECTOR_SEARCH_MAX,
						handle->preset_search_selector, dy, nk_vec2(nk_widget_width(ctx), 7*dy));
					if(old_sel != handle->preset_search_selector)
						handle->preset_find_matches = true;
				}
				nk_menubar_end(ctx);

				_expose_main_preset_list(handle, ctx, handle->preset_find_matches);

#if 0
				_expose_main_preset_info(handle, ctx);
#endif
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

					bool is_readonly = false;

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
						is_readonly = control->is_readonly;
					}
					else if(param)
					{
						automation = &param->automation;
						c = _param_union_as_double(&handle->forge, param->range, &param->min);
						d = _param_union_as_double(&handle->forge, param->range, &param->max);
						is_readonly = param->is_readonly;
					}

					if(automation)
					{
						auto_t old_auto = *automation;

						nk_menubar_begin(ctx);
						{
							nk_layout_row_dynamic(ctx, dy, 1);

							const auto_type_t auto_type = automation->type;
							automation->type = nk_combo(ctx, auto_labels, AUTO_MAX,
								automation->type, dy, nk_vec2(nk_widget_width(ctx), dy*5));
							if(auto_type != automation->type)
							{
								const int32_t src_enabled = is_readonly;
								const int32_t snk_enabled = !is_readonly;
								const int32_t learning = false;

								if(automation->type == AUTO_MIDI)
								{
									// initialize
									automation->midi.channel = -1;
									automation->midi.controller = -1;
									automation->midi.a = 0x0;
									automation->midi.b = 0x7f;
									automation->c = c;
									automation->d = d;
									automation->src_enabled = src_enabled;
									automation->snk_enabled = snk_enabled;
									automation->learning = learning;
								}
								else if(automation->type == AUTO_OSC)
								{
									// initialize
									const char *label = "";
									if(port)
										label = port->symbol;
									else if(param)
										label = param->label;

									LilvNode *name_node = lilv_plugin_get_name(mod->plug);
									const char *mod_name = lilv_node_as_string(name_node);

									snprintf(automation->osc.path, sizeof(automation->osc.path), "/%s/%s", mod_name, label);
									for(unsigned i = 0; automation->osc.path[i]; i++)
									{
										switch(automation->osc.path[i])
										{
											case ' ':
											case '#':
												automation->osc.path[i] = '_';
												break;
											default:
												automation->osc.path[i] = tolower(automation->osc.path[i]);
												break;
										}
									}

									automation->osc.a = 0.0;
									automation->osc.b = 1.0;
									automation->c = c;
									automation->d = d;
									automation->src_enabled = src_enabled;
									automation->snk_enabled = snk_enabled;
									automation->learning = learning;
								}
							}
						}
						nk_menubar_end(ctx);

						if(automation->type == AUTO_MIDI)
						{
							const double inc = 1.0; //FIXME
							const float ipp = 1.f; //FIXME

							nk_layout_row_dynamic(ctx, dy, 6);

							if(  _dial_bool(ctx, &automation->learning, nk_rgb(0xff, 0xff, 0xff), true)
								&& automation->learning)
							{
								// reset channel and controller when switching to learning mode
								automation->midi.channel = -1;
								automation->midi.controller = -1;
							}
								nk_label(ctx, "Learn", NK_TEXT_LEFT);
							_dial_bool(ctx, &automation->snk_enabled, nk_rgb(0xff, 0xff, 0xff), true);
								nk_label(ctx, "Input", NK_TEXT_LEFT);
							_dial_bool(ctx, &automation->src_enabled, nk_rgb(0xff, 0xff, 0xff), true);
								nk_label(ctx, "Output", NK_TEXT_LEFT);

							nk_layout_row_dynamic(ctx, dy, 1);
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
							const double inc = 1.0; //FIXME
							const float ipp = 1.f; //FIXME

							nk_layout_row_dynamic(ctx, dy, 6);
							if(  _dial_bool(ctx, &automation->learning, nk_rgb(0xff, 0xff, 0xff), true)
								&& automation->learning)
							{
								// reset path
								automation->osc.path[0] = '\0';
							}
								nk_label(ctx, "Learn", NK_TEXT_LEFT);
							_dial_bool(ctx, &automation->snk_enabled, nk_rgb(0xff, 0xff, 0xff), true);
								nk_label(ctx, "Input", NK_TEXT_LEFT);
							_dial_bool(ctx, &automation->src_enabled, nk_rgb(0xff, 0xff, 0xff), true);
								nk_label(ctx, "Output", NK_TEXT_LEFT);

							nk_layout_row_dynamic(ctx, dy, 1);
							const nk_flags res = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD,
								automation->osc.path, 128, _osc_path_filter);
							(void)res;

							nk_property_double(ctx, "OSC Minimum", 0.0, &automation->osc.a, 1.0, inc, ipp);
							nk_property_double(ctx, "OSC Maximum", 0.0, &automation->osc.b, 1.0, inc, ipp);
							nk_spacing(ctx, 1);
							nk_property_double(ctx, "Target Minimum", c, &automation->c, d, inc, ipp);
							nk_property_double(ctx, "Target Maximum", c, &automation->d, d, inc, ipp);
						}

						if(memcmp(&old_auto, automation, sizeof(auto_t))) // needs sync
						{
							//printf("automation needs sync\n");
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
								if(port)
									_patch_port_osc_automation_add(handle, port, automation);
								else if(param)
									_patch_param_osc_automation_add(handle, param, automation);
							}
						}
					}
				}

				_group_end(ctx, &bb);
			}
		}

		nk_group_end(ctx);
	}

	if(handle->show_sidebar)
	{
		if(_group_begin(ctx, "Controls", NK_WINDOW_TITLE, &bb))
		{
			nk_menubar_begin(ctx);
			{
				const nk_flags args = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT;

				{
					const float dim [2] = {0.6, 0.4};
					nk_layout_row(ctx, NK_DYNAMIC, dy, 2, dim);

					const size_t old_len = _textedit_len(&handle->port_search_edit);
					const nk_flags flags = nk_edit_buffer(ctx, args, &handle->port_search_edit, nk_filter_default);
					_textedit_zero_terminate(&handle->port_search_edit);
					if( (flags & NK_EDIT_COMMITED) || (old_len != _textedit_len(&handle->port_search_edit)) )
						handle->prop_find_matches = true;
					if( (flags & NK_EDIT_ACTIVE) && handle->has_control_a)
						nk_textedit_select_all(&handle->port_search_edit);

					const property_selector_search_t old_sel = handle->property_search_selector;
					handle->property_search_selector = nk_combo(ctx, property_search_labels, PROPERTY_SELECTOR_SEARCH_MAX,
						handle->property_search_selector, dy, nk_vec2(nk_widget_width(ctx), 7*dy));
					if(old_sel != handle->property_search_selector)
						handle->prop_find_matches = true;
				}

				mod_t *mod = handle->module_selector;
				if(mod)
				{
					{
						const float dim [2] = {0.1, 0.9};
						nk_layout_row(ctx, NK_DYNAMIC, dy, 2, dim);

						nk_label(ctx, "Alias", NK_TEXT_LEFT);

						const size_t old_len = strlen(mod->alias);
						const nk_flags flags = nk_edit_string_zero_terminated(ctx, args, mod->alias, ALIAS_MAX, nk_filter_default);
						if( (flags & NK_EDIT_COMMITED) || (old_len != strlen(mod->alias)) )
						{
							if(  _message_request(handle)
								&& synthpod_patcher_set(&handle->regs, &handle->forge,
									mod->urn, 0, handle->regs.synthpod.module_alias.urid,
									strlen(mod->alias) + 1, handle->forge.String, mod->alias) )
							{
								_message_write(handle);
							}
						}
						//FIXME implement select-all
					}

					const unsigned nuis = _hash_size(&mod->uis);
					if(nuis)
					{
						nk_layout_row_dynamic(ctx, dy, nuis);

						const bool single_ui = _hash_size(&mod->uis) == 1;

						HASH_FOREACH(&mod->uis, mod_ui_itr)
						{
							mod_ui_t *mod_ui = *mod_ui_itr;
							const LilvUI *ui = mod_ui->ui;
							const LilvNode *ui_node = lilv_ui_get_uri(ui);

							const bool is_running = _mod_ui_is_running(mod_ui);
							const char *label = "Show plugin GUI";

							if(!single_ui)
							{
								if(lilv_ui_is_a(ui, handle->regs.ui.x11.node))
									label = "X11";
								else if(lilv_ui_is_a(ui, handle->regs.ui.gtk2.node))
									label = "Gtk2";
								else if(lilv_ui_is_a(ui, handle->regs.ui.gtk3.node))
									label = "Gtk3";
								else if(lilv_ui_is_a(ui, handle->regs.ui.qt4.node))
									label = "Qt4";
								else if(lilv_ui_is_a(ui, handle->regs.ui.qt5.node))
									label = "Qt5";
								else if(lilv_ui_is_a(ui, handle->regs.ui.kx_widget.node))
									label = "KX";
								else if(lilv_world_ask(handle->world, ui_node, handle->regs.core.extension_data.node, handle->regs.ui.show_interface.node))
									label = "Show";
							}

							const bool is_still_running = _toolbar_label(ctx, is_running, 0x0, label);
							if(is_still_running != is_running)
							{
								if(is_running)
									_mod_ui_stop(mod_ui, true); // stop existing UI
								else
									_mod_ui_run(mod_ui, true); // run UI
							}
						}
					}

					{
						nk_layout_row_dynamic(ctx, dy, 1);
						if(nk_button_label(ctx, "Randomize"))
						{
							_mod_randomize(handle, mod);
						}
					}
				}
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
	}
}

static void
_expose_main_footer(plughandle_t *handle, struct nk_context *ctx, float dy)
{
	DBG;
	nk_layout_row_dynamic(ctx, dy, 6);
	{
		time_t t1;
		time(&t1);

		const float khz = handle->sample_rate * 1e-3;
		const float ms = handle->period_size *handle->num_periods / khz;
		nk_labelf(ctx, NK_TEXT_LEFT, "DEV: %"PRIi32" x %"PRIi32" @ %.1f kHz (%.2f ms)",
			handle->period_size, handle->num_periods, khz, ms);

		nk_labelf(ctx, NK_TEXT_LEFT, "DSP: %.1f | %.1f | %.1f %%",
			handle->prof.min, handle->prof.avg, handle->prof.max);

		nk_labelf(ctx, NK_TEXT_LEFT, "CPU: %"PRIi32" / %"PRIi32,
			handle->cpus_used, handle->cpus_available);

		if(nk_widget_is_mouse_clicked(ctx, NK_BUTTON_LEFT))
			handle->t0 = t1;
		const uint32_t secs = difftime(t1, handle->t0);
		const int32_t ts = secs % 60;
		const int32_t _tm = secs / 60;
		const int32_t tm = _tm % 60;
		const int32_t th = _tm / 60;
		nk_labelf(ctx, NK_TEXT_LEFT, "TIM: %02"PRIu32":%02"PRIu32":%02"PRIu32, th, tm, ts);

		char buf [32];
		struct tm *ti = localtime(&t1);
		strftime(buf, 32, "%F | %T", ti);
		nk_label(ctx, buf, NK_TEXT_LEFT);

		nk_label(ctx, "Synthpod: "SYNTHPOD_VERSION, NK_TEXT_RIGHT);
	}
}

static void
_init(plughandle_t *handle)
{
	DBG;
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
	LilvNode *synthpod_bundle = lilv_new_file_uri(handle->world, NULL, SYNTHPOD_BUNDLE_DIR);
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
	handle->node.osc_Event = lilv_new_uri(handle->world, LV2_OSC__Event);
	handle->node.time_Position = lilv_new_uri(handle->world, LV2_TIME__Position);
	handle->node.patch_Message = lilv_new_uri(handle->world, LV2_PATCH__Message);
	handle->node.xpress_Message = lilv_new_uri(handle->world, XPRESS_PREFIX"Message");

	_discover_bundles(handle);

	sp_regs_init(&handle->regs, handle->world, handle->map);

	// patch:Get [patch:property spod:moduleList]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.module_list.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:CPUsAvailable]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.cpus_available.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:CPUsUsed]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.cpus_used.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:periodSize]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.period_size.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:numPeriods]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.num_periods.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:graphPositionX]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.graph_position_x.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:graphPositionY]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.graph_position_y.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:columnEnabled]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.column_enabled.urid) )
	{
		_message_write(handle);
	}

	// patch:Get [patch:property spod:rowEnabled]
	if(  _message_request(handle)
		&& synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.row_enabled.urid) )
	{
		_message_write(handle);
	}
}

static void
_deinit(plughandle_t *handle)
{
	DBG;
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

		_undiscover_bundles(handle);

		lilv_world_free(handle->world);
	}
}

static void
_expose(struct nk_context *ctx, struct nk_rect wbounds, void *data)
{
	DBG;
	plughandle_t *handle = data;
	const struct nk_user_font *font = ctx->style.font;
	const struct nk_style *style = &handle->win.ctx.style;

	handle->scale = nk_pugl_get_scale(&handle->win);
	handle->dy = 20.f * handle->scale;
	handle->dy2 = font->height + 2 * style->window.header.label_padding.y;

	handle->has_control_a = nk_pugl_is_shortcut_pressed(&ctx->input, 'a', true);

	const char *window_name = "synthpod";
	if(nk_begin(ctx, window_name, wbounds, NK_WINDOW_NO_SCROLLBAR))
	{
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

static struct nk_image
_icon_load(plughandle_t *handle, const char *bundle_path, const char *file)
{
	DBG;
	struct nk_image img;
	char *path;
	if(asprintf(&path, "%s%s", bundle_path, file) != -1)
	{
		img = nk_pugl_icon_load(&handle->win, path);
		free(path);
	}
	else
	{
		img = nk_image_id(0);
	}

	return img;
}

static void
_icon_unload(plughandle_t *handle, struct nk_image img)
{
	DBG;
	if(img.handle.id != 0)
	{
		nk_pugl_icon_unload(&handle->win, img);
	}
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor, const char *plugin_uri,
	const char *bundle_path, LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	DBG;
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	void *parent = NULL;
	LV2UI_Resize *host_resize = NULL;
	LV2_Options_Option *opts = NULL;
	xpress_map_t *voice_map = NULL;

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
		else if(!strcmp(features[i]->URI, XPRESS__voiceMap))
			voice_map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_OPTIONS__options))
			opts = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
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

	xpress_init(&handle->xpress, 0, handle->map, voice_map,
		XPRESS_EVENT_NONE, NULL, NULL, NULL); //FIXME use xpress_map()

	if(handle->log)
		lv2_log_logger_init(&handle->logger, handle->map, handle->log);

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

	if(asprintf(&cfg->font.face, "%sAbel-Regular.ttf", bundle_path) == -1)
		cfg->font.face = NULL;
	cfg->font.size = 15;

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
	//
	struct nk_style_button *bst = &handle->win.ctx.style.button;
	bst->image_padding.x = -1;
	bst->image_padding.y = -1;
	bst->text_hover = nk_rgb(0xff, 0xff, 0xff);
	bst->text_active = nk_rgb(0xff, 0xff, 0xff);

	handle->scale = nk_pugl_get_scale(&handle->win);

	handle->scrolling = nk_vec2(0.f, 0.f);

	handle->plugin_collapse_states = NK_MAXIMIZED;
	handle->preset_import_collapse_states = NK_MAXIMIZED;
	handle->preset_export_collapse_states = NK_MINIMIZED;
	handle->plugin_info_collapse_states = NK_MINIMIZED;
	handle->preset_info_collapse_states = NK_MINIMIZED;

	nk_textedit_init_fixed(&handle->bundle_search_edit, handle->bundle_search_buf, SEARCH_BUF_MAX);
	nk_textedit_init_fixed(&handle->plugin_search_edit, handle->plugin_search_buf, SEARCH_BUF_MAX);
	nk_textedit_init_fixed(&handle->preset_search_edit, handle->preset_search_buf, SEARCH_BUF_MAX);
	nk_textedit_init_fixed(&handle->port_search_edit, handle->port_search_buf, SEARCH_BUF_MAX);
	nk_textedit_init_fixed(&handle->mod_alias_edit, handle->mod_alias_buf, ALIAS_MAX);

	handle->first = true;

	handle->type = PROPERTY_TYPE_AUDIO; //FIXME make configurable

	handle->icon.atom = _icon_load(handle, bundle_path, "atom.png");
	handle->icon.audio = _icon_load(handle, bundle_path, "audio.png");
	handle->icon.control = _icon_load(handle, bundle_path, "control.png");
	handle->icon.cv = _icon_load(handle, bundle_path, "cv.png");
	handle->icon.midi = _icon_load(handle, bundle_path, "midi.png");
	handle->icon.osc = _icon_load(handle, bundle_path, "osc.png");
	handle->icon.patch = _icon_load(handle, bundle_path, "patch.png");
	handle->icon.time = _icon_load(handle, bundle_path, "time.png");
	handle->icon.xpress = _icon_load(handle, bundle_path, "xpress.png");
	handle->icon.automaton = _icon_load(handle, bundle_path, "automaton.png");
	handle->icon.plus = _icon_load(handle, bundle_path, "plus.png");
	handle->icon.download = _icon_load(handle, bundle_path, "download.png");
	handle->icon.cancel = _icon_load(handle, bundle_path, "cancel.png");
	handle->icon.house = _icon_load(handle, bundle_path, "house.png");
	handle->icon.layers = _icon_load(handle, bundle_path, "layers.png");
	handle->icon.user = _icon_load(handle, bundle_path, "user.png");
	handle->icon.settings = _icon_load(handle, bundle_path, "settings.png");
	handle->icon.menu = _icon_load(handle, bundle_path, "menu.png");

	handle->show_sidebar = 1;
	handle->show_bottombar = 1;

	time(&handle->t0);
	srand(time(NULL));

#if defined(USE_CAIRO_CANVAS)
	lv2_canvas_init(&handle->canvas, handle->map);
#endif

	handle->supports_x11 = _check_support_for_ui("synthpod_sandbox_x11");
	handle->supports_gtk2 = _check_support_for_ui("synthpod_sandbox_gtk2");
	handle->supports_gtk3= _check_support_for_ui("synthpod_sandbox_gtk3");
	handle->supports_qt4 = _check_support_for_ui("synthpod_sandbox_qt4");
	handle->supports_qt5 = _check_support_for_ui("synthpod_sandbox_qt5");
	handle->supports_kx = _check_support_for_ui("synthpod_sandbox_kx");
	handle->supports_show = _check_support_for_ui("synthpod_sandbox_show");

	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	DBG;
	plughandle_t *handle = instance;

	_icon_unload(handle, handle->icon.atom);
	_icon_unload(handle, handle->icon.audio);
	_icon_unload(handle, handle->icon.control);
	_icon_unload(handle, handle->icon.cv);
	_icon_unload(handle, handle->icon.midi);
	_icon_unload(handle, handle->icon.osc);
	_icon_unload(handle, handle->icon.patch);
	_icon_unload(handle, handle->icon.time);
	_icon_unload(handle, handle->icon.xpress);
	_icon_unload(handle, handle->icon.automaton);
	_icon_unload(handle, handle->icon.plus);
	_icon_unload(handle, handle->icon.download);
	_icon_unload(handle, handle->icon.cancel);
	_icon_unload(handle, handle->icon.house);
	_icon_unload(handle, handle->icon.layers);
	_icon_unload(handle, handle->icon.user);
	_icon_unload(handle, handle->icon.settings);
	_icon_unload(handle, handle->icon.menu);

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

	_hash_free(&handle->bundle_matches);
	_hash_free(&handle->plugin_matches);
	_hash_free(&handle->preset_matches);
	_hash_free(&handle->port_matches);
	_hash_free(&handle->param_matches);
	_hash_free(&handle->dynam_matches);

	xpress_deinit(&handle->xpress);
	_deinit(handle);

	free(handle);
}

static void
_add_connection(plughandle_t *handle, const LV2_Atom_Object *obj)
{
	DBG;
	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom *src_symbol = NULL;
	const LV2_Atom_URID *snk_module = NULL;
	const LV2_Atom *snk_symbol = NULL;
	const LV2_Atom_Float *link_gain = NULL;

	lv2_atom_object_get(obj,
		handle->regs.synthpod.source_module.urid, &src_module,
		handle->regs.synthpod.source_symbol.urid, &src_symbol,
		handle->regs.synthpod.sink_module.urid, &snk_module,
		handle->regs.synthpod.sink_symbol.urid, &snk_symbol,
		handle->regs.param.gain.urid, &link_gain,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const char *src_sym = src_symbol
		? LV2_ATOM_BODY_CONST(src_symbol) : NULL;
	const LV2_URID snk_urn = snk_module
		? snk_module->body : 0;
	const char *snk_sym = snk_symbol
		? LV2_ATOM_BODY_CONST(snk_symbol) : NULL;
	const float gain = link_gain
		? link_gain->body : 1.f;

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
					mod_conn = _mod_conn_add(handle, src_mod, snk_mod, false);
				if(mod_conn)
				{
					port_conn_t *port_conn = _port_conn_find(mod_conn, src_port, snk_port);
					if(port_conn)
						port_conn->gain = gain; // update gain only
					else
						port_conn = _port_conn_add(mod_conn, src_port, snk_port, gain);
				}
			}
		}
	}
}

static void
_rem_connection(plughandle_t *handle, const LV2_Atom_Object *obj)
{
	DBG;
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
_add_node(plughandle_t *handle, const LV2_Atom_Object *obj)
{
	DBG;
	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom_URID *snk_module = NULL;
	const LV2_Atom_Float *pos_x = NULL;
	const LV2_Atom_Float *pos_y = NULL;

	lv2_atom_object_get(obj,
		handle->regs.synthpod.source_module.urid, &src_module,
		handle->regs.synthpod.sink_module.urid, &snk_module,
		handle->regs.synthpod.node_position_x.urid, &pos_x,
		handle->regs.synthpod.node_position_y.urid, &pos_y,
		0);

	const LV2_URID src_urn = src_module
		? src_module->body : 0;
	const LV2_URID snk_urn = snk_module
		? snk_module->body : 0;
	const float x = pos_x
		? pos_x->body : 0.f;
	const float y = pos_y
		? pos_y->body : 0.f;

	if(src_urn && snk_urn)
	{
		mod_t *src_mod = _mod_find_by_urn(handle, src_urn);
		mod_t *snk_mod = _mod_find_by_urn(handle, snk_urn);

		if(src_mod && snk_mod)
		{
			mod_conn_t *mod_conn = _mod_conn_find(handle, src_mod, snk_mod);
			if(!mod_conn) // does not yet exist
				mod_conn = _mod_conn_add(handle, src_mod, snk_mod, false);
			if(mod_conn)
			{
				if(x != 0.f)
					mod_conn->pos.x = x;
				if(y != 0.f)
					mod_conn->pos.y = y;
			}
		}
	}
}

static void
_add_automation(plughandle_t *handle, const LV2_Atom_Object *obj)
{
	DBG;
	const LV2_Atom_URID *src_module = NULL;
	const LV2_Atom *src_symbol = NULL;
	const LV2_Atom_URID *src_property = NULL;
	const LV2_Atom_Int *midi_channel = NULL;
	const LV2_Atom_Int *midi_controller = NULL;
	const LV2_Atom_String *osc_path = NULL;
	const LV2_Atom_Double *src_min = NULL;
	const LV2_Atom_Double *src_max = NULL;
	const LV2_Atom_Double *snk_min = NULL;
	const LV2_Atom_Double *snk_max = NULL;
	const LV2_Atom_Bool *src_enabled = NULL;
	const LV2_Atom_Bool *snk_enabled = NULL;

	lv2_atom_object_get(obj,
		handle->regs.synthpod.sink_module.urid, &src_module,
		handle->regs.synthpod.sink_symbol.urid, &src_symbol,
		handle->regs.patch.property.urid, &src_property,
		handle->regs.midi.channel.urid, &midi_channel,
		handle->regs.midi.controller_number.urid, &midi_controller,
		handle->regs.osc.path.urid, &osc_path,
		handle->regs.synthpod.source_min.urid, &src_min,
		handle->regs.synthpod.source_max.urid, &src_max,
		handle->regs.synthpod.sink_min.urid, &snk_min,
		handle->regs.synthpod.sink_max.urid, &snk_max,
		handle->regs.synthpod.source_enabled.urid, &src_enabled,
		handle->regs.synthpod.sink_enabled.urid, &snk_enabled,
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

	if(!automation)
		return;

	automation->src_enabled = src_enabled ? src_enabled->body : false;
	automation->snk_enabled= snk_enabled ? snk_enabled->body : false;
	automation->c = snk_min ? snk_min->body : 0.0; //FIXME
	automation->d = snk_max ? snk_max->body : 0.0; //FIXME

	if(obj->body.otype == handle->regs.midi.Controller.urid)
	{
		automation->type = AUTO_MIDI;
		midi_auto_t *mauto = &automation->midi;

		mauto->a = src_min ? src_min->body : 0x0;
		mauto->b = src_max ? src_max->body : 0x7f;

		mauto->channel = midi_channel ? midi_channel->body : -1;
		mauto->controller = midi_controller ? midi_controller->body : -1;
	}
	else if(obj->body.otype == handle->regs.osc.message.urid)
	{
		automation->type = AUTO_OSC;
		osc_auto_t *oauto = &automation->osc;

		oauto->a = src_min ? src_min->body : 0x0;
		oauto->b = src_max ? src_max->body : 0x7f;

		strncpy(oauto->path, LV2_ATOM_BODY_CONST(osc_path), 128); //FIXME
	}
}

static void
_add_notification(plughandle_t *handle, const LV2_Atom_Object *obj)
{
	DBG;
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
	DBG;
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
	DBG;
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
	DBG;
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

							_set_module_selector(handle, NULL);
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

							// patch:Get [patch:property spod:nodeList]
							if(  _message_request(handle)
								&& synthpod_patcher_get(&handle->regs, &handle->forge,
									0, 0, handle->regs.synthpod.node_list.urid) )
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
						else if( (prop == handle->regs.synthpod.node_list.urid)
							&& (value->type == handle->forge.Tuple) )
						{
							const LV2_Atom_Tuple *tup = (const LV2_Atom_Tuple *)value;

							LV2_ATOM_TUPLE_FOREACH(tup, itm)
							{
								_add_node(handle, (const LV2_Atom_Object *)itm);
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
						else if( (prop == handle->regs.synthpod.graph_position_x.urid)
							&& (value->type == handle->forge.Float) )
						{
							const LV2_Atom_Float *graph_position_x = (const LV2_Atom_Float *)value;

							handle->scrolling.x = graph_position_x->body;

							nk_pugl_post_redisplay(&handle->win);
						}
						else if( (prop == handle->regs.synthpod.graph_position_y.urid)
							&& (value->type == handle->forge.Float) )
						{
							const LV2_Atom_Float *graph_position_y = (const LV2_Atom_Float *)value;

							handle->scrolling.y = graph_position_y->body;

							nk_pugl_post_redisplay(&handle->win);
						}
						else if( (prop == handle->regs.synthpod.column_enabled.urid)
							&& (value->type == handle->forge.Bool) )
						{
							const LV2_Atom_Bool *column_enabled = (const LV2_Atom_Bool *)value;

							handle->show_sidebar = column_enabled->body;

							nk_pugl_post_redisplay(&handle->win);
						}
						else if( (prop == handle->regs.synthpod.row_enabled.urid)
							&& (value->type == handle->forge.Bool) )
						{
							const LV2_Atom_Bool *row_enabled = (const LV2_Atom_Bool *)value;

							handle->show_bottombar = row_enabled->body;

							nk_pugl_post_redisplay(&handle->win);
						}
						else if( (prop == handle->regs.synthpod.cpus_used.urid)
							&& (value->type == handle->forge.Int) )
						{
							const LV2_Atom_Int *cpus_used = (const LV2_Atom_Int *)value;

							handle->cpus_used = cpus_used->body;

							nk_pugl_post_redisplay(&handle->win);
						}
						else if( (prop == handle->regs.synthpod.cpus_available.urid)
							&& (value->type == handle->forge.Int) )
						{
							const LV2_Atom_Int *cpus_available = (const LV2_Atom_Int *)value;

							handle->cpus_available = cpus_available->body;

							nk_pugl_post_redisplay(&handle->win);
						}
						else if( (prop == handle->regs.synthpod.period_size.urid)
							&& (value->type == handle->forge.Int) )
						{
							const LV2_Atom_Int *period_size = (const LV2_Atom_Int *)value;

							handle->period_size = period_size->body;

							nk_pugl_post_redisplay(&handle->win);
						}
						else if( (prop == handle->regs.synthpod.num_periods.urid)
							&& (value->type == handle->forge.Int) )
						{
							const LV2_Atom_Int *num_periods = (const LV2_Atom_Int *)value;

							handle->num_periods = num_periods->body;

							nk_pugl_post_redisplay(&handle->win);
						}
						else if( (prop == handle->regs.idisp.surface.urid)
							&& (value->type == handle->forge.Tuple)
							&& subj )
						{
							const LV2_Atom_Tuple *tup = (const LV2_Atom_Tuple *)value;
							const LV2_Atom_Int *width = (const LV2_Atom_Int *)lv2_atom_tuple_begin(tup);
							const LV2_Atom_Int *height = (const LV2_Atom_Int *)lv2_atom_tuple_next(&width->atom);
							const LV2_Atom_Vector *vec = (const LV2_Atom_Vector *)lv2_atom_tuple_next(&height->atom);

							const uint32_t w = width ? width->body : 256;
							const uint32_t h = height ? height->body : 256;

							mod_t *mod = _mod_find_by_urn(handle, subj);
							if(mod)
							{
								const size_t body_size = vec->atom.size - sizeof(LV2_Atom_Vector_Body);
								const size_t nchild = body_size / vec->body.child_size;
								if(nchild == w*h)
								{
									const void *data = LV2_ATOM_CONTENTS(LV2_Atom_Vector, vec);

									_image_free(handle, &mod->idisp.img);
									mod->idisp.img = _image_new(handle, w, h, data);
									mod->idisp.w = w;
									mod->idisp.h = h;

									nk_pugl_post_redisplay(&handle->win);
								}
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
						const LV2_Atom_String *mod_alias = NULL;
						const LV2_Atom_URID *ui_uri = NULL;

						lv2_atom_object_get(body,
							handle->regs.core.plugin.urid, &plugin,
							handle->regs.synthpod.module_position_x.urid, &mod_pos_x,
							handle->regs.synthpod.module_position_y.urid, &mod_pos_y,
							handle->regs.synthpod.module_alias.urid, &mod_alias,
							handle->regs.ui.ui.urid, &ui_uri, //FIXME use this
							0); //FIXME query more

						const LV2_URID urid = plugin
							? plugin->body
							: 0;

						const char *uri = urid
							? handle->unmap->unmap(handle->unmap->handle, urid)
							: NULL;

						const LV2_URID ui_urn = ui_uri
							? ui_uri->body
							: 0;

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

							if(mod_alias && (mod_alias->atom.type == handle->forge.String) )
							{
								strncpy(mod->alias, LV2_ATOM_BODY_CONST(&mod_alias->atom), ALIAS_MAX-1);
							}

							if(ui_urn)
							{
								// look for ui, and run it
								HASH_FOREACH(&mod->uis, mod_ui_itr)
								{
									mod_ui_t *mod_ui = *mod_ui_itr;

									if(_mod_ui_is_running(mod_ui))
										_mod_ui_stop(mod_ui, false);

									if(mod_ui->urn == ui_urn)
										_mod_ui_run(mod_ui, false);
								}
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
							//_log_note(handle, "%s: got patch:remove for <%s>\n", __func__,
							//	handle->unmap->unmap(handle->unmap->handle, prop->key));

							if(  (prop->key == handle->regs.synthpod.connection_list.urid)
								&& (prop->value.type == handle->forge.Object) )
							{
								_rem_connection(handle, (const LV2_Atom_Object *)&prop->value);
							}
							else if(  (prop->key == handle->regs.synthpod.node_list.urid)
								&& (prop->value.type == handle->forge.Object) )
							{
								//FIXME never reached
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
							else if( (prop->key == handle->regs.synthpod.automation_list.urid)
								&& (prop->value.type == handle->forge.URID) )
							{
								//FIXME implement
							}
						}

						LV2_ATOM_OBJECT_FOREACH(add, prop)
						{
							//_log_note(handle, "%s: got patch:add for <%s>\n", __func__,
							//	handle->unmap->unmap(handle->unmap->handle, prop->key));

							if(  (prop->key == handle->regs.synthpod.connection_list.urid)
								&& (prop->value.type == handle->forge.Object) )
							{
								_add_connection(handle, (const LV2_Atom_Object *)&prop->value);
							}
							else if(  (prop->key == handle->regs.synthpod.node_list.urid)
								&& (prop->value.type == handle->forge.Object) )
							{
								_add_node(handle, (const LV2_Atom_Object *)&prop->value);
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
							else if( (prop->key == handle->regs.synthpod.automation_list.urid)
								&& (prop->value.type == handle->forge.Object) )
							{
								_add_automation(handle, (const LV2_Atom_Object *)&prop->value);
							}
						}
					}
				}
				else if(obj->body.otype == handle->regs.patch.copy.urid)
				{
					const LV2_Atom_URID *subject = NULL;
					const LV2_Atom_URID *destination = NULL;

					lv2_atom_object_get(obj,
						handle->regs.patch.subject.urid, &subject,
						handle->regs.patch.destination.urid, &destination,
						0);

					const LV2_URID subj = subject && (subject->atom.type == handle->forge.URID)
						? subject->body
						: 0;
					const LV2_URID dest = destination && (destination->atom.type == handle->forge.URID)
						? destination->body
						: 0;

					if(subj && dest)
					{
						mod_t *mod = _mod_find_by_urn(handle, subj);
						if(mod)
						{
							const char *bndl = handle->unmap->unmap(handle->unmap->handle, dest);
							bndl = !strncmp(bndl, "file://", 7)
								? bndl + 7
								: bndl;

							// reload presets for this module
							mod->presets = _preset_reload(handle->world, &handle->regs, mod->plug,
								mod->presets, bndl);
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
	DBG;
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
				_mod_ui_stop(mod_ui, true);
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

static int
_resize(LV2UI_Handle instance, int width, int height)
{
	DBG;
	plughandle_t *handle = instance;

	return nk_pugl_resize(&handle->win, width, height);
}

static const LV2UI_Resize resize_ext = {
	.ui_resize = _resize
};

static const void *
extension_data(const char *uri)
{
	DBG;
	if(!strcmp(uri, LV2_UI__idleInterface))
		return &idle_ext;
	else if(!strcmp(uri, LV2_UI__resize))
		return &resize_ext;

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
