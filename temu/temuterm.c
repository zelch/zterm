#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeys.h>
#include <gdk/gdkkeysyms.h>

#include "terminal.h"
#include "regexp.h"

typedef struct bind_s {
	guint state;
	guint key_min, key_max;
	guint base;
	struct bind_s *next;
	char *cmd;
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
	gulong died_id, destroyed_id;
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
temu_title_changed(TemuTerminal *terminal, gpointer data)
{
	tracker_t *tracker = (tracker_t *) data;
	terms_t *terms = tracker->terms;
	GValue value = { 0 };

	g_value_init(&value, G_TYPE_STRING);
	g_object_get_property(G_OBJECT(terminal), "window_title", &value);
	gtk_window_set_title(GTK_WINDOW(terms->window), g_value_get_string(&value));
}

static gboolean
term_died (TemuTerminal *term, gpointer user_data)
{
	tracker_t *tracker = (tracker_t *) user_data;
	terms_t *terms = tracker->terms;

	gtk_notebook_prev_page (terms->notebook);
	gtk_widget_hide (GTK_WIDGET(term));
	gtk_container_remove (GTK_CONTAINER(terms->notebook), GTK_WIDGET(term));

	return TRUE;
}

static gboolean
term_destroyed (TemuTerminal *term, gpointer user_data)
{
	tracker_t *tracker = (tracker_t *) user_data;
	terms_t *terms = tracker->terms;


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
		char str[32];
		tracker_t *tracker;

		term = temu_terminal_new();
		gtk_widget_show(term);

		snprintf(str, sizeof(str), "Terminal %d", n);
		label = gtk_label_new(str);
		gtk_notebook_set_current_page(terms->notebook, gtk_notebook_append_page (terms->notebook, term, label));
		gtk_widget_realize(term);
		gtk_widget_show(label);

		if (terms->colors) {
			color_t *color;

			for (color = terms->colors; color; color = color->next)
				temu_screen_set_color (TEMU_SCREEN (term), color->n, &color->color);
		}
		if (terms->font)
			temu_screen_set_font (TEMU_SCREEN (term), terms->font);

		temu_screen_get_base_geometry_hints(TEMU_SCREEN(term), &geom, &hints);
		gtk_window_set_geometry_hints (GTK_WINDOW(terms->window), GTK_WIDGET(term), &geom, hints);

		setenv("TERM", "temu", 1);
		setenv("COLORTERM", "temu", 1);

		tracker = calloc(1, sizeof (tracker_t));
		tracker->terms = terms;
		tracker->n = n;
		g_signal_connect (GTK_OBJECT (term), "child_died", G_CALLBACK (term_died), tracker);
		g_signal_connect (GTK_OBJECT (term), "destroy", G_CALLBACK (term_destroyed), tracker);
		g_signal_connect (G_OBJECT(term), "title_changed", G_CALLBACK(temu_title_changed), tracker);

		if (cmd) {
			char *argv[] = {
				"/bin/sh",
				"-c",
				cmd,
				NULL
			};
			temu_terminal_execve(TEMU_TERMINAL(term), argv[0], argv, terms->envp);
		} else {
			char *argv[] = {
				getenv("SHELL"), // one good temp. hack deserves another
				"--login",
				NULL
			};
			temu_terminal_execve(TEMU_TERMINAL(term), argv[0], argv, terms->envp);
		}

		terms->active[n] = term;
		terms->alive++;

		temu_reorder (terms);
	}

	gtk_notebook_set_current_page (terms->notebook, gtk_notebook_page_num (terms->notebook, terms->active[n]));
}

static gboolean
term_key_event (GtkWidget * widget, GdkEventKey * event, gpointer user_data)
{
	terms_t *terms = (terms_t *) user_data;
	bind_t	*cur;
	guint state = event->state;

	state &= 0xED;

	for (cur = terms->keys; cur; cur = cur->next) {
		if ((state == cur->state) && (event->keyval >= cur->key_min) && (event->keyval <= cur->key_max)) {
			term_switch (terms, cur->base + (event->keyval - cur->key_min), cur->cmd);
			return TRUE;
		}
	}

	return FALSE;
}

static void
temu_parse_bind (terms_t *terms, char **subs)
{
	bind_t *bind = calloc (1, sizeof (bind_t));
	bind->next = terms->keys;
	terms->keys = bind;

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

void
temu_parse_config (terms_t *terms)
{
	struct regexp *line = regexp_new("(.*?)\n", 0);
	struct regexp *bind = regexp_new("^bind:[ \t]+([0-9]+)[ \t]+([0-9]+)[ \t]+([a-zA-Z0-9_]+)(-([a-zA-Z0-9_]+))?([ \t]+(.*?))?$", 0);
	struct regexp *color = regexp_new("^color:[ \t]+([0-9]+)[ \t]+(.*?)$", 0);
	struct regexp *font = regexp_new("^font:[ \t]+(.*?)$", 0);
	struct regexp_iterator *iterator = NULL;
	char **subs = NULL;
	FILE *f;
	char *file;
	long f_len;
	int j;
	char *t1, *t2;

	if (!bind || !line || !color || !font)
		return;

	f = fopen("/home/warp/.temuterm/config", "r");
	if (!f)
		return;
	fseek(f, 0, SEEK_END);
	f_len = ftell(f);
	fseek(f, 0, SEEK_SET);
	file = calloc (1, f_len);
	fread (file, f_len, 1, f);
	fclose(f);

	t1 = file;
	while (*t1) {
		t2 = strchr (t1, '\n');
		*t2++ = '\0';

		j = t1[0] == '#' ? 1 : 0;

		if (!j) {
			j = regexp_find_first_str (t1, &subs, bind, &iterator);
			if (j)
				temu_parse_bind (terms, subs);
			regexp_find_free (&subs, bind, &iterator);
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

		if (!j)
			fprintf (stderr, "Unable to parse line in config: '%s'\n", t1);

		t1 = t2;
	}
	regexp_free (bind);
	regexp_free (color);
	regexp_free (font);
	free (file);

	return;
}

int main(int argc, char *argv[], char *envp[])
{
	GtkWidget *window, *notebook;
	GdkColor black = { .red = 0, .green = 0, .blue = 0 };
	terms_t *terms;

	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 0);
	gtk_window_set_default_size (GTK_WINDOW(window), 1024, 768);
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

	term_switch (terms, 0, NULL);

	gtk_widget_show(window);

	g_signal_connect (GTK_OBJECT (window),
			"key-press-event",
			GTK_SIGNAL_FUNC (term_key_event),
			terms);

	gtk_main();

	return 0;
}
