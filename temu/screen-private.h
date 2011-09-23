#ifndef TEMU_SCREEN_PRIVATE_h
#define TEMU_SCREEN_PRIVATE_h 1

#include <glib.h>
#include <gdk/gdk.h>
#include <pango/pango.h>
#include <X11/Xft/Xft.h>

#include "screen.h"
#include "glyphcache.h"

typedef struct _TScreenMove TScreenMove;
typedef struct _TScreenLine TScreenLine;

struct _TScreenMove {
	TScreenMove *next, *prev;
	gint dx, dy;
	GdkRectangle rect;
	GdkRectangle base;
};

struct _TScreenLine {
	temu_line_attr_t attr;
	gint len;	/* before newline */
	temu_cell_t *c;
};

struct _TemuScreenPrivate {
	GdkDrawable *pixmap;
	gint double_buffered;

	GdkGC *gc;
	XftDraw *xftdraw;
	XftColor color[TEMU_SCREEN_MAX_COLORS];
	GdkColor gdk_color[TEMU_SCREEN_MAX_COLORS];

	PangoFontDescription *fontdesc;
	TGlyphCache *gcache;
	gint font_ascent;

	gint width, height;
	gint visible_height;

	gint scroll_top;
	gint scroll_offset, view_offset;

	gint select_x, select_y;
	gboolean selected;

	gint clicks;
	GTimeVal last_click;

	temu_scr_attr_t screen_attr;
	TScreenLine *lines;

	temu_cell_t clear_cell, resize_cell;

	TScreenMove moves;
	GMemChunk *moves_chunk;
	GTrashStack *moves_free;

	GdkRectangle update_rect;
	GdkRegion *update_region;

	GdkCursor *cursor_bar;		/* I beam cursor */
	GdkCursor *cursor_dot;		/* The blank cursor */
	GdkCursor *cursor_current;	/* The current cursor */

	gint idle_id;
	char cur_selection[16*1024];
};

#endif
