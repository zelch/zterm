#ifndef TEMU_SCREEN_PRIVATE_h
#define TEMU_SCREEN_PRIVATE_h 1

#include <glib.h>
#include <gdk/gdk.h>
#include <pango/pango.h>
#include <X11/Xft/Xft.h>

#include "screen.h"
#include "glyphcache.h"

typedef struct _TScreenMove TScreenMove;

struct _TScreenMove {
	TScreenMove *next, *prev;
	gint dx, dy;
	GdkRectangle rect;
};

struct _TemuScreenPrivate {
	GdkGC *gc;
	XftDraw *xftdraw;
	XftColor color[TEMU_SCREEN_MAX_COLORS];
	GdkColor gdk_color[TEMU_SCREEN_MAX_COLORS];

	PangoFontDescription *fontdesc;
	TGlyphCache *gcache;
	gint font_ascent;


	gint width, height;
	gint visible_height;
	temu_cell_t **screen;
	gint scroll_offset, view_offset;
	gboolean ignore_allocation;

	temu_cell_t clear_cell, resize_cell;

	TScreenMove moves;
	GMemChunk *moves_chunk;
	GTrashStack *moves_free;

	GdkRectangle update_rect;
	GdkRegion *update_region;

	gint idle_id;
};

#endif
