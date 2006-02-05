
#include <stdio.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>

#include "terminal.h"
#include "pty.h"
#include "emul.h"

/* Only scroll if only shift is set, out of these */
#define TEMU_TERMINAL_SCROLL_MASK	(GDK_SHIFT_MASK|GDK_CONTROL_MASK|GDK_MOD1_MASK)

struct _TemuTerminalPrivate {
	TemuPty *pty;
	GtkIMContext *im_context;
	TemuEmul *emul;
};

enum {
	PROP_0, /* gtk wants property_id to be > 0 */
	PROP_WINDOW_TITLE,
	PROP_ICON_TITLE
};

enum {
	SIG_WINDOW_TITLE_CHANGED,
	SIG_ICON_TITLE_CHANGED,
	SIG_CHILD_DIED,
	SIG_LAST
};

static guint signals[SIG_LAST] = { 0 };

/*
 * object/memory management
 */

static void temu_terminal_init(TemuTerminal *terminal);

static void temu_terminal_realize(GtkWidget *widget);
static void temu_terminal_unrealize(GtkWidget *widget);

static void temu_terminal_destroy(GtkObject *object);
static void temu_terminal_set_property(GObject *object, guint id, const GValue *value, GParamSpec *pspec);
static void temu_terminal_get_property(GObject *object, guint id, GValue *value, GParamSpec *pspec);

static void temu_terminal_size_allocate(GtkWidget *widget, GtkAllocation *allocation);

static void temu_terminal_im_commit(GtkIMContext *im, gchar *text, gpointer data);
static void temu_terminal_im_preedit_changed(GtkIMContext *im, gpointer data);

static gboolean temu_terminal_key_press_event(GtkWidget *widget, GdkEventKey *event);

static gboolean temu_terminal_button_press_event(GtkWidget *widget, GdkEventButton *event);

static void temu_terminal_class_init(TemuTerminalClass *klass)
{
	GObjectClass *gobject_class;
	GtkWidgetClass *widget_class;
	TemuScreenClass *screen_class;

	gobject_class = G_OBJECT_CLASS(klass);
	widget_class = GTK_WIDGET_CLASS(klass);
	screen_class = TEMU_SCREEN_CLASS(klass);

	GTK_OBJECT_CLASS(klass)->destroy = temu_terminal_destroy;
	gobject_class->set_property = temu_terminal_set_property;
	gobject_class->get_property = temu_terminal_get_property;
	
	g_object_class_install_property(
		gobject_class,
		PROP_WINDOW_TITLE,
		g_param_spec_string(
			"window_title",
			"Window Title",
			"The terminal's window title",
			NULL,
			G_PARAM_READWRITE
		)
	);
	g_object_class_install_property(
		gobject_class,
		PROP_ICON_TITLE,
		g_param_spec_string(
			"icon_title",
			"Icon Title",
			"The terminal's icon window title",
			NULL,
			G_PARAM_READWRITE
		)
	);
	
	signals[SIG_WINDOW_TITLE_CHANGED] = g_signal_new(
		"window_title_changed",
		G_OBJECT_CLASS_TYPE(gobject_class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET(TemuTerminalClass, window_title_changed),
		NULL,
		NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0
	);
	signals[SIG_ICON_TITLE_CHANGED] = g_signal_new(
		"icon_title_changed",
		G_OBJECT_CLASS_TYPE(gobject_class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET(TemuTerminalClass, icon_title_changed),
		NULL,
		NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0
	);
	signals[SIG_CHILD_DIED] = g_signal_new (
		"child_died",
		G_TYPE_FROM_CLASS(gobject_class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET(TemuTerminalClass, child_died),
		NULL,
		NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0
	);

	widget_class->realize = temu_terminal_realize;
	widget_class->unrealize = temu_terminal_unrealize;
	widget_class->size_allocate = temu_terminal_size_allocate;
	widget_class->key_press_event = temu_terminal_key_press_event;
	widget_class->button_press_event = temu_terminal_button_press_event;
}

GType temu_terminal_get_type(void)
{
	static GtkType temu_terminal_type = 0;
	static const GTypeInfo temu_terminal_info = {
		sizeof(TemuTerminalClass),

		(GBaseInitFunc)NULL,
		(GBaseFinalizeFunc)NULL,

		(GClassInitFunc)temu_terminal_class_init,
		(GClassFinalizeFunc)NULL,
		(gconstpointer)NULL,

		sizeof(TemuTerminal),
		0,
		(GInstanceInitFunc)temu_terminal_init,

		(GTypeValueTable*)NULL,
	};
	
	if (temu_terminal_type == 0) {
		temu_terminal_type = g_type_register_static(
			TEMU_TYPE_SCREEN,
			"TemuTerminal",
			&temu_terminal_info,
			0
		);
	}

	return temu_terminal_type;
}

GtkWidget *temu_terminal_new(void)
{
	return GTK_WIDGET(g_object_new(temu_terminal_get_type(), NULL));
}

static void temu_terminal_init(TemuTerminal *terminal)
{
	TemuTerminalPrivate *priv;
	priv = terminal->priv = g_malloc0(sizeof(*terminal->priv));
	priv->emul = temu_emul_new(TEMU_SCREEN(terminal));
}

static void temu_terminal_realize(GtkWidget *widget)
{
	TemuTerminal *terminal = TEMU_TERMINAL(widget);
	TemuTerminalPrivate *priv = terminal->priv;
	TemuScreenClass *screen_class;
	GdkRectangle area;

	screen_class = g_type_class_peek(TEMU_TYPE_SCREEN);
	if (GTK_WIDGET_CLASS(screen_class)->realize) {
		GTK_WIDGET_CLASS(screen_class)->realize(widget);
	}

	priv->im_context = gtk_im_multicontext_new();
	gtk_im_context_set_client_window(priv->im_context, widget->window);

	g_signal_connect(G_OBJECT(priv->im_context), "commit",
		G_CALLBACK(temu_terminal_im_commit), terminal);
	g_signal_connect(G_OBJECT(priv->im_context), "preedit-changed",
		G_CALLBACK(temu_terminal_im_preedit_changed), terminal);

	gtk_im_context_focus_in(priv->im_context);
		
	/* HACK: do this properly, set to the cursor position */
	area.x = 1;
	area.y = 1;
	area.width = 1;
	area.height = 1;
	gtk_im_context_set_cursor_location(priv->im_context, &area);
}

static void temu_terminal_unrealize(GtkWidget *widget)
{
	TemuTerminal *terminal = TEMU_TERMINAL(widget);
	TemuTerminalPrivate *priv = terminal->priv;
	TemuScreenClass *screen_class;

	if (priv->im_context) {
		g_object_unref(G_OBJECT(priv->im_context));
		priv->im_context = NULL;
	}

	screen_class = g_type_class_peek(TEMU_TYPE_SCREEN);
	if (GTK_WIDGET_CLASS(screen_class)->unrealize) {
		GTK_WIDGET_CLASS(screen_class)->unrealize(widget);
	}
}

static void temu_terminal_destroy(GtkObject *object)
{
	TemuTerminal *terminal = TEMU_TERMINAL(object);
	TemuTerminalPrivate *priv = terminal->priv;
	TemuScreenClass *screen_class;

	g_return_if_fail (TEMU_IS_TERMINAL(object));

	if (!priv)
		goto chain_destroy;

	if (priv->pty) {
		temu_pty_destroy(priv->pty);
		priv->pty = NULL;
	}

	temu_emul_destroy(priv->emul);
	g_free(priv);
	terminal->priv = NULL;

chain_destroy:
	screen_class = g_type_class_peek(TEMU_TYPE_SCREEN);
	if (GTK_OBJECT_CLASS(screen_class)->destroy) {
		GTK_OBJECT_CLASS(screen_class)->destroy(object);
	}
}

static void temu_terminal_set_property(GObject *object, guint id, const GValue *value, GParamSpec *pspec)
{
	TemuTerminal *terminal = TEMU_TERMINAL(object);

	switch (id) {
		case PROP_WINDOW_TITLE:
			temu_terminal_set_window_title(terminal, g_value_get_string(value));
			break;
		case PROP_ICON_TITLE:
			temu_terminal_set_icon_title(terminal, g_value_get_string(value));
			break;
	}
}

static void temu_terminal_get_property(GObject *object, guint id, GValue *value, GParamSpec *pspec)
{
	TemuTerminal *terminal = TEMU_TERMINAL(object);

	switch (id) {
		case PROP_WINDOW_TITLE:
			g_value_set_string(value, terminal->window_title);
			break;
		case PROP_ICON_TITLE:
			g_value_set_string(value, terminal->icon_title);
			break;
	}
}

static void temu_terminal_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	TemuTerminal *terminal = TEMU_TERMINAL(widget);
	TemuScreen *screen = TEMU_SCREEN(widget);
	TemuTerminalPrivate *priv = terminal->priv;
	TemuScreenClass *screen_class;

	screen_class = g_type_class_peek(TEMU_TYPE_SCREEN);
	if (GTK_WIDGET_CLASS(screen_class)->size_allocate) {
		GTK_WIDGET_CLASS(screen_class)->size_allocate(widget, allocation);
	}

	if (priv->pty) {
		temu_pty_set_size(priv->pty,
			temu_screen_get_cols(screen),
			temu_screen_get_rows(screen));
	}
}

static void temu_terminal_im_commit(GtkIMContext *im, gchar *text, gpointer data)
{
	TemuTerminal *terminal = TEMU_TERMINAL(data);
	TemuTerminalPrivate *priv = terminal->priv;

	g_io_channel_write_chars(
		priv->pty->master,
		text, strlen(text),
		NULL, NULL
	);

	return;
}

static void temu_terminal_im_preedit_changed(GtkIMContext *im, gpointer data)
{
	TemuTerminal *terminal = TEMU_TERMINAL(data);
	TemuTerminalPrivate *priv = terminal->priv;
	gchar *text, *next;
	gint pos, x, b;
	
	gtk_im_context_get_preedit_string(priv->im_context, &text, NULL, &pos);
	
	fprintf(stderr, "FIXME: Pre-edit: (%d) ", pos);
	for (x = b = 0; x < pos; x++) {
		next = g_utf8_next_char(&text[b]);
		for (; b < next - text; b++)
			fprintf(stderr, "%c", text[b]);
	}

	fprintf(stderr, "\e[7m");
	next = g_utf8_next_char(&text[b]);
	for (; b < next - text; b++)
		fprintf(stderr, "%c", text[b]);

	fprintf(stderr, "\e[0m");
	if (b < strlen(text))
		fprintf(stderr, "%s", &text[b]);
	fprintf(stderr, "\n");

	g_free(text);
}

static gboolean temu_terminal_key_press_event(GtkWidget *widget, GdkEventKey *event)
{
	TemuTerminal *terminal = TEMU_TERMINAL(widget);
	TemuScreen *screen = TEMU_SCREEN(terminal);
	TemuTerminalPrivate *priv = terminal->priv;
	gchar buf[16];
	gint count;

	temu_screen_hide_pointer (screen);

	switch (event->keyval) {
	  case GDK_Page_Up:
		if (GDK_SHIFT_MASK == (event->state & TEMU_TERMINAL_SCROLL_MASK)) {
			temu_screen_scroll_back(screen, temu_screen_get_rows(screen) / 2);
			return TRUE;
		}
		break;
	  case GDK_Page_Down:
		if (GDK_SHIFT_MASK == (event->state & TEMU_TERMINAL_SCROLL_MASK)) {
			temu_screen_scroll_forward(screen, temu_screen_get_rows(screen) / 2);
			return TRUE;
		}
		break;
	}

	temu_screen_scroll_offset(screen, 0);

	/* rain - temporary hack to make the xim input-method work properly */
	if (!(event->state & GDK_MOD1_MASK)) {
		if (gtk_im_context_filter_keypress(priv->im_context, event))
			return TRUE;
	}

	if (temu_emul_translate(priv->emul, event, buf, &count)) {
		g_io_channel_write_chars(
			priv->pty->master,
			buf, count,
			NULL, NULL
		);

		return TRUE;
	} else {
//		fprintf(stderr, "Ignored keypress: %04x\n", event->keyval);
	}

	return FALSE;
}

static gboolean temu_terminal_button_press_event(GtkWidget *widget, GdkEventButton *event)
{
	TemuTerminal *terminal = TEMU_TERMINAL(widget);
	TemuTerminalPrivate *priv = terminal->priv;
	TemuScreenClass *screen_class;
	GtkClipboard *clipboard;
	gchar *text;

	temu_screen_show_pointer (TEMU_SCREEN(terminal));

	screen_class = g_type_class_peek(TEMU_TYPE_SCREEN);
	if (GTK_WIDGET_CLASS(screen_class)->button_press_event) {
		if (GTK_WIDGET_CLASS(screen_class)->button_press_event(widget, event))
			return TRUE;
	}

	if (event->button != 2 || event->type != GDK_BUTTON_PRESS)
		return FALSE;

	if (GTK_WIDGET_REALIZED(widget)) {
		clipboard = gtk_clipboard_get_for_display(gtk_widget_get_display(widget), GDK_SELECTION_PRIMARY);
	} else {
		clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY); /* wing it */
	}

	text = gtk_clipboard_wait_for_text(clipboard);
	if (text) {
		g_io_channel_write_chars(
			priv->pty->master,
			text, strlen(text),
			NULL, NULL
		);
		g_free(text);
	}

	return TRUE;
}

static gboolean temu_terminal_error_from_app(GIOChannel *chan, GIOCondition cond, gpointer data)
{
	TemuTerminal *terminal = data;
	TemuTerminalPrivate *priv = terminal->priv;

	if (priv->pty) {
		temu_pty_destroy(priv->pty);
		priv->pty = NULL;
	}

	g_signal_emit(terminal, signals[SIG_CHILD_DIED], 0);
	return TRUE;
}

static gboolean temu_terminal_display_from_app(GIOChannel *chan, GIOCondition cond, gpointer data)
{
	TemuTerminal *terminal = data;
	TemuTerminalPrivate *priv = terminal->priv;
	gchar buffer[8192];
	GIOStatus status;
	gsize count;

	for (;;) {
		status = g_io_channel_read_chars(
			chan,
			buffer,
			sizeof(buffer),
			&count,
			NULL
		);

		if (status != G_IO_STATUS_NORMAL)
			break;

		temu_emul_emulate(priv->emul, buffer, count);
		while ((count = temu_emul_get_responses(priv->emul, buffer, sizeof(buffer))) > 0) {
			g_io_channel_write_chars(
				priv->pty->master,
				buffer, count,
				NULL, NULL
			);
		}
	}

	return TRUE;
}

void temu_terminal_execve(TemuTerminal *terminal, const char *file, char *const argv[], char *const envp[])
{
	TemuScreen *screen = TEMU_SCREEN(terminal);
	TemuTerminalPrivate *priv = terminal->priv;

	if (priv->pty)
		return;

	priv->pty = temu_pty_new_execve(
		temu_terminal_display_from_app,
		temu_terminal_error_from_app,
		terminal,
		argv[0],
		argv,
		envp
	);

	temu_pty_set_size(priv->pty,
		temu_screen_get_cols(screen),
		temu_screen_get_rows(screen));
}

void temu_terminal_set_window_title(TemuTerminal *terminal, const gchar *title)
{
	if (terminal->window_title != NULL) {
		g_free(terminal->window_title);
	}

	terminal->window_title = title ? g_strdup(title) : NULL;

	g_signal_emit(terminal, signals[SIG_WINDOW_TITLE_CHANGED], 0);
}

void temu_terminal_set_icon_title(TemuTerminal *terminal, const gchar *title)
{
	if (terminal->icon_title != NULL) {
		g_free(terminal->icon_title);
	}

	terminal->icon_title = title ? g_strdup(title) : NULL;

	g_signal_emit(terminal, signals[SIG_ICON_TITLE_CHANGED], 0);
}
