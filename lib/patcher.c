/*
 * Copyright (c) 2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#include <stdbool.h>

#include <Elementary.h>
#include <Evas_GL.h>

#include <patcher.h>

#define PATCHER_TYPE "Matrix Patcher"

#define PATCHER_CONNECT_REQUEST "connect,request"
#define PATCHER_DISCONNECT_REQUEST "disconnect,request"
#define PATCHER_REALIZE_REQUEST "realize,request"
#define PATCHER_ZOOM_CHANGED "zoom,changed"

#define NUM_VERTS 2
#define ZOOM_MIN 0.05

#if 0
# define debugf(...) fprintf(stderr, __VA_ARGS__)
#else
# define debugf(...) {}
#endif

enum {
	CONNECTED				= (1 << 0),
	VERTICAL				= (1 << 1),
	HORIZONTAL			= (1 << 2),
	VERTICAL_EDGE		= (1 << 3),
	HORIZONTAL_EDGE	= (1 << 4),
	BOX							= (1 << 5),
	FEEDBACK				= (1 << 6),
	INDIRECT				= (1 << 7)
};

typedef struct _point_t point_t;
typedef struct _line_t line_t;
typedef struct _triangle_t triangle_t;
typedef struct _rectangle_t rectangle_t;
typedef struct _patcher_t patcher_t;

struct _point_t {
	GLfloat x;
	GLfloat y;
};

struct _line_t {
	point_t p0;
	point_t p1;
	point_t p2;
};

struct _triangle_t {
	point_t p0;
	point_t p1;
	point_t p2;
};

struct _rectangle_t {
	point_t p0;
	point_t p1;
	point_t p2;
	point_t p3;
};

struct _patcher_t {
	bool active;

	struct {
		intptr_t *cols;
		intptr_t *rows;
	} data;

	Evas_Object *self;
	Evas_Object *parent;
	Evas_Object *glview;
	Evas_GL_API *glapi;
	GLuint program;
	GLuint vtx_shader;
	GLuint fgmt_shader;

	GLuint vlines;
	int nlines;

	GLuint vboxs;
	int nboxs;

	GLuint vtriangs;
	int ntriangs;

	GLuint vconns;
	int nconns;

	int X, Y;
	int x, y;
	int w, h;
	int W, H;
	float scale;
	int ncols, nrows;
	float span, span1, span2;
	float x0, y0;
	int ax, ay;
	int sx, sy;

	uint8_t **matrix;
	Evas_Object **cols;
	Evas_Object **rows;
	bool needs_predraw;
	bool realizing;
};

static Evas_Object *parent = NULL;

static const Evas_Smart_Cb_Description _smart_callbacks [] = {
	{PATCHER_CONNECT_REQUEST, "(ii)(ii)"},
	{PATCHER_DISCONNECT_REQUEST, "(ii)(ii)"},
	{PATCHER_REALIZE_REQUEST, "(ii)(ii)"},
	{PATCHER_ZOOM_CHANGED, "f"},
	{NULL, NULL}
};

EVAS_SMART_SUBCLASS_NEW(PATCHER_TYPE, _patcher,
	Evas_Smart_Class, Evas_Smart_Class,
	evas_object_smart_clipped_class_get, _smart_callbacks);

static inline void
_rel_to_abs(patcher_t *priv, float ax, float ay, float *_fx, float *_fy)
{
	debugf("_rel_to_abs\n");
	ay = priv->nrows - ay;
	const float fx = priv->x0 + priv->span * ( ax + ay);
	const float fy = priv->y0 + priv->span * (-ax + ay);

	*_fx = fx;
	*_fy = fy;
}

static inline void
_abs_to_rel(patcher_t *priv, float fx, float fy, int *_ax, int *_ay)
{
	debugf("_abs_to_rel\n");
	float ax = floor( (-priv->x0 + fx + priv->y0 - fy) * priv->span2);
	float ay = floor( (-priv->x0 + fx - priv->y0 + fy) * priv->span2);
	ay = priv->nrows - 1 - ay;

	if( (ax >= 0) && (ax < priv->ncols) && (ay >= 0) && (ay < priv->nrows) )
	{
		// inside-bounds
	}
	else if(ax >= priv->ncols)
	{
		ax = -1; // out-of-bounds

		ay = floor( (-priv->y0 - fy) * priv->span1);
		if( (ay < 0) || (ay >= priv->nrows) )
			ay = -1; // out-of-bounds
	}
	else if(ay >= priv->nrows)
	{
		ax = floor( (priv->y0 - fy) * priv->span1);
		if( (ax < 0) || (ax >= priv->ncols) )
			ax = -1; // out-of-bounds

		ay = -1; // out-of-bounds
	}
	else
	{
		ax = -1; // out-of-bounds
		ay = -1; // out-of-bounds
	}

	*_ax = ax;
	*_ay = ay;
}

static inline void
_screen_to_abs(patcher_t *priv, int sx, int sy, float *_fx, float *_fy)
{
	debugf("_screen_to_abs\n");
	float fx = sx;
	fx /= priv->W; //TODO precalc
	fx *= 2.f;
	fx -= 1.f;

	float fy = sy;
	fy /= priv->H; //TODO precalc
	fy = 1.f - fy;
	fy *= 2.f;
	fy -= 1.f;

	*_fx = fx;
	*_fy = fy;
}

static inline void
_abs_to_screen(patcher_t *priv, float fx, float fy, int *_sx, int *_sy)
{
	debugf("_abs_to_screen\n");
	float sx = fx;
	sx += 1.f;
	sx *= 0.5;
	sx *= priv->W;
	
	float sy = fy;
	sy += 1.f;
	sy *= 0.5;
	sy = 1.f - sy;
	sy *= priv->H;

	*_sx = floor(sx);
	*_sy = floor(sy);
}

static void
_precalc(patcher_t *priv)
{
	debugf("_precalc\n");
	if(!priv->active)
		return;

	if(priv->ncols > priv->nrows)
	{
		priv->span = 1.f*priv->scale / priv->ncols;
		const float offset = priv->span * (priv->ncols - priv->nrows) * 0.5;
		priv->x0 = -1.f*priv->scale + offset;
		priv->y0 =  0.f*priv->scale + offset;
	}
	else if(priv->nrows > priv->ncols)
	{
		priv->span = priv->scale / priv->nrows;
		const float offset = priv->span * (priv->nrows - priv->ncols) * 0.5;
		priv->x0 = -1.f*priv->scale + offset;
		priv->y0 =  0.f*priv->scale - offset;
	}
	else // priv->nrows == priv->ncols
	{
		priv->span = 1.f*priv->scale / priv->ncols;
		priv->x0 = -1.f*priv->scale;
		priv->y0 =  0.f*priv->scale;
	}

	priv->span1 = 1.f / priv->span;
	priv->span2 = 0.5 / priv->span;
}

static void
_predraw(patcher_t *priv)
{
	debugf("_predraw\n");
	Evas_GL_API *gl = priv->glapi;

	_precalc(priv);

	const int nlines = priv->ncols + priv->nrows + 2;
	const size_t slines = nlines * sizeof(line_t);
	line_t *vlines = calloc(1, slines);
	if(vlines)
	{
		for(int col = 0; col <= priv->ncols; col++)
		{
			const int row = priv->nrows;
			line_t *line = &vlines[col];

			_rel_to_abs(priv, col, 0,   &line->p0.x, &line->p0.y);
			_rel_to_abs(priv, col, row, &line->p1.x, &line->p1.y);
			line->p2.x = -1.f; //line->p1.x - 0.5;
			line->p2.y = line->p1.y;
		}
		
		for(int row = 0; row <= priv->nrows; row++)
		{
			const int col = priv->ncols;
			line_t *line = &vlines[priv->ncols + 1 + row];

			_rel_to_abs(priv, 0,   row, &line->p0.x, &line->p0.y);
			_rel_to_abs(priv, col, row, &line->p1.x, &line->p1.y);
			line->p2.x = 1.f; //line->p1.x + 0.5;
			line->p2.y = line->p1.y;
		}
	
		gl->glBindBuffer(GL_ARRAY_BUFFER, priv->vlines);
		gl->glBufferData(GL_ARRAY_BUFFER, slines, vlines, GL_STATIC_DRAW);
		priv->nlines = nlines * 3;

		free(vlines);
	}
}

static inline void
_patcher_labels_move_resize(patcher_t *priv)
{
	debugf("_patcher_labels_move_resize\n");
	if(!priv->active)
		return;

	float fx, fy;
	int sx0, sy0, sx1, sy1;

	_precalc(priv);

	// get label height FIXME
	_rel_to_abs(priv, 0, 1, &fx, &fy);
	_abs_to_screen(priv, fx, fy, &sx1, &sy1);
	_rel_to_abs(priv, 0, 0, &fx, &fy);
	_abs_to_screen(priv, fx, fy, &sx0, &sy0);
	const int sspan = sy1 - sy0 - 1;

	for(int i=0; i<priv->ncols; i++)
	{
		_rel_to_abs(priv, i, priv->nrows, &fx, &fy);
		_abs_to_screen(priv, fx, fy, &sx1, &sy1);
		sx1 += priv->X;
		sy1 += priv->Y;
		if(sx1 < priv->x) sx1 = priv->x;

		_abs_to_screen(priv, -1.f, fy, &sx0, &sy0);
		sx0 += priv->X;
		sy0 += priv->Y;
		if(sx0 < priv->x) sx0 = priv->x;

		evas_object_move(priv->cols[i], sx0, sy0);
		evas_object_resize(priv->cols[i], sx1 - sx0, sspan);
	}

	for(int j=0; j<priv->nrows; j++)
	{
		_rel_to_abs(priv, priv->ncols, j, &fx, &fy);
		_abs_to_screen(priv, fx, fy, &sx0, &sy0);
		sx0 += priv->X;
		sy0 += priv->Y;
		if(sx0 > priv->x + priv->w) sx0 = priv->x + priv->w;

		_rel_to_abs(priv, priv->ncols, j, &fx, &fy);
		_abs_to_screen(priv, 1.f, fy, &sx1, &sy1);
		sx1 += priv->X;
		sy1 += priv->Y;
		if(sx1 > priv->x + priv->w) sx1 = priv->x + priv->w;

		evas_object_move(priv->rows[j], sx0, sy0);
		evas_object_resize(priv->rows[j], sx1 - sx0, sspan);
	}
}

//--------------------------------//
// a helper function to load shaders from a shader source
static GLuint
_load_shader(patcher_t *priv, GLenum type, const char *shader_src)
{
	debugf("_load_shader\n");
	Evas_GL_API *gl = priv->glapi;
	GLuint shader;
	GLint compiled;
	
	// Create the shader object
	shader = gl->glCreateShader(type);
	if(shader==0)
		return 0;
	
	// Load/Compile shader source
	gl->glShaderSource(shader, 1, &shader_src, NULL);
	gl->glCompileShader(shader);
	gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	
	if(!compiled)
	{
		GLint info_len = 0;
		gl->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
		if(info_len > 1)
		{
			char* info_log = calloc(1, info_len);
			if(info_log)
			{
				gl->glGetShaderInfoLog(shader, info_len, NULL, info_log);
				fprintf(stderr, "Error compiling shader:\n%s\n======\n%s\n======\n", info_log, shader_src );
				free(info_log);
			}
		}
		gl->glDeleteShader(shader);
		return 0;
	}
	
	return shader;
}

// Initialize the shader and program object
static int
_init_shaders(patcher_t *priv)
{
	debugf("_init_shaders\n");
	Evas_GL_API *gl = priv->glapi;
	GLbyte vShaderStr[] =
		"attribute vec4 vPosition;\n"
		"attribute vec4 inColor;\n"
		"varying vec4 fragColor;\n"
		"void main()\n"
		"{\n"
		"  gl_Position = vPosition;\n"
		"  fragColor = inColor;\n"
		"}\n";
	
	GLbyte fShaderStr[] =
		"varying vec4 fragColor;\n"
		"void main()\n"
		"{\n"
		"  gl_FragColor = fragColor;\n"
		"}\n";
	
	GLint linked;
	
	// Load the vertex/fragment shaders
	priv->vtx_shader  = _load_shader(priv, GL_VERTEX_SHADER, (const char*)vShaderStr);
	priv->fgmt_shader = _load_shader(priv, GL_FRAGMENT_SHADER, (const char*)fShaderStr);
	
	// Create the program object
	priv->program = gl->glCreateProgram();
	if(priv->program==0)
		return 0;
	
	gl->glAttachShader(priv->program, priv->vtx_shader);
	gl->glAttachShader(priv->program, priv->fgmt_shader);
	
	gl->glBindAttribLocation(priv->program, 0, "vPosition");
	gl->glBindAttribLocation(priv->program, 1, "inColor");
	gl->glLinkProgram(priv->program);
	gl->glGetProgramiv(priv->program, GL_LINK_STATUS, &linked);
	
	if(!linked)
	{
		GLint info_len = 0;
		gl->glGetProgramiv(priv->program, GL_INFO_LOG_LENGTH, &info_len);
		if(info_len > 1)
		{
			char* info_log = calloc(1, info_len);
			if(info_log)
			{
				gl->glGetProgramInfoLog(priv->program, info_len, NULL, info_log);
				fprintf(stderr, "Error linking program:\n%s\n", info_log);
				free(info_log);
			}
		}
		gl->glDeleteProgram(priv->program);
		return 0;
	}
	
	return 1;
}

// Callbacks
// intialize callback that gets called once for intialization
static void
_init_gl(Evas_Object *obj)
{
	debugf("_init_gl\n");
	patcher_t *priv = evas_object_data_get(obj, "priv");
	Evas_GL_API *gl = priv->glapi;

	gl->glGenBuffers(1, &priv->vboxs);
	priv->nboxs = 4;

	gl->glGenBuffers(1, &priv->vtriangs);
	priv->ntriangs = 3;

	gl->glGenBuffers(1, &priv->vconns);
	priv->nconns = 4;

	gl->glGenBuffers(1, &priv->vlines);
	
	if(!_init_shaders(priv))
	{
		fprintf(stderr, "Error Initializing Shaders\n");
		return;
	}

	priv->needs_predraw = true;
}

// delete callback gets called when glview is deleted
static void
_del_gl(Evas_Object *obj)
{
	debugf("_del_gl\n");
	patcher_t *priv = evas_object_data_get(obj, "priv");
	Evas_GL_API *gl = priv->glapi;

	gl->glDeleteShader(priv->vtx_shader);
	gl->glDeleteShader(priv->fgmt_shader);
	gl->glDeleteProgram(priv->program);
	gl->glDeleteBuffers(1, &priv->vboxs);
	gl->glDeleteBuffers(1, &priv->vtriangs);
	gl->glDeleteBuffers(1, &priv->vconns);
	gl->glDeleteBuffers(1, &priv->vlines);
}

// resize callback gets called every time object is resized
static void
_resize_gl(Evas_Object *obj)
{
	debugf("_resize_gl\n");
	patcher_t *priv = evas_object_data_get(obj, "priv");
	Evas_GL_API *gl = elm_glview_gl_api_get(obj);
	int x, y, w, h;

	elm_glview_size_get(obj, &w, &h);

	// preserve aspect ratio
	if(w > h)
	{
		x = 0;
		y = (h - w) / 2;
		h = w;
	}
	else if(h > w)
	{
		x = (w - h) / 2;
		y = 0;
		w = h;
	}
	else
	{
		x = 0;
		y = 0;
	}

	gl->glViewport(x, y, w, h);

	// automagically calls _draw_gl
}

static void
_dump(patcher_t *priv)
{
	debugf("active: %i\n", priv->active);
	debugf("x, y: %i %i\n", priv->x, priv->y);
	debugf("X, Y: %i %i\n", priv->X, priv->Y);
	debugf("w, h: %i %i\n", priv->w, priv->h);
	debugf("W, H: %i %i\n", priv->W, priv->H);
	debugf("scale: %f\n", priv->scale);
	debugf("ncols, nrows: %i %i\n", priv->ncols, priv->nrows);
	debugf("span, span1, span2: %f %f %f\n", priv->span, priv->span1, priv->span2);
	debugf("x0, y0: %f %f\n", priv->x0, priv->y0);
	debugf("ax, ay: %i %i\n\n", priv->ax, priv->ay);
}

// draw callback is where all the main GL rendering happens
static void
_draw_gl(Evas_Object *obj)
{
	debugf("_draw_gl\n");

	Evas_GL_API *gl = elm_glview_gl_api_get(obj);
	patcher_t *priv = evas_object_data_get(obj, "priv");

	if(priv->needs_predraw)
	{
		_predraw(priv);
		priv->needs_predraw = false;
	}
	
	gl->glClearColor(0.25, 0.25, 0.25, 1.f); // light gray
	gl->glClear(GL_COLOR_BUFFER_BIT);

	if(priv->active)
	{
		gl->glUseProgram(priv->program);
		gl->glEnableVertexAttribArray(0);

		rectangle_t vconns;
		gl->glLineWidth(0.f);
		gl->glVertexAttrib4f(1, 1.f, 1.f, 1.f, 1.f);
		gl->glBindBuffer(GL_ARRAY_BUFFER, priv->vconns);
		for(int i=0; i<priv->ncols; i++)
		{
			uint8_t *col = priv->matrix[i];
			for(int j=0; j<priv->nrows; j++)
			{
				if(col[j] & FEEDBACK)
				{
					gl->glVertexAttrib4f(1, 0.2, 0.2, 0.2, 1.f);
					_rel_to_abs(priv, i+0, j+0, &vconns.p0.x, &vconns.p0.y);
					_rel_to_abs(priv, i+0, j+1, &vconns.p1.x, &vconns.p1.y);
					_rel_to_abs(priv, i+1, j+1, &vconns.p2.x, &vconns.p2.y);
					_rel_to_abs(priv, i+1, j+0, &vconns.p3.x, &vconns.p3.y);

					gl->glBufferData(GL_ARRAY_BUFFER, sizeof(rectangle_t), &vconns, GL_DYNAMIC_DRAW);
					gl->glVertexAttribPointer(0, NUM_VERTS, GL_FLOAT, GL_FALSE, 0, 0);
					gl->glDrawArrays(GL_TRIANGLE_FAN, 0, priv->nconns);
				}
				else if(col[j] & INDIRECT)
				{
					gl->glVertexAttrib4f(1, 0.15, 0.15, 0.15, 1.f);
					_rel_to_abs(priv, i+0, j+0, &vconns.p0.x, &vconns.p0.y);
					_rel_to_abs(priv, i+0, j+1, &vconns.p1.x, &vconns.p1.y);
					_rel_to_abs(priv, i+1, j+1, &vconns.p2.x, &vconns.p2.y);
					_rel_to_abs(priv, i+1, j+0, &vconns.p3.x, &vconns.p3.y);

					gl->glBufferData(GL_ARRAY_BUFFER, sizeof(rectangle_t), &vconns, GL_DYNAMIC_DRAW);
					gl->glVertexAttribPointer(0, NUM_VERTS, GL_FLOAT, GL_FALSE, 0, 0);
					gl->glDrawArrays(GL_TRIANGLE_FAN, 0, priv->nconns);
				}
			}
		}

		// draw lines
		gl->glVertexAttrib4f(1, 0.5, 0.5, 0.5, 1.f);
		gl->glBindBuffer(GL_ARRAY_BUFFER, priv->vlines);
		for(int i=0; i<priv->nlines; i+=3)
		{
			gl->glLineWidth(2.f);
			gl->glVertexAttribPointer(0, NUM_VERTS, GL_FLOAT, GL_FALSE, 0, (const void *)(i*sizeof(point_t)));
			gl->glDrawArrays(GL_LINES, 0, 2);

			gl->glLineWidth(1.f);
			gl->glVertexAttribPointer(0, NUM_VERTS, GL_FLOAT, GL_FALSE, 0, (const void *)((i+1)*sizeof(point_t)));
			gl->glDrawArrays(GL_LINES, 0, 2);
		}

		gl->glLineWidth(0.f);
		gl->glVertexAttrib4f(1, 1.f, 1.f, 1.f, 1.f);
		gl->glBindBuffer(GL_ARRAY_BUFFER, priv->vconns);
		for(int i=0; i<priv->ncols; i++)
		{
			uint8_t *col = priv->matrix[i];
			for(int j=0; j<priv->nrows; j++)
			{
				if(col[j] & HORIZONTAL)
				{
					gl->glVertexAttrib4f(1, 0.8, 0.8, 0.8, 1.f);
					_rel_to_abs(priv, i+0.0, j+0.4, &vconns.p0.x, &vconns.p0.y);
					_rel_to_abs(priv, i+0.0, j+0.6, &vconns.p1.x, &vconns.p1.y);
					_rel_to_abs(priv, i+1.0, j+0.6, &vconns.p2.x, &vconns.p2.y);
					_rel_to_abs(priv, i+1.0, j+0.4, &vconns.p3.x, &vconns.p3.y);

					gl->glBufferData(GL_ARRAY_BUFFER, sizeof(rectangle_t), &vconns, GL_DYNAMIC_DRAW);
					gl->glVertexAttribPointer(0, NUM_VERTS, GL_FLOAT, GL_FALSE, 0, 0);
					gl->glDrawArrays(GL_TRIANGLE_FAN, 0, priv->nconns);
				}
				else if(col[j] & HORIZONTAL_EDGE)
				{
					gl->glVertexAttrib4f(1, 0.8, 0.8, 0.8, 1.f);
					_rel_to_abs(priv, i+0.4, j+0.4, &vconns.p0.x, &vconns.p0.y);
					_rel_to_abs(priv, i+0.4, j+0.6, &vconns.p1.x, &vconns.p1.y);
					_rel_to_abs(priv, i+1.0, j+0.6, &vconns.p2.x, &vconns.p2.y);
					_rel_to_abs(priv, i+1.0, j+0.4, &vconns.p3.x, &vconns.p3.y);

					gl->glBufferData(GL_ARRAY_BUFFER, sizeof(rectangle_t), &vconns, GL_DYNAMIC_DRAW);
					gl->glVertexAttribPointer(0, NUM_VERTS, GL_FLOAT, GL_FALSE, 0, 0);
					gl->glDrawArrays(GL_TRIANGLE_FAN, 0, priv->nconns);
				}

				if(col[j] & VERTICAL)
				{
					gl->glVertexAttrib4f(1, 0.8, 0.8, 0.8, 1.f);
					_rel_to_abs(priv, i+0.4, j+0.0, &vconns.p0.x, &vconns.p0.y);
					_rel_to_abs(priv, i+0.4, j+1.0, &vconns.p1.x, &vconns.p1.y);
					_rel_to_abs(priv, i+0.6, j+1.0, &vconns.p2.x, &vconns.p2.y);
					_rel_to_abs(priv, i+0.6, j+0.0, &vconns.p3.x, &vconns.p3.y);

					gl->glBufferData(GL_ARRAY_BUFFER, sizeof(rectangle_t), &vconns, GL_DYNAMIC_DRAW);
					gl->glVertexAttribPointer(0, NUM_VERTS, GL_FLOAT, GL_FALSE, 0, 0);
					gl->glDrawArrays(GL_TRIANGLE_FAN, 0, priv->nconns);
				}
				else if(col[j] & VERTICAL_EDGE)
				{
					gl->glVertexAttrib4f(1, 0.8, 0.8, 0.8, 1.f);
					_rel_to_abs(priv, i+0.4, j+0.4, &vconns.p0.x, &vconns.p0.y);
					_rel_to_abs(priv, i+0.4, j+1.0, &vconns.p1.x, &vconns.p1.y);
					_rel_to_abs(priv, i+0.6, j+1.0, &vconns.p2.x, &vconns.p2.y);
					_rel_to_abs(priv, i+0.6, j+0.4, &vconns.p3.x, &vconns.p3.y);

					gl->glBufferData(GL_ARRAY_BUFFER, sizeof(rectangle_t), &vconns, GL_DYNAMIC_DRAW);
					gl->glVertexAttribPointer(0, NUM_VERTS, GL_FLOAT, GL_FALSE, 0, 0);
					gl->glDrawArrays(GL_TRIANGLE_FAN, 0, priv->nconns);
				}

				if(col[j] & CONNECTED)
				{
					gl->glVertexAttrib4f(1, 1.f, 1.f, 1.f, 1.f);
					_rel_to_abs(priv, i+0.2, j+0.2, &vconns.p0.x, &vconns.p0.y);
					_rel_to_abs(priv, i+0.2, j+0.8, &vconns.p1.x, &vconns.p1.y);
					_rel_to_abs(priv, i+0.8, j+0.8, &vconns.p2.x, &vconns.p2.y);
					_rel_to_abs(priv, i+0.8, j+0.2, &vconns.p3.x, &vconns.p3.y);

					gl->glBufferData(GL_ARRAY_BUFFER, sizeof(rectangle_t), &vconns, GL_DYNAMIC_DRAW);
					gl->glVertexAttribPointer(0, NUM_VERTS, GL_FLOAT, GL_FALSE, 0, 0);
					gl->glDrawArrays(GL_TRIANGLE_FAN, 0, priv->nconns);
				}
			}
		}

		triangle_t vtriangs;
		gl->glLineWidth(0.f);
		gl->glVertexAttrib4f(1, 0.8, 0.8, 0.8, 1.f);
		gl->glBindBuffer(GL_ARRAY_BUFFER, priv->vtriangs);
		for(int i=0; i<priv->ncols; i++)
		{
			int j = priv->nrows - 1;
			if(priv->matrix[i][j] & (VERTICAL | VERTICAL_EDGE) )
			{
				_rel_to_abs(priv, i + 0.2, j + 1.0, &vtriangs.p0.x, &vtriangs.p0.y);
				_rel_to_abs(priv, i + 0.8, j + 1.0, &vtriangs.p1.x, &vtriangs.p1.y);
				_rel_to_abs(priv, i + 0.5, j + 1.3, &vtriangs.p2.x, &vtriangs.p2.y);

				gl->glBufferData(GL_ARRAY_BUFFER, sizeof(triangle_t), &vtriangs, GL_DYNAMIC_DRAW);
				gl->glVertexAttribPointer(0, NUM_VERTS, GL_FLOAT, GL_FALSE, 0, 0);
				gl->glDrawArrays(GL_TRIANGLE_FAN, 0, priv->ntriangs);
			}
		}
		for(int j=0; j<priv->nrows; j++)
		{
			int i = priv->ncols - 1;
			if(priv->matrix[i][j] & (HORIZONTAL | HORIZONTAL_EDGE) )
			{
				_rel_to_abs(priv, i + 1.0, j + 0.2, &vtriangs.p0.x, &vtriangs.p0.y);
				_rel_to_abs(priv, i + 1.0, j + 0.8, &vtriangs.p1.x, &vtriangs.p1.y);
				_rel_to_abs(priv, i + 1.3, j + 0.5, &vtriangs.p2.x, &vtriangs.p2.y);

				gl->glBufferData(GL_ARRAY_BUFFER, sizeof(triangle_t), &vtriangs, GL_DYNAMIC_DRAW);
				gl->glVertexAttribPointer(0, NUM_VERTS, GL_FLOAT, GL_FALSE, 0, 0);
				gl->glDrawArrays(GL_TRIANGLE_FAN, 0, priv->ntriangs);
			}
		}

		rectangle_t vboxs;
		gl->glLineWidth(4.f);
		gl->glVertexAttrib4f(1, 1.f, 1.f, 1.f, 1.f);
		gl->glBindBuffer(GL_ARRAY_BUFFER, priv->vboxs);
		for(int i=0; i<priv->ncols; i++)
		{
			uint8_t *col = priv->matrix[i];
			for(int j=0; j<priv->nrows; j++)
			{
				if(col[j] & BOX)
				{
					_rel_to_abs(priv, i + 0, j + 0, &vboxs.p0.x, &vboxs.p0.y);
					_rel_to_abs(priv, i + 0, j + 1, &vboxs.p1.x, &vboxs.p1.y);
					_rel_to_abs(priv, i + 1, j + 1, &vboxs.p2.x, &vboxs.p2.y);
					_rel_to_abs(priv, i + 1, j + 0, &vboxs.p3.x, &vboxs.p3.y);

					gl->glBufferData(GL_ARRAY_BUFFER, sizeof(rectangle_t), &vboxs, GL_DYNAMIC_DRAW);
					gl->glVertexAttribPointer(0, NUM_VERTS, GL_FLOAT, GL_FALSE, 0, 0);
					gl->glDrawArrays(GL_LINE_LOOP, 0, priv->nboxs);
				}
			}
		}

		gl->glDisableVertexAttribArray(0);
	}

	gl->glFlush();
	//gl->glFinish();
}

static inline int 
_patcher_object_source_idx_get(Evas_Object *o, intptr_t id)
{
	debugf("_patcher_object_source_idx_get\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	for(int i=0; i<priv->ncols; i++)
	{
		if(priv->data.cols[i] == id)
			return i;
	}

	return -1;
}

static inline int 
_patcher_object_sink_idx_get(Evas_Object *o, intptr_t id)
{
	debugf("_patcher_object_sink_idx_get\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	for(int i=0; i<priv->nrows; i++)
	{
		if(priv->data.rows[i] == id)
			return i;
	}

	return -1;
}

static void
_patcher_smart_init(Evas_Object *o)
{
	debugf("_patcher_smart_init\n");
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas *e = evas_object_evas_get(o);

	priv->data.cols = priv->ncols ? calloc(priv->ncols, sizeof(intptr_t)) : NULL;
	priv->data.rows = priv->nrows ? calloc(priv->nrows, sizeof(intptr_t)) : NULL;

	priv->matrix = priv->ncols ? calloc(priv->ncols, sizeof(uint8_t *)) : NULL;
	if(priv->matrix)
	{
		for(int i=0; i<priv->ncols; i++)
			priv->matrix[i] = priv->nrows ? calloc(priv->nrows, sizeof(uint8_t)) : NULL;
	}

	priv->cols = priv->ncols ? calloc(priv->ncols, sizeof(Evas_Object *)) : NULL;
	if(priv->cols)
	{
		for(int i=0; i<priv->ncols; i++)
		{
			Evas_Object *lbl = edje_object_add(e);
			if(lbl)
			{
				priv->cols[i] = lbl;

				edje_object_file_set(lbl, SYNTHPOD_DATA_DIR"/synthpod.edj",
					"/patcher/port");
				edje_object_signal_emit(lbl, "source", PATCHER_UI);
				evas_object_pass_events_set(lbl, EINA_TRUE);
				evas_object_show(lbl);
				evas_object_smart_member_add(lbl, priv->glview);
			}
		}
	}

	priv->rows = priv->nrows ? calloc(priv->nrows, sizeof(Evas_Object *)) : NULL;
	{
		for(int j=0; j<priv->nrows; j++)
		{
			Evas_Object *lbl = edje_object_add(e);
			if(lbl)
			{
				priv->rows[j] = lbl;

				edje_object_file_set(lbl, SYNTHPOD_DATA_DIR"/synthpod.edj",
					"/patcher/port");
				edje_object_signal_emit(lbl, "sink", PATCHER_UI);
				evas_object_pass_events_set(lbl, EINA_TRUE);
				evas_object_show(lbl);
				evas_object_smart_member_add(lbl, priv->glview);
			}
		}
	}

	priv->needs_predraw = true;
	elm_glview_changed_set(priv->glview); // refresh
	_patcher_labels_move_resize(priv);
}

static void
_patcher_smart_deinit(Evas_Object *o, bool object_del)
{
	debugf("_patcher_smart_deinit\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	if(priv->ncols)
	{
		if(priv->data.cols)
		{
			free(priv->data.cols);
			priv->data.cols = NULL;
		}

		if(priv->cols)
		{
			for(int i=0; i<priv->ncols; i++)
			{
				if(object_del)
				{
					evas_object_smart_member_del(priv->cols[i]);
					evas_object_del(priv->cols[i]);
				}
				priv->cols[i] = NULL;
			}
			free(priv->cols);
			priv->cols = NULL;
		}

		if(priv->matrix)
		{
			for(int i=0; i<priv->ncols; i++)
				free(priv->matrix[i]);
			free(priv->matrix);
			priv->matrix = NULL;
		}

		priv->ncols = 0;
	}

	if(priv->nrows)
	{
		if(priv->data.rows)
		{
			free(priv->data.rows);
			priv->data.rows = NULL;
		}

		if(priv->rows)
		{
			for(int j=0; j<priv->nrows; j++)
			{
				if(object_del)
				{
					evas_object_smart_member_del(priv->rows[j]);
					evas_object_del(priv->rows[j]);
				}
				priv->rows[j] = NULL;
			}
			free(priv->rows);
			priv->rows = NULL;
		}

		priv->nrows = 0;
	}
}

static inline void
_clear_lines(patcher_t *priv)
{
	debugf("_clear_lines\n");
	for(int i=0; i<priv->ncols; i++)
	{
		uint8_t *col = priv->matrix[i];
		for(int j=0; j<priv->nrows; j++)
		{
			col[j] &= CONNECTED | FEEDBACK | INDIRECT; // invalidate all line data
		}
	}

	for(int i=0; i<priv->ncols; i++)
		edje_object_signal_emit(priv->cols[i], "off", PATCHER_UI);
	for(int j=0; j<priv->nrows; j++)
		edje_object_signal_emit(priv->rows[j], "off", PATCHER_UI);
}

static Eina_Bool
_mouse_move_raw2(patcher_t *priv, int ax, int ay)
{
	debugf("_mouse_move_raw2\n");

	if( (ax != priv->ax) || (ay != priv->ay) )
	{
		_clear_lines(priv);

		if( (ax != -1) && (ay != -1) ) // hover over matrix node
		{
			priv->matrix[ax][ay] |= HORIZONTAL_EDGE | VERTICAL_EDGE | BOX;
			edje_object_signal_emit(priv->cols[ax], "on", PATCHER_UI);
			edje_object_signal_emit(priv->rows[ay], "on", PATCHER_UI);

			for(int i=ax+1; i<priv->ncols; i++)
				priv->matrix[i][ay] |= HORIZONTAL;

			for(int j=ay+1; j<priv->nrows; j++)
				priv->matrix[ax][j] |= VERTICAL;
		}
		else if( (ax != -1) && (ay == -1) ) // hover over matrix source
		{
			edje_object_signal_emit(priv->cols[ax], "on", PATCHER_UI);

			for(int j=0; j<priv->nrows; j++)
			{
				if(priv->matrix[ax][j] & CONNECTED)
				{
					priv->matrix[ax][j] |= HORIZONTAL_EDGE | VERTICAL_EDGE | BOX;
					edje_object_signal_emit(priv->rows[j], "on", PATCHER_UI);

					for(int i=ax+1; i<priv->ncols; i++)
						priv->matrix[i][j] |= HORIZONTAL;

					for(int i=j+1; i<priv->nrows; i++)
						priv->matrix[ax][i] |= VERTICAL;
				}
			}
		}
		else if( (ax == -1) && (ay != -1) ) // hover over matrix sink
		{
			edje_object_signal_emit(priv->rows[ay], "on", PATCHER_UI);

			for(int i=0; i<priv->ncols; i++)
			{
				if(priv->matrix[i][ay] & CONNECTED)
				{
					priv->matrix[i][ay] |= HORIZONTAL_EDGE | VERTICAL_EDGE | BOX;
					edje_object_signal_emit(priv->cols[i], "on", PATCHER_UI);

					for(int j=i+1; j<priv->ncols; j++)
						priv->matrix[j][ay] |= HORIZONTAL;

					for(int j=ay+1; j<priv->nrows; j++)
						priv->matrix[i][j] |= VERTICAL;
				}
			}
		}

		priv->ax = ax;
		priv->ay = ay;

		return EINA_TRUE;
	}

	return EINA_FALSE;
}

static Eina_Bool
_mouse_move_raw(patcher_t *priv, int sx, int sy)
{
	debugf("_mouse_move_raw\n");
	float fx, fy;
	int ax, ay;

	sx -= priv->X;
	sy -= priv->Y;

	_screen_to_abs(priv, sx, sy, &fx, &fy);
	_abs_to_rel(priv, fx, fy, &ax, &ay);

	return _mouse_move_raw2(priv, ax, ay);
}

static void
_mouse_in(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	debugf("_mouse_in\n");
	patcher_t *priv = data;
	Evas_Event_Mouse_In *ev = event_info;

	priv->sx = ev->output.x;
	priv->sy = ev->output.y;

	if(_mouse_move_raw(priv, ev->output.x, ev->output.y))
		elm_glview_changed_set(priv->glview); // refresh
}

static void
_mouse_out(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	debugf("_mouse_out\n");
	patcher_t *priv = data;

	priv->ax = -1;
	priv->ay = -1;
	priv->sx = 0;
	priv->sy = 0;

	_clear_lines(priv);
	elm_glview_changed_set(priv->glview); // refresh
}

static void
_mouse_down(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	debugf("_mouse_down\n");
	Evas_Event_Mouse_Down *ev = event_info;
	patcher_t *priv = data;

	if( (priv->ax != -1) && (priv->ay != -1) )
	{
		const patcher_event_t evi [2] = {
			{
				.index = priv->ax,
				.id = priv->data.cols[priv->ax]
			},
			{
				.index = priv->ay,
				.id = priv->data.rows[priv->ay]
			}
		};

		if(priv->matrix[priv->ax][priv->ay] & CONNECTED) // is on currently
			evas_object_smart_callback_call(priv->self, PATCHER_DISCONNECT_REQUEST, (void *)evi);
		else // is off currently
			evas_object_smart_callback_call(priv->self, PATCHER_CONNECT_REQUEST, (void *)evi);
	}
	else if( (priv->ax != -1) && (priv->ay == -1) )
	{
		bool has_connections = false;
		for(int j=0; j<priv->nrows; j++)
		{
			if(priv->matrix[priv->ax][j] & CONNECTED)
			{
				has_connections = true;
				break;
			}
		}

		for(int j=0; j<priv->nrows; j++)
		{
			const patcher_event_t evi [2] = {
				{
					.index = priv->ax,
					.id = priv->data.cols[priv->ax]
				},
				{
					.index = j,
					.id = priv->data.rows[j]
				}
			};

			if(has_connections && (priv->matrix[priv->ax][j] & CONNECTED) ) // is on currently
				evas_object_smart_callback_call(priv->self, PATCHER_DISCONNECT_REQUEST, (void *)evi);
			else if(!has_connections && !(priv->matrix[priv->ax][j] & CONNECTED) )
				evas_object_smart_callback_call(priv->self, PATCHER_CONNECT_REQUEST, (void *)evi);
		}
	}
	else if( (priv->ax == -1) && (priv->ay != -1) )
	{
		bool has_connections = false;
		for(int i=0; i<priv->ncols; i++)
		{
			if(priv->matrix[i][priv->ay] & CONNECTED)
			{
				has_connections = true;
				break;
			}
		}

		for(int i=0; i<priv->ncols; i++)
		{
			const patcher_event_t evi [2] = {
				{
					.index = i,
					.id = priv->data.cols[i]
				},
				{
					.index = priv->ay,
					.id = priv->data.rows[priv->ay]
				}
			};

			if(has_connections && (priv->matrix[i][priv->ay] & CONNECTED) ) // is on currently
				evas_object_smart_callback_call(priv->self, PATCHER_DISCONNECT_REQUEST, (void *)evi);
			else if(!has_connections && !(priv->matrix[i][priv->ay] & CONNECTED) )
				evas_object_smart_callback_call(priv->self, PATCHER_CONNECT_REQUEST, (void *)evi);
		}
	}
}

static void
_mouse_move(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	debugf("_mouse_move\n");
	patcher_t *priv = data;
	Evas_Event_Mouse_Move *ev = event_info;

	priv->sx = ev->cur.output.x;
	priv->sy = ev->cur.output.y;

	if(_mouse_move_raw(priv, priv->sx, priv->sy))
		elm_glview_changed_set(priv->glview); // refresh
}

static void
_zoom(patcher_t *priv)
{
	if(!priv->active)
		return;

	const float zoom = priv->scale;
	evas_object_smart_callback_call(priv->self, PATCHER_ZOOM_CHANGED, (void *)&zoom);

	_mouse_move_raw(priv, 0, 0);
	priv->needs_predraw = true;
	_mouse_move_raw(priv, priv->sx, priv->sy);
	elm_glview_changed_set(priv->glview); // refresh
	_patcher_labels_move_resize(priv);
}

static void
_zoom_in(patcher_t *priv)
{
	priv->scale += ZOOM_MIN;

	_zoom(priv);
}

static void
_zoom_out(patcher_t *priv)
{
	priv->scale -= ZOOM_MIN;

	if(priv->scale < ZOOM_MIN)
		priv->scale = ZOOM_MIN;

	_zoom(priv);
}

static void
_mouse_wheel(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	debugf("_mouse_wheel\n");
	patcher_t *priv = data;
	Evas_Event_Mouse_Wheel *ev = event_info;

	priv->sx = ev->output.x;
	priv->sy = ev->output.y;

	if(ev->z < 0)
		_zoom_in(priv);
	else
		_zoom_out(priv);
}

static void
_patcher_smart_add(Evas_Object *o)
{
	debugf("_patcher_smart_add\n");
	EVAS_SMART_DATA_ALLOC(o, patcher_t);

	_patcher_parent_sc->add(o);

	memset(priv, 0x0, sizeof(patcher_t));
	priv->self = o;
	priv->parent = parent;
	priv->scale = 0.5;

	priv->data.cols = NULL;
	priv->data.rows = NULL;
	priv->matrix = NULL;
	priv->cols = NULL;
	priv->rows = NULL;
	priv->ax = -1;
	priv->ay = -1;
	priv->sx = 0;
	priv->sy = 0;

	// create a new glview object
	priv->glview = priv->parent ? elm_glview_add(priv->parent) : NULL;
	if(priv->glview)
	{
		priv->glview = priv->glview;
		priv->glapi = elm_glview_gl_api_get(priv->glview);

		evas_object_size_hint_align_set(priv->glview, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_size_hint_weight_set(priv->glview, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);

		evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_IN, _mouse_in, priv);
		evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_OUT, _mouse_out, priv);
		evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_DOWN, _mouse_down, priv);
		evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_MOVE, _mouse_move, priv);
		evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_WHEEL, _mouse_wheel, priv);

		evas_object_data_set(priv->glview, "priv", priv);
		elm_glview_mode_set(priv->glview, ELM_GLVIEW_DIRECT);
		elm_glview_resize_policy_set(priv->glview, ELM_GLVIEW_RESIZE_POLICY_RECREATE);
		elm_glview_render_policy_set(priv->glview, ELM_GLVIEW_RENDER_POLICY_ON_DEMAND);
		elm_glview_init_func_set(priv->glview, _init_gl);
		elm_glview_del_func_set(priv->glview, _del_gl);
		elm_glview_resize_func_set(priv->glview, _resize_gl);
		elm_glview_render_func_set(priv->glview, _draw_gl);
		
		evas_object_show(priv->glview);
		evas_object_smart_member_del(priv->glview);
		evas_object_smart_member_add(priv->glview, o);
	}

	_patcher_smart_init(o);
}

static void
_patcher_smart_del(Evas_Object *o)
{
	debugf("_patcher_smart_del\n");

	evas_object_event_callback_del(o, EVAS_CALLBACK_MOUSE_IN, _mouse_in);
	evas_object_event_callback_del(o, EVAS_CALLBACK_MOUSE_OUT, _mouse_out );
	evas_object_event_callback_del(o, EVAS_CALLBACK_MOUSE_DOWN, _mouse_down);
	evas_object_event_callback_del(o, EVAS_CALLBACK_MOUSE_MOVE, _mouse_move);
	evas_object_event_callback_del(o, EVAS_CALLBACK_MOUSE_WHEEL, _mouse_wheel);

	_patcher_smart_deinit(o, false);

	_patcher_parent_sc->del(o);
}

static void
_patcher_smart_move(Evas_Object *o, Evas_Coord x, Evas_Coord y)
{
	debugf("_patcher_smart_move\n");
	evas_object_smart_changed(o);
}

static void
_patcher_smart_resize(Evas_Object *o, Evas_Coord w, Evas_Coord h)
{
	debugf("_patcher_smart_resize\n");
	evas_object_smart_changed(o);
}

static void
_patcher_smart_calculate(Evas_Object *o)
{
	debugf("_patcher_smart_calculate\n");
	patcher_t *priv = evas_object_smart_data_get(o);
	Evas_Coord x, y, w, h;

	evas_object_geometry_get(o, &x, &y, &w, &h);

	priv->w = w;
	priv->h = h;

	priv->W = w > h ? w : h;
	priv->H = h > w ? h : w;

	priv->x = x;
	priv->y = y;

	priv->X = x - (priv->W - priv->w) / 2;
	priv->Y = y - (priv->H - priv->h) / 2;

	_patcher_labels_move_resize(priv);
	evas_object_move(priv->glview, x, y);
	evas_object_resize(priv->glview, w, h);
}

static void
_patcher_smart_set_user(Evas_Smart_Class *sc)
{
	debugf("_patcher_smart_set_user\n");
	// function overloading 
	sc->add = _patcher_smart_add;
	sc->del = _patcher_smart_del;
	sc->move = _patcher_smart_move;
	sc->resize = _patcher_smart_resize;
	sc->calculate = _patcher_smart_calculate;
}

Evas_Object *
patcher_object_add(Evas_Object *_parent)
{
	debugf("_patcher_object_add\n");
	parent = _parent;
	Evas_Object *obj = evas_object_smart_add(evas_object_evas_get(_parent), _patcher_smart_class_new());
	parent = NULL;

	return obj;
}

void
patcher_object_dimension_set(Evas_Object *o, int sources, int sinks)
{
	debugf("_patcher_object_dimension_set\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	if( (priv->ncols == sources) && (priv->nrows == sinks) )
		return;

	_patcher_smart_deinit(o, true);

	priv->ncols = sources;
	priv->nrows = sinks;
	priv->active = priv->ncols && priv->nrows;

	_patcher_smart_init(o);
}

static inline void 
_patcher_object_connected_idx_set(Evas_Object *o, int src_idx, int snk_idx,
	Eina_Bool state)
{
	debugf("_patcher_object_connected_idx_set\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	if(state)
		priv->matrix[src_idx][snk_idx] |= CONNECTED;
	else
		priv->matrix[src_idx][snk_idx] &= ~CONNECTED;

	elm_glview_changed_set(priv->glview); // refresh
}

static inline void
_patcher_object_indirected_idx_set(Evas_Object *o, int src_idx, int snk_idx,
	int indirect)
{
	debugf("_patcher_object_indirected_idx_set\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	priv->matrix[src_idx][snk_idx] &= ~(FEEDBACK | INDIRECT);
	switch(indirect)
	{
		case 1:
			priv->matrix[src_idx][snk_idx] |= INDIRECT;
			break;
		case 0:
			// do nothing
			break;
		case -1:
			priv->matrix[src_idx][snk_idx] |= FEEDBACK;
			break;
	}

	elm_glview_changed_set(priv->glview); // refresh
}

void
patcher_object_connected_set(Evas_Object *o, intptr_t src_id,
	intptr_t snk_id, Eina_Bool state, int indirect)
{
	debugf("patcher_object_connected_set\n");
	patcher_t *priv = evas_object_smart_data_get(o);
	int src_idx = _patcher_object_source_idx_get(o, src_id);
	int snk_idx = _patcher_object_sink_idx_get(o, snk_id);
	if( (src_idx == -1) || (snk_idx == -1) )
		return;
		
	_patcher_object_connected_idx_set(o, src_idx, snk_idx, state);
	_patcher_object_indirected_idx_set(o, src_idx, snk_idx, indirect);

	if(!priv->realizing)
	{
		int ax = priv->ax;
		int ay = priv->ay;
		_mouse_move_raw2(priv, -1, -1);
		_mouse_move_raw2(priv, ax, ay);
		elm_glview_changed_set(priv->glview); // refresh
	}
}

void
patcher_object_source_id_set(Evas_Object *o, int src_idx, intptr_t id)
{
	debugf("patcher_object_source_id_set\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	if(priv->data.cols)
		priv->data.cols[src_idx] = id;
}

void
patcher_object_sink_id_set(Evas_Object *o, int snk_idx, intptr_t id)
{
	debugf("patcher_object_sink_id_set\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	if(priv->data.rows)
		priv->data.rows[snk_idx] = id;
}

void
patcher_object_source_color_set(Evas_Object *o, int src_idx, int col)
{
	debugf("patcher_object_source_color_set\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	char msg [7];
	sprintf(msg, "col,%02i", col);
	edje_object_signal_emit(priv->cols[src_idx], msg, PATCHER_UI);
}

void
patcher_object_sink_color_set(Evas_Object *o, int snk_idx, int col)
{
	debugf("patcher_object_sink_color_set\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	char msg [7];
	sprintf(msg, "col,%02i", col);
	edje_object_signal_emit(priv->rows[snk_idx], msg, PATCHER_UI);
}

void
patcher_object_source_label_set(Evas_Object *o, int src_idx, const char *label)
{
	debugf("patcher_object_source_label_set\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	edje_object_part_text_set(priv->cols[src_idx], "text.port", label);
}

void
patcher_object_sink_label_set(Evas_Object *o, int snk_idx, const char *label)
{
	debugf("patcher_object_sink_label_set\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	edje_object_part_text_set(priv->rows[snk_idx], "text.port", label);
}

void
patcher_object_source_group_set(Evas_Object *o, int src_idx, const char *group)
{
	debugf("patcher_object_source_group_set\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	edje_object_part_text_set(priv->cols[src_idx], "text.client", group);
}

void
patcher_object_sink_group_set(Evas_Object *o, int snk_idx, const char *group)
{
	debugf("patcher_object_sink_group_set\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	edje_object_part_text_set(priv->rows[snk_idx], "text.client", group);
}

void
patcher_object_realize(Evas_Object *o)
{
	debugf("patcher_object_realize\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	priv->realizing = true;

	for(int src_idx=0; src_idx<priv->ncols; src_idx++)
	{
		for(int snk_idx=0; snk_idx<priv->nrows; snk_idx++)
		{
			patcher_event_t evi [2] = {
				{
					.index = src_idx,
					.id = priv->data.cols[src_idx]
				},
				{
					.index = snk_idx,
					.id = priv->data.rows[snk_idx]
				}
			};

			evas_object_smart_callback_call(o, PATCHER_REALIZE_REQUEST, (void *)evi);
		}
	}

	priv->realizing = false;

	_mouse_move_raw(priv, 0, 0);
	_mouse_move_raw(priv, priv->sx, priv->sy);
	elm_glview_changed_set(priv->glview); // refresh
}

void
patcher_object_zoom_in(Evas_Object *o)
{
	debugf("patcher_object_zoom_in\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	_zoom_in(priv);
}

void
patcher_object_zoom_out(Evas_Object *o)
{
	debugf("patcher_object_zoom_out\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	_zoom_out(priv);
}

void
patcher_object_zoom_set(Evas_Object *o, float zoom)
{
	debugf("patcher_object_zoom_get\n");
	patcher_t *priv = evas_object_smart_data_get(o);

	if(zoom < ZOOM_MIN)
		zoom = ZOOM_MIN;

	priv->scale = zoom;
	_zoom(priv);
}
