/*
 * Rendering functions (off-screen and on-screen)
 */
#include "screen.h"
#include "screen-private.h"

void temu_screen_render_moves_xft(TemuScreen *screen, GdkRegion *inv_region)
{
	GtkWidget *widget = GTK_WIDGET(screen);
	TemuScreenPrivate *priv = screen->priv;
	TScreenMove *node, *next;

	node = priv->moves.next;
	if (node == &priv->moves)
		return;

	for (; node != &priv->moves; node = next) {
		GdkRectangle final_rect;

		next = node->next;

		final_rect = node->rect;
		final_rect.x += node->dx;
		final_rect.y += node->dy;

		if (!inv_region ||
		    gdk_region_rect_in(inv_region, &final_rect) != GDK_OVERLAP_RECTANGLE_IN)
		{
			gdk_draw_drawable(
				widget->window,
				priv->gc,
				widget->window,
				node->rect.x, node->rect.y,
				final_rect.x, final_rect.y,
				node->rect.width, node->rect.height
			);
		}

		node->prev->next = node->next;
		node->next->prev = node->prev;
		g_trash_stack_push(&priv->moves_free, node);
	}
	
	return;
}

static void temu_screen_fg_bg(TemuScreen *screen, temu_attr_t attr, gint *fg, gint *bg)
{
	if (GET_ATTR(attr, NEGATIVE)) {
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
			widget->window,
			priv->gc,
			x,
			y + gfi->y_offset + 1,
			x + screen->font_width - 1,
			y + gfi->y_offset + 1
		);

		if (GET_ATTR(cell->attr, UNDERLINE) >= 2)
		gdk_draw_line(
			widget->window,
			priv->gc,
			x,
			y + gfi->y_offset + 3,
			x + screen->font_width - 1,
			y + gfi->y_offset + 3
		);
	}

	if (GET_ATTR(cell->attr, OVERLINE)) {
		gdk_draw_line(
			widget->window,
			priv->gc,
			x,
			y + 1,
			x + screen->font_width - 1,
			y + 1
		);
	}

	if (GET_ATTR(cell->attr, OVERSTRIKE)) {
		gdk_draw_line(
			widget->window,
			priv->gc,
			x,
			y + (screen->font_height / 2),
			x + screen->font_width - 1,
			y + (screen->font_height / 2)
		);
	}

	if (GET_ATTR(cell->attr, FRAME) == 1) {
		gdk_draw_rectangle(
			widget->window,
			priv->gc,
			FALSE,
			x,
			y,
			screen->font_width - 1,
			screen->font_height - 1
		);
	} else if (GET_ATTR(cell->attr, FRAME) == 2) {
		gdk_draw_arc(
			widget->window,
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
	TemuScreenPrivate *priv = screen->priv;

	GdkRectangle clip;

	gint x1, y1, x2, y2;
	gint y;

	gdk_region_get_clipbox(region, &clip);

	x1 = clip.x / screen->font_width;
	x2 = (clip.x + clip.width - 1) / screen->font_width;
	y1 = clip.y / screen->font_height;
	y2 = (clip.y + clip.height - 1) / screen->font_height;

	if (x1 < 0) x1 = 0;
	if (x2 >= priv->width) x2 = priv->width - 1;
	if (y1 < 0) y1 = 0;
	if (y2 >= priv->height) y2 = priv->height - 1;

	clip.x = (x1 - 1)*screen->font_width;
	clip.width = (x2 - x1 + 2)*screen->font_width;
	clip.height = screen->font_height;

	for (y = y1; y <= y2; y++) {
		gint mod_y = (y + priv->scroll_offset + priv->view_offset) % priv->height;
		gint x, w;

		clip.y = y*screen->font_height;
		if (gdk_region_rect_in(region, &clip) == GDK_OVERLAP_RECTANGLE_OUT)
			continue;

		x = x1;
		if (x1 > 0 && GET_ATTR(priv->screen[mod_y][x1-1].attr, WIDE))
			x--;

		w = x2 - x1 + 1;

		temu_screen_render_line_bg(screen, x*screen->font_width, y*screen->font_height, &priv->screen[mod_y][x], w);
		temu_screen_render_line_text(screen, x*screen->font_width, y*screen->font_height, &priv->screen[mod_y][x], w);
	}
}
