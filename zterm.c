#define _GNU_SOURCE
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

GdkRGBA colors[256] = {
#include "256colors.h"
};

#define MAX_WINDOWS 8

int start_width = 1024;
int start_height = 768;

typedef enum bind_actions {
	BIND_ACT_SWITCH	= 0,
	BIND_ACT_CUT,
	BIND_ACT_PASTE,
} bind_actions_t;

typedef struct bind_s {
	guint state;
	guint key_min, key_max;
	guint base;
	struct bind_s *next;
	char *cmd;
	bind_actions_t action;
} bind_t;

typedef struct color_s {
	GdkColor color;
	guint n;
	struct color_s *next;
} color_t;

typedef struct window_s {
	GtkNotebook *notebook;
	GtkWidget *window;
	GtkWidget *menu;
	GtkWidget *m_copy;
	GtkWidget *m_paste;
	GtkWidget *m_t_decorate;
	GtkWidget *m_t_fullscreen;
	GtkWidget *m_t_tabbar;
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
} terms_t;

terms_t terms;
window_t windows[MAX_WINDOWS];

static gboolean term_button_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data);
int new_window (void);

static void
temu_reorder (void)
{
#if 0
	int i, j;

	for (i = j = 0; i < terms.n_active && j < terms.alive; i++)
		if (terms.active[i])
			gtk_notebook_reorder_child (terms.notebook, terms.active[i], j++);
#endif
}


static void
temu_window_title_change (VteTerminal *terminal, GtkWidget *window, int n)
{
	gchar *new_str;

	if (vte_terminal_get_window_title(terminal)) {
		if (asprintf(&new_str, "%s [%d]", vte_terminal_get_window_title(terminal), n) < 0) {
			return; // Memory allocation issue.
		}
	} else {
		if (asprintf(&new_str, "zterm [%d]", n) < 0) {
			return; // Memory allocation issue.
		}
	}

	gtk_window_set_title(GTK_WINDOW(window), new_str);
	free (new_str);
}

static void
temu_window_title_changed(VteTerminal *terminal, gpointer data)
{
	temu_window_title_change (terminal, terms.window, (long) data);
}

static gboolean
term_died (VteTerminal *term, gpointer user_data)
{
	int status, n = (long) user_data;

	waitpid (-1, &status, WNOHANG);

	gtk_notebook_prev_page (terms.notebook);
	gtk_widget_hide (GTK_WIDGET(term));
	gtk_container_remove (GTK_CONTAINER(terms.notebook), GTK_WIDGET(term));
	terms.active[n] = NULL;
	gtk_widget_destroy (GTK_WIDGET(term));

	return TRUE;
}

static gboolean
term_destroyed (VteTerminal *term, gpointer user_data)
{
	int n = (long) user_data;

//	fprintf (stderr, "%s: %d\n", __func__, tracker->n);

	g_object_unref (G_OBJECT(term));

	terms.active[n] = NULL;
	terms.alive--;

	if (!terms.alive)
		gtk_main_quit();

	return TRUE;
}

static void
term_switch (long n, char *cmd)
{
	if (n > terms.n_active)
		return;

	if (!terms.active[n]) {
		GtkWidget *term, *label;
		char str[32];

		term = vte_terminal_new();
		g_object_ref (G_OBJECT(term));
		gtk_widget_show(term);
		gtk_widget_set_hexpand (term, TRUE);
		gtk_widget_set_vexpand (term, TRUE);

		vte_terminal_set_colors (VTE_TERMINAL (term), NULL, NULL, &colors[0], MIN(255, sizeof (colors) / sizeof(colors[0])));

		if (terms.font) {
			PangoFontDescription *font = pango_font_description_from_string(terms.font);
			if (font) {
				vte_terminal_set_font (VTE_TERMINAL (term), font);
				pango_font_description_free (font);
			} else {
				printf ("Unable to load font '%s'\n", terms.font);
			}
		}
		vte_terminal_set_allow_bold (VTE_TERMINAL (term), FALSE);
		vte_terminal_set_cursor_blink_mode (VTE_TERMINAL (term), VTE_CURSOR_BLINK_OFF);
		vte_terminal_set_scrollback_lines (VTE_TERMINAL (term), 512);
		vte_terminal_set_mouse_autohide (VTE_TERMINAL (term), TRUE);

		unsetenv ("TERM");
		unsetenv ("COLORTERM");
		setenv("TERM", "temu", 1);
		setenv("COLORTERM", "temu", 1);

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
			vte_terminal_spawn_sync (VTE_TERMINAL (term), VTE_PTY_NO_LASTLOG | VTE_PTY_NO_UTMP | VTE_PTY_NO_WTMP | VTE_PTY_NO_HELPER, NULL, argv, environ, G_SPAWN_CHILD_INHERITS_STDIN | G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL);
		} else {
			char *argv[] = {
				getenv("SHELL"), // one good temp. hack deserves another
				"--login",
				NULL
			};
			vte_terminal_spawn_sync (VTE_TERMINAL (term), VTE_PTY_NO_LASTLOG | VTE_PTY_NO_UTMP | VTE_PTY_NO_WTMP | VTE_PTY_NO_HELPER, NULL, argv, environ, G_SPAWN_CHILD_INHERITS_STDIN | G_SPAWN_SEARCH_PATH | G_SPAWN_FILE_AND_ARGV_ZERO, NULL, NULL, NULL, NULL, NULL);
		}

		snprintf(str, sizeof(str), "Terminal %ld", n);
		label = gtk_label_new(str);

		g_signal_connect (term, "button-press-event", G_CALLBACK (term_button_event), &terms);
		gtk_notebook_set_current_page(terms.notebook, gtk_notebook_append_page (terms.notebook, term, label));
		gtk_widget_realize(term);
		gtk_widget_show(label);

		terms.active[n] = term;
		terms.alive++;

		temu_reorder ();
	}

	gtk_notebook_set_current_page (terms.notebook, gtk_notebook_page_num (terms.notebook, terms.active[n]));
	temu_window_title_change (VTE_TERMINAL(terms.active[n]), terms.window, n);
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
	bind_t	*cur;
	guint state = event->state;

	state &= 0xED;
//	fprintf (stderr, "%s: keyval: %d, state: 0x%x\n", __func__, event->keyval, state);

	for (cur = terms.keys; cur; cur = cur->next) {
		if ((event->keyval >= cur->key_min) && (event->keyval <= cur->key_max)) {
			if (state == cur->state) {
				switch (cur->action) {
					case BIND_ACT_SWITCH:
						term_switch (cur->base + (event->keyval - cur->key_min), cur->cmd);
						break;
					case BIND_ACT_CUT:
						widget = gtk_notebook_get_nth_page(terms.notebook, gtk_notebook_get_current_page(terms.notebook));
						vte_terminal_copy_clipboard (VTE_TERMINAL(widget));
						break;
					case BIND_ACT_PASTE:
						widget = gtk_notebook_get_nth_page(terms.notebook, gtk_notebook_get_current_page(terms.notebook));
						vte_terminal_paste_clipboard (VTE_TERMINAL(widget));
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
	if (event->button == 3) {
		gtk_widget_show_all(terms.menu);
		gtk_menu_popup(GTK_MENU(terms.menu), NULL, NULL, NULL, NULL, event->button, event->time);
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
	bind->next = terms.keys;
	terms.keys = bind;

	if (!strcasecmp(subs[0], "CUT")) {
		bind->action = BIND_ACT_CUT;
	} else if (!strcasecmp(subs[0], "PASTE")) {
		bind->action = BIND_ACT_PASTE;
	} else {
		return;
	}
	bind->state = strtol (subs[1], NULL, 0);
	bind->key_min = bind->key_max = gdk_keyval_from_name (subs[2]);
//	printf ("Binding: keyval: %d, state: 0x%x, action: %d\n", bind->key_min, bind->state, bind->action);
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
	GtkWidget *widget = gtk_notebook_get_nth_page(terms.notebook, gtk_notebook_get_current_page(terms.notebook));
	vte_terminal_copy_clipboard (VTE_TERMINAL(widget));
}

void
do_paste (GtkMenuItem *item, void *data)
{
	GtkWidget *widget = gtk_notebook_get_nth_page(terms.notebook, gtk_notebook_get_current_page(terms.notebook));
	vte_terminal_paste_clipboard (VTE_TERMINAL(widget));
}

void
do_t_decorate (GtkMenuItem *item, void *data)
{
	gboolean decorated = gtk_window_get_decorated (GTK_WINDOW(terms.window));
	gtk_window_set_decorated(GTK_WINDOW(terms.window), !decorated);
}

void
do_t_tabbar (GtkMenuItem *item, void *data)
{
	gboolean show_tabs = gtk_notebook_get_show_tabs (GTK_NOTEBOOK(terms.notebook));
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(terms.notebook), !show_tabs);
}

void
temu_parse_config (void)
{
#define MATCHES	16
	regex_t bind_action, bind_switch, color, font, size;
	regmatch_t regexp_matches[MATCHES];
	char *subs[MATCHES] = { 0 };
	FILE *f;
	char *file = NULL;
	long f_len;
	int j, ret;
	char *t1, *t2;
	char conffile[512] = { 0 };
	size_t read;

	regcomp (&bind_action, "^bind:[ \t]+([a-zA-Z_]+)[ \t]+([0-9]+)[ \t]+([a-zA-Z0-9_]+)$", REG_EXTENDED);
	regcomp (&bind_switch, "^bind:[ \t]+([0-9]+)[ \t]+([0-9]+)[ \t]+([a-zA-Z0-9_]+)(-([a-zA-Z0-9_]+))?([ \t]+(.*?))?$", REG_EXTENDED);
	regcomp (&color, "^color:[ \t]+([0-9]+)[ \t]+(.*?)$", REG_EXTENDED);
	regcomp (&font, "^font:[ \t]+(.*?)$", REG_EXTENDED);
	regcomp (&size, "^size:[ \t]+([0-9]+)x([0-9]+)$", REG_EXTENDED);

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

int new_window (void)
{
	GtkWidget *window, *notebook;
	GdkRGBA black = { .red = 0, .green = 0, .blue = 0, .alpha = 1 };
	int i;

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
	gtk_widget_override_background_color(window, GTK_STATE_NORMAL, &black);

	notebook = gtk_notebook_new();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK(notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK(notebook), FALSE);
	gtk_container_add(GTK_CONTAINER(window), notebook);
	gtk_container_set_border_width (GTK_CONTAINER(window), 0);
	gtk_container_set_border_width (GTK_CONTAINER(notebook), 0);
	gtk_widget_show(notebook);

	windows[i].window = GTK_WIDGET(window);
	windows[i].notebook = GTK_NOTEBOOK(notebook);

	windows[i].menu = gtk_menu_new();

	windows[i].m_copy = gtk_menu_item_new_with_mnemonic("_Copy");
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_copy);
	g_signal_connect(windows[i].m_copy, "activate", G_CALLBACK(do_copy), NULL);

	windows[i].m_paste = gtk_menu_item_new_with_mnemonic("_Paste");
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_paste);
	g_signal_connect(windows[i].m_paste, "activate", G_CALLBACK(do_paste), NULL);

	windows[i].m_t_decorate = gtk_menu_item_new_with_mnemonic("_Toggle decorations");
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_t_decorate);
	g_signal_connect(windows[i].m_t_decorate, "activate", G_CALLBACK(do_t_decorate), NULL);

	windows[i].m_t_tabbar = gtk_menu_item_new_with_mnemonic("_Toggle tab bar");
	gtk_menu_shell_append(GTK_MENU_SHELL(windows[i].menu), windows[i].m_t_tabbar);
	g_signal_connect(windows[i].m_t_tabbar, "activate", G_CALLBACK(do_t_tabbar), NULL);

	gtk_widget_set_can_focus(notebook, FALSE);

	gtk_widget_show(window);

	g_signal_connect (notebook, "switch_page", G_CALLBACK (term_switch_page), GTK_NOTEBOOK(notebook));

	g_signal_connect (window, "key-press-event", G_CALLBACK (term_key_event), &terms);

	return i;
}

int main(int argc, char *argv[], char *envp[])
{
	int i;

	gtk_init(&argc, &argv);

	memset (&terms, 0, sizeof (terms));
	terms.envp = envp;
	temu_parse_config ();
	if (!terms.n_active) {
		fprintf (stderr, "Unable to read config file, or no terminals defined.\n");
		exit (0);
	}
	terms.active = calloc(terms.n_active, sizeof(*terms.active));

	{
		bind_t *cur;
		char *cmd = NULL;

		for (cur = terms.keys; cur; cur = cur->next) {
			if (cur->base == 0) {
				cmd = cur->cmd;
				break;
			}
		}

		term_switch (0, cmd);
	}

	gtk_main();

	printf("Exiting, can free here. (%d)\n", terms.n_active);
	for (i = 0; i < terms.n_active; i++) {
		if (terms.active[i]) {
			gtk_widget_hide (GTK_WIDGET(terms.active[i]));
			gtk_container_remove (GTK_CONTAINER(terms.notebook), GTK_WIDGET(terms.active[i]));
			gtk_widget_destroy (GTK_WIDGET(terms.active[i]));
		}
	}

	gtk_widget_destroy (GTK_WIDGET(terms.notebook));
	free (terms.active);
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
