//#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <vte/vte.h>
#include <regex.h>

extern char **environ;
#ifdef HAVE_LIBBSD
#  include <bsd/string.h>
#endif

GdkRGBA colors[256] = {
#include "256colors.h"
};

#define MAX_WINDOWS 8
#define MAX_COLOR_SCHEMES 8

int start_width = 1024;
int start_height = 768;

typedef enum bind_actions {
	BIND_ACT_SWITCH	= 0,
	BIND_ACT_CUT,
	BIND_ACT_PASTE,
	BIND_ACT_MENU,
	BIND_ACT_NEXT_TERM,
	BIND_ACT_PREV_TERM,
} bind_actions_t;

typedef struct bind_s {
	guint state;
	guint key_min, key_max;
	guint base;
	struct bind_s *next;
	char *cmd;
	bind_actions_t action;
} bind_t;

typedef struct color_scheme_s {
	GdkRGBA foreground;
	GdkRGBA background;
	char name[32];
} color_scheme_t;

typedef struct window_s {
	GtkNotebook *notebook;
	GtkWidget *window;
	GtkWidget *menu;
	GtkWidget *m_copy;
	GtkWidget *m_paste;
	GtkWidget *m_sep_0;
	GtkWidget *m_show_terms;
	GtkWidget *m_sep_0_1;
	GtkWidget *m_next_term;
	GtkWidget *m_prev_term;
	GtkWidget *m_sep_1;
	GtkWidget *m_t_decorate;
	GtkWidget *m_t_fullscreen;
	GtkWidget *m_t_tabbar;
	GtkWidget *m_sep_2;
	GtkWidget *m_reload_config;
	GtkWidget *m_sep_3;
	GtkWidget *m_move[MAX_WINDOWS];
	GtkWidget *m_sep_4;
	GtkWidget *m_color_scheme[MAX_COLOR_SCHEMES];
	GdkWindowState window_state;
	int color_scheme;
} window_t;

typedef struct terms_s {
	GtkWidget **active; // Indexes to the widgets for a given term.
	int *active_window; // Indexes to the window for a given term.
	gint n_active; // Total number of configured terms.
	gint alive; // Total number of 'alive' terms.
	char **envp;

	/* Configuration options. */
	bind_t	*keys;
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

terms_t terms;
window_t windows[MAX_WINDOWS];

static gboolean term_button_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data);
int new_window (void);
void destroy_window (int i);
void rebuild_menu_switch_lists (void);
void destroy_window_menu (int i);
void rebuild_window_menu (GtkWidget *window, long int i);

static void
temu_reorder (void)
{
	int window_i;
	int i, j;

	for (window_i = 0; window_i < MAX_WINDOWS; window_i++) {
		if (windows[window_i].window) {
			for (i = j = 0; i < terms.n_active; i++) {
				if (terms.active[i] && terms.active_window[i] == window_i) {
					gtk_notebook_reorder_child (windows[window_i].notebook, terms.active[i], j++);
				}
			}
		}
	}
}


static void temu_window_state_changed (GtkWidget *widget, GdkEventWindowState *event, void *data)
{
	window_t *window = data;

	window->window_state = event->new_window_state;
}

static void
temu_window_title_change (VteTerminal *terminal, long int n)
{
	char window_str[16] = { 0 };
	const char *title_str = "zterm";
	gchar new_str[64] = { 0 };
	int max_windows = 0;
	int window_i = terms.active_window[n];
	int notebook_i = 0;

	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (windows[i].window) {
			max_windows++;
		}
	}

	if (max_windows != 1) {
		snprintf (window_str, sizeof (window_str), "[%d]: ", window_i + 1);
	}

	if (vte_terminal_get_window_title(terminal)) {
		title_str = vte_terminal_get_window_title(terminal);
	}

	if (snprintf(new_str, sizeof(new_str) - 1, "%s%s [%ld]", window_str, title_str, n + 1) < 0) {
		return; // Memory allocation issue.
	}

	gtk_window_set_title(GTK_WINDOW(windows[window_i].window), new_str);

	notebook_i = gtk_notebook_page_num (windows[window_i].notebook, GTK_WIDGET (terminal));
	if (notebook_i >= 0) {
		gtk_notebook_set_menu_label_text (windows[window_i].notebook, GTK_WIDGET (terminal), new_str);
		gtk_notebook_set_tab_label_text (windows[window_i].notebook, GTK_WIDGET (terminal), new_str);
	}
}

static void
temu_window_title_changed(VteTerminal *terminal, gpointer data)
{
	gint n = (long) data;

	temu_window_title_change (terminal, n);
}

static void
prune_windows (void)
{
	int active = 0;

	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (windows[i].window) {
			int active_pages = gtk_notebook_get_n_pages(windows[i].notebook);
			active += active_pages;
			if (active_pages <= 0) {
				destroy_window (i);
			}
		}
	}

	if (active != terms.alive) {
		printf ("Found %d active tabs, but %d terms alive.\n", active, terms.alive);
	}

	if (active <= 0) {
		gtk_main_quit();
	}
}

static gboolean
term_died (VteTerminal *term, gpointer user_data)
{
	int status, n = -1, window_i = -1;

	waitpid (-1, &status, WNOHANG);

	for (int i = 0; i < terms.n_active; i++) {
		if (terms.active[i] == GTK_WIDGET(term)) {
			n = i;
			window_i = terms.active_window[n];
			break;
		}
	}
	if (n == -1 || window_i == -1) {
		printf ("Unable to find term that died.\n");
		return FALSE;
	}

	//printf ("Removing dead term %d from window %d.\n", n, window_i);
	gtk_notebook_prev_page (windows[window_i].notebook);
	gtk_widget_hide (GTK_WIDGET(term));
	gtk_container_remove (GTK_CONTAINER(windows[window_i].notebook), GTK_WIDGET(term));
	gtk_widget_destroy (GTK_WIDGET(term));

	prune_windows ();

	return TRUE;
}

static gboolean
term_destroyed (VteTerminal *term, gpointer user_data)
{
	int n = (long) user_data;

//	fprintf (stderr, "%s: %d\n", __func__, n);

	g_object_unref (G_OBJECT(term));

	terms.active[n] = NULL;
	terms.alive--;

	if (!terms.alive)
		gtk_main_quit();

	return TRUE;
}

static void
term_set_window (int n, int window_i)
{
	GtkWidget *term = terms.active[n];

	// Create a new window if we are passed a non-existant window.
	if (!windows[window_i].window) {
		window_i = new_window ();
		//printf ("Setting term %d to NEW window %d.\n", n, window_i);
	}

	// Set the new active window before removing from the old window.
	// This is done first so that all of the window titles come out right.
	terms.active_window[n] = window_i;

	// Remove from any previous window.
	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (windows[i].window) {
			if (gtk_notebook_page_num (windows[i].notebook, term) >= 0) {
				//printf ("Removing term %d from window %d.\n", n, i);
				gtk_notebook_prev_page (windows[i].notebook);
				gtk_widget_hide (term);
				gtk_container_remove (GTK_CONTAINER(windows[i].notebook), GTK_WIDGET(term));
			}
		}
	}

	if (terms.color_schemes[windows[window_i].color_scheme].name[0]) {
		vte_terminal_set_colors (VTE_TERMINAL (term), &terms.color_schemes[windows[window_i].color_scheme].foreground, &terms.color_schemes[windows[window_i].color_scheme].background, &colors[0], MIN(256, sizeof (colors) / sizeof(colors[0])));
	} else {
		vte_terminal_set_colors (VTE_TERMINAL (term), NULL, NULL, &colors[0], MIN(256, sizeof (colors) / sizeof(colors[0])));
	}

	gtk_notebook_set_current_page(windows[window_i].notebook, gtk_notebook_append_page (windows[window_i].notebook, term, NULL));
	gtk_widget_realize(term);
	gtk_widget_show(term);

	prune_windows ();
	temu_reorder ();
}

static void
term_config (GtkWidget *term, int window_i)
{
	if (terms.font) {
		PangoFontDescription *font = pango_font_description_from_string(terms.font);
		if (font) {
			vte_terminal_set_font (VTE_TERMINAL (term), font);
			pango_font_description_free (font);
		} else {
			printf ("Unable to load font '%s'\n", terms.font);
		}
	}
	vte_terminal_set_word_char_exceptions (VTE_TERMINAL (term), terms.word_char_exceptions);
	vte_terminal_set_audible_bell (VTE_TERMINAL (term), terms.audible_bell);
	vte_terminal_set_font_scale (VTE_TERMINAL (term), terms.font_scale);
	vte_terminal_set_scroll_on_output (VTE_TERMINAL (term), terms.scroll_on_output);
	vte_terminal_set_scroll_on_keystroke (VTE_TERMINAL (term), terms.scroll_on_keystroke);
	vte_terminal_set_bold_is_bright (VTE_TERMINAL (term), terms.bold_is_bright);
	vte_terminal_set_cursor_blink_mode (VTE_TERMINAL (term), VTE_CURSOR_BLINK_OFF);
	vte_terminal_set_scrollback_lines (VTE_TERMINAL (term), terms.scrollback_lines);
	vte_terminal_set_mouse_autohide (VTE_TERMINAL (term), terms.mouse_autohide);

	if (terms.color_schemes[windows[window_i].color_scheme].name[0]) {
		vte_terminal_set_colors (VTE_TERMINAL (term), &terms.color_schemes[windows[window_i].color_scheme].foreground, &terms.color_schemes[windows[window_i].color_scheme].background, &colors[0], MIN(256, sizeof (colors) / sizeof(colors[0])));
	} else {
		vte_terminal_set_colors (VTE_TERMINAL (term), NULL, NULL, &colors[0], MIN(256, sizeof (colors) / sizeof(colors[0])));
	}
}

static void
term_switch (long n, char *cmd, int window_i)
{
	if (n > terms.n_active)
		return;

	if (!terms.active[n]) {
		GtkWidget *term;

		term = vte_terminal_new();
		g_object_ref (G_OBJECT(term));
		gtk_widget_show(term);
		gtk_widget_set_hexpand (term, TRUE);
		gtk_widget_set_vexpand (term, TRUE);

		term_config(term, window_i);


		g_signal_connect_after (G_OBJECT (term), "child-exited", G_CALLBACK (term_died), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "destroy", G_CALLBACK (term_destroyed), (void *) n);
		g_signal_connect (G_OBJECT(term), "window_title_changed", G_CALLBACK(temu_window_title_changed), (void *) n);

		if (cmd) {
			char *argv[] = {
				"/bin/sh",
				"-c",
				cmd,
				NULL
			};
			vte_terminal_spawn_async (VTE_TERMINAL (term), VTE_PTY_DEFAULT, NULL, argv, environ, G_SPAWN_DEFAULT, NULL, NULL, NULL, -1, NULL, NULL, NULL);
		} else {
			char *argv[] = {
				getenv("SHELL"), // one good temp. hack deserves another
				"--login",
				NULL
			};
			vte_terminal_spawn_async (VTE_TERMINAL (term), VTE_PTY_DEFAULT, NULL, argv, environ, G_SPAWN_DEFAULT, NULL, NULL, NULL, -1, NULL, NULL, NULL);
		}

		terms.active[n] = term;
		terms.alive++;

		// This is the index of the term, not a pointer to the window.
		//
		// This is because the term may be moved to a new window, and we want
		// the button press event to still operate against the correct window.
		g_signal_connect (term, "button-press-event", G_CALLBACK (term_button_event), (void *) n);

		term_set_window (n, window_i);
	}

	if (window_i != terms.active_window[n]) {
		window_i = terms.active_window[n];
		gtk_window_present (GTK_WINDOW(windows[window_i].window));
	}

	gtk_notebook_set_current_page (windows[window_i].notebook, gtk_notebook_page_num (windows[window_i].notebook, terms.active[n]));
	temu_window_title_change (VTE_TERMINAL(terms.active[n]), n);
	gtk_widget_grab_focus (GTK_WIDGET(terms.active[n]));
}

static void
term_switch_page (GtkNotebook *notebook, GtkWidget *page, gint page_num, gpointer user_data)
{
	VteTerminal *term;
	long n, found = 0;

	term = VTE_TERMINAL(gtk_notebook_get_nth_page (notebook, page_num));

#if 0
	fprintf (stderr, "page_num: %d, current_page: %d, page: %p, term: %p, notebook: %p, user_data: %p\n",
			page_num, gtk_notebook_get_current_page (notebook), page, term, notebook, user_data);
#endif

	for (n = 0; n < terms.n_active; n++) {
		if (term == VTE_TERMINAL(terms.active[n])) {
			found = 1;
			break;
		}
	}
	if (found) {
		temu_window_title_changed (term, (void *) n);
		gtk_widget_grab_focus (GTK_WIDGET(term));
	}
}

static gboolean
term_key_event (GtkWidget * widget, GdkEventKey * event, gpointer user_data)
{
	window_t *window = (window_t *) user_data;
	bind_t	*cur;
	guint state = event->state;

	//state &= 0xED;
	state &= GDK_MODIFIER_MASK ^ (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK | GDK_BUTTON4_MASK | GDK_BUTTON5_MASK);
	// FIXME: This is a memory leak when uncommented, because we're supposed to free the return value of gtk_accelerator_name.
	//fprintf (stderr, "%s: keyval: %d (%s), state: 0x%x, 0x%x, group: %d, is_modifier: %d, '%s'\n", __func__, event->keyval, gdk_keyval_name(event->keyval), state, event->state, event->group, event->is_modifier, gtk_accelerator_name(event->keyval, state));

	for (cur = terms.keys; cur; cur = cur->next) {
		//fprintf (stderr, "key_min: %d (%s), key_max: %d (%s), state: 0x%x (%s)\n", cur->key_min, gdk_keyval_name(cur->key_min), cur->key_max, gdk_keyval_name(cur->key_max), cur->state, gtk_accelerator_name(0, cur->state));
		if ((event->keyval >= cur->key_min) && (event->keyval <= cur->key_max)) {
			if (state == cur->state) {
				switch (cur->action) {
					case BIND_ACT_SWITCH:
						term_switch (cur->base + (event->keyval - cur->key_min), cur->cmd, window - &windows[0]);
						break;
					case BIND_ACT_CUT:
						widget = gtk_notebook_get_nth_page(window->notebook, gtk_notebook_get_current_page(window->notebook));
						vte_terminal_copy_clipboard_format (VTE_TERMINAL(widget), VTE_FORMAT_TEXT);
						break;
					case BIND_ACT_PASTE:
						widget = gtk_notebook_get_nth_page(window->notebook, gtk_notebook_get_current_page(window->notebook));
						vte_terminal_paste_clipboard (VTE_TERMINAL(widget));
						break;
					case BIND_ACT_MENU:
						printf ("Bringing up menu.\n");
						gtk_widget_show_all(window->menu);
						gtk_menu_popup_at_pointer(GTK_MENU(window->menu), NULL);
						break;
					case BIND_ACT_NEXT_TERM:
						gtk_notebook_next_page(GTK_NOTEBOOK(window->notebook));
						break;
					case BIND_ACT_PREV_TERM:
						gtk_notebook_prev_page(GTK_NOTEBOOK(window->notebook));
						break;
				}
				return TRUE;
			}
		}
	}

	return FALSE;
}

static gboolean
term_button_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	long int n = (long int) user_data;
	window_t *window = &windows[terms.active_window[n]];

	if (event->button == 3) {
		gtk_widget_show_all(window->menu);
		gtk_menu_popup_at_pointer(GTK_MENU(window->menu), NULL);
		return TRUE;
	}

	return FALSE;
}

static void
temu_parse_bind_switch (char **subs, bind_actions_t action)
{
	bind_t *bind = calloc (1, sizeof (bind_t));
	bind->next = terms.keys;
	terms.keys = bind;

	bind->action = action;
	bind->base = strtol(subs[0], NULL, 0);
	bind->state = strtol(subs[1], NULL, 0);
	// The new style is actually a GTK accelerator string, but with only the modifier component.
	if (bind->state == 0) {
		gtk_accelerator_parse(subs[1], NULL, &bind->state);
		printf("Parsing '%s' as accelerator, result: 0x%x\n", subs[1], bind->state);
	}
	bind->key_min = gdk_keyval_from_name (subs[2]);
	if (subs[4])
		bind->key_max = gdk_keyval_from_name (subs[4]);
	else
		bind->key_max = bind->key_min;

	if (subs[6])
		bind->cmd = strdup (subs[6]);

	terms.n_active += bind->key_max - bind->key_min;
	terms.n_active++;
}

static void
temu_parse_bind_action (char **subs)
{
	bind_t *bind = calloc (1, sizeof (bind_t));

	if (!strcasecmp(subs[0], "CUT")) {
		bind->action = BIND_ACT_CUT;
	} else if (!strcasecmp(subs[0], "PASTE")) {
		bind->action = BIND_ACT_PASTE;
	} else if (!strcasecmp(subs[0], "MENU")) {
		bind->action = BIND_ACT_MENU;
	} else if (!strcasecmp(subs[0], "NEXT_TERM")) {
		bind->action = BIND_ACT_NEXT_TERM;
	} else if (!strcasecmp(subs[0], "PREV_TERM")) {
		bind->action = BIND_ACT_PREV_TERM;
	} else {
		fprintf(stderr, "Unknown bind action '%s'.\n", subs[0]);
		free(bind);
		return;
	}

	bind->next = terms.keys;
	terms.keys = bind;

	bind->state = strtol (subs[1], NULL, 0);
	// The new style is actually a GTK accelerator string, but with only the modifier component.
	if (bind->state == 0) {
		gtk_accelerator_parse(subs[1], NULL, &bind->state);
		printf("Parsing '%s' as accelerator, result: 0x%x\n", subs[1], bind->state);
	}
	bind->key_min = bind->key_max = gdk_keyval_from_name (subs[2]);
	// Handle the case where we want to bind a raw key value by number.
	// This was created because gdk has no name for the menu key on OS X.
	if (bind->key_min == 0xffffff) {
		bind->key_min = bind->key_max = strtol(subs[2], NULL, 0);
	}
	//printf ("Binding: keyval: 0x%x, state: 0x%x, action: %d (%s %s %s)\n", bind->key_min, bind->state, bind->action, subs[0], subs[1], subs[2]);
}

static void
temu_free_keys (void)
{
	while (terms.keys != NULL) {
		bind_t *next = terms.keys->next;
		
		free(terms.keys);

		terms.keys = next;
	}
}

static void
temu_parse_color (char **subs)
{
	int n = strtol (subs[0], NULL, 0);
	if (n >= (sizeof(colors) / sizeof(colors[0]))) {
		return;
	}

	gdk_rgba_parse (&colors[n], subs[1]);
}

static void
temu_parse_font (char **subs)
{
	if (terms.font)
		free (terms.font);
	terms.font = strdup (subs[0]);
}

static void
temu_parse_size (char **subs)
{
	start_width = strtol(subs[0], NULL, 10);
	start_height = strtol(subs[1], NULL, 10);
}

void
gen_subs (char *str, char *subs[], regmatch_t matches[], size_t count)
{
	int i;

	for (i = 1; i < count; i++) {
		if (matches[i].rm_so != -1) {
			int len = matches[i].rm_eo - matches[i].rm_so;
			if (subs[i - 1] != NULL) {
				free (subs[i - 1]);
				subs[i - 1] = NULL;
			}
			subs[i - 1] = calloc (1, len + 1);
			memcpy (subs[i - 1], str + matches[i].rm_so, len);
		}
	}
}

void free_subs (char *subs[], size_t count)
{
	int i;
	for (i = 0; i < count; i++) {
		if (subs[i] != NULL) {
			free (subs[i]);
			subs[i] = NULL;
		}
	}
}

void
do_copy (GtkMenuItem *item, void *data)
{
	window_t *window = (window_t *) data;
	GtkWidget *widget = gtk_notebook_get_nth_page(window->notebook, gtk_notebook_get_current_page(window->notebook));
	vte_terminal_copy_clipboard_format (VTE_TERMINAL(widget), VTE_FORMAT_TEXT);
}

void
do_paste (GtkMenuItem *item, void *data)
{
	window_t *window = (window_t *) data;
	GtkWidget *widget = gtk_notebook_get_nth_page(window->notebook, gtk_notebook_get_current_page(window->notebook));
	vte_terminal_paste_clipboard (VTE_TERMINAL(widget));
}

void
do_t_decorate (GtkMenuItem *item, void *data)
{
	window_t *window = (window_t *) data;
	gboolean decorated = gtk_window_get_decorated (GTK_WINDOW(window->window));
	gtk_window_set_decorated(GTK_WINDOW(window->window), !decorated);
}

void
do_t_fullscreen (GtkMenuItem *item, void *data)
{
	window_t *window = (window_t *) data;
	if (window->window_state & GDK_WINDOW_STATE_FULLSCREEN) {
		gtk_window_unfullscreen (GTK_WINDOW (window->window));
	} else {
		gtk_window_fullscreen (GTK_WINDOW (window->window));
	}
}

void
do_t_tabbar (GtkMenuItem *item, void *data)
{
	window_t *window = (window_t *) data;
	gboolean show_tabs = gtk_notebook_get_show_tabs (GTK_NOTEBOOK(window->notebook));
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(window->notebook), !show_tabs);
}

void
do_show_terms (GtkMenuItem *item, void *data)
{
	long int window_i = (long int) data;
	gboolean return_value;

	g_signal_emit_by_name (G_OBJECT(windows[window_i].notebook), "popup-menu", &return_value);
}

void
do_next_term (GtkMenuItem *item, void *data)
{
	window_t *window = (window_t *) data;

	gtk_notebook_next_page(GTK_NOTEBOOK(window->notebook));
}

void
do_prev_term (GtkMenuItem *item, void *data)
{
	window_t *window = (window_t *) data;

	gtk_notebook_prev_page(GTK_NOTEBOOK(window->notebook));
}

void
do_move_to_window (GtkMenuItem *item, void *data)
{
	long int new_window_i = (long int) data;
	long int window_i = -1;
	int i;
	GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET(item));
	GtkWidget *widget;

	for (i = 0; i < MAX_WINDOWS; i++) {
		if (windows[i].window) {
			if (parent == windows[i].menu) {
				window_i = i;
			}
		}
	}

	if (window_i == -1) {
		printf ("Unable to find window.\n");
		return;
	}

	widget = gtk_notebook_get_nth_page(windows[window_i].notebook, gtk_notebook_get_current_page(windows[window_i].notebook));

	for (i = 0; i < terms.n_active; i++) {
		if (terms.active[i] == widget) {
			term_set_window (i, new_window_i);
			term_switch (i, NULL, window_i);
			break;
		}
	}
}

void
do_set_window_color_scheme (GtkMenuItem *item, void *data)
{
	long int color_scheme = (long int) data;
	long int window_i = -1;
	int i;
	GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET(item));

	for (i = 0; i < MAX_WINDOWS; i++) {
		if (windows[i].window) {
			if (parent == windows[i].menu) {
				window_i = i;
			}
		}
	}

	if (window_i == -1) {
		printf ("Unable to find window.\n");
		return;
	}

	windows[window_i].color_scheme = color_scheme;

	for (i = 0; i < terms.n_active; i++) {
		if (terms.active[i] && terms.active_window[i] == window_i) {
			vte_terminal_set_colors (VTE_TERMINAL (terms.active[i]), &terms.color_schemes[windows[window_i].color_scheme].foreground, &terms.color_schemes[windows[window_i].color_scheme].background, &colors[0], MIN(256, sizeof (colors) / sizeof(colors[0])));
		}
	}
}


// REG_ENHANCED does not exist on Linux, but we still want to compile. (I know, picky picky.)
#ifndef REG_ENHANCED
#define REG_ENHANCED	0
#endif

void
temu_parse_config (void)
{
#define MATCHES	16
	regex_t bind_action, bind_switch, color, color_scheme, font, size, env, other;
	regmatch_t regexp_matches[MATCHES];
	char *subs[MATCHES] = { 0 };
	FILE *f;
	char *file = NULL;
	long f_len;
	int j, ret;
	char *t1, *t2;
	char conffile[512] = { 0 };
	size_t read;
	int n_color_scheme = 0;

	ret = regcomp (&bind_action, "^bind:[ \t]+([a-zA-Z_]+)[ \t]+([^\\s]+)[ \t]+([a-zA-Z0-9_]+)$", REG_ENHANCED | REG_EXTENDED);
	if (ret) {
		char errbuf[128] = { 0 };

		regerror(ret, &bind_action, errbuf, sizeof(errbuf) - 1);
		fprintf(stderr, "%s %d (%s): recomp failed: %d (%s)\n", __FILE__, __LINE__, __func__, ret, errbuf);
	}
	ret = regcomp (&bind_switch, "^bind:[ \t]+([0-9]+)[ \t]+([^\\s]+)[ \t]+([a-zA-Z0-9_]+)(-([a-zA-Z0-9_]+))?([ \t]+(.*?))?$", REG_ENHANCED | REG_EXTENDED);
	if (ret) {
		char errbuf[128] = { 0 };

		regerror(ret, &bind_action, errbuf, sizeof(errbuf) - 1);
		fprintf(stderr, "%s %d (%s): recomp failed: %d (%s)\n", __FILE__, __LINE__, __func__, ret, errbuf);
	}
	ret = regcomp (&color, "^color:[ \t]+([0-9]+)[ \t]+(.*?)$", REG_ENHANCED | REG_EXTENDED);
	if (ret) {
		char errbuf[128] = { 0 };

		regerror(ret, &bind_action, errbuf, sizeof(errbuf) - 1);
		fprintf(stderr, "%s %d (%s): recomp failed: %d (%s)\n", __FILE__, __LINE__, __func__, ret, errbuf);
	}
	ret = regcomp (&color_scheme, "^color_scheme:[ \t]+([-a-zA-Z0-9_ ]*?)[ \t]+(#[0-9a-fA-F]+)[ \t]+(#[0-9a-fA-F]+)$", REG_ENHANCED | REG_EXTENDED);
	if (ret) {
		char errbuf[128] = { 0 };

		regerror(ret, &bind_action, errbuf, sizeof(errbuf) - 1);
		fprintf(stderr, "%s %d (%s): recomp failed: %d (%s)\n", __FILE__, __LINE__, __func__, ret, errbuf);
	}
	ret = regcomp (&font, "^font:[ \t]+(.*?)$", REG_ENHANCED | REG_EXTENDED);
	if (ret) {
		char errbuf[128] = { 0 };

		regerror(ret, &bind_action, errbuf, sizeof(errbuf) - 1);
		fprintf(stderr, "%s %d (%s): recomp failed: %d (%s)\n", __FILE__, __LINE__, __func__, ret, errbuf);
	}
	ret = regcomp (&size, "^size:[ \t]+([0-9]+)x([0-9]+)$", REG_ENHANCED | REG_EXTENDED);
	if (ret) {
		char errbuf[128] = { 0 };

		regerror(ret, &bind_action, errbuf, sizeof(errbuf) - 1);
		fprintf(stderr, "%s %d (%s): recomp failed: %d (%s)\n", __FILE__, __LINE__, __func__, ret, errbuf);
	}
	ret = regcomp (&env, "^env:[ \t]+([^=]*?)=(.*?)$", REG_ENHANCED | REG_EXTENDED);
	if (ret) {
		char errbuf[128] = { 0 };

		regerror(ret, &bind_action, errbuf, sizeof(errbuf) - 1);
		fprintf(stderr, "%s %d (%s): recomp failed: %d (%s)\n", __FILE__, __LINE__, __func__, ret, errbuf);
	}
	ret = regcomp (&other, "^([^: ]*):[ \t]+(.*?)$", REG_ENHANCED | REG_EXTENDED);
	if (ret) {
		char errbuf[128] = { 0 };

		regerror(ret, &bind_action, errbuf, sizeof(errbuf) - 1);
		fprintf(stderr, "%s %d (%s): recomp failed: %d (%s)\n", __FILE__, __LINE__, __func__, ret, errbuf);
	}

	temu_free_keys();
	// FIXME: We need to correctly handle the case where this number changes with a reload, it's going to be a bit rough.
	terms.n_active = 0;

	snprintf(conffile, sizeof(conffile) - 1, "%s/.zterm/config", getenv("HOME"));
	f = fopen(conffile, "r");
	if (!f)
		goto done;
	fseek(f, 0, SEEK_END);
	f_len = ftell(f);
	fseek(f, 0, SEEK_SET);
	file = calloc (1, f_len + 1);
	read = fread (file, f_len, 1, f);
	fclose(f);
	if (read != 1)
		goto done;

	t1 = file;
	while (*t1) {
		t2 = strchr (t1, '\n');
		*t2++ = '\0';

		j = t1[0] == '#' ? 1 : 0;
		if (!j) {
			j = t1[0] == '\0' ? 1 : 0;
		}

		if (!j) {
			ret = regexec (&bind_action, t1, MATCHES, regexp_matches, 0);
			if (!ret) {
				gen_subs (t1, subs, regexp_matches, MATCHES);
				temu_parse_bind_action (subs);
				free_subs (subs, MATCHES);
				j++;
			}
		}

		if (!j) {
			ret = regexec (&bind_switch, t1, MATCHES, regexp_matches, 0);
			if (!ret) {
				gen_subs (t1, subs, regexp_matches, MATCHES);
				temu_parse_bind_switch (subs, BIND_ACT_SWITCH);
				free_subs (subs, MATCHES);
				j++;
			}
		}

		if (!j) {
			ret = regexec (&color, t1, MATCHES, regexp_matches, 0);
			if (!ret) {
				gen_subs (t1, subs, regexp_matches, MATCHES);
				temu_parse_color (subs);
				free_subs (subs, MATCHES);
				j++;
			}
		}

		if (!j && n_color_scheme < (sizeof (terms.color_schemes) / sizeof (terms.color_schemes[0]))) {
			ret = regexec (&color_scheme, t1, MATCHES, regexp_matches, 0);
			if (!ret) {
				gen_subs (t1, subs, regexp_matches, MATCHES);
				strlcpy (terms.color_schemes[n_color_scheme].name, subs[0], sizeof (terms.color_schemes[n_color_scheme].name));
				gdk_rgba_parse (&terms.color_schemes[n_color_scheme].foreground, subs[1]);
				gdk_rgba_parse (&terms.color_schemes[n_color_scheme].background, subs[2]);
				n_color_scheme++;
				free_subs (subs, MATCHES);
				j++;
			}
		}

		if (!j) {
			ret = regexec (&font, t1, MATCHES, regexp_matches, 0);
			if (!ret) {
				gen_subs (t1, subs, regexp_matches, MATCHES);
				temu_parse_font (subs);
				free_subs (subs, MATCHES);
				j++;
			}
		}

		if (!j) {
			ret = regexec (&size, t1, MATCHES, regexp_matches, 0);
			if (!ret) {
				gen_subs (t1, subs, regexp_matches, MATCHES);
				temu_parse_size (subs);
				free_subs (subs, MATCHES);
				j++;
			}
		}

		if (!j) {
			ret = regexec (&env, t1, MATCHES, regexp_matches, 0);
			if (!ret) {
				gen_subs (t1, subs, regexp_matches, MATCHES);
				setenv(subs[0], subs[1], 1);
				free_subs (subs, MATCHES);
				j++;
			}
		}

		if (!j) {
			ret = regexec (&other, t1, MATCHES, regexp_matches, 0);
			if (!ret) {
				gen_subs (t1, subs, regexp_matches, MATCHES);
				if (!strcmp(subs[0], "audible_bell")) {
					terms.audible_bell = atoi(subs[1]);
				} else if (!strcmp(subs[0], "word_char_exceptions")) {
					strlcpy (terms.word_char_exceptions, subs[1], sizeof (terms.word_char_exceptions));
				} else if (!strcmp(subs[0], "font_scale")) {
					terms.font_scale = atof(subs[1]);
				} else if (!strcmp(subs[0], "scroll_on_output")) {
					terms.scroll_on_output = atoi(subs[1]);
				} else if (!strcmp(subs[0], "scroll_on_keystroke")) {
					terms.scroll_on_keystroke = atoi(subs[1]);
				} else if (!strcmp(subs[0], "rewrap_on_resize")) {
					terms.rewrap_on_resize = atoi(subs[1]);
				} else if (!strcmp(subs[0], "scrollback_lines")) {
					terms.scrollback_lines = atoi(subs[1]);
				} else if (!strcmp(subs[0], "allow_bold")) {
					terms.allow_bold = atoi(subs[1]);
				} else if (!strcmp(subs[0], "bold_is_bright")) {
					terms.bold_is_bright = atoi(subs[1]);
				} else if (!strcmp(subs[0], "mouse_autohide")) {
					terms.mouse_autohide = atoi(subs[1]);
				} else {
					fprintf (stderr, "Unable to parse line in config: '%s' (%s)\n", t1, subs[0]);
				}
				free_subs (subs, MATCHES);
				j++;
			}
		}

		if (!j)
			fprintf (stderr, "Unable to parse line in config: '%s'\n", t1);

		t1 = t2;
	}

done:
	regfree (&bind_action);
	regfree (&bind_switch);
	regfree (&color);
	regfree (&font);
	regfree (&size);
	free (file);

	return;
}

void
do_reload_config (GtkMenuItem *item, void *data)
{
	int old_n_active = terms.n_active;

	temu_parse_config();
	if (!terms.n_active) {
		fprintf (stderr, "Unable to read config file, or no terminals defined.\n");
		return;
	}

	// We actively don't want to worry about the number shrinking.  That just makes too much pain.
	// But we absolutely have to handle it growing.
	if (terms.n_active > old_n_active) {
		fprintf(stderr, "%s %d (%s): old_n_active: %d, terms.n_active: %d\n", __FILE__, __LINE__, __func__, old_n_active, terms.n_active);
		terms.active = realloc(terms.active, terms.n_active * sizeof(*terms.active));
		terms.active_window = realloc(terms.active, terms.n_active * sizeof(*terms.active_window));
		memset(&terms.active[old_n_active], 0, (terms.n_active - old_n_active) * sizeof(*terms.active));
		memset(&terms.active_window[old_n_active], 0, (terms.n_active - old_n_active) * sizeof(*terms.active_window));
	}

	for (int i = 0; i < terms.n_active; i++) {
		if (terms.active[i]) {
			term_config(terms.active[i], terms.active_window[i]);
		}
	}

	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (windows[i].window) {
			rebuild_window_menu(windows[i].window, i);
		}
	}
}

void
rebuild_window_menu (GtkWidget *window, long int i)
{
	if (windows[i].menu != NULL) {
		destroy_window_menu(i);
	}

	windows[i].menu = gtk_menu_new();

	windows[i].m_copy = gtk_menu_item_new_with_mnemonic("_Copy");
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_copy);
	g_signal_connect(windows[i].m_copy, "activate", G_CALLBACK(do_copy), &windows[i]);

	windows[i].m_paste = gtk_menu_item_new_with_mnemonic("_Paste");
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_paste);
	g_signal_connect(windows[i].m_paste, "activate", G_CALLBACK(do_paste), &windows[i]);

	windows[i].m_sep_0 = gtk_separator_menu_item_new ();
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_sep_0);

	windows[i].m_show_terms = gtk_menu_item_new_with_mnemonic("_Show terminals");
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_show_terms);
	g_signal_connect(windows[i].m_show_terms, "activate", G_CALLBACK(do_show_terms), (void *) i);

	// I am unable to explain why this is necessary.
	// But if it is missing, on a config reload (which rebuilds menus), the
	// next widget appears to get destroyed despite no destruction call being
	// mode.
	GtkWidget *tmp_widget = gtk_separator_menu_item_new();
	gtk_widget_destroy(tmp_widget);


	windows[i].m_sep_0_1 = gtk_separator_menu_item_new ();
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_sep_0_1);

	windows[i].m_prev_term = gtk_menu_item_new_with_mnemonic("_Previous terminal");
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_prev_term);
	g_signal_connect(windows[i].m_prev_term, "activate", G_CALLBACK(do_prev_term), &windows[i]);

	windows[i].m_next_term = gtk_menu_item_new_with_mnemonic("_Next terminal");
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_next_term);
	g_signal_connect(windows[i].m_next_term, "activate", G_CALLBACK(do_next_term), &windows[i]);

	windows[i].m_sep_1 = gtk_separator_menu_item_new ();
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_sep_1);

	windows[i].m_t_decorate = gtk_menu_item_new_with_mnemonic("Toggle _decorations");
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_t_decorate);
	g_signal_connect(windows[i].m_t_decorate, "activate", G_CALLBACK(do_t_decorate), &windows[i]);

	windows[i].m_t_fullscreen = gtk_menu_item_new_with_mnemonic("Toggle _fullscreen");
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_t_fullscreen);
	g_signal_connect(windows[i].m_t_fullscreen, "activate", G_CALLBACK(do_t_fullscreen), &windows[i]);

	windows[i].m_t_tabbar = gtk_menu_item_new_with_mnemonic("Toggle _tab bar");
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_t_tabbar);
	g_signal_connect(windows[i].m_t_tabbar, "activate", G_CALLBACK(do_t_tabbar), &windows[i]);

	windows[i].m_sep_2 = gtk_separator_menu_item_new ();
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_sep_2);

	windows[i].m_reload_config = gtk_menu_item_new_with_mnemonic("_Reload config file");
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_reload_config);
	g_signal_connect(windows[i].m_reload_config, "activate", G_CALLBACK(do_reload_config), &windows[i]);

	windows[i].m_sep_3 = gtk_separator_menu_item_new ();
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_sep_3);

	for (long int j = 0; j < MAX_COLOR_SCHEMES && terms.color_schemes[j].name[0]; j++) {
		windows[i].m_color_scheme[j] = gtk_menu_item_new_with_mnemonic (terms.color_schemes[j].name);
		gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_color_scheme[j]);
		g_signal_connect(windows[i].m_color_scheme[j], "activate", G_CALLBACK(do_set_window_color_scheme), (void *) j);
	}

	windows[i].m_sep_4 = gtk_separator_menu_item_new ();
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_sep_4);

	rebuild_menu_switch_lists ();
}

int new_window (void)
{
	GtkWidget *window, *notebook;
	long int i;

	for (i = 0; i < MAX_WINDOWS; i++) {
		if (!windows[i].window) {
			break;
		}
	}

	if (i == MAX_WINDOWS) {
		fprintf (stderr, "ERROR: Unable to allocate new window.\n");
		return -1;
	}

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 0);
	gtk_window_set_default_size (GTK_WINDOW(window), start_width, start_height);

	notebook = gtk_notebook_new();
	g_object_set (G_OBJECT (notebook), "scrollable", TRUE, "enable-popup", TRUE, NULL);
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK(notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK(notebook), FALSE);
	gtk_container_add(GTK_CONTAINER(window), notebook);
	gtk_container_set_border_width (GTK_CONTAINER(window), 0);
	gtk_container_set_border_width (GTK_CONTAINER(notebook), 0);
	gtk_widget_show(notebook);

	windows[i].window = GTK_WIDGET(window);
	windows[i].notebook = GTK_NOTEBOOK(notebook);

	rebuild_window_menu(window, i);


	gtk_widget_set_can_focus(notebook, FALSE);

	gtk_widget_show(window);

	g_signal_connect (notebook, "switch_page", G_CALLBACK (term_switch_page), GTK_NOTEBOOK(notebook));

	g_signal_connect (window, "key-press-event", G_CALLBACK (term_key_event), &windows[i]);
	g_signal_connect (window, "window-state-event", G_CALLBACK (temu_window_state_changed), &windows[i]);

	// Alright, this is annoying.
	// For reasons that I currently can't figure out, if we ask for the initial
	// window size to be reasonably large, we get a maximized window by
	// default.
	// This behavior is fairly inconsistent.
	//
	// To avoid that, we need to present the window, and then tell it to unmaximize.
	// Otherwise when we add the first term to the notebook we get a maximized window.
	//
	// Oh, and then we need to present again, because otherwise we lose focus
	// again when we unmaximize.
	//
	// I'd really like to better understand this behavior, but, eh, this works.
	gtk_window_present (GTK_WINDOW(window));
	gtk_window_unmaximize (GTK_WINDOW (window));
	gtk_window_present (GTK_WINDOW(window));

	return i;
}

void
add_menu_switch_item (int menu_window, long item_window, int new)
{
	char title[64] = { 0 };
	if (new) {
		snprintf (title, sizeof (title), "Move to _new window");
	} else {
		snprintf (title, sizeof (title), "Move to window _%ld", item_window + 1);
	}

	windows[menu_window].m_move[item_window] = gtk_menu_item_new_with_mnemonic (title);
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[menu_window].menu), windows[menu_window].m_move[item_window]);
	g_signal_connect(windows[menu_window].m_move[item_window], "activate", G_CALLBACK(do_move_to_window), (void *) item_window);
}

void rebuild_menu_switch_lists (void)
{
	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (windows[i].window) {
			for (long n = 0; n < MAX_WINDOWS; n++) {
				if (windows[i].m_move[n]) {
					gtk_widget_destroy (GTK_WIDGET (windows[i].m_move[n]));
					windows[i].m_move[n] = NULL;
				}
			}

			long first_empty = -1;
			for (long n = 0; n < MAX_WINDOWS; n++) {
				if (n == i) { // Don't offer to move to the same window, that's just weird.
					continue;
				} else if (windows[n].window) {
					add_menu_switch_item (i, n, 0);
				} else if (first_empty == -1) {
					first_empty = n;
				}
			}

			if (first_empty != -1) {
				add_menu_switch_item (i, first_empty, 1);
			}
		}
	}
}

void destroy_window_menu (int i)
{
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_show_terms));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_copy));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_paste));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_next_term));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_prev_term));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_reload_config));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_t_decorate));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_t_fullscreen));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_t_tabbar));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_sep_0));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_sep_0_1));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_sep_1));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_sep_2));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_sep_3));
	gtk_widget_destroy (GTK_WIDGET (windows[i].m_sep_4));
	gtk_widget_destroy (GTK_WIDGET (windows[i].menu));
	windows[i].menu = NULL;
	windows[i].m_copy = NULL;
	windows[i].m_paste = NULL;
	windows[i].m_next_term = NULL;
	windows[i].m_prev_term = NULL;
	windows[i].m_reload_config = NULL;
	windows[i].m_t_decorate = NULL;
	windows[i].m_t_fullscreen = NULL;
	windows[i].m_t_tabbar = NULL;
	windows[i].m_sep_0 = NULL;
	windows[i].m_sep_0_1 = NULL;
	windows[i].m_sep_1 = NULL;
	windows[i].m_sep_2 = NULL;
	windows[i].m_sep_3 = NULL;
	windows[i].m_sep_4 = NULL;
}

void destroy_window (int i)
{
	if (windows[i].window) {
		for (long n = 0; n < MAX_WINDOWS; n++) {
			if (windows[i].m_move[n]) {
				gtk_widget_destroy (GTK_WIDGET (windows[i].m_move[n]));
				windows[i].m_move[n] = NULL;
			}
		}
		for (long n = 0; n < MAX_COLOR_SCHEMES; n++) {
			if (windows[i].m_color_scheme[n]) {
				gtk_widget_destroy (GTK_WIDGET (windows[i].m_color_scheme[n]));
				windows[i].m_color_scheme[n] = NULL;
			}
		}
		destroy_window_menu (i);
		gtk_widget_destroy (GTK_WIDGET (windows[i].notebook));
		gtk_widget_destroy (GTK_WIDGET (windows[i].window));
		windows[i].notebook = NULL;
		windows[i].window = NULL;
	}

	rebuild_menu_switch_lists ();
}

int main(int argc, char *argv[], char *envp[])
{
	int i;

	gtk_init(&argc, &argv);

	memset (&terms, 0, sizeof (terms));
	terms.envp = envp;
	printf ("Using VTE: %s (%s)\n", vte_get_features(), vte_get_user_shell());
	terms.audible_bell = TRUE;
	terms.font_scale = 1;
	terms.scroll_on_output = FALSE;
	terms.scroll_on_keystroke = TRUE;
	terms.rewrap_on_resize = TRUE;
	terms.scrollback_lines = 512;
	terms.allow_bold = FALSE;
	terms.bold_is_bright = TRUE;
	terms.mouse_autohide = TRUE;

	temu_parse_config ();
	if (!terms.n_active) {
		fprintf (stderr, "Unable to read config file, or no terminals defined.\n");
		exit (0);
	}
	terms.active = calloc(terms.n_active, sizeof(*terms.active));
	terms.active_window = calloc(terms.n_active, sizeof(*terms.active_window));

	chdir(getenv("HOME"));

	{
		bind_t *cur;
		char *cmd = NULL;

		for (cur = terms.keys; cur; cur = cur->next) {
			if (cur->base == 0) {
				cmd = cur->cmd;
				break;
			}
		}

		term_switch (0, cmd, 0);
	}

	gtk_main();

	printf("Exiting, can free here. (%d)\n", terms.n_active);
	for (i = 0; i < terms.n_active; i++) {
		if (terms.active[i]) {
			gtk_widget_hide (GTK_WIDGET(terms.active[i]));
			gtk_container_remove (GTK_CONTAINER(windows[terms.active_window[i]].notebook), GTK_WIDGET(terms.active[i]));
			gtk_widget_destroy (GTK_WIDGET(terms.active[i]));
		}
	}

	for (i = 0; i < MAX_WINDOWS; i++) {
		destroy_window (i);
	}

	free (terms.active);
	free (terms.active_window);
	terms.active = NULL;

	if (terms.font) {
		free (terms.font);
		terms.font = NULL;
	}
	{
		bind_t *keys, *next;
		for (keys = terms.keys; keys; keys = next) {
			next = keys->next;
			if (keys->cmd) {
				free (keys->cmd);
				keys->cmd = NULL;
			}
			free (keys);
		}
		terms.keys = NULL;
	}

	return 0;
}
