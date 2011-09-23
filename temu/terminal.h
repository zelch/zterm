#ifndef TEMU_TERMINAL_h
#define TEMU_TERMINAL_h 1

#include <gtk/gtk.h>

#include "screen.h"

G_BEGIN_DECLS

typedef struct _TemuTerminalPrivate	TemuTerminalPrivate;
typedef struct _TemuTerminal		TemuTerminal;

struct _TemuTerminal {
	TemuScreen screen;

	gchar *window_title;
	gchar *icon_title;

	TemuTerminalPrivate *priv;

	void *client_data;
};

typedef struct _TemuTerminalClass	TemuTerminalClass;
struct _TemuTerminalClass {
	TemuScreenClass parent_class;

	/* signal handlers */
	void (*window_title_changed)	(TemuTerminal *);
	void (*icon_title_changed)	(TemuTerminal *);
	void (*child_died)		(TemuTerminal *);
};

#define TEMU_TYPE_TERMINAL	(temu_terminal_get_type())

#define TEMU_TERMINAL(obj)	(GTK_CHECK_CAST((obj), TEMU_TYPE_TERMINAL, TemuTerminal))
#define TEMU_IS_TERMINAL(obj)	GTK_CHECK_TYPE((obj), TEMU_TYPE_TERMINAL)

#define TEMU_TERMINAL_CLASS(klass)	(GTK_CHECK_CLASS_CAST((klass), TEMU_TYPE_TERMINAL, TemuTerminalClass))
#define TEMU_IS_TERMINAL_CLASS(obj)	GTK_CHECK_CLASS_TYPE((obj), TEMU_TYPE_TERMINAL)
#define TEMU_TERMINAL_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), TEMU_TYPE_TERMINAL, TemuTerminalClass))

GType		temu_terminal_get_type		(void) G_GNUC_CONST;

GtkWidget*	temu_terminal_new		(void);

void		temu_terminal_execve		(TemuTerminal *terminal,
						 const char *file,
						 char *const argv[],
						 char *const envp[]);

void temu_terminal_set_window_title		(TemuTerminal *terminal,
						 const gchar *title);
void temu_terminal_set_icon_title		(TemuTerminal *terminal,
						 const gchar *title);

void temu_terminal_insert_text(GtkWidget *widget, const char *text);
const char *temu_terminal_get_selection_text(GtkWidget *widget);

G_END_DECLS

#endif
