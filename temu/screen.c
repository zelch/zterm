#include <X11/Xft/Xft.h>

#include <gdk/gdkpango.h>
#include <gdk/gdkx.h>

#include <gtk/gtk.h>

#include <fontconfig/fontconfig.h>

#include "screen.h"
#include "screen-private.h"
#include "screen-xft.h"
#include "glyphcache.h"

#define TEMU_SCREEN_UPDATE_DELAY_MS	5

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

static gboolean temu_screen_button_press_event(GtkWidget *widget, GdkEventButton *event);
static gboolean temu_screen_button_motion_event(GtkWidget *widget, GdkEventMotion *event);
static gboolean temu_screen_button_release_event(GtkWidget *widget, GdkEventButton *event);

static void temu_screen_fill_rect_internal(TemuScreen *screen, gint x, gint y, gint width, gint height, const temu_cell_t *cell);
static void temu_screen_invalidate_cell(TemuScreen *screen, gint x, gint y);
static void temu_screen_apply_updates(TemuScreen *screen);

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

	widget_class->button_press_event = temu_screen_button_press_event;
	widget_class->button_release_event = temu_screen_button_release_event;
	widget_class->motion_notify_event = temu_screen_button_motion_event;
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
	priv->screen_attr = 0;

	priv->scroll_top = 0;
	priv->scroll_offset = 0;
	priv->view_offset = 0;

	priv->width = 0;
	priv->height = 0;
	priv->lines = NULL;

	priv->visible_height = 25;
	temu_screen_resize(screen, 80, 100);

	/* on-screen */
	screen->font_width = 0;
	screen->font_height = 0;

	/* selections */
	priv->selected = FALSE;
	priv->select_x = priv->select_y = 0;

	/* options */
	priv->double_buffered = TRUE;

	priv->fontdesc = pango_font_description_new();
//	pango_font_description_set_family(priv->fontdesc, "Terminal");
//	pango_font_description_set_family(priv->fontdesc, "Sans");
//	pango_font_description_set_family(priv->fontdesc, "Bitstream Vera Sans Mono");
//	pango_font_description_set_family(priv->fontdesc, "FreeMono");
	pango_font_description_set_family(priv->fontdesc, "zanz646");
	pango_font_description_set_size(priv->fontdesc, 12 * PANGO_SCALE);
	pango_font_description_set_weight(priv->fontdesc, PANGO_WEIGHT_BOLD);

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
	TemuScreenPrivate *priv = screen->priv;

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
				|	GDK_KEY_RELEASE_MASK
				|	GDK_BUTTON_PRESS_MASK
				|	GDK_BUTTON_RELEASE_MASK
				|	GDK_BUTTON1_MOTION_MASK;
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

	if (priv->double_buffered) {
		priv->pixmap = gdk_pixmap_new(
			widget->window,
			widget->allocation.width,
			widget->allocation.height,
			-1
		);
	} else {
		priv->pixmap = g_object_ref(widget->window);
	}

	GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);

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
		priv->pixmap,
		priv->gc,
		TRUE,
		0, 0,
		widget->allocation.width,
		widget->allocation.height
	);

	priv->xftdraw = XftDrawCreate(
		GDK_DISPLAY_XDISPLAY(gtk_widget_get_display(widget)),
		GDK_DRAWABLE_XID(priv->pixmap),
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

	temu_screen_set_font_description(screen, priv->fontdesc);
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

	if (priv->pixmap) {
		g_object_unref(priv->pixmap);
		priv->pixmap = NULL;
	}

	if (priv->gcache) {
		glyph_cache_destroy(priv->gcache);
		priv->gcache = NULL;
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
		g_free(priv->lines[i].c);
	g_free(priv->lines);

	g_mem_chunk_destroy(priv->moves_chunk);

	/* options */
	pango_font_description_free(priv->fontdesc);

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
		GdkPixmap *new_pixmap;

		gdk_window_move_resize(
			widget->window,
			allocation->x,
			allocation->y,
			allocation->width,
			allocation->height
		);

		if (priv->double_buffered) {
			new_pixmap = gdk_pixmap_new(
				widget->window,
				allocation->width,
				allocation->height,
				-1
			);

			gdk_draw_drawable(
				new_pixmap,
				priv->gc,
				priv->pixmap,
				0, 0, 0, 0,
				allocation->width,
				allocation->height
			);
		} else {
			new_pixmap = g_object_ref(widget->window);
		}

		g_object_unref(priv->pixmap);
		priv->pixmap = new_pixmap;

		XftDrawChange(priv->xftdraw, GDK_DRAWABLE_XID(priv->pixmap));
	}
}

/*
 * Selections
 */

/* They're implemented like this, because it's easier to handle
   scrolling and such. */
void temu_screen_select_clear(TemuScreen *screen)
{
	TemuScreenPrivate *priv = screen->priv;
	gint y, x;

	if (!priv->selected)
		return;

	for (y = 0; y < priv->height; y++) {
		if (!GET_ATTR(priv->lines[y].attr, LINE_SELECTED))
			continue;

		SET_ATTR(priv->lines[y].attr, LINE_SELECTED, 0);
		for (x = 0; x < priv->width; x++) {
			SET_ATTR(priv->lines[y].c[x].attr, SELECTED, 0);
			temu_screen_invalidate_cell(screen, x, y);
		}
	}

	priv->selected = FALSE;

	temu_screen_apply_updates(screen);
}

void temu_screen_select(TemuScreen *screen, gint fx, gint fy, gint tx, gint ty) {
	GtkWidget *widget = GTK_WIDGET(screen);
	TemuScreenPrivate *priv = screen->priv;
	gint i, count;
	gint x, y;
	gchar *buffer, *p;
	GtkClipboard *clipboard;

	g_return_if_fail(fx >= 0 && fx < priv->width && fy >= 0 && fy < priv->visible_height);
	g_return_if_fail(tx >= 0 && tx < priv->width && ty >= 0 && ty < priv->visible_height);

	temu_screen_select_clear(screen);

	count = (ty*priv->width+tx) - (fy*priv->width+fx);
	if (count < 0) {
		gint tmp;
		tmp = fx; fx = tx; tx = tmp;
		tmp = fy; fy = ty; ty = tmp;
		count = -count;
	}
	count++;

	/* Slurp up the -whole- last line if we're past its end */
	ty = (ty + priv->scroll_offset + priv->view_offset + priv->height) % priv->height;
	if (tx >= priv->lines[ty].len) {
		count += priv->width - tx - 1;
	}

	p = buffer = g_alloca(
		count*6		/* utf-8 chars, overkill alloc :x */
		+(fy - ty + 1)	/* newlines for non-wrapped lines */
		+1		/* NUL */
	);

	x = fx;
	y = (fy + priv->scroll_offset + priv->view_offset + priv->height) % priv->height;
	SET_ATTR(priv->lines[y].attr, LINE_SELECTED, 1);
	for (i = 0; i < count; i++) {
		if (x < priv->lines[y].len
		 && (x <= 0 || !GET_ATTR(priv->lines[y].c[x-1].attr, WIDE))) {
			p += g_unichar_to_utf8(priv->lines[y].c[x].glyph, p);
		}

		SET_ATTR(priv->lines[y].c[x].attr, SELECTED, 1);
		temu_screen_invalidate_cell(screen, x, y);
		x++;
		if (x >= priv->width) {
			if (!GET_ATTR(priv->lines[y].attr, LINE_WRAPPED))
				*p++ = '\n';

			x = 0;
			y = (y + 1) % priv->height;
			if (count - i)
				SET_ATTR(priv->lines[y].attr, LINE_SELECTED, 1);
		}
	}

	*p = '\0';

	if (GTK_WIDGET_REALIZED(widget)) {
		clipboard = gtk_clipboard_get_for_display(gtk_widget_get_display(widget), GDK_SELECTION_PRIMARY);
	} else {
		clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY); /* wing it */
	}
	gtk_clipboard_set_text(clipboard, buffer, p - buffer);

	priv->selected = TRUE;

	temu_screen_apply_updates(screen);
}

static gboolean temu_screen_button_press_event(GtkWidget *widget, GdkEventButton *event)
{
	TemuScreen *screen = TEMU_SCREEN(widget);
	TemuScreenPrivate *priv = screen->priv;

	if (event->button != 1)
		return FALSE;

	temu_screen_select_clear(screen);
	priv->select_x = event->x / screen->font_width;
	priv->select_y = event->y / screen->font_height;

	return TRUE;
}

static gboolean temu_screen_button_motion_event(GtkWidget *widget, GdkEventMotion *event)
{
	TemuScreen *screen = TEMU_SCREEN(widget);
	TemuScreenPrivate *priv = screen->priv;
	gint tx, ty;

	if (!(event->state & GDK_BUTTON1_MASK))
		return FALSE;
	if (priv->select_x == -1)
		return FALSE;

	tx = event->x / screen->font_width;
	ty = event->y / screen->font_height;

	temu_screen_select(
		screen,
		priv->select_x, priv->select_y,
		tx, ty
	);

	return TRUE;
}

static gboolean temu_screen_button_release_event(GtkWidget *widget, GdkEventButton *event)
{
	TemuScreenPrivate *priv = TEMU_SCREEN(widget)->priv;
	priv->select_x = priv->select_y = -1;
	return TRUE;
}

/*
 * Internal low-level screen management
 */

/* resize */
static void temu_screen_resize(TemuScreen *screen, gint width, gint height)
{
	TemuScreenPrivate *priv = screen->priv;
	gint old_width, old_height;
	gint min_top;
	gint i;

	old_width = priv->width;
	old_height = priv->height;
	
	if (old_width != width) {
		priv->width = width;

		for (i = 0; i < priv->height; i++)
			priv->lines[i].c = g_realloc(priv->lines[i].c, width*sizeof(*priv->lines[i].c));

		temu_screen_fill_rect_internal(
			screen,
			old_width, 0,
			(priv->width - old_width), priv->height,
			&priv->resize_cell
		);
	}
	
	if (old_height != height) {
		priv->height = height;
		priv->lines = g_realloc(priv->lines, height * sizeof(*priv->lines));

		for (i = old_height; i < height; i++) {
			priv->lines[i].attr = 0;
			priv->lines[i].len = 0;
			priv->lines[i].c = g_malloc(priv->width*sizeof(*priv->lines[i].c));
		}

		temu_screen_fill_rect_internal(
			screen,
			0, old_height,
			priv->width, (priv->height - old_height),
			&priv->resize_cell
		);
	}

	/* Update top of scroll buffer */
	min_top = priv->scroll_offset + priv->visible_height;
	if (priv->scroll_top <= priv->scroll_offset)
		min_top -= priv->height;
	if (min_top > priv->scroll_top)
		priv->scroll_top = (min_top + priv->height) % priv->height;
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
		priv->idle_id = g_timeout_add_full(
			G_PRIORITY_LOW,
			TEMU_SCREEN_UPDATE_DELAY_MS,
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

static gboolean temu_screen_batch_move(TemuScreen *screen, GdkRectangle *rect, gint dx, gint dy)
{
	GtkWidget *widget = GTK_WIDGET(screen);
	TemuScreenPrivate *priv = screen->priv;
	TScreenMove *prev = priv->moves.prev;
	GdkRectangle clear_rect;

	gint sw = widget->allocation.width;
	gint sh = widget->allocation.height;

	/* This junk basically checks to see if either stuff got scrolled
	   off (and thus, does not matter), or this new region encompases
	   the old one (and thus, scrolls the scrolled stuff) */
	if (dx < 0) {
		if (prev->rect.x > 0 && (rect->x - prev->rect.x) != dx) {
			g_warning("not batched: move left without margin");
			return FALSE;
		}
	} else if (dx > 0) {
		if ((prev->rect.x+prev->rect.width) < sw
		 && ((rect->x+rect->width) - (prev->rect.x+prev->rect.width)) != dx) {
			g_warning("not batched: move right without margin");
			return FALSE;
		}
	}

	if (dy < 0) {
		if (prev->rect.y > 0 && (rect->y - prev->rect.y) != dy) {
			g_warning("not batched: move up without margin");
			return FALSE;
		}
	} else if (dy > 0) {
		if ((prev->rect.y+prev->rect.height) < sh
		 && ((rect->y+rect->height) - (prev->rect.y+prev->rect.height)) != dy) {
			g_warning("not batched: move down without margin");
			return FALSE;
		}
	}

	/* Now, we have to see if it was a copy or a move */
	if (prev->dx) {
		clear_rect.y = prev->rect.y;
		clear_rect.height = prev->rect.height;
		if (prev->dx < 0) {
			/* Moving left, right should be cleared */
			clear_rect.x = prev->rect.x + prev->rect.width + prev->dx;
			clear_rect.width = -prev->dx;
		} else {
			/* Moving right, left should be cleared */
			clear_rect.x = prev->rect.x;
			clear_rect.width = prev->dx;
		}

		if (clear_rect.x < 0) {
			clear_rect.width += clear_rect.x;
			clear_rect.x = 0;
		} else if ((clear_rect.x+clear_rect.width) > sw) {
			clear_rect.width = sw - clear_rect.x;
		}

		if (gdk_region_rect_in(priv->update_region, &clear_rect)
		 != GDK_OVERLAP_RECTANGLE_IN) {
			g_warning("not batched: without clear x");
			return FALSE; /* Cleared area wasn't fully updated (copying) */
		}
	}

	if (prev->dy) {
		clear_rect.x = prev->rect.x;
		clear_rect.width = prev->rect.width;
		if (prev->dy < 0) {
			/* Moving left, right should be cleared */
			clear_rect.y = prev->rect.y + prev->rect.height + prev->dy;
			clear_rect.height = -prev->dy;
		} else {
			/* Moving right, left should be cleared */
			clear_rect.y = prev->rect.y;
			clear_rect.height = prev->dy;
		}

		if (clear_rect.y < 0) {
			clear_rect.height += clear_rect.y;
			clear_rect.y = 0;
		} else if ((clear_rect.y+clear_rect.height) > sh) {
			clear_rect.height = sh - clear_rect.y;
		}

		if (gdk_region_rect_in(priv->update_region, &clear_rect)
		 != GDK_OVERLAP_RECTANGLE_IN)
			return FALSE; /* Cleared area wasn't fully updated (copying) */
	}

	/* Yay, we can batch it! */
	prev->dx += dx;
	prev->dy += dy;

	return TRUE;
}

static void temu_screen_apply_move(TemuScreen *screen, GdkRectangle *rect, gint cols, gint lines)
{
	TemuScreenPrivate *priv = screen->priv;
	GdkRectangle csg_rect;
	GdkRegion *region, *scroll, *stay, *csg;
	gint dy, dx;

	dx = cols * screen->font_width;
	dy = lines * screen->font_height;

	csg_rect = *rect;
	csg_rect.y -= priv->scroll_offset + priv->view_offset;
	temu_screen_rect_scale(screen, &csg_rect);

	/* batch it if we can */
	if (priv->moves.prev == &priv->moves	/* no previous move */
	 || !priv->update_region		/* no updates (copying) */
	 || !temu_screen_batch_move(screen, &csg_rect, dx, dy) /* try it. */
	) {
		/* failed, we have to add this one */
		TScreenMove *move;

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
	}

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
	return &priv->lines[y].c[x];
}

static void temu_screen_cell_set(TemuScreen *screen, gint x, gint y, const temu_cell_t *cell)
{
	TemuScreenPrivate *priv = screen->priv;

	if (x > 0 && GET_ATTR(priv->lines[y].c[x-1].attr, WIDE)) {
		temu_cell_t tmp_cell = priv->lines[y].c[x-1];

		tmp_cell.glyph = L' ';
		SET_ATTR(tmp_cell.attr, WIDE, 0);
		priv->lines[y].c[x-1] = tmp_cell;
		temu_screen_invalidate_cell(screen, x-1, y);
	}

	priv->lines[y].c[x] = *cell;
	temu_screen_invalidate_cell(screen, x, y);

	if (GET_ATTR(cell->attr, WIDE) && x < (priv->width-1)) {
		temu_cell_t tmp_cell = *cell;

		tmp_cell.glyph = L' ';
		SET_ATTR(tmp_cell.attr, WIDE, 0);

		priv->lines[y].c[x+1] = tmp_cell;
		temu_screen_invalidate_cell(screen, x+1, y);
	}
}

static void temu_screen_fill_rect_internal(TemuScreen *screen, gint x, gint y, gint width, gint height, const temu_cell_t *cell)
{
	TemuScreenPrivate *priv = screen->priv;

	gint step = GET_ATTR(cell->attr, WIDE)?2:1;
	gint x2 = x + width, y2 = y + height;
	gint i, j;

	gboolean shorten;

	shorten = (cell->glyph == L' ');

	for (i = y; i < y2; i++) {
		gint mod_i = i % priv->height;

		if (x2 > priv->lines[mod_i].len) {
			if (shorten)
				priv->lines[mod_i].len = x;
			else
				priv->lines[mod_i].len = x2;
		}

		for (j = x; j < x2; j += step)
			temu_screen_cell_set(screen, j, mod_i, cell);
	}

	temu_screen_apply_updates(screen);
}

static void temu_screen_move_lines_noupdate(TemuScreen *screen, gint lines, gint y, gint height)
{
	TemuScreenPrivate *priv = screen->priv;
	gint abslines = lines<0?-lines:lines;
	TScreenLine *saved;
	gint i;

	saved = g_alloca(sizeof(*saved) * abslines);

	if (lines < 0) {
		/* move them down */
		for (i = 0; i < abslines; i++) {
			gint mod_y = (y + height + i) % priv->height;
			saved[i] = priv->lines[mod_y];
		}

		for (i = height-1; i >= 0; i--) {
			gint mod_y = (y + i - lines) % priv->height;
			gint mod_y_from = (y + i) % priv->height;
			priv->lines[mod_y] = priv->lines[mod_y_from];
		}

		for (i = 0; i < abslines; i++) {
			gint mod_y = (y + i) % priv->height;
			priv->lines[mod_y] = saved[i];
		}
	} else {
		/* move them up */
		for (i = 0; i < lines; i++) {
			gint mod_y = (y + i - lines + priv->height) % priv->height;
			saved[i] = priv->lines[mod_y];
		}

		for (i = 0; i < height; i++) {
			gint mod_y = (y + i - lines + priv->height) % priv->height;
			gint mod_y_from = (y + i) % priv->height;
			priv->lines[mod_y] = priv->lines[mod_y_from];
		}

		for (i = 0; i < lines; i++) {
			gint mod_y = (y + i - lines + height) % priv->height;
			priv->lines[mod_y] = saved[i];
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
	temu_screen_render_text_xft(screen, event->region);

	return FALSE;
}

/*
 * Stuff for updating/getting options
 */

/* size stuff */
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

/* font stuff */
void temu_screen_set_font_description(TemuScreen *screen, PangoFontDescription *desc)
{
	GtkWidget *widget = GTK_WIDGET(screen);
	TemuScreenPrivate *priv = screen->priv;

	if (desc != priv->fontdesc /* hack :x */) {
		pango_font_description_free(priv->fontdesc);
		priv->fontdesc = desc;
	}

	if (priv->gcache)
		glyph_cache_destroy(priv->gcache);

	priv->gcache = glyph_cache_new(widget, desc);
	priv->font_ascent = glyph_cache_font_ascent(priv->gcache);
	screen->font_width = glyph_cache_font_width(priv->gcache);
	screen->font_height = glyph_cache_font_height(priv->gcache);
}

/* cell to fill resized areas with */
void temu_screen_set_resize_cell(TemuScreen *screen, const temu_cell_t *cell)
{
	TemuScreenPrivate *priv = screen->priv;
	priv->resize_cell = *cell;
}

/* global attr, line attr */
void temu_screen_set_screen_attr(TemuScreen *screen, guint attr)
{
	TemuScreenPrivate *priv = screen->priv;

	if (GET_ATTR_BASE(priv->screen_attr,SCREEN_UPDATE)
	 != GET_ATTR_BASE(attr,SCREEN_UPDATE)) {
		GdkRectangle *urect = &priv->update_rect;

		urect->x = urect->y = 0;
		urect->width = priv->width;
		urect->height = priv->visible_height;

		temu_screen_apply_updates(screen);
	}

	priv->screen_attr = attr;
}

guint temu_screen_get_screen_attr(TemuScreen *screen)
{
	TemuScreenPrivate *priv = screen->priv;
	return priv->screen_attr;
}

void temu_screen_set_line_attr(TemuScreen *screen, gint line, guint attr)
{
	TemuScreenPrivate *priv = screen->priv;

	g_return_if_fail(line >= 0 && line < priv->visible_height);

	line += priv->scroll_offset;

	if (GET_ATTR_BASE(priv->lines[line].attr,LINE_UPDATE)
	 != GET_ATTR_BASE(attr,LINE_UPDATE)) {
		GdkRectangle *urect = &priv->update_rect;

		if (urect->width)
			temu_screen_apply_updates(screen);

		urect->x = 0;
		urect->y = line;
		urect->width = priv->width;
		urect->height = 1;

		temu_screen_apply_updates(screen);
	}

	priv->lines[line].attr = attr;
}

guint temu_screen_get_line_attr(TemuScreen *screen, gint line)
{
	TemuScreenPrivate *priv = screen->priv;
	g_return_val_if_fail(line >= 0 && line < priv->visible_height, 0);
	return priv->lines[line].attr;
}

/*
 * Visible functions for updating the screen and such
 */

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
	gint mod_y;

	if (x < 0 || y < 0 || x >= priv->width || y >= priv->visible_height)
		return;

	mod_y = (y + priv->scroll_offset) % priv->height;

	if (x >= priv->lines[mod_y].len)
		priv->lines[mod_y].len = x + 1;

	temu_screen_cell_set(screen, x, mod_y, cell);
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

	if ((x+cols) > priv->lines[y].len)
		priv->lines[y].len = x+cols;

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

	if ((x+cols) > priv->lines[y].len)
		priv->lines[y].len = x+cols;

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

	if ((x+cols) > priv->lines[y].len)
		priv->lines[y].len = x+cols;

	if (written)
		*written = i;

	temu_screen_apply_updates(screen);

	return cols;
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

			if (x < priv->lines[mod_y].len) {
				priv->lines[mod_y].len += dx;
				if (priv->lines[mod_y].len < 0)
					priv->lines[mod_y].len = 0;
			}

			for (tx = x1; tx != x2; tx += xdir)
				priv->lines[mod_y].c[tx] = priv->lines[mod_y_from].c[tx - dx];
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

	if (priv->select_y >= y && priv->select_y < (y+height))
		priv->select_y -= rows;

	keep_lines = height - scroll_lines;
	clear_lines = height - keep_lines;

	y += priv->scroll_offset;

	/* scroll up in to scrollback */
	if (y == priv->scroll_offset && rows > 0) {
		GdkRectangle rect;
		gint min_top;

		/* FIXME: Make this support the view_offset instead of clearing it. */
		temu_screen_scroll_offset(screen, 0);

		/* scroll stuff up */
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

		/* move stuff down that's past the region (so it stays in the same spot) */
		if (height < priv->visible_height) {
			start = y + height;
			end = y + priv->visible_height - start;
			temu_screen_move_lines_noupdate(screen, -clear_lines, start, end);
		}

		/* clear the new area */
		start = y + height;
		end = clear_lines;

		temu_screen_fill_rect_internal(
			screen,
			0, start,
			priv->width, end,
			cell
		);

		/* Update top of scroll buffer */
		min_top = priv->scroll_offset + priv->visible_height;
		if (priv->scroll_top < priv->scroll_offset)
			min_top -= priv->height;
		if (min_top > priv->scroll_top)
			priv->scroll_top = (min_top + priv->height) % priv->height;

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

/* scrolling back */
void temu_screen_scroll_offset(TemuScreen *screen, gint offset)
{
	TemuScreenPrivate *priv = screen->priv;
	gint min_offset;
	gint delta;
	GdkRectangle rect, *urect;

	min_offset = priv->scroll_top - priv->scroll_offset;
	if (min_offset > 0) min_offset -= priv->height;

	offset = -offset; /* internally, we use a negative offset :x */

	if (offset > 0)
		offset = 0;
	if (offset < min_offset)
		offset = min_offset;

	delta = priv->view_offset - offset;

	rect.x = 0;
	rect.y = priv->scroll_offset + priv->view_offset;
	rect.width = priv->width;
	rect.height = priv->visible_height;
	temu_screen_apply_move(
		screen,
		&rect,
		0,
		delta
	);

	priv->view_offset = offset;

	urect = &priv->update_rect;
	urect->x = 0;
	urect->width = priv->width;
	if (delta < 0) {
		/* Scrolling up, refresh bottom */
		urect->y = priv->visible_height + delta;
		urect->height = -delta;
	} else {
		/* Scrolling down, refresh top */
		urect->y = 0;
		urect->height = delta;
	}
	temu_screen_apply_updates(screen);
}

void temu_screen_scroll_back(TemuScreen *screen, gint lines)
{
	TemuScreenPrivate *priv = screen->priv;
	temu_screen_scroll_offset(screen, -priv->view_offset + lines);
}

void temu_screen_scroll_top(TemuScreen *screen)
{
	TemuScreenPrivate *priv = screen->priv;
	temu_screen_scroll_offset(screen, priv->scroll_top);
}

void temu_screen_scroll_clear(TemuScreen *screen)
{
	TemuScreenPrivate *priv = screen->priv;
	temu_screen_scroll_offset(screen, 0);
	priv->scroll_top = priv->scroll_offset;
}

gint temu_screen_scroll_offset_max(TemuScreen *screen)
{
	TemuScreenPrivate *priv = screen->priv;
	gint min_offset;

	min_offset = priv->scroll_top - priv->scroll_offset;
	if (min_offset > 0) min_offset -= priv->height;

	return -min_offset;
}
