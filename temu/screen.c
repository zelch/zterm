#include "config.h"

#include <X11/Xft/Xft.h>

#include <gdk/gdkpango.h>
#include <gdk/gdkx.h>

#include <gtk/gtk.h>

#include <fontconfig/fontconfig.h>

#ifdef HAVE_XKB
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#endif

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
static void temu_screen_destroy(GtkObject *object);

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

	GTK_OBJECT_CLASS (klass)->destroy = temu_screen_destroy;
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
	priv->resize_cell.ch.glyph = L' ';
	priv->resize_cell.attr.fg = TEMU_SCREEN_FG_DEFAULT;
	priv->resize_cell.attr.bg = TEMU_SCREEN_BG_DEFAULT;

	/* updates */
	priv->moves.next = priv->moves.prev = &priv->moves;
	priv->moves_chunk = g_mem_chunk_new("TemuScreen moves", sizeof(TScreenMove), 10*sizeof(TScreenMove), G_ALLOC_ONLY);
	priv->moves_free = NULL;

	/* cell screen */
	/*memset(&priv->screen_attr, 0, sizeof(temu_scr_attr_t));*/

	priv->scroll_top = 0;
	priv->scroll_offset = 0;
	priv->view_offset = 0;

	priv->width = 0;
	priv->height = 0;
	priv->lines = NULL;

	priv->visible_height = 24;
	temu_screen_resize(screen, 80, 100);

	/* on-screen */
	screen->font_width = 0;
	screen->font_height = 0;

	/* selections */
	priv->selected = FALSE;
	priv->select_x = priv->select_y = 0;

	/* options */
	priv->double_buffered = TRUE;

	priv->fontdesc = pango_font_description_from_string("Fixed 14");
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

#include "256colors.h"

		{ 255, 255, 255 },
	};

	TemuScreen *screen = TEMU_SCREEN(widget);
	TemuScreenPrivate *priv = screen->priv;

	GdkWindowAttr attributes;
	gint attributes_mask;
	GdkRectangle *urect;
	GdkPixmap *cursor_dot_pm;

	GdkDisplay *display = gtk_widget_get_display(widget);
	GdkVisual *visual = gtk_widget_get_visual(widget);
	GdkColormap *colormap = gtk_widget_get_colormap(widget);

	gint i;

	GTK_WIDGET_UNSET_FLAGS(widget, GTK_DOUBLE_BUFFERED);

	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.x = widget->allocation.x;
	attributes.y = widget->allocation.y;
	attributes.width = widget->allocation.width;
	attributes.height = widget->allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = visual;
	attributes.colormap = colormap;
	attributes.event_mask = gtk_widget_get_events(widget);
	attributes.event_mask |=	GDK_EXPOSURE_MASK
				|	GDK_KEY_PRESS_MASK
				|	GDK_KEY_RELEASE_MASK
				|	GDK_BUTTON_PRESS_MASK
				|	GDK_BUTTON_RELEASE_MASK
				|	GDK_POINTER_MOTION_MASK;
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

	cursor_dot_pm = gdk_pixmap_create_from_data(widget->window,
			"\0", 1, 1, 1,
			&widget->style->black,
			&widget->style->black);

	priv->cursor_bar = gdk_cursor_new_for_display(display, GDK_XTERM);
	priv->cursor_dot = gdk_cursor_new_from_pixmap(cursor_dot_pm, cursor_dot_pm,
			&widget->style->black,
			&widget->style->black,
			0, 0);

	gdk_window_set_cursor(widget->window, priv->cursor_bar);
	priv->cursor_current = priv->cursor_bar;
	g_object_unref (cursor_dot_pm);

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

	priv->xftdraw = XftDrawCreate(
		GDK_DISPLAY_XDISPLAY(display),
		GDK_DRAWABLE_XID(priv->pixmap),
		GDK_VISUAL_XVISUAL(visual),
		GDK_COLORMAP_XCOLORMAP(colormap)
	);

	for (i = 0; i < TEMU_SCREEN_MAX_COLORS; i++) {
		XRenderColor rcolor = {
			.red	= (colors[i][0] << 8) | colors[i][0],
			.green	= (colors[i][1] << 8) | colors[i][1],
			.blue	= (colors[i][2] << 8) | colors[i][2],
			.alpha	= 0xffff
		};

		XftColorAllocValue(
			GDK_DISPLAY_XDISPLAY(display),
			GDK_VISUAL_XVISUAL(visual),
			GDK_COLORMAP_XCOLORMAP(colormap),
			&rcolor,
			&priv->color[i]
		);

		priv->gdk_color[i].red = rcolor.red;
		priv->gdk_color[i].green = rcolor.green;
		priv->gdk_color[i].blue = rcolor.blue;
		gdk_rgb_find_color(colormap, &priv->gdk_color[i]);
	}

	temu_screen_set_font_description(screen, priv->fontdesc);

	urect = &priv->update_rect;
	urect->x = urect->y = 0;
	urect->width = priv->width;
	urect->height = priv->height;
	temu_screen_apply_updates(screen);
	gtk_widget_queue_draw(widget);
}

static void temu_screen_unrealize(GtkWidget *widget)
{
	TemuScreen *screen = TEMU_SCREEN(widget);
	TemuScreenPrivate *priv = screen->priv;
	gint i;
	GdkDisplay *display = gtk_widget_get_display(widget);
	GdkVisual *visual = gtk_widget_get_visual(widget);
	GdkColormap *colormap = gtk_widget_get_colormap(widget);

	if (priv->idle_id) {
		g_source_remove(priv->idle_id);
		priv->idle_id = 0;
	}

	if (widget->window) {
		gdk_window_hide(widget->window);
		gdk_window_destroy(widget->window);
		widget->window = NULL;
	}

	if (priv->xftdraw) {
		XftDrawDestroy(priv->xftdraw);

		for (i = 0; i < TEMU_SCREEN_MAX_COLORS; i++) {
			XftColorFree(
				GDK_DISPLAY_XDISPLAY(display),
				GDK_VISUAL_XVISUAL(visual),
				GDK_COLORMAP_XCOLORMAP(colormap),
				&priv->color[i]
			);
		}
		
		priv->xftdraw = NULL;
	}

	if (priv->cursor_bar) {
		gdk_cursor_unref (priv->cursor_bar);
		priv->cursor_bar = NULL;
	}

	if (priv->cursor_dot) {
		gdk_cursor_unref (priv->cursor_dot);
		priv->cursor_dot = NULL;
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

static void temu_screen_destroy(GtkObject *object)
{
	GtkWidget *widget;
	GtkWidgetClass *widget_class;

	TemuScreen *screen;
	TemuScreenPrivate *priv;

	gint i;

	widget = GTK_WIDGET(object);

	screen = TEMU_SCREEN(object);
	priv = screen->priv;
	if (!priv)
		goto destroy_chain;

	/* cell screen */
	for (i = 0; i < priv->height; i++)
		g_free(priv->lines[i].c);
	g_free(priv->lines);

	g_mem_chunk_destroy(priv->moves_chunk);
	if (priv->update_region)
	    gdk_region_destroy(priv->update_region);

	/* options */
	pango_font_description_free(priv->fontdesc);

	/* on-screen/realized */
	temu_screen_unrealize(widget);

	g_free(priv);
	screen->priv = NULL;

destroy_chain:
	widget_class = g_type_class_peek(GTK_TYPE_WIDGET);
	if (GTK_OBJECT_CLASS(widget_class)->destroy) {
		(GTK_OBJECT_CLASS(widget_class))->destroy(object);
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

	gdk_window_move_resize(
		widget->window,
		allocation->x,
		allocation->y,
		allocation->width,
		allocation->height
	);
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
		if (!priv->lines[y].attr.selected)
			continue;

		priv->lines[y].attr.selected = 0;
		for (x = 0; x < priv->width; x++) {
			priv->lines[y].c[x].attr.selected = 0;
			temu_screen_invalidate_cell(screen, x, y);
		}
	}

	priv->selected = FALSE;

	temu_screen_apply_updates(screen);
}

gboolean temu_screen_isbreak (TemuScreen *screen, temu_cell_t c0, temu_cell_t c1)
{
	if (c0.attr.wide)
		return FALSE;

	if (g_unichar_type (c0.ch.glyph) == g_unichar_type (c1.ch.glyph))
		return FALSE;

	if (g_unichar_isalnum (c0.ch.glyph) && g_unichar_isalnum(c1.ch.glyph))
		return FALSE;

	return TRUE;
}

void temu_screen_select(TemuScreen *screen, gint fx, gint fy, gint tx, gint ty, gint clicks) {
	GtkWidget *widget = GTK_WIDGET(screen);
	TemuScreenPrivate *priv = screen->priv;
	gint lines, buf_len;
	gint x, y;
	gchar *buffer, *p;
	GtkClipboard *clipboard;

	g_return_if_fail(fx >= 0 && fx < priv->width && fy >= 0 && fy < priv->visible_height);
	g_return_if_fail(tx >= 0 && tx < priv->width && ty >= 0 && ty < priv->visible_height);
	g_assert(clicks < 3);

	temu_screen_select_clear(screen);

	if (ty < fy) {
		gint tmp;
		tmp = fx; fx = tx; tx = tmp;
		tmp = fy; fy = ty; ty = tmp;
	} else if (ty == fy && tx < fx) {
		gint tmp;
		tmp = fx; fx = tx; tx = tmp;
	}

	lines = ty - fy + 1;

	fy = (fy + priv->scroll_offset + priv->view_offset + priv->height) % priv->height;
	ty = (ty + priv->scroll_offset + priv->view_offset + priv->height) % priv->height;

	switch (clicks) {
	  case 0: /* 1 click - character selection */
		break;
	  case 1: /* 2 clicks - word selection */
		while (fx > 0 && !temu_screen_isbreak (screen, priv->lines[fy].c[fx - 1], priv->lines[fy].c[fx]))
			fx--;
		break;
	  case 2: /* 3 clicks - line selection */
		fx = 0;
		break;
	}

	if (fx > priv->lines[fy].len) {
		fx = priv->lines[fy].len;
	}

	/* Slurp up whole double-width char at start */
	if (fx > 0 && priv->lines[fy].c[fx-1].attr.wide) {
		fx--;
	}

	switch (clicks) {
	  case 0: /* 1 click - character selection */
		break;
	  case 1: /* 2 clicks - word selection */
		while (tx < (priv->lines[ty].len-1) && !temu_screen_isbreak (screen, priv->lines[ty].c[tx], priv->lines[ty].c[tx + 1]))
			tx++;
		break;
	  case 2: /* 3 clicks - line selection */
		tx = priv->width - 1;
		break;
	}

	if (tx >= priv->lines[ty].len) {
		tx = priv->width - 1;
	}

	/* Slurp up whole double-width char at end */
	if (tx < (priv->width-1) && priv->lines[ty].c[tx].attr.wide) {
		tx++;
	}

	buf_len = lines*priv->width*6	/* utf-8 chars, overkill alloc :x */
		+lines			/* newlines for non-wrapped lines */
		+1;			/* NUL */
	p = buffer = g_alloca(buf_len);

	for (y = fy, x = fx; y != ty || x <= tx; ) {
		if (!( (p - buffer + 6 + 1) < (buf_len) )) {
			fprintf(stderr, "%d %d\n",
				(int)(p - buffer + 6 + 1),
				(int)(buf_len)
			);
			g_assert((p - buffer + 6 + 1) < (buf_len));
		}

		if (x < priv->lines[y].len
				&& (x <= 0 || !priv->lines[y].c[x-1].attr.wide)
				&& g_unichar_validate(priv->lines[y].c[x].ch.glyph)) {
			p += g_unichar_to_utf8(priv->lines[y].c[x].ch.glyph, p);
		}

		priv->lines[y].c[x].attr.selected = 1;
		temu_screen_invalidate_cell(screen, x, y);

		x++;
		if (x >= priv->width) {
			/*
			 * Behold, great evil.
			 *
			 * Don't copy trailing spaces of a line, at least not if we copy
			 * the newline.
			 *
			 * This really should happen above in the whole end of line check
			 * stuff, but I can't be bothered right now, maybe later.
			 */
			if (!priv->lines[y].attr.wrapped) {
				while (*--p == ' ');
				*++p = '\n';
				p++;
			}

			if (y == ty)
				break;

			x = 0;
			priv->lines[y].attr.selected = 1;
			y = (y + 1) % priv->height;
		}
	}
	priv->lines[ty].attr.selected = 1;
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

void temu_screen_show_pointer (TemuScreen *screen)
{
	TemuScreenPrivate *priv = screen->priv;

	if (priv->cursor_current != priv->cursor_bar) {
		gdk_window_set_cursor (GTK_WIDGET(screen)->window, priv->cursor_bar);
		priv->cursor_current = priv->cursor_bar;
	}
}

void temu_screen_hide_pointer (TemuScreen *screen)
{
	TemuScreenPrivate *priv = screen->priv;

	if (priv->cursor_current != priv->cursor_dot) {
		gdk_window_set_cursor (GTK_WIDGET(screen)->window, priv->cursor_dot);
		priv->cursor_current = priv->cursor_dot;
	}
}

static gboolean temu_screen_button_press_event(GtkWidget *widget, GdkEventButton *event)
{
	TemuScreen *screen = TEMU_SCREEN(widget);
	TemuScreenPrivate *priv = screen->priv;
	GTimeVal time;

	temu_screen_show_pointer (screen);

	if ((event->button != 1 && event->button != 3)
			|| event->type != GDK_BUTTON_PRESS)
		return FALSE;

	g_get_current_time (&time);
	// XXX: Assumes that time never goes backwards.
	time.tv_sec -= priv->last_click.tv_sec;
	time.tv_usec -= priv->last_click.tv_usec;
	if (time.tv_usec < 0) {
		time.tv_sec--;
		time.tv_usec += 1000000;
	}
	g_get_current_time (&priv->last_click);

	if (time.tv_sec || time.tv_usec >= 250000)
		priv->clicks = 0;
	else
		priv->clicks++;

	priv->clicks %= 3;

	temu_screen_select_clear(screen);
	if (event->button == 1) {
		priv->select_x = event->x / screen->font_width;
		priv->select_y = event->y / screen->font_height;
		if (priv->clicks)
		    temu_screen_select(screen, priv->select_x, priv->select_y, priv->select_x, priv->select_y, priv->clicks);
	} else {
		gint tx, ty;

		if (priv->select_x == -1)
			return FALSE;
		tx = event->x / screen->font_width;
		if (tx < 0) tx = 0;
		else if (tx >= priv->width) tx = priv->width-1;

		ty = event->y / screen->font_height;
		if (ty < 0) ty = 0;
		else if (ty >= priv->visible_height) ty = priv->visible_height-1;

		temu_screen_select(screen, priv->select_x, priv->select_y, tx, ty, priv->clicks);
	}

	return TRUE;
}

static gboolean temu_screen_button_motion_event(GtkWidget *widget, GdkEventMotion *event)
{
	TemuScreen *screen = TEMU_SCREEN(widget);
	TemuScreenPrivate *priv = screen->priv;
	gint tx, ty;

	temu_screen_show_pointer (screen);

	if (!(event->state & GDK_BUTTON1_MASK))
		return FALSE;
	if (priv->select_x == -1)
		return FALSE;

	tx = event->x / screen->font_width;
	if (tx < 0) tx = 0;
	else if (tx >= priv->width) tx = priv->width-1;

	ty = event->y / screen->font_height;
	if (ty < 0) ty = 0;
	else if (ty >= priv->visible_height) ty = priv->visible_height-1;

	temu_screen_select(
		screen,
		priv->select_x, priv->select_y,
		tx, ty, priv->clicks
	);

	return TRUE;
}

static gboolean temu_screen_button_release_event(GtkWidget *widget, GdkEventButton *event)
{
	temu_screen_show_pointer (TEMU_SCREEN(widget));
	return TRUE;
}

/*
 * Internal low-level screen management
 */

/* resize */
static void temu_screen_resize(TemuScreen *screen, gint width, gint height)
{
	GtkWidget *widget = GTK_WIDGET(screen);
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
			memset(&priv->lines[i].attr, 0, sizeof(temu_line_attr_t));
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

	/* Resize the back buffer */
	if (priv->double_buffered && GTK_WIDGET_REALIZED(widget)) {
		GdkPixmap *new_pixmap;

		new_pixmap = gdk_pixmap_new(
			widget->window,
			priv->width * screen->font_width,
			priv->visible_height * screen->font_height,
			-1
		);

		gdk_draw_drawable(
			new_pixmap,
			priv->gc,
			priv->pixmap,
			0, 0, 0, 0,
			priv->width * screen->font_width,
			priv->visible_height * screen->font_height
		);

		g_object_unref(priv->pixmap);
		priv->pixmap = new_pixmap;

		XftDrawChange(priv->xftdraw, GDK_DRAWABLE_XID(priv->pixmap));
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
		event.expose.region = gdk_region_new();

		temu_screen_expose (widget, &event.expose);

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
	while (y >= priv->height) y -= priv->height;

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
	TemuScreenPrivate *priv = screen->priv;
	TScreenMove *prev = priv->moves.prev;
	GdkRectangle clear_rect;

	gint sw = priv->width * screen->font_width;
	gint sh = priv->height * screen->font_height;

	if (
			(prev->base.x == rect->x) &&
			(prev->base.y == rect->y) &&
			(prev->base.width == rect->width) &&
			(prev->base.height == rect->height)
	   ) {
		/* Yay, we can batch it! */
		if (dx < 0)
			prev->rect.x -= dx;
		prev->rect.width -= abs(dx);
		if (dy < 0)
			prev->rect.y -= dy;
		prev->rect.height -= abs(dy);

		prev->dx += dx;
		prev->dy += dy;

		return TRUE;
	}


	/* Make sure this rect scrolls the -entire- region */
	if (dx) {
		if (prev->rect.x < rect->x)
			return FALSE;
		if ((prev->rect.x+prev->rect.width) > (rect->x+rect->width))
			return FALSE;
	}

	if (dy) {
		if (prev->rect.y < rect->y)
			return FALSE;
		if ((prev->rect.y+prev->rect.height) > (rect->y+rect->height))
			return FALSE;
	}

	/* This junk basically checks to see if either stuff got scrolled
	   off (and thus, does not matter), or this new region encompases
	   the old one (and thus, scrolls the scrolled stuff) */
	if (dx < 0) {
		if (prev->rect.x > 0 && (rect->x - prev->rect.x) != dx) {
			return FALSE;
		}
	} else if (dx > 0) {
		if ((prev->rect.x+prev->rect.width) < sw
		 && ((rect->x+rect->width) - (prev->rect.x+prev->rect.width)) != dx) {
			return FALSE;
		}
	}

	if (dy < 0) {
		if (prev->rect.y > 0 && (rect->y - prev->rect.y) != dy) {
			return FALSE;
		}
	} else if (dy > 0) {
		if ((prev->rect.y+prev->rect.height) < sh
		 && ((rect->y+rect->height) - (prev->rect.y+prev->rect.height)) != dy) {
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
		 != GDK_OVERLAP_RECTANGLE_IN) {
			return FALSE; /* Cleared area wasn't fully updated (copying) */
		}
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
		move->base = csg_rect;
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

	if (x > 0 && priv->lines[y].c[x-1].attr.wide) {
		temu_cell_t tmp_cell = priv->lines[y].c[x-1];

		tmp_cell.ch.glyph = L' ';
		tmp_cell.attr.wide = 0;
		priv->lines[y].c[x-1] = tmp_cell;
		temu_screen_invalidate_cell(screen, x-1, y);
	}

	priv->lines[y].c[x] = *cell;
	temu_screen_invalidate_cell(screen, x, y);

	if (cell->attr.wide && x < (priv->width-1)) {
		temu_cell_t tmp_cell = *cell;

		tmp_cell.ch.glyph = L' ';
		tmp_cell.attr.wide = 0;

		priv->lines[y].c[x+1] = tmp_cell;
		temu_screen_invalidate_cell(screen, x+1, y);
	}
}

static void temu_screen_fill_rect_internal(TemuScreen *screen, gint x, gint y, gint width, gint height, const temu_cell_t *cell)
{
	TemuScreenPrivate *priv = screen->priv;

	gint step = cell->attr.wide?2:1;
	gint x2 = x + width, y2 = y + height;
	gint i, j;

	gboolean shorten;

	shorten = (cell->ch.glyph == L' ');

	for (i = y; i < y2; i++) {
		gint mod_i = i % priv->height;

		if (x2 >= priv->lines[mod_i].len) {
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
#if 1 /* rain - slightly less awful hack */
			GtkWidget *toplevel;
			
			toplevel = gtk_widget_get_toplevel(widget);
			/*
			 * need_default_size makes the window use the requisition
			 * size (versus staring at the wall and ignoring it.)
			 */
			if (GTK_IS_WINDOW(toplevel))
				GTK_WINDOW(toplevel)->need_default_size = TRUE;
			
			gtk_widget_queue_resize(widget);
#else
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
#endif
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

	geom->base_width = screen->font_width;
	geom->base_height = screen->font_height;
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
void temu_screen_set_screen_attr(TemuScreen *screen, temu_scr_attr_t *attr)
{
	TemuScreenPrivate *priv = screen->priv;

	if (attr->negative != priv->screen_attr.negative) {
		GdkRectangle *urect = &priv->update_rect;

		urect->x = urect->y = 0;
		urect->width = priv->width;
		urect->height = priv->visible_height;

		temu_screen_apply_updates(screen);
	}

	priv->screen_attr = *attr;
}

temu_scr_attr_t *temu_screen_get_screen_attr(TemuScreen *screen)
{
	TemuScreenPrivate *priv = screen->priv;
	return &priv->screen_attr;
}

void temu_screen_set_line_attr(TemuScreen *screen, gint line, temu_line_attr_t *attr)
{
	TemuScreenPrivate *priv = screen->priv;

	g_return_if_fail(line >= 0 && line < priv->visible_height);

	line = (line + priv->scroll_offset) % priv->height;

	if (priv->lines[line].attr.wide != attr->wide
	 || priv->lines[line].attr.dhl != attr->dhl) {
		GdkRectangle *urect = &priv->update_rect;

		/* FIXME: Updates too often. */

		if (urect->width)
			temu_screen_apply_updates(screen);

		urect->x = 0;
		urect->y = line;
		urect->width = priv->width;
		urect->height = 1;

		temu_screen_apply_updates(screen);
	}

	priv->lines[line].attr = *attr;
}

temu_line_attr_t *temu_screen_get_line_attr(TemuScreen *screen, gint line)
{
	TemuScreenPrivate *priv = screen->priv;
	g_return_val_if_fail(line >= 0 && line < priv->visible_height, 0);
	return &priv->lines[line].attr;
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
		if ((x+cols+cells[i].attr.wide) >= priv->width)
			break;

		temu_screen_cell_set(screen, x+cols, y, &cells[i]);
		cols += cells[i].attr.wide?2:1;
	}

	if ((x+cols) > priv->lines[y].len)
		priv->lines[y].len = x+cols;

	if (written)
		*written = i;

	temu_screen_apply_updates(screen);

	return cols;
}

gint temu_screen_set_utf8_text(TemuScreen *screen, gint x, gint y, const gchar *text, temu_attr_t attr, temu_attr_t colors, gint length, gint *written)
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
		cell.ch.glyph = g_utf8_get_char_validated(&text[i], length - i);
		if (cell.ch.glyph == (gunichar)-1) {
			cell.ch.glyph = G_UNICHAR_UNKNOWN_GLYPH;
			i++;
		} else if (cell.ch.glyph == (gunichar)-2) {
			break;
		} else {
			i = g_utf8_next_char(&text[i]) - text;
		}

		cell.attr.wide = !!(g_unichar_iswide(cell.ch.glyph));

		if ((x+cols+cell.attr.wide) >= priv->width)
			break;

		temu_screen_cell_set(screen, x+cols, y, &cell);
		cols += cell.attr.wide?2:1;
	}

	if ((x+cols) > priv->lines[y].len)
		priv->lines[y].len = x+cols;

	if (written)
		*written = i;

	temu_screen_apply_updates(screen);

	return cols;
}

gint temu_screen_set_ucs4_text(TemuScreen *screen, gint x, gint y, const gunichar *text, temu_attr_t attr, temu_attr_t colors, gint length, gint *written)
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
		cell.ch.glyph = text[i];
		cell.attr.wide = !!(g_unichar_iswide(cell.ch.glyph));

		if ((x+cols+cell.attr.wide) >= priv->width)
			break;

		temu_screen_cell_set(screen, x+cols, y, &cell);
		cols += cell.attr.wide?2:1;
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
	if (!delta)
		return;

	/* Move the stuff currently on the screen */
	rect.x = 0;
	rect.y = priv->scroll_offset + priv->view_offset;
	rect.width = priv->width;
	rect.height = priv->visible_height;
	temu_screen_apply_move(screen, &rect, 0, delta);

	priv->view_offset = offset;

	/* Redraw the new area */
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

void temu_screen_emit_bell(TemuScreen *screen)
{
#ifdef HAVE_XKB /* XkbBell way */
	static Atom TerminalBellAtom = None;
	Display *dpy;

	dpy = GDK_WINDOW_XDISPLAY(GTK_WIDGET(screen)->window);

	if (TerminalBellAtom == None)
		TerminalBellAtom = XInternAtom(dpy, "TerminalBell", False);
	
	XkbBell(
		dpy,
		GDK_WINDOW_XWINDOW(GTK_WIDGET(screen)->window),
		100, /* percent */
		TerminalBellAtom
	);
#else /* simple beep */
	gdk_beep();
#endif
}

void temu_screen_set_color (TemuScreen *screen, guint n, GdkColor *color)
{
	XRenderColor rcolor;
	TemuScreenPrivate *priv = screen->priv;
	GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(screen));
	GdkVisual *visual = gtk_widget_get_visual(GTK_WIDGET(screen));
	GdkColormap *colormap = gtk_widget_get_colormap(GTK_WIDGET(screen));

	if (n >= TEMU_SCREEN_MAX_COLORS)
		return; // FIXME: Return an error of some kind?

	priv->gdk_color[n] = *color;

	rcolor.red      = priv->gdk_color[n].red;
	rcolor.green    = priv->gdk_color[n].green;
	rcolor.blue     = priv->gdk_color[n].blue;
	rcolor.alpha    = 0xffff;

	XftColorFree (
			GDK_DISPLAY_XDISPLAY (display),
			GDK_VISUAL_XVISUAL (visual),
			GDK_COLORMAP_XCOLORMAP (colormap),
			&priv->color[n]
			);

	XftColorAllocValue(
			GDK_DISPLAY_XDISPLAY(display),
			GDK_VISUAL_XVISUAL(visual),
			GDK_COLORMAP_XCOLORMAP(colormap),
			&rcolor,
			&priv->color[n]
			);

	gdk_rgb_find_color(colormap, &priv->gdk_color[n]);
}

void temu_screen_set_font (TemuScreen *screen, const char *font)
{
	PangoFontDescription *fontdesc;

	fontdesc = pango_font_description_from_string(font);
	temu_screen_set_font_description(screen, fontdesc);
}
