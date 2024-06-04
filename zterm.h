#ifndef _ZTERM_H
#define _ZTERM_H

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <vte/vte.h>

#define MAX_WINDOWS 8
#define MAX_COLOR_SCHEMES 8

typedef enum bind_actions {
	BIND_ACT_SWITCH	= 0,
	BIND_ACT_CUT,
	BIND_ACT_CUT_HTML,
	BIND_ACT_PASTE,
	BIND_ACT_MENU,
	BIND_ACT_NEXT_TERM,
	BIND_ACT_PREV_TERM,
	BIND_ACT_OPEN_URI,
} bind_actions_t;

typedef struct bind_s {
	guint state;
	guint key_min, key_max;
	guint base;
	struct bind_s *next;
	char *cmd;
	char **argv;
	char **env;
	bind_actions_t action;
} bind_t;

typedef struct {
	char *cmd;
	char **argv;
	char **env;
} exec_t;

typedef struct bind_button_s {
	guint state;
	guint button;
	struct bind_button_s *next;
	bind_actions_t action;
} bind_button_t;

typedef struct color_scheme_s {
	GdkRGBA foreground;
	GdkRGBA background;
	char name[32];
	char action[32];
} color_scheme_t;

typedef struct window_s {
	GtkNotebook *notebook;
	GtkWidget *window;
	GtkWidget *menu;
	GtkEventController *key_controller;
	int color_scheme;
} window_t;

typedef struct terms_s {
	struct {
		int spawned;
		int moving;
		int window;
		char *cmd;
		char **argv; // NULL terminated, only evaluated if cmd is NULL.
		char **env; // If this term has a unique environment.
		char *hyperlink_uri;
		GtkWidget *term;
	} *active;
	gint n_active; // Total number of configured terms.
	gint alive; // Total number of 'alive' terms.
	char **envp;

	/* Configuration options. */
	bind_t	*keys;
	bind_button_t *buttons;
	char	*font;
	gboolean audible_bell;
	char     word_char_exceptions[64];
	gdouble  font_scale;
	gboolean scroll_on_output;
	gboolean scroll_on_keystroke;
	gboolean rewrap_on_resize;
	glong    scrollback_lines;
	gboolean allow_bold;
	gboolean bold_is_bright;
	gboolean mouse_autohide;

	color_scheme_t color_schemes[MAX_COLOR_SCHEMES];
} terms_t;

extern GdkRGBA colors[256];

extern terms_t terms;
extern window_t windows[MAX_WINDOWS];

extern GtkApplication *app;

extern int start_width;
extern int start_height;

extern unsigned int bind_mask;

int _fprintf(bool print, FILE *io, const char *fmt, ...);
#define errorf(format, ...)		_fprintf(true, stderr, "ERROR: %s %d (%s): " format "\n", __FILE__, __LINE__, __func__ __VA_OPT__(,) __VA_ARGS__)
#define infof(format, ...)		_fprintf(true, stderr, "Info: %s %d (%s): " format "\n", __FILE__, __LINE__, __func__ __VA_OPT__(,) __VA_ARGS__)
#if DEBUG
#define FUNC_DEBUG	true
#define debugf(format, ...)		_fprintf(FUNC_DEBUG, stderr, "Debug: %s %d (%s): " format "\n", __FILE__, __LINE__, __func__ __VA_OPT__(,) __VA_ARGS__)
#else
#define debugf(format, ...)		_fnullf(stderr, "Debug: %s %d (%s): " format "\n", __FILE__, __LINE__, __func__ __VA_OPT__(,) __VA_ARGS__)
#endif
int _fnullf(FILE *io, const char *fmt, ...);
void do_copy (GSimpleAction *self, GVariant *parameter, gpointer user_data);
void term_set_window (int n, int window_i);
void term_switch (long n, char *cmd, char **argv, char **env, int window_i);
void temu_parse_config (void);
void term_config (GtkWidget *term, int window_i);
void rebuild_menus(void);

#endif // _ZTERM_H

// vim: set ts=4 sw=4 noexpandtab :
