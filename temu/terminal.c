
#include <stdio.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>

#include "terminal.h"
#include "pty.h"
#include "emul.h"

struct _TemuTerminalPrivate {
	TemuPty *pty;
	GtkIMContext *im_context;
	TemuEmul *emul;
};

/*
 * object/memory management
 */

static void temu_terminal_init(TemuTerminal *terminal);

static void temu_terminal_realize(GtkWidget *widget);
static void temu_terminal_unrealize(GtkWidget *widget);

static void temu_terminal_finalize(GObject *object);

static void temu_terminal_size_allocate(GtkWidget *widget, GtkAllocation *allocation);

static void temu_terminal_im_commit(GtkIMContext *im, gchar *text, gpointer data);
static gboolean temu_terminal_key_press_event(GtkWidget *widget, GdkEventKey *event);

static void temu_terminal_class_init(TemuTerminalClass *klass)
{
	GObjectClass *gobject_class;
	GtkWidgetClass *widget_class;
	TemuScreenClass *screen_class;

	gobject_class = G_OBJECT_CLASS(klass);
	widget_class = GTK_WIDGET_CLASS(klass);
	screen_class = TEMU_SCREEN_CLASS(klass);

	gobject_class->finalize = temu_terminal_finalize;
	widget_class->realize = temu_terminal_realize;
	widget_class->unrealize = temu_terminal_unrealize;
	widget_class->size_allocate = temu_terminal_size_allocate;
	widget_class->key_press_event = temu_terminal_key_press_event;
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

	screen_class = g_type_class_peek(TEMU_TYPE_SCREEN);
	if (GTK_WIDGET_CLASS(screen_class)->realize) {
		GTK_WIDGET_CLASS(screen_class)->realize(widget);
	}

	priv->im_context = gtk_im_multicontext_new();
	gtk_im_context_set_client_window(priv->im_context, widget->window);
	g_signal_connect(G_OBJECT(priv->im_context), "commit",
		G_CALLBACK(temu_terminal_im_commit), terminal);
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

static void temu_terminal_finalize(GObject *object)
{
	TemuTerminal *terminal = TEMU_TERMINAL(object);
	TemuTerminalPrivate *priv = terminal->priv;
	TemuScreenClass *screen_class;

	if (priv->pty) {
		temu_pty_destroy(priv->pty);
		priv->pty = NULL;
	}

	temu_emul_destroy(priv->emul);
	g_free(priv);

	screen_class = g_type_class_peek(TEMU_TYPE_SCREEN);
	if (G_OBJECT_CLASS(screen_class)->finalize) {
		G_OBJECT_CLASS(screen_class)->finalize(object);
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

static gboolean temu_terminal_key_press_event(GtkWidget *widget, GdkEventKey *event)
{
	TemuTerminal *terminal = TEMU_TERMINAL(widget);
	TemuTerminalPrivate *priv = terminal->priv;
	guchar buf[16];
	gint count;

	if (gtk_im_context_filter_keypress(priv->im_context, event))
		return TRUE;

	if (temu_emul_translate(priv->emul, event, buf, &count)) {
		g_io_channel_write_chars(
			priv->pty->master,
			buf, count,
			NULL, NULL
		);

		return TRUE;
	} else {
		fprintf(stderr, "Ignored keypress: %04x\n", event->keyval);
	}

	return FALSE;
}

static gboolean temu_terminal_error_from_app(GIOChannel *chan, GIOCondition cond, gpointer data)
{
	TemuTerminal *terminal = data;
	TemuTerminalPrivate *priv = terminal->priv;
	const gchar text[] = "\033[0;41;37;1mError.";
	temu_emul_emulate(priv->emul, text, sizeof(text)-1);
	gtk_main_quit();
	return FALSE;
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