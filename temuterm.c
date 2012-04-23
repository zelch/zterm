#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeys.h>
#include <gdk/gdkkeysyms.h>

#include "terminal.h"
#include "regexp.h"

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

typedef struct terms_s {
	GtkNotebook *notebook;
	GtkWidget **active;
	GtkWidget *window;
	gint n_active, alive;
	char **envp;

	/* Configuration options. */
	bind_t	*keys;
	color_t	*colors;
	char	*font;
} terms_t;

typedef struct tracker_s {
	terms_t	*terms;
	gint n;
} tracker_t;

static void
temu_reorder (terms_t *terms)
{
	int i, j;

	for (i = j = 0; i < terms->n_active && j < terms->alive; i++)
		if (terms->active[i])
			gtk_notebook_reorder_child (terms->notebook, terms->active[i], j++);
}


static void
temu_window_title_change (TemuTerminal *terminal, GtkWidget *window, int n)
{
	gchar *new_str;

	if (asprintf(&new_str, "%s [%d]", terminal->window_title, n) < 0) {
		return; // Memory allocation issue.
	}

	gtk_window_set_title(GTK_WINDOW(window), new_str);
	free (new_str);
}

static void
temu_window_title_changed(TemuTerminal *terminal, gpointer data)
{
	tracker_t *tracker = (tracker_t *) data;
	terms_t *terms = tracker->terms;

	temu_window_title_change (terminal, terms->window, tracker->n);
}

static gboolean
term_died (TemuTerminal *term, gpointer user_data)
{
	tracker_t *tracker = (tracker_t *) user_data;
	terms_t *terms = tracker->terms;
	int status;

	waitpid (-1, &status, WNOHANG);

	gtk_notebook_prev_page (terms->notebook);
	gtk_widget_hide (GTK_WIDGET(term));
	gtk_container_remove (GTK_CONTAINER(terms->notebook), GTK_WIDGET(term));
	terms->active[tracker->n] = NULL;
	gtk_widget_destroy (GTK_WIDGET(term));

	return TRUE;
}

static gboolean
term_destroyed (TemuTerminal *term, gpointer user_data)
{
	tracker_t *tracker = (tracker_t *) user_data;
	terms_t *terms = tracker->terms;

//	fprintf (stderr, "%s: %d\n", __func__, tracker->n);

	g_object_unref (G_OBJECT(term));

	terms->active[tracker->n] = NULL;
	terms->alive--;

	free (tracker);

	if (!terms->alive)
		gtk_main_quit();

	return TRUE;
}

static void
term_switch (terms_t *terms, gint n, char *cmd)
{
	if (n > terms->n_active)
		return;

	if (!terms->active[n]) {
		GtkWidget *term, *label;
		GdkGeometry geom = { 0 };
		GdkWindowHints hints = 0;
		TemuTerminal *terminal;
		char str[32];
		tracker_t *tracker;

		term = temu_terminal_new();
		g_object_ref (G_OBJECT(term));
		gtk_object_sink (GTK_OBJECT(term));
		gtk_widget_show(term);

//		fprintf (stderr, "New window: %d, %p\n", n, term);
		terminal = TEMU_TERMINAL(term);
		if (!terminal->window_title)
			terminal->window_title = g_strdup("temuterm");

		if (terms->colors) {
			color_t *color;

			for (color = terms->colors; color; color = color->next)
				temu_screen_set_color (TEMU_SCREEN (term), color->n, &color->color);
		}
		if (terms->font)
			temu_screen_set_font (TEMU_SCREEN (term), terms->font);

		temu_screen_get_base_geometry_hints(TEMU_SCREEN(term), &geom, &hints);
		gtk_window_set_geometry_hints (GTK_WINDOW(terms->window), GTK_WIDGET(term), &geom, hints);

		unsetenv ("TERM");
		unsetenv ("COLORTERM");
		setenv("TERM", "temu", 1);
		setenv("COLORTERM", "temu", 1);

		tracker = calloc(1, sizeof (tracker_t));
		tracker->terms = terms;
		tracker->n = n;
		terminal->client_data = tracker;
		g_signal_connect_after (GTK_OBJECT (term), "child_died", G_CALLBACK (term_died), tracker);
		g_signal_connect_after (GTK_OBJECT (term), "destroy", G_CALLBACK (term_destroyed), tracker);
		g_signal_connect (G_OBJECT(term), "window_title_changed", G_CALLBACK(temu_window_title_changed), tracker);

		if (cmd) {
			char *argv[] = {
				"/bin/sh",
				"-c",
				cmd,
				NULL
			};
			temu_terminal_execve(TEMU_TERMINAL(term), argv[0], argv, environ);
		} else {
			char *argv[] = {
				getenv("SHELL"), // one good temp. hack deserves another
				"--login",
				NULL
			};
			temu_terminal_execve(TEMU_TERMINAL(term), argv[0], argv, environ);
		}

		snprintf(str, sizeof(str), "Terminal %d", n);
		label = gtk_label_new(str);

		gtk_notebook_set_current_page(terms->notebook, gtk_notebook_append_page (terms->notebook, term, label));
		gtk_widget_realize(term);
		gtk_widget_show(label);

		terms->active[n] = term;
		terms->alive++;

		temu_reorder (terms);
	}

	gtk_notebook_set_current_page (terms->notebook, gtk_notebook_page_num (terms->notebook, terms->active[n]));
//	temu_window_title_change (TEMU_TERMINAL(terms->active[n]), terms->window, n);
}

static void
term_switch_page (GtkNotebook *notebook, GtkNotebookPage *page, gint page_num, gpointer user_data)
{
	TemuTerminal *term;

	term = TEMU_TERMINAL(gtk_notebook_get_nth_page (notebook, page_num));
	/*
	fprintf (stderr, "page_num: %d, current_page: %d, page: %p, term: %p, notebook: %p, user_data: %p\n",
			page_num, gtk_notebook_get_current_page (notebook), page, term, notebook, user_data);
			*/
	gtk_widget_grab_focus (GTK_WIDGET(term));
	temu_window_title_changed (term, term->client_data);
}

static gboolean
term_key_event (GtkWidget * widget, GdkEventKey * event, gpointer user_data)
{
	terms_t *terms = (terms_t *) user_data;
	bind_t	*cur;
	guint state = event->state;
	GtkClipboard *clipboard;
	gchar *text;

	state &= 0xED;
//	fprintf (stderr, "%s: keyval: %d, state: 0x%x\n", __func__, event->keyval, state);

	for (cur = terms->keys; cur; cur = cur->next) {
		if ((event->keyval >= cur->key_min) && (event->keyval <= cur->key_max)) {
			if (state == cur->state) {
				switch (cur->action) {
					case BIND_ACT_SWITCH:
						term_switch (terms, cur->base + (event->keyval - cur->key_min), cur->cmd);
						break;
					case BIND_ACT_CUT:
						widget = gtk_notebook_get_nth_page(terms->notebook, gtk_notebook_get_current_page(terms->notebook));
						if (GTK_WIDGET_REALIZED(widget)) {
							clipboard = gtk_clipboard_get_for_display(gtk_widget_get_display(widget), GDK_SELECTION_CLIPBOARD);
						} else {
							clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD); /* wing it */
						}

						text = temu_terminal_get_selection_text(widget);

//						printf("cut: '%s'\n", text);
						gtk_clipboard_set_text(clipboard, text, strlen(text));
						break;
					case BIND_ACT_PASTE:
						widget = gtk_notebook_get_nth_page(terms->notebook, gtk_notebook_get_current_page(terms->notebook));
						if (GTK_WIDGET_REALIZED(widget)) {
							clipboard = gtk_clipboard_get_for_display(gtk_widget_get_display(widget), GDK_SELECTION_CLIPBOARD);
						} else {
							clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD); /* wing it */
						}

						text = gtk_clipboard_wait_for_text(clipboard);
						if (text) {
							//printf("paste: '%s'\n", text);
							temu_terminal_insert_text(widget, text);
							g_free(text);
						} else { // Oh boy, this is going to be 'Fun'.
							GtkSelectionData *data;
							data = gtk_clipboard_wait_for_contents(clipboard, gdk_atom_intern_static_string("STRING"));
							if (data) {
								//printf("paste; len: %d, '%s'\n", data->length, data->data);
								{
									char buf[data->length + 1];
									memset(buf, '\0', data->length + 1);
									memcpy(buf, data->data, data->length);
									temu_terminal_insert_text(widget, buf);
								}
								gtk_selection_data_free(data);
							}
						}
						break;
				}
				return TRUE;
			}
		}
	}

	return FALSE;
}

static void
temu_parse_bind_switch (terms_t *terms, char **subs, bind_actions_t action)
{
	bind_t *bind = calloc (1, sizeof (bind_t));
	bind->next = terms->keys;
	terms->keys = bind;

	bind->action = action;
	bind->base = strtol(subs[0], NULL, 0);
	bind->state = strtol(subs[1], NULL, 0);
	bind->key_min = gdk_keyval_from_name (subs[2]);
	if (subs[4])
		bind->key_max = gdk_keyval_from_name (subs[4]);
	else
		bind->key_max = bind->key_min;

	if (subs[5])
		bind->cmd = strdup (subs[5]);

	terms->n_active += bind->key_max - bind->key_min;
	terms->n_active++;
}

static void
temu_parse_bind_action (terms_t *terms, char **subs)
{
	bind_t *bind = calloc (1, sizeof (bind_t));
	bind->next = terms->keys;
	terms->keys = bind;

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
temu_parse_color (terms_t *terms, char **subs)
{
	color_t *color = calloc (1, sizeof (color_t));
	color->next = terms->colors;
	terms->colors = color;

	color->n = strtol (subs[0], NULL, 0);
	gdk_color_parse (subs[1], &color->color);
}

static void
temu_parse_font (terms_t *terms, char **subs)
{
	if (terms->font)
		free (terms->font);
	terms->font = strdup (subs[0]);
}

static void
temu_parse_size (terms_t *terms, char **subs)
{
	start_width = strtol(subs[0], NULL, 10);
	start_height = strtol(subs[1], NULL, 10);
}

void
temu_parse_config (terms_t *terms)
{
	struct regexp *bind_action = regexp_new("^bind:[ \t]+([a-zA-Z_]+)[ \t]+([0-9]+)[ \t]+([a-zA-Z0-9_]+)$", 0);
	struct regexp *bind_switch = regexp_new("^bind:[ \t]+([0-9]+)[ \t]+([0-9]+)[ \t]+([a-zA-Z0-9_]+)(-([a-zA-Z0-9_]+))?([ \t]+(.*?))?$", 0);
	struct regexp *color = regexp_new("^color:[ \t]+([0-9]+)[ \t]+(.*?)$", 0);
	struct regexp *font = regexp_new("^font:[ \t]+(.*?)$", 0);
	struct regexp *size = regexp_new("^size:[ \t]+([0-9]+)x([0-9]+)$", 0);
	struct regexp_iterator *iterator = NULL;
	char **subs = NULL;
	FILE *f;
	char *file = NULL;
	long f_len;
	int j;
	char *t1, *t2;
	char conffile[512] = { 0 };

	if (!bind_action || !bind_switch || !color || !font || !size) {
		printf("Unable to compile regexp for config file parsing!\n");
		goto done;
	}

	snprintf(conffile, sizeof(conffile) - 1, "%s/.temuterm/config", getenv("HOME"));
	f = fopen(conffile, "r");
	if (!f)
		goto done;
	fseek(f, 0, SEEK_END);
	f_len = ftell(f);
	fseek(f, 0, SEEK_SET);
	file = calloc (1, f_len + 1);
	fread (file, f_len, 1, f);
	fclose(f);

	t1 = file;
	while (*t1) {
		t2 = strchr (t1, '\n');
		*t2++ = '\0';

		j = t1[0] == '#' ? 1 : 0;

		if (!j) {
			j = regexp_find_first_str (t1, &subs, bind_action, &iterator);
			if (j)
				temu_parse_bind_action (terms, subs);
			regexp_find_free (&subs, bind_action, &iterator);
		}

		if (!j) {
			j = regexp_find_first_str (t1, &subs, bind_switch, &iterator);
			if (j)
				temu_parse_bind_switch (terms, subs, BIND_ACT_SWITCH);
			regexp_find_free (&subs, bind_switch, &iterator);
		}

		if (!j) {
			j = regexp_find_first_str (t1, &subs, color, &iterator);
			if (j)
				temu_parse_color (terms, subs);
			regexp_find_free (&subs, color, &iterator);
		}

		if (!j) {
			j = regexp_find_first_str (t1, &subs, font, &iterator);
			if (j)
				temu_parse_font (terms, subs);
			regexp_find_free (&subs, font, &iterator);
		}

		if (!j) {
			j = regexp_find_first_str (t1, &subs, size, &iterator);
			if (j)
				temu_parse_size (terms, subs);
			regexp_find_free (&subs, size, &iterator);
		}

		if (!j)
			fprintf (stderr, "Unable to parse line in config: '%s'\n", t1);

		t1 = t2;
	}

done:
	if (bind_action) 
		regexp_free (bind_action);
	if (bind_switch)
		regexp_free (bind_switch);
	if (color)
		regexp_free (color);
	if (font)
		regexp_free (font);
	if (size)
		regexp_free (size);
	if (file)
		free (file);

	return;
}

int main(int argc, char *argv[], char *envp[])
{
	GtkWidget *window, *notebook;
	GdkColor black = { .red = 0, .green = 0, .blue = 0 };
	terms_t *terms;
	int i;

	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 0);
	gtk_window_set_default_size (GTK_WINDOW(window), start_width, start_height);
//	gtk_window_set_default_size (GTK_WINDOW(window), 800, 600);
	gtk_widget_modify_bg(window, GTK_STATE_NORMAL, &black);

	notebook = gtk_notebook_new();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK(notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK(notebook), FALSE);
	gtk_container_add(GTK_CONTAINER(window), notebook);
	gtk_container_set_border_width (GTK_CONTAINER(window), 0);
	gtk_container_set_border_width (GTK_CONTAINER(notebook), 0);
	GTK_WIDGET_UNSET_FLAGS (notebook, GTK_CAN_FOCUS);
	gtk_widget_show(notebook);

	terms = calloc(1, sizeof (terms_t));
	terms->notebook = GTK_NOTEBOOK(notebook);
	terms->envp = envp;
	terms->window = window;
	temu_parse_config (terms);
	if (!terms->n_active) {
		fprintf (stderr, "Unable to read config file, or no terminals defined.\n");
		exit (0);
	}
	terms->active = calloc(terms->n_active, sizeof(*terms->active));

	{
		bind_t *cur;
		char *cmd = NULL;

		for (cur = terms->keys; cur; cur = cur->next) {
			if (cur->base == 0) {
				cmd = cur->cmd;
				break;
			}
		}

		term_switch (terms, 0, cmd);
	}

	gtk_widget_show(window);

	gtk_signal_connect (GTK_OBJECT (notebook), "switch_page", GTK_SIGNAL_FUNC (term_switch_page), GTK_NOTEBOOK(notebook));

	g_signal_connect (GTK_OBJECT (window),
			"key-press-event",
			GTK_SIGNAL_FUNC (term_key_event),
			terms);

	gtk_main();

	printf("Exiting, can free here. (%d)\n", terms->n_active);
	for (i = 0; i < terms->n_active; i++) {
		if (terms->active[i]) {
			gtk_widget_hide (GTK_WIDGET(terms->active[i]));
			gtk_container_remove (GTK_CONTAINER(terms->notebook), GTK_WIDGET(terms->active[i]));
			gtk_widget_destroy (GTK_WIDGET(terms->active[i]));
		}
	}

	gtk_widget_destroy (GTK_WIDGET(terms->notebook));
	printf("Unrefed notebook, maybe not right.\n");
	free (terms->active);
	terms->active = NULL;

	if (terms->font) {
		free (terms->font);
		terms->font = NULL;
	}
	if (terms->colors) {
		free (terms->colors);
		terms->colors = NULL;
	}
	{
		bind_t *keys, *next;
		for (keys = terms->keys; keys; keys = next) {
			next = keys->next;
			if (keys->cmd) {
				free (keys->cmd);
				keys->cmd = NULL;
			}
			free (keys);
		}
		terms->keys = NULL;
	}

	free (terms);

	return 0;
}
