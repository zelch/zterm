
#include <gdk/gdkx.h>
#include <fontconfig/fontconfig.h>

/* neenur neenur: */
#define PANGO_ENABLE_ENGINE
#include <pango/pangoxft.h>

#include "glyphcache.h"

TGlyphCache *glyph_cache_new(GtkWidget *widget,
                             PangoFontDescription *font_desc)
{
	TGlyphCache *cache;

	cache = g_new(TGlyphCache, 1);
	memset(cache, 0, sizeof(*cache));

	cache->context = gtk_widget_get_pango_context(widget);
	cache->lang = pango_context_get_language(cache->context);

	cache->display = gtk_widget_get_display(widget);
	cache->screen = gtk_widget_get_screen(widget);

	if (font_desc)
		glyph_cache_set_font(cache, font_desc);

	return cache;
}

void glyph_cache_destroy(TGlyphCache *cache) {
	if (cache->hash) {
		g_hash_table_destroy(cache->hash);
		g_mem_chunk_destroy(cache->chunk);
		/* font_set can't be unref'd apparently */
	}
	/* Neither can context or lang */
	g_free(cache);
}

void glyph_cache_set_font(TGlyphCache *cache, PangoFontDescription *font_desc)
{
	PangoFontMap *font_map;
	PangoFontMetrics *metrics;

	if (cache->hash) {
		g_hash_table_destroy(cache->hash);
		g_mem_chunk_destroy(cache->chunk);
		g_object_unref(cache->font_set);
	}

	cache->hash = g_hash_table_new(NULL, NULL);

	cache->chunk = g_mem_chunk_new(
		"Glyph information",
		sizeof(TGlyphInfo),
		512,
		G_ALLOC_ONLY
	);

	font_map = pango_xft_get_font_map(
		GDK_DISPLAY_XDISPLAY(cache->display),
		GDK_SCREEN_XNUMBER(cache->screen)
	);

	cache->font_set = pango_font_map_load_fontset(
		font_map,
		cache->context,
		font_desc,
		cache->lang
	);

	g_object_unref(font_map);

	metrics = pango_fontset_get_metrics(cache->font_set);

	cache->ascent = pango_font_metrics_get_ascent(metrics);
	cache->descent = pango_font_metrics_get_descent(metrics);

	cache->width = pango_font_metrics_get_approximate_char_width(metrics);
	cache->height = cache->ascent + cache->descent;

	pango_font_metrics_unref(metrics);
}

TGlyphInfo *glyph_cache_get_info(TGlyphCache *cache, gunichar glyph) {
	TGlyphInfo *gfi;
	XftFont *xftfont;
	PangoFont *font;
	PangoGlyph pglyph;
	PangoRectangle ink, logical;
	PangoFontMetrics *metrics;
	gint ascent, descent;
	gint x_offset;
	gint nominal_width;
	double scale_y, scale_x;

	if (!cache->hash)
		return NULL;

	gfi = g_hash_table_lookup(cache->hash, GINT_TO_POINTER(glyph));
	if (gfi)
		return gfi;

	font = pango_fontset_get_font(cache->font_set, glyph);

	pglyph = pango_xft_font_get_glyph(font, glyph);
	pango_font_get_glyph_extents(font, pglyph, &ink, &logical);

	metrics = pango_font_get_metrics(font, cache->lang);
	ascent = pango_font_metrics_get_ascent(metrics);
	descent = pango_font_metrics_get_descent(metrics);
	pango_font_metrics_unref(metrics);

	xftfont = pango_xft_font_get_font(font);

	x_offset = 0;

	nominal_width = cache->width;
	if (g_unichar_iswide(glyph))
		nominal_width *= 2;

	scale_x = scale_y = 1.0;

	if (logical.width > nominal_width) {
		if (logical.width)
			scale_x = (double)nominal_width / (double)logical.width;
		else
			scale_x = 1.0;
	}

	if (logical.height != cache->height) {
		double scale;

		if (ascent) {
			scale_y = (double)cache->ascent / (double)ascent;
		} else {
			scale_y = 1.0;
		}
		if (descent) {
			scale = (double)cache->descent / (double)descent;
			if (scale < scale_y)
				scale_y = scale;
		}
	}

	if (scale_x >= 1.0) {
		scale_x = 1.0;
		x_offset += (nominal_width - logical.width) / 2;
	}

	if (scale_x < 1.0 || scale_y != 1.0) {
		FcBool scalable;
		FcPattern *pattern;
		FcMatrix mat;

		FcPatternGetBool(xftfont->pattern, FC_SCALABLE, 0, &scalable);
		if (!scalable) {
			/* Bah. Need better handling of non-scalable fonts */
			if (scale_x < 1.0) scale_x = 1.0;
			scale_y = 1.0;
		} else if (scale_x < 1.0 || scale_y != 1.0) {
			pattern = FcPatternDuplicate(xftfont->pattern);

			mat.xx = scale_x; mat.xy = 0.0;
			mat.yx = 0.0; mat.yy = scale_y;

			FcPatternAddMatrix(pattern, FC_MATRIX, &mat);

			xftfont = XftFontOpenPattern(
				GDK_DISPLAY_XDISPLAY(cache->display),
				pattern
			);
		}

		ascent = ascent * scale_y;
	}

	g_object_unref(font);

	gfi = g_chunk_new(TGlyphInfo, cache->chunk);
	gfi->font = xftfont;

	gfi->x_offset = PANGO_PIXELS(x_offset);

	gfi->y_offset = cache->ascent + (cache->ascent - ascent);
	gfi->y_offset = PANGO_PIXELS(gfi->y_offset);

	g_hash_table_insert(cache->hash, GINT_TO_POINTER(glyph), gfi);

	return gfi;
}
