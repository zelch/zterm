#include <X11/Xft/Xft.h>

#include <gdk/gdkpango.h>
#include <gdk/gdkx.h>

#include <gtk/gtk.h>

#include <fontconfig/fontconfig.h>

#include "screen.h"
#include "screen-private.h"
#include "screen-xft.h"
#include "glyphcache.h"

static void temu_screen_resize(TemuScreen *screen, gint width, gint height);

/*
 * object/memory management
 */

static void temu_screen_init(TemuScreen *screen);
static void temu_screen_finalize(GObject *object);

static void temu_screen_realize(GtkWidget *widget);
static void temu_screen_unrealize(GtkWidget *widget);

static void temu_screen_size_request(GtkWidget *widget, GtkRequisition *requisition);
static void temu_screen_size_allocate(GtkWidget *widget, GtkAllocation *allocation);

static gboolean temu_screen_expose(GtkWidget *widget, GdkEventExpose *event);

static void temu_screen_fill_rect_internal(TemuScreen *screen, gint x, gint y, gint width, gint height, const temu_cell_t *cell);

static void temu_screen_class_init(TemuScreenClass *klass)
{
	GObjectClass *gobject_class;
	GtkWidgetClass *widget_class;

	gobject_class = G_OBJECT_CLASS(klass);
	widget_class = GTK_WIDGET_CLASS(klass);

	gobject_class->finalize = temu_screen_finalize;
	widget_class->realize = temu_screen_realize;
	widget_class->unrealize = temu_screen_unrealize;
	widget_class->size_request = temu_screen_size_request;
	widget_class->size_allocate = temu_screen_size_allocate;
	widget_class->expose_event = temu_screen_expose;
}

GType temu_screen_get_type(void)
{
	static GtkType temu_screen_type = 0;
	static const GTypeInfo temu_screen_info = {
		sizeof(TemuScreenClass),

		(GBaseInitFunc)NULL,
		(GBaseFinalizeFunc)NULL,
		
		(GClassInitFunc)temu_screen_class_init,
		(GClassFinalizeFunc)NULL,
		(gconstpointer)NULL,

		sizeof(TemuScreen),
		0,
		(GInstanceInitFunc)temu_screen_init,

		(GTypeValueTable*)NULL,
	};
	
	if (temu_screen_type == 0) {
		temu_screen_type = g_type_register_static(
			GTK_TYPE_WIDGET,
			"TemuScreen",
			&temu_screen_info,
			0
		);
	}

	return temu_screen_type;
}

GtkWidget *temu_screen_new(void)
{
	return GTK_WIDGET(g_object_new(temu_screen_get_type(), NULL));
}

static void temu_screen_init(TemuScreen *screen)
{
	GtkWidget *widget = GTK_WIDGET(screen);
	TemuScreenPrivate *priv;

	GTK_WIDGET_SET_FLAGS(widget, GTK_CAN_FOCUS);
	gtk_widget_set_redraw_on_allocate(widget, FALSE);

	priv = screen->priv = g_malloc0(sizeof(*screen->priv));

	/* default attributes */
	priv->resize_cell.glyph = L' ';
	SET_ATTR(priv->resize_cell.attr, FG, 7);
	SET_ATTR(priv->resize_cell.attr, BG, 0);

	/* updates */
	priv->moves.next = priv->moves.prev = &priv->moves;
	priv->moves_chunk = g_mem_chunk_new("TemuScreen moves", sizeof(TScreenMove), 10*sizeof(TScreenMove), G_ALLOC_ONLY);
	priv->moves_free = NULL;

	/* cell screen */
	priv->scroll_offset = 0;
	priv->view_offset = 0;

	priv->width = 0;
	priv->height = 0;
	priv->screen = NULL;

	priv->visible_height = 25;
	temu_screen_resize(screen, 80, 100);

	/* on-screen */
	screen->font_width = 0;
	screen->font_height = 0;
}

static void temu_screen_realize(GtkWidget *widget)
{
	const guint8 colors[TEMU_SCREEN_MAX_COLORS][3] = {
		{ 0, 0, 0 },
		{ 170, 0, 0 },
		{ 0, 170, 0 },
		{ 170, 85, 0 },
		{ 0, 0, 170 },
		{ 170, 0, 170 },
		{ 0, 170, 170 },
		{ 170, 170, 170 },

		{ 85, 85, 85 },
		{ 255, 85, 85 },
		{ 85, 255, 85 },
		{ 255, 255, 85 },
		{ 85, 85, 255 },
		{ 255, 85, 255 },
		{ 85, 255, 255 },
		{ 255, 255, 255 },

		{ 255, 255, 255 },
	};

	TemuScreen *screen = TEMU_SCREEN(widget);
	TemuScreenPrivate *priv;

	GdkWindowAttr attributes;
	gint attributes_mask;

	gint i;

	GTK_WIDGET_UNSET_FLAGS(widget, GTK_DOUBLE_BUFFERED);

	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.x = widget->allocation.x;
	attributes.y = widget->allocation.y;
	attributes.width = widget->allocation.width;
	attributes.height = widget->allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = gtk_widget_get_visual(widget);
	attributes.colormap = gtk_widget_get_colormap(widget);
	attributes.event_mask = gtk_widget_get_events(widget);
	attributes.event_mask |=	GDK_EXPOSURE_MASK
				|	GDK_KEY_PRESS_MASK
				|	GDK_KEY_RELEASE_MASK;
	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

	widget->window = gdk_window_new(
		gtk_widget_get_parent_window(widget),
		&attributes,
		attributes_mask
	);
	gdk_window_set_user_data(widget->window, widget);

	gdk_window_move_resize(
		widget->window,
		widget->allocation.x,
		widget->allocation.y,
		widget->allocation.width,
		widget->allocation.height
	);

	gdk_window_show(widget->window);

	GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);

	priv = screen->priv;

	{
		GdkColor black = { .red = 0, .green = 0, .blue = 0 };
		priv->gc = gdk_gc_new(widget->window);
		gdk_rgb_find_color(
			gdk_drawable_get_colormap(widget->window),
			&black
		);
		gdk_gc_set_foreground(priv->gc, &black);
	}
	
	gdk_draw_rectangle(
		widget->window,
		priv->gc,
		TRUE,
		0, 0,
		widget->allocation.width,
		widget->allocation.height
	);

	priv->xftdraw = XftDrawCreate(
		GDK_DISPLAY_XDISPLAY(gtk_widget_get_display(widget)),
		GDK_DRAWABLE_XID(widget->window),
		GDK_VISUAL_XVISUAL(gtk_widget_get_visual(widget)),
		GDK_COLORMAP_XCOLORMAP(gtk_widget_get_colormap(widget))
	);

	for (i = 0; i < TEMU_SCREEN_MAX_COLORS; i++) {
		XRenderColor rcolor = {
			.red	= (colors[i][0] << 8) | colors[i][0],
			.green	= (colors[i][1] << 8) | colors[i][1],
			.blue	= (colors[i][2] << 8) | colors[i][2],
			.alpha	= 0xffff
		};

		XftColorAllocValue(
			GDK_DISPLAY_XDISPLAY(gtk_widget_get_display(widget)),
			GDK_VISUAL_XVISUAL(gtk_widget_get_visual(widget)),
			GDK_COLORMAP_XCOLORMAP(gtk_widget_get_colormap(widget)),
			&rcolor,
			&priv->color[i]
		);

		priv->gdk_color[i].red = rcolor.red;
		priv->gdk_color[i].green = rcolor.green;
		priv->gdk_color[i].blue = rcolor.blue;
		gdk_rgb_find_color(gtk_widget_get_colormap(widget), &priv->gdk_color[i]);
	}

	priv->fontdesc = pango_font_description_new();
//	pango_font_description_set_family(priv->fontdesc, "Terminal");
//	pango_font_description_set_family(priv->fontdesc, "Sans");
//	pango_font_description_set_family(priv->fontdesc, "Bitstream Vera Sans Mono");
//	pango_font_description_set_family(priv->fontdesc, "FreeMono");
	pango_font_description_set_family(priv->fontdesc, "zanz646");

	pango_font_description_set_size(priv->fontdesc, 12 * PANGO_SCALE);
	pango_font_description_set_weight(priv->fontdesc, PANGO_WEIGHT_BOLD);
//	pango_font_description_set_style(priv->fontdesc, PANGO_STYLE_ITALIC);

	priv->gcache = glyph_cache_new(widget, priv->fontdesc);
	priv->font_ascent = glyph_cache_font_ascent(priv->gcache);
	screen->font_width = glyph_cache_font_width(priv->gcache);
	screen->font_height = glyph_cache_font_height(priv->gcache);
}

static void temu_screen_unrealize(GtkWidget *widget)
{
	TemuScreen *screen = TEMU_SCREEN(widget);
	TemuScreenPrivate *priv = screen->priv;
	gint i;

	if (priv->idle_id) {
		g_source_remove(priv->idle_id);
		priv->idle_id = 0;
	}

	if (widget->window) {
		g_object_unref(widget->window);
		widget->window = NULL;
	}

	if (priv->gcache) {
		glyph_cache_destroy(priv->gcache);
		priv->gcache = NULL;
	}

	if (priv->xftdraw) {
		XftDrawDestroy(priv->xftdraw);

		for (i = 0; i < TEMU_SCREEN_MAX_COLORS; i++) {
			XftColorFree(
				GDK_DISPLAY_XDISPLAY(gtk_widget_get_display(widget)),
				GDK_VISUAL_XVISUAL(gtk_widget_get_visual(widget)),
				GDK_COLORMAP_XCOLORMAP(gtk_widget_get_colormap(widget)),
				&priv->color[i]
			);
		}
		
		priv->xftdraw = NULL;
	}

	if (priv->gc) {
		g_object_unref(G_OBJECT(priv->gc));
		priv->gc = NULL;
	}

	GTK_WIDGET_UNSET_FLAGS(widget, GTK_REALIZED);
}

static void temu_screen_finalize(GObject *object)
{
	GtkWidget *widget;
	GtkWidgetClass *widget_class;

	TemuScreen *screen;
	TemuScreenPrivate *priv;

	gint i;

	widget = GTK_WIDGET(object);

	screen = TEMU_SCREEN(object);
	priv = screen->priv;

	/* cell screen */
	for (i = 0; i < priv->height; i++)
		g_free(priv->screen[i]);
	g_free(priv->screen);

	g_mem_chunk_destroy(priv->moves_chunk);

	/* on-screen/realized */
	temu_screen_unrealize(widget);

	g_free(priv);

	widget_class = g_type_class_peek(GTK_TYPE_WIDGET);
	if (G_OBJECT_CLASS(widget_class)->finalize) {
		(G_OBJECT_CLASS(widget_class))->finalize(object);
	}
}

static void temu_screen_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
	TemuScreen *screen;
	TemuScreenPrivate *priv;

	screen = TEMU_SCREEN(widget);
	priv = screen->priv;

	requisition->width = priv->width * screen->font_width;
	requisition->height = priv->visible_height * screen->font_height;
}

static void temu_screen_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	TemuScreen *screen;
	TemuScreenPrivate *priv;
	glong width, height, old_visible_height;

	if (!GTK_WIDGET_REALIZED(widget))
		return;

	screen = TEMU_SCREEN(widget);
	priv = screen->priv;

	if (!screen->font_width || !screen->font_height) {
		width = height = 0;
	} else {
		width = allocation->width / screen->font_width;
		height = allocation->height / screen->font_height;
	}

	old_visible_height = priv->visible_height;

	priv->visible_height = height;
	if (height > priv->height)
		priv->height = height;
	temu_screen_resize(screen, width, priv->height);

	if (old_visible_height < priv->visible_height)
		temu_screen_fill_rect_internal(
			screen,
			0, priv->scroll_offset + old_visible_height,
			width, priv->visible_height - old_visible_height,
			&priv->resize_cell
		);

	widget->allocation = *allocation;

	if (GTK_WIDGET_REALIZED(widget)) {
		gdk_window_move_resize(
			widget->window,
			allocation->x,
			allocation->y,
			allocation->width,
			allocation->height
		);

		XftDrawChange(priv->xftdraw, GDK_DRAWABLE_XID(widget->window));
	}
}

/*
 * Internal low-level screen management
 */

/* resize */
static void temu_screen_resize(TemuScreen *screen, gint width, gint height)
{
	TemuScreenPrivate *priv = screen->priv;
	gint old_width, old_height;
	gint i;

	old_width = priv->width;
	old_height = priv->height;
	
	if (old_width != width) {
		priv->width = width;

		for (i = 0; i < priv->height; i++)
			priv->screen[i] = g_realloc(priv->screen[i], width * sizeof(*priv->screen[i]));

		temu_screen_fill_rect_internal(
			screen,
			old_width, 0,
			(priv->width - old_width), priv->height,
			&priv->resize_cell
		);
	}
	
	if (old_height != height) {
		priv->height = height;
		priv->screen = g_realloc(priv->screen, height * sizeof(*priv->screen));

		for (i = old_height; i < height; i++)
			priv->screen[i] = g_malloc(priv->width * sizeof(*priv->screen[i]));

		temu_screen_fill_rect_internal(
			screen,
			0, old_height,
			priv->width, (priv->height - old_height),
			&priv->resize_cell
		);
	}
}

/* updates */
static void temu_screen_rect_scale(TemuScreen *screen, GdkRectangle *rect)
{
	rect->x *= screen->font_width;
	rect->y *= screen->font_height;
	rect->width *= screen->font_width;
	rect->height *= screen->font_height;
}

static gboolean temu_screen_idle_func(gpointer data)
{
	TemuScreen *screen = TEMU_SCREEN(data);
	TemuScreenPrivate *priv = screen->priv;
	GtkWidget *widget = GTK_WIDGET(screen);

	priv->idle_id = 0;

	if (!widget->window)
		return FALSE;

	if (priv->update_region || priv->moves.next != &priv->moves) {
		GdkEvent event;

		event.expose.type = GDK_EXPOSE;
		event.expose.window = g_object_ref(widget->window);
		event.expose.count = 0;
		if (priv->update_region) {
			event.expose.region = gdk_region_copy(priv->update_region);
		} else {
			event.expose.region = gdk_region_new();
		}

		gtk_widget_send_expose(widget, &event);

		gdk_region_destroy(event.expose.region);
		g_object_unref(widget->window);
	}

	return FALSE;
}

static void temu_screen_schedule_update(TemuScreen *screen)
{
	TemuScreenPrivate *priv = screen->priv;
	if (!priv->idle_id)
		priv->idle_id = g_idle_add_full(
			G_PRIORITY_LOW,
			temu_screen_idle_func,
			screen,
			NULL
		);
}

static void temu_screen_invalidate_cell(TemuScreen *screen, gint x, gint y)
{
	TemuScreenPrivate *priv = screen->priv;

	y -= priv->scroll_offset + priv->view_offset;
	while (y < 0) y += priv->height;

	if (!priv->update_rect.width) {
		priv->update_rect.x = x;
		priv->update_rect.y = y;
		priv->update_rect.width = 1;
		priv->update_rect.height = 1;
		return;
	}

	if (x < priv->update_rect.x) {
		priv->update_rect.width += priv->update_rect.x - x;
		priv->update_rect.x = x;
	} else if (x >= (priv->update_rect.x + priv->update_rect.width)) {
		priv->update_rect.width = (x - priv->update_rect.x + 1);
	}

	if (y < priv->update_rect.y) {
		priv->update_rect.height += priv->update_rect.y - y;
		priv->update_rect.y = y;
	} else if (y >= (priv->update_rect.y + priv->update_rect.height)) {
		priv->update_rect.height = (y - priv->update_rect.y + 1);
	}
}

static void temu_screen_apply_updates(TemuScreen *screen)
{
	TemuScreenPrivate *priv = screen->priv;
	GdkRectangle *urect = &priv->update_rect;

	if (!urect->width)
		return;

	temu_screen_rect_scale(screen, urect);

	if (!priv->update_region)
		priv->update_region = gdk_region_new();
	gdk_region_union_with_rect(priv->update_region, urect);

	temu_screen_schedule_update(screen);

	/**/
	urect->width = 0;
}

static void temu_screen_apply_move(TemuScreen *screen, GdkRectangle *rect, gint cols, gint lines)
{
	TemuScreenPrivate *priv = screen->priv;
	TScreenMove *move;
	GdkRectangle csg_rect;
	GdkRegion *region, *scroll, *stay, *csg;
	gint dy, dx;

	dx = cols * screen->font_width;
	dy = lines * screen->font_height;

	csg_rect = *rect;
	csg_rect.y -= priv->scroll_offset + priv->view_offset;
	temu_screen_rect_scale(screen, &csg_rect);

	/* add this to our list of moves */
	if (priv->moves_free) {
		move = g_trash_stack_pop(&priv->moves_free);
	} else {
		move = g_chunk_new(TScreenMove, priv->moves_chunk);
	}
	move->dx = dx;
	move->dy = dy;
	move->rect = csg_rect;
	move->prev = priv->moves.prev;
	move->prev->next = move;
	move->next = &priv->moves;
	move->next->prev = move;

	temu_screen_schedule_update(screen);

	/* update regions */
	region = priv->update_region;
	if (!region)
		return;

	/* area inside the scroll area */
	csg = gdk_region_rectangle(&csg_rect);
	scroll = gdk_region_copy(region);
	gdk_region_intersect(scroll, csg);
	gdk_region_offset(scroll, dx, dy);
	gdk_region_destroy(csg);

	/* area outside the scroll area */
	csg_rect.x += dx;
	csg_rect.y += dy;
	csg = gdk_region_rectangle(&csg_rect);
	stay = gdk_region_copy(region);
	gdk_region_subtract(stay, csg);
	gdk_region_destroy(csg);

	/* combine, apply */
	gdk_region_destroy(region);
	gdk_region_union(scroll, stay);
	gdk_region_destroy(stay);
	priv->update_region = scroll;
}

/* low-level cell modifications (absolute, not scroll offsetted coords) */
static inline const temu_cell_t *temu_screen_cell_get(TemuScreen *screen, gint x, gint y)
{
	TemuScreenPrivate *priv = screen->priv;
	return &priv->screen[y][x];
}

static void temu_screen_cell_set(TemuScreen *screen, gint x, gint y, const temu_cell_t *cell)
{
	TemuScreenPrivate *priv = screen->priv;

	if (x > 0 && GET_ATTR(priv->screen[y][x-1].attr, WIDE)) {
		temu_cell_t tmp_cell = priv->screen[y][x-1];

		tmp_cell.glyph = L' ';
		SET_ATTR(tmp_cell.attr, WIDE, 0);
		priv->screen[y][x-1] = tmp_cell;
		temu_screen_invalidate_cell(screen, x-1, y);
	}

	priv->screen[y][x] = *cell;
	temu_screen_invalidate_cell(screen, x, y);

	if (GET_ATTR(cell->attr, WIDE) && x < (priv->width-1)) {
		temu_cell_t tmp_cell = *cell;

		tmp_cell.glyph = L' ';
		SET_ATTR(tmp_cell.attr, WIDE, 0);

		priv->screen[y][x+1] = tmp_cell;
		temu_screen_invalidate_cell(screen, x+1, y);
	}
}

static void temu_screen_fill_rect_internal(TemuScreen *screen, gint x, gint y, gint width, gint height, const temu_cell_t *cell)
{
	TemuScreenPrivate *priv = screen->priv;

	gint step = GET_ATTR(cell->attr, WIDE)?2:1;
	gint x2 = x + width, y2 = y + height;
	gint i, j;

	for (i = y; i < y2; i++) {
		gint mod_i = i % priv->height;
		for (j = x; j < x2; j += step)
			temu_screen_cell_set(screen, j, mod_i, cell);
	}

	temu_screen_apply_updates(screen);
}

static void temu_screen_move_lines_noupdate(TemuScreen *screen, gint lines, gint y, gint height)
{
	TemuScreenPrivate *priv = screen->priv;
	gint abslines = lines<0?-lines:lines;
	temu_cell_t **saved;
	gint i;

	saved = g_alloca(sizeof(*saved) * abslines);

	if (lines < 0) {
		/* move them down */
		for (i = 0; i < abslines; i++) {
			gint mod_y = (y + height + i) % priv->height;
			saved[i] = priv->screen[mod_y];
		}

		for (i = height-1; i >= 0; i--) {
			gint mod_y = (y + i - lines) % priv->height;
			gint mod_y_from = (y + i) % priv->height;
			priv->screen[mod_y] = priv->screen[mod_y_from];
		}

		for (i = 0; i < abslines; i++) {
			gint mod_y = (y + i) % priv->height;
			priv->screen[mod_y] = saved[i];
		}
	} else {
		/* move them up */
		for (i = 0; i < lines; i++) {
			gint mod_y = (y + i - lines + priv->height) % priv->height;
			saved[i] = priv->screen[mod_y];
		}

		for (i = 0; i < height; i++) {
			gint mod_y = (y + i - lines + priv->height) % priv->height;
			gint mod_y_from = (y + i) % priv->height;
			priv->screen[mod_y] = priv->screen[mod_y_from];
		}

		for (i = 0; i < lines; i++) {
			gint mod_y = (y + i - lines + height) % priv->height;
			priv->screen[mod_y] = saved[i];
		}
	}
}

static void temu_screen_move_lines(TemuScreen *screen, gint lines, gint y, gint height)
{
	TemuScreenPrivate *priv = screen->priv;
	GdkRectangle rect;

	rect.x = 0; rect.width = priv->width;
	rect.y = y; rect.height = height;
	temu_screen_apply_move(screen, &rect, 0, -lines);

	temu_screen_move_lines_noupdate(screen, lines, y, height);
}

/*
 * Rendering
 */
static gboolean temu_screen_expose(GtkWidget *widget, GdkEventExpose *event)
{
	TemuScreen *screen = TEMU_SCREEN(widget);
	TemuScreenPrivate *priv = screen->priv;

	if (priv->idle_id) {
		g_source_remove(priv->idle_id);
		priv->idle_id = 0;
	}

	temu_screen_render_moves_xft(screen, priv->update_region);

	if (priv->update_region) {
		gdk_region_union(event->region, priv->update_region);
		gdk_region_destroy(priv->update_region);
		priv->update_region = NULL;
	}

	temu_screen_render_text_xft(screen, event->region);

	return FALSE;
}

/*
 * Visible functions for updating the screen and such
 */

void temu_screen_set_size(TemuScreen *screen, gint width, gint height, gint scrollback)
{
	TemuScreenPrivate *priv = screen->priv;
	GtkWidget *widget = GTK_WIDGET(screen);
	gint old_width = priv->width;

	if (width < 0)
		width = priv->width;
	if (height < 0)
		height = priv->visible_height;
	if (scrollback < 0)
		scrollback = priv->height;

	if (width != old_width || scrollback != priv->height)
		temu_screen_resize(screen, width, scrollback);

	if (width != old_width || height != priv->visible_height) {
		priv->visible_height = height;

		if (GTK_WIDGET_REALIZED(widget)) {
			/* gtk SUCKS. */
			/* If anyone finds a better way to make this resize happen,
			   PLEASE LET ME KNOW.  This is awful, terrible, and horrible,
			   all at the same time. */

			GtkWidget *toplevel;

			gtk_widget_queue_resize(widget);

			toplevel = gtk_widget_get_toplevel(widget);
			if (GTK_IS_WINDOW(toplevel)) {
				gtk_window_reshow_with_initial_size(GTK_WINDOW(toplevel));
				gtk_widget_queue_resize(widget);
			}
		}
	}
}

gint temu_screen_get_cols(TemuScreen *screen)
{
	TemuScreenPrivate *priv = screen->priv;
	return priv->width;
}

gint temu_screen_get_rows(TemuScreen *screen)
{
	TemuScreenPrivate *priv = screen->priv;
	return priv->visible_height;
}

void temu_screen_get_base_geometry_hints(TemuScreen *screen, GdkGeometry *geom, GdkWindowHints *mask)
{
	*mask = 0;

	geom->min_width = screen->font_width;
	geom->min_height = screen->font_height; 
	*mask |= GDK_HINT_MIN_SIZE;

	geom->base_width = 0;
	geom->base_height = 0;
	*mask |= GDK_HINT_BASE_SIZE;

	geom->width_inc = screen->font_width;
	geom->height_inc = screen->font_height;
	*mask |= GDK_HINT_RESIZE_INC;
}

/* text */
const temu_cell_t *temu_screen_get_cell(TemuScreen *screen, gint x, gint y)
{
	TemuScreenPrivate *priv = screen->priv;

	if (x < 0 || y < 0 || x >= priv->width || y >= priv->visible_height)
		return NULL;

	return temu_screen_cell_get(screen, x, (y + priv->scroll_offset) % priv->height);
}

void temu_screen_set_cell(TemuScreen *screen, gint x, gint y, const temu_cell_t *cell)
{
	TemuScreenPrivate *priv = screen->priv;

	if (x < 0 || y < 0 || x >= priv->width || y >= priv->visible_height)
		return;

	temu_screen_cell_set(screen, x, (y + priv->scroll_offset) % priv->height, cell);
	temu_screen_apply_updates(screen);
}

gint temu_screen_set_cell_text(TemuScreen *screen, gint x, gint y, const temu_cell_t *cells, gint length, gint *written)
{
	TemuScreenPrivate *priv = screen->priv;
	gint i, cols;

	if (written) *written = 0;
	if (y < 0) return 0;
	if (y >= priv->visible_height) return 0;
	y += priv->scroll_offset;
	y %= priv->height;

	cols = 0;

	for (i = 0; i < length; i++) {
		if ((x+cols+GET_ATTR(cells[i].attr, WIDE)) >= priv->width)
			break;

		temu_screen_cell_set(screen, x+cols, y, &cells[i]);
		cols += GET_ATTR(cells[i].attr, WIDE)?2:1;
	}

	if (written)
		*written = i;

	temu_screen_apply_updates(screen);

	return cols;
}

gint temu_screen_set_utf8_text(TemuScreen *screen, gint x, gint y, const gchar *text, temu_attr_t attr, gint length, gint *written)
{
	TemuScreenPrivate *priv = screen->priv;
	gint i, cols;
	temu_cell_t cell;

	if (written) *written = 0;
	if (y < 0) return 0;
	if (y >= priv->visible_height) return 0;
	y += priv->scroll_offset;
	y %= priv->height;

	cols = 0;

	cell.attr = attr;

	for (i = 0; i < length; ) {
		cell.glyph = g_utf8_get_char_validated(&text[i], length - i);
		if (cell.glyph == (gunichar)-1) {
			cell.glyph = G_UNICHAR_UNKNOWN_GLYPH;
			i++;
		} else if (cell.glyph == (gunichar)-2) {
			break;
		} else {
			i = g_utf8_next_char(&text[i]) - text;
		}

		SET_ATTR(cell.attr, WIDE, g_unichar_iswide(cell.glyph));

		if ((x+cols+GET_ATTR(cell.attr, WIDE)) >= priv->width)
			break;

		temu_screen_cell_set(screen, x+cols, y, &cell);
		cols += GET_ATTR(cell.attr, WIDE)?2:1;
	}

	if (written)
		*written = i;

	temu_screen_apply_updates(screen);

	return cols;
}

gint temu_screen_set_ucs4_text(TemuScreen *screen, gint x, gint y, const gunichar *text, temu_attr_t attr, gint length, gint *written)
{
	TemuScreenPrivate *priv = screen->priv;
	gint i, cols;
	temu_cell_t cell;

	if (written) *written = 0;
	if (y < 0) return 0;
	if (y >= priv->visible_height) return 0;
	y += priv->scroll_offset;
	y %= priv->height;

	cols = 0;

	cell.attr = attr;

	for (i = 0; i < length; i++) {
		cell.glyph = text[i];
		SET_ATTR(cell.attr, WIDE, g_unichar_iswide(cell.glyph));

		if ((x+cols+GET_ATTR(cell.attr, WIDE)) >= priv->width)
			break;

		temu_screen_cell_set(screen, x+cols, y, &cell);
		cols += GET_ATTR(cell.attr, WIDE)?2:1;
	}

	if (written)
		*written = i;

	temu_screen_apply_updates(screen);

	return cols;
}

/* cell to fill resized areas with */
void temu_screen_set_resize_cell(TemuScreen *screen, const temu_cell_t *cell)
{
	TemuScreenPrivate *priv = screen->priv;
	priv->resize_cell = *cell;
}

/* scrolling/movement */
void temu_screen_move_rect(TemuScreen *screen, gint x, gint y, gint width, gint height, gint dx, gint dy, const temu_cell_t *cell)
{
	TemuScreenPrivate *priv = screen->priv;

	gint tx, ty;
	gint fx, fy, fw, fh;
	gint x1, x2, y1, y2;
	gint xdir, ydir;

	/* Original position */
	if (x < 0) { width += x; x = 0; }
	if ((x+width) > priv->width) width = priv->width - x;
	if (width <= 0) return;

	if (y < 0) { height += y; y = 0; }
	if ((y+height) > priv->visible_height) height = priv->visible_height - y;
	if (height <= 0) return;

	/* Final position */
	fx = x + dx; fw = width;
	fy = y + dy; fh = height;
	if (fx < 0) { fw += fx; fx = 0; }
	if ((fx+fw) > priv->width) fw = priv->width - fx;

	if (fy < 0) { fh += fy; fy = 0; }
	if ((fy+fh) > priv->height) fh = priv->height - fy;

	y += priv->scroll_offset;
	fy += priv->scroll_offset;

	/* Move current text */
	if (fw && fh) {
		GdkRectangle rect;

		if (dx < 0)	{ x1 = fx; x2 = fx + fw; xdir = 1; }
		else		{ x2 = fx - 1; x1 = fx + fw - 1; xdir = -1; }
		if (dy < 0)	{ y1 = fy; y2 = fy + fh; ydir = 1; }
		else		{ y2 = fy - 1; y1 = fy + fh - 1; ydir = -1; }

		/* FIXME: Handle wide characters on boundaries */
		for (ty = y1; ty != y2; ty += ydir) {
			gint mod_y = ty % priv->height;
			gint mod_y_from = (ty - dy + priv->height) % priv->height;
			for (tx = x1; tx != x2; tx += xdir)
				priv->screen[mod_y][tx] = priv->screen[mod_y_from][tx - dx];
		}

		rect.x = x;
		rect.y = y;
		rect.width = fw;
		rect.height = fh;
		temu_screen_apply_move(screen, &rect, dx, dy);
	}

	/* Clear new areas */
	if (cell) {
		if (dx) {
			fy = y; fh = height;
	
			if (dx < 0) { fx = x + width + dx; fw = -dx; }
			else { fx = x; fw = dx; }
	
			temu_screen_fill_rect_internal(screen, fx, fy, fw, fh, cell);
		}
	
		if (dy) {
			fx = x; fw = width;
	
			if (dy < 0) { fy = y + height + dy; fh = -dy; }
			else { fy = y; fh = dy; }
	
			temu_screen_fill_rect_internal(screen, fx, fy, fw, fh, cell);
		}
	}
}

void temu_screen_do_scroll(TemuScreen *screen, gint rows, gint y, gint height, const temu_cell_t *cell)
{
	TemuScreenPrivate *priv = screen->priv;
	gint start, end;
	gint scroll_lines, keep_lines, clear_lines;

	if (!rows)
		return;

	if (y < 0) { height += y; y = 0; }
	if ((y+height) > priv->visible_height) height = priv->visible_height - y;
	if (height <= 0) return;

	scroll_lines = rows<0?-rows:rows;

	if (scroll_lines > height)
		rows = scroll_lines = height;

	keep_lines = height - scroll_lines;
	clear_lines = height - keep_lines;

	y += priv->scroll_offset;

	/* scroll up in to scrollback */
	if (y == priv->scroll_offset && rows > 0) {
		GdkRectangle rect;

		rect.x = 0;
		rect.y = y;
		rect.width = priv->width;
		rect.height = height;
		temu_screen_apply_move(
			screen,
			&rect,
			0,
			-rows
		);

		priv->scroll_offset += rows;
		priv->scroll_offset %= priv->height;

		if (height < priv->visible_height) {
			start = y + height;
			end = y + priv->visible_height - start;
			temu_screen_move_lines_noupdate(screen, -clear_lines, start, end);
		}

		start = y + height;
		end = clear_lines;

		temu_screen_fill_rect_internal(
			screen,
			0, start,
			priv->width, end,
			cell
		);

		return;
	}

	/* scroll */
	if (keep_lines) {
		if (rows < 0) {
			/* scroll down */
			start = y;
		} else {
			/* scroll up */
			start = y + clear_lines;
		}

		end = keep_lines;

		temu_screen_move_lines(screen, rows, start, end);
	}
	
	/* clear */
	if (rows < 0) {
		/* scroll down */
		start = y;
	} else {
		/* scroll up */
		start = y + keep_lines;
	}

	end = clear_lines;

	temu_screen_fill_rect_internal(
		screen,
		0, start,
		priv->width, end,
		cell
	);
}

void temu_screen_fill_rect(TemuScreen *screen, gint x, gint y, gint width, gint height, const temu_cell_t *cell)
{
	TemuScreenPrivate *priv = screen->priv;

	if (x < 0) { width += x; x = 0; }
	if ((x+width) > priv->width) width = priv->width - x;
	if (width <= 0) return;

	if (y < 0) { height += y; y = 0; }
	if ((y+height) > priv->visible_height) height = priv->visible_height - y;
	if (height <= 0) return;

	temu_screen_fill_rect_internal(
		screen,
		x, y + priv->scroll_offset,
		width, height,
		cell
	);
}
