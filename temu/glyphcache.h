#ifndef GLYPHCACHE_h
#define GLYPHCACHE_h 1

#include <glib.h>
#include <gtk/gtk.h>
#include <pango/pango.h>
#include <X11/Xft/Xft.h>

typedef struct _TGlyphInfo	TGlyphInfo;
typedef struct _TGlyphCache	TGlyphCache;

struct _TGlyphInfo {
	XftFont *font;
	gint x_offset, y_offset;
};

struct _TGlyphCache {
	/* these are all private, really. */
	GHashTable *hash;
	GMemChunk *chunk;

	GdkDisplay *display;
	GdkScreen *screen;

	PangoContext *context;
	PangoFontset *font_set;
	PangoLanguage *lang;

	gint ascent, descent;
	gint width, height;
};

#define glyph_cache_font_width(cache)	PANGO_PIXELS((cache)->width)
#define glyph_cache_font_ascent(cache)	PANGO_PIXELS((cache)->ascent)
#define glyph_cache_font_height(cache)	PANGO_PIXELS((cache)->height)

TGlyphCache *glyph_cache_new(GtkWidget *widget,
                             PangoFontDescription *font_desc);
void glyph_cache_destroy(TGlyphCache *cache);

void glyph_cache_set_font(TGlyphCache *cache, PangoFontDescription *font_desc);
TGlyphInfo *glyph_cache_get_info(TGlyphCache *cache, gunichar glyph);

#endif
