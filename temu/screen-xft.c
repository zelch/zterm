/*
 * Rendering functions (off-screen and on-screen)
 */
#include <gdk/gdkx.h>
#include "screen.h"
#include "screen-private.h"

void temu_screen_render_moves_xft(TemuScreen *screen, GdkRegion *inv_region)
{
	GtkWidget *widget = GTK_WIDGET(screen);
	TemuScreenPrivate *priv = screen->priv;
	TScreenMove *node, *next;
	GdkRectangle clip;

	node = priv->moves.next;
	if (node == &priv->moves)
		return;

	clip.x = clip.y = 0;
	clip.width = priv->width * screen->font_width;
	clip.height = priv->visible_height * screen->font_height;

	for (; node != &priv->moves; node = next) {
		GdkRectangle final_rect, clipped;

		next = node->next;

		final_rect = node->rect;
		final_rect.x += node->dx;
		final_rect.y += node->dy;

		gdk_rectangle_intersect(&clip, &final_rect, &clipped);

		if (priv->double_buffered) {
			/* If we're double-buffered, we don't have to
			   redraw the text -- we know it's in our back buffer */
			if (!inv_region ||
			    gdk_region_rect_in(inv_region, &clipped) != GDK_OVERLAP_RECTANGLE_IN)
			{
				/* Not a completely updated area, move it */
				gdk_draw_drawable(
					priv->pixmap,
					priv->gc,
					priv->pixmap,
					clipped.x - node->dx, clipped.y - node->dy,
					clipped.x, clipped.y,
					clipped.width, clipped.height
				);

				/* And copy it to the front buffer */
				gdk_draw_drawable(
					widget->window,
					priv->gc,
					priv->pixmap,
					clipped.x, clipped.y,
					clipped.x, clipped.y,
					clipped.width, clipped.height
				);
			}
		} else {
			/* Otherwise, redrawing is about the best we can do */
			gdk_region_union_with_rect(inv_region, &clipped);
		}

		node->prev->next = node->next;
		node->next->prev = node->prev;
		g_trash_stack_push(&priv->moves_free, node);
	}
	
	return;
}

static void temu_screen_fg_bg(TemuScreen *screen, temu_attr_t attr, gint *fg, gint *bg)
{
	TemuScreenPrivate *priv = screen->priv;

	if (GET_ATTR(attr, NEGATIVE) ^ GET_ATTR(priv->screen_attr, SCREEN_NEGATIVE)) {
		*fg = GET_ATTR(attr, BG);
		if (*fg == TEMU_SCREEN_BG_DEFAULT) *fg = 0;
		*bg = GET_ATTR(attr, FG);
		if (*bg == TEMU_SCREEN_FG_DEFAULT) *bg = 7;
	} else {
		*fg = GET_ATTR(attr, FG);
		if (*fg == TEMU_SCREEN_FG_DEFAULT) *fg = 7;
		*bg = GET_ATTR(attr, BG);
		if (*bg == TEMU_SCREEN_BG_DEFAULT) *bg = 0;
	}

	if (GET_ATTR(attr, BOLD)) *fg |= 0x8;
	if (GET_ATTR(attr, BLINK)) *bg |= 0x8;

	if (GET_ATTR(attr, SELECTED)) {
		if (GET_ATTR(attr, CURSOR)) {
			if (*bg != 15)	{ *fg = 0; *bg = 15; }
			else		{ *fg = 15; *bg = 7; }
		} else {
			if (*bg != 7)	{ *fg = 0; *bg = 7; }
			else		{ *fg = 7; *bg = 8; }
		}
	} else if (GET_ATTR(attr, CURSOR)) {
		if (*bg != 7)	{ *fg = 0; *bg = 7; }
		else		{ *fg = 0; *bg = 15; }
	}
}

static void temu_screen_render_line_bg(TemuScreen *screen, gint x, gint y, const temu_cell_t *cell, gint count)
{
	TemuScreenPrivate *priv = screen->priv;

	gint w;
	gint fg, bg;
	gint last_bg;

	temu_screen_fg_bg(screen, cell->attr, &fg, &bg);
	for (;;) {
		w = 0;
		last_bg = bg;
		for (;;) {
			w++; cell++;
			if (!--count)
				break;

			temu_screen_fg_bg(screen, cell->attr, &fg, &bg);

			if (bg != last_bg)
				break;
		}

		w *= screen->font_width;

		XftDrawRect(
			priv->xftdraw,
			&priv->color[last_bg],
			x, y, w, screen->font_height
		);

		x += w;
		
		if (!count)
			break;
	}
}

static void temu_screen_render_char_effects(TemuScreen *screen, gint x, gint y, const temu_cell_t *cell, gint fg, const TGlyphInfo *gfi)
{
	GtkWidget *widget = GTK_WIDGET(screen);
	TemuScreenPrivate *priv = screen->priv;

	gdk_gc_set_foreground(priv->gc, &priv->gdk_color[fg]);

	/* TODO: Italics! */

	if (GET_ATTR(cell->attr, UNDERLINE)) {
		gdk_draw_line(
			priv->pixmap,
			priv->gc,
			x,
			y + gfi->y_offset + 1,
			x + screen->font_width - 1,
			y + gfi->y_offset + 1
		);

		if (GET_ATTR(cell->attr, UNDERLINE) >= 2)
		gdk_draw_line(
			priv->pixmap,
			priv->gc,
			x,
			y + gfi->y_offset + 3,
			x + screen->font_width - 1,
			y + gfi->y_offset + 3
		);
	}

	if (GET_ATTR(cell->attr, OVERLINE)) {
		gdk_draw_line(
			priv->pixmap,
			priv->gc,
			x,
			y + 1,
			x + screen->font_width - 1,
			y + 1
		);
	}

	if (GET_ATTR(cell->attr, OVERSTRIKE)) {
		gdk_draw_line(
			priv->pixmap,
			priv->gc,
			x,
			y + (screen->font_height / 2),
			x + screen->font_width - 1,
			y + (screen->font_height / 2)
		);
	}

	if (GET_ATTR(cell->attr, FRAME) == 1) {
		gdk_draw_rectangle(
			priv->pixmap,
			priv->gc,
			FALSE,
			x,
			y,
			screen->font_width - 1,
			screen->font_height - 1
		);
	} else if (GET_ATTR(cell->attr, FRAME) == 2) {
		gdk_draw_arc(
			priv->pixmap,
			priv->gc,
			FALSE,
			x,
			y,
			screen->font_width - 1,
			screen->font_height - 1,
			0,
			360 * 64
		);
	}
}

static void temu_screen_render_line_text(TemuScreen *screen, gint x, gint y, const temu_cell_t *cell, gint count)
{
	TemuScreenPrivate *priv = screen->priv;

	gint w;

	TGlyphInfo *gfi;
	XftCharFontSpec glyphs[count];

	gint fg, bg;
	gint last_fg;

	temu_screen_fg_bg(screen, cell->attr, &fg, &bg);
	for (;;) {
		w = 0;
		last_fg = fg;
		for (;;) {
			gunichar glyph;

			glyph = cell->glyph;
			if (!g_unichar_isprint(glyph))
				glyph = 0xFFFD;

			gfi = glyph_cache_get_info(priv->gcache, glyph);

			if (!GET_ATTR(cell->attr, HIDDEN)) {
				glyphs[w].font = gfi->font;
				glyphs[w].ucs4 = glyph;
				glyphs[w].x = gfi->x_offset + x;
				glyphs[w].y = gfi->y_offset + y;
				w++;

				temu_screen_render_char_effects(screen, x, y, cell, fg, gfi);
			}

			x += screen->font_width;

			cell++;
			if (!--count)
				break;

			temu_screen_fg_bg(screen, cell->attr, &fg, &bg);

			if (fg != last_fg)
				break;
		}

		XftDrawCharFontSpec(
			priv->xftdraw,
			&priv->color[last_fg],
			glyphs,
			w
		);

		if (!count)
			break;
	}
}

void temu_screen_render_text_xft(TemuScreen *screen, GdkRegion *region)
{
	GtkWidget *widget = GTK_WIDGET(screen);
	TemuScreenPrivate *priv = screen->priv;

	GdkRectangle clip, draw;
	GdkRegion *draw_region;

	gint x1, y1, x2, y2;
	gint y;

	clip.x = clip.y = 0;
	clip.width = priv->width * screen->font_width;
	clip.height = priv->visible_height * screen->font_height;

	if (priv->update_region) {
		draw_region = gdk_region_rectangle(&clip);
		gdk_region_intersect(draw_region, priv->update_region);

		gdk_region_destroy(priv->update_region);
		priv->update_region = NULL;

		if (!priv->double_buffered)
			gdk_region_union(draw_region, region);
	} else if (!priv->double_buffered) {
		draw_region = gdk_region_rectangle(&clip);
		gdk_region_intersect(draw_region, region);
	} else {
		draw_region = NULL;
	}

	if (draw_region && gdk_region_empty(draw_region)) {
		gdk_region_destroy(draw_region);
		draw_region = NULL;
	}

	if (draw_region) {
		gdk_region_get_clipbox(draw_region, &clip);

		x1 = clip.x / screen->font_width;
		x2 = (clip.x + clip.width - 1) / screen->font_width;
		y1 = clip.y / screen->font_height;
		y2 = (clip.y + clip.height - 1) / screen->font_height;

		for (y = y1; y <= y2; y++) {
			gint mod_y = (y + priv->scroll_offset + priv->view_offset + priv->height) % priv->height;
			gint x, w;

			x = x1;
			if (x1 > 0 && GET_ATTR(priv->lines[mod_y].c[x1-1].attr, WIDE))
				x--;

			w = x2 - x1 + 1;

			draw.x = x*screen->font_width;
			draw.y = y*screen->font_height;
			draw.width = w*screen->font_width;
			draw.height = screen->font_height;

			if (gdk_region_rect_in(draw_region, &draw) == GDK_OVERLAP_RECTANGLE_OUT)
				continue;

			temu_screen_render_line_bg(screen, draw.x, draw.y, &priv->lines[mod_y].c[x], w);
			temu_screen_render_line_text(screen, draw.x, draw.y, &priv->lines[mod_y].c[x], w);
		}

		gdk_region_destroy(draw_region);
	}

	if (priv->double_buffered) {
		gdk_region_get_clipbox(region, &clip);

		gdk_gc_set_clip_region(priv->gc, region);
		gdk_draw_drawable(
			widget->window,
			priv->gc,
			priv->pixmap,
			clip.x, clip.y,
			clip.x, clip.y,
			clip.width, clip.height
		);
		gdk_gc_set_clip_region(priv->gc, NULL);
	}
}
