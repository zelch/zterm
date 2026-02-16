#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <gdk/gdk.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#define MAX_WINDOWS 8
#define MAX_COLOR_SCHEMES 8

typedef enum bind_actions {
	BIND_ACT_SWITCH = 0,
	BIND_ACT_CUT,
	BIND_ACT_CUT_HTML,
	BIND_ACT_PASTE,
	BIND_ACT_MENU,
	BIND_ACT_NEXT_TERM,
	BIND_ACT_PREV_TERM,
	BIND_ACT_OPEN_URI,
	BIND_ACT_CUT_URI,
} bind_actions_t;

typedef struct bind_s {
	guint		   state;
	guint		   key_min, key_max;
	guint		   base;
	struct bind_s *next;
	bind_actions_t action;
} bind_t;

/* Per-terminal configuration: command, env, and working directory. Separate from key bindings. */
typedef struct terminal_config_s {
	const char **argv;				/* NULL = use default shell */
	const char **env;				/* NULL = use inherited env */
	const char	*working_directory; /* NULL = use cwd */
} terminal_config_t;

typedef struct {
	const char **argv;
	const char **env;
} exec_t;

typedef struct {
	long	n;
	int		window_i;
	exec_t *cli_exec;
} cmd_t;

typedef struct bind_button_s {
	guint				  state;
	guint				  button;
	struct bind_button_s *next;
	bind_actions_t		  action;
} bind_button_t;

typedef struct color_scheme_s {
	GdkRGBA foreground;
	GdkRGBA background;
	char	name[32];
	char	action[32];
} color_scheme_t;

typedef struct window_s {
	GtkNotebook		   *notebook;
	GtkWidget		   *window;
	GtkWidget		   *header;
	GtkWidget		   *header_button;
	GtkWidget		   *menu;
	GMenuModel		   *menu_model;
	GMenuModel		   *menu_model_term_list;
	GtkEventController *key_controller;
	int					color_scheme;
	double				menu_x,
	  menu_y; // Where the mouse cursor was when we opened the menu.
	char *menu_hyperlink_uri;
} window_t;

typedef struct term_instance_s {
	int			 spawned;
	int			 moving;
	int			 window;
	const char **argv;				// NULL terminated.
	const char **env;				// If this term has a unique environment.
	const char	*working_directory; /* Working directory for spawn (NULL = default) */
	char		*hyperlink_uri;
	char		 title[256];
	GtkWidget	*term;
} term_instance_t;

typedef struct color_override_s {
	int						 index;
	GdkRGBA					 color;
	struct color_override_s *next;
} color_override_t;

typedef struct bind_ignore_s {
	guint				  state;
	struct bind_ignore_s *next;
} bind_ignore_t;

typedef struct terms_s {
	term_instance_t *active;

	gint		   n_active; // Total number of configured terms.
	gint		   alive;	 // Total number of 'alive' terms.
	char		  **envp;
	const char	 **startup_env; /* Copy of environ at process start (before config). */

	/* Configuration options. */
	bind_t			 *keys;
	terminal_config_t *terminal_configs; /* [0..MAX_TABS-1], per-terminal command/env/directory */
	bind_button_t	 *buttons;
	color_override_t *color_overrides;
	const char		**env; /* Global env: "KEY=value" or "!VAR", same format as per-terminal env */
	bind_ignore_t	 *ignores;
	char			 *font;
	bool			  audible_bell;
	char			 *word_char_exceptions;
	gdouble			  font_scale;
	bool			  scroll_on_output;
	bool			  scroll_on_keystroke;
	glong			  scrollback_lines;
	bool			  bold_is_bright;
	bool			  mouse_autohide;

	color_scheme_t color_schemes[MAX_COLOR_SCHEMES];
} terms_t;

extern GdkRGBA colors[256];

extern terms_t	terms;
extern window_t windows[MAX_WINDOWS];

extern GtkApplication *app;

extern int start_width;
extern int start_height;

extern unsigned int key_bind_mask;
extern unsigned int button_bind_mask;

static inline const char **strdupv (const char **v)
{
	return (const char **) g_strdupv ((char **) v);
}

static inline void strfreev (const char **v)
{
	return g_strfreev ((char **) v);
}

int _fprintf (bool print, FILE *io, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));

#define errorf(format, ...)                                                                                                      \
	_fprintf (true, stderr, "ERROR: %s %d (%s): " format "\n", __FILE__, __LINE__, __func__ __VA_OPT__ (, ) __VA_ARGS__)
#define infof(format, ...)                                                                                                       \
	_fprintf (true, stderr, "Info: %s %d (%s): " format "\n", __FILE__, __LINE__, __func__ __VA_OPT__ (, ) __VA_ARGS__)
#if DEBUG
#	define FUNC_DEBUG true
#	define debugf(format, ...)                                                                                                  \
		_fprintf (FUNC_DEBUG, stderr, "Debug: %s %d (%s): " format "\n", __FILE__, __LINE__, __func__ __VA_OPT__ (, ) __VA_ARGS__)
#else
#	define debugf(format, ...)                                                                                                  \
		_fnullf (stderr, "Debug: %s %d (%s): " format "\n", __FILE__, __LINE__, __func__ __VA_OPT__ (, ) __VA_ARGS__)
#endif
int		 _fnullf (const FILE *io, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
void	 do_copy (GSimpleAction *self, GVariant *parameter, gpointer user_data);
bool	 term_find (GtkWidget *term, int *i);
void	 term_set_window (int n, int window_i);
void	 term_switch (long n, const char **argv, const char **env, const char *working_directory, int window_i);
bool	 temu_parse_config (void);
void	 term_config (GtkWidget *term, int window_i);
bool	 zterm_parse_config ();
void	 zterm_save_config ();
void zterm_free_terminal_configs (void);
void zterm_ensure_terminal_configs (void);
void zterm_set_terminal_config (int index, const char **argv, const char **env, const char *working_directory);
void zterm_set_terminal_config_range (int base, int count, const char **argv, const char **env);
void zterm_set_global_env (const char **env);
gboolean process_uri (int64_t term_n, window_t *window, bind_actions_t action, double x, double y, bool menu);
void	 rebuild_menus (void);
void	 rebuild_term_list (long int window_n);
void	 do_preferences (GSimpleAction *self, GVariant *parameter, gpointer data);

// vim: set ts=4 sw=4 noexpandtab :
