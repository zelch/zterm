#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <vte/vte.h>
#include <regex.h>
#include <stdbool.h>
#include "zterm.h"

#define GTK4 1

extern char **environ;
#ifdef HAVE_LIBBSD
#  include <bsd/string.h>
#endif

GdkRGBA colors[256] = {
#include "256colors.h"
};

#define DEBUG	1
//#undef DEBUG

#define debugf(format, ...)		_fdebugf(stderr, "%s %d (%s): " format "\n", __FILE__, __LINE__, __func__ __VA_OPT__(,) __VA_ARGS__)

int _fdebugf(FILE *io, const char *fmt, ...)
{
#ifdef DEBUG
	va_list args;

	va_start (args, fmt);

	int ret = vfprintf(io, fmt, args);
	fflush(io);
	return ret;
#else
	return 0;
#endif
}

int start_width = 1024;
int start_height = 768;

terms_t terms;
window_t windows[MAX_WINDOWS];

GtkApplication *app;

static gboolean term_button_event (GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data);
int new_window (void);
void destroy_window (int i);
/*
void rebuild_menu_switch_lists (void);
void destroy_window_menu (int i);
*/

static void
temu_reorder (void)
{
	int window_i;
	int i, j;

	for (window_i = 0; window_i < MAX_WINDOWS; window_i++) {
		if (windows[window_i].window) {
			for (i = j = 0; i < terms.n_active; i++) {
				if (terms.active[i].term && terms.active[i].window == window_i) {
					gtk_notebook_reorder_child (windows[window_i].notebook, terms.active[i].term, j++);
				}
			}
		}
	}
}

static void
temu_window_title_change (VteTerminal *terminal, long int n)
{
	char window_str[16] = { 0 };
	const char *title_str = "zterm";
	gchar new_str[64] = { 0 };
	int max_windows = 0;
	int window_i = terms.active[n].window;
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
	int pruned = 0;

	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (windows[i].window) {
			int active_pages = gtk_notebook_get_n_pages(windows[i].notebook);
			debugf("%d pages for notebook %p of window %d", gtk_notebook_get_n_pages(windows[i].notebook), windows[i].notebook, i);
			active += active_pages;
			if (active_pages <= 0) {
				destroy_window (i);
				pruned++;
			}
		}
	}

	if (active != terms.alive) {
		printf ("Found %d active tabs, but %d terms alive.\n", active, terms.alive);
	}

	if (active <= 0) {
		printf("Attempting to exit...\n");
		g_application_quit(G_APPLICATION(app));
	}

	if (pruned) {
		debugf("Pruned %d windows.", pruned);

		rebuild_menus();
	}
	debugf("");
}

static gboolean
term_died (VteTerminal *term, gpointer user_data)
{
	int status, n = -1, window_i = -1;

	printf("Term died...\n");

	waitpid (-1, &status, WNOHANG);

	for (int i = 0; i < terms.n_active; i++) {
		if (terms.active[i].term == GTK_WIDGET(term)) {
			n = i;
			window_i = terms.active[n].window;
			break;
		}
	}
	if (n == -1 || window_i == -1) {
		printf ("Unable to find term that died.\n");
		return FALSE;
	}

	debugf ("Removing dead term %d from window %d.", n, window_i);
	gtk_notebook_prev_page (windows[window_i].notebook);
	gtk_widget_hide (GTK_WIDGET(term));
	int i = gtk_notebook_page_num(windows[window_i].notebook, GTK_WIDGET(term));
	debugf("About to remove notebook page %d", i);
	gtk_notebook_remove_page(windows[window_i].notebook, i);
	debugf("");

	prune_windows ();
	debugf("");

	return TRUE;
}

static gboolean
term_unrealized (VteTerminal *term, gpointer user_data)
{
	int n = (long) user_data;

	debugf("Got unrealize for term %d.", n);
	if (terms.active[n].moving) {
		debugf("Not destroying term %d because we are moving it.", n);
		return FALSE;
	}

	g_object_unref (G_OBJECT(term));

	terms.active[n].spawned--;
	terms.active[n].term = NULL;
	terms.alive--;

	if (!terms.alive) {
		printf("Attempting to exit...\n");
		g_application_quit(G_APPLICATION(app));
	}

	debugf("");

	return TRUE;
}

void
term_set_window (int n, int window_i)
{
	GtkWidget *term = terms.active[n].term;

	// Create a new window if we are passed a non-existant window.
	if (!windows[window_i].window) {
		window_i = new_window ();
		debugf ("Setting term %d to NEW window %d.", n, window_i);
	}

	// Set the new active window before removing from the old window.
	// This is done first so that all of the window titles come out right.
	terms.active[n].window = window_i;

	/*
	 * When we remove the terminal from the existing notebook, this may cause
	 * it to be unrealized.
	 *
	 * When that happens, we really don't want it to actually be destroyed, or
	 * for that to trigger an exit if it's the only terminal.
	 *
	 * As such, the moving indicator prevents those things.
	 */
	terms.active[n].moving++;

	// Remove from any previous window.
	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (windows[i].window) {
			if (gtk_notebook_page_num (windows[i].notebook, term) >= 0) {
				debugf("Removing term %d from window %d.", n, i);
				gtk_notebook_prev_page (windows[i].notebook);
				debugf("%d pages for notebook %p of window %d", gtk_notebook_get_n_pages(windows[i].notebook), windows[i].notebook, i);
				//gtk_notebook_remove_page(windows[i].notebook, gtk_notebook_page_num(windows[i].notebook, GTK_WIDGET(term)));
				gtk_notebook_detach_tab(windows[i].notebook, GTK_WIDGET(term));
				debugf("%d pages for notebook %p of window %d", gtk_notebook_get_n_pages(windows[i].notebook), windows[i].notebook, i);
			}
		}
	}

	if (terms.color_schemes[windows[window_i].color_scheme].name[0]) {
		vte_terminal_set_colors (VTE_TERMINAL (term), &terms.color_schemes[windows[window_i].color_scheme].foreground, &terms.color_schemes[windows[window_i].color_scheme].background, &colors[0], MIN(256, sizeof (colors) / sizeof(colors[0])));
	} else {
		vte_terminal_set_colors (VTE_TERMINAL (term), NULL, NULL, &colors[0], MIN(256, sizeof (colors) / sizeof(colors[0])));
	}

	int i = gtk_notebook_append_page (windows[window_i].notebook, term, NULL);
	gtk_notebook_set_current_page(windows[window_i].notebook, i);
	gtk_widget_set_can_focus(term, true);
	gtk_widget_realize(term);
	gtk_widget_show(term);
	gtk_widget_grab_focus(term);

	terms.active[n].moving--;

	prune_windows ();
	temu_reorder ();
}

void
term_config (GtkWidget *term, int window_i)
{
	if (terms.font) {
		pango_cairo_font_map_set_default(NULL); // Force a full reload of the fontmap, darn it.

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
	vte_terminal_set_enable_sixel (VTE_TERMINAL (term), true);
	vte_terminal_set_allow_hyperlink (VTE_TERMINAL (term), true);

	if (terms.color_schemes[windows[window_i].color_scheme].name[0]) {
		vte_terminal_set_colors (VTE_TERMINAL (term), &terms.color_schemes[windows[window_i].color_scheme].foreground, &terms.color_schemes[windows[window_i].color_scheme].background, &colors[0], MIN(256, sizeof (colors) / sizeof(colors[0])));
	} else {
		vte_terminal_set_colors (VTE_TERMINAL (term), NULL, NULL, &colors[0], MIN(256, sizeof (colors) / sizeof(colors[0])));
	}
}

static void
term_realized (GtkWidget *widget, void *data)
{
	int n = (long int) data;

	debugf("Realized for terminal %d, spawned: %d, cmd: %s.", n, terms.active[n].spawned, terms.active[n].cmd);

	if (!terms.active[n].spawned) {
		if (terms.active[n].cmd) {
			char *argv[] = {
				"/bin/sh",
				"-c",
				terms.active[n].cmd,
				NULL
			};
			vte_terminal_spawn_async (VTE_TERMINAL (terms.active[n].term), VTE_PTY_DEFAULT, NULL, argv, environ, G_SPAWN_DEFAULT, NULL, NULL, NULL, -1, NULL, NULL, NULL);
		} else {
			char *argv[] = {
				getenv("SHELL"), // one good temp. hack deserves another
				"--login",
				NULL
			};
			vte_terminal_spawn_async (VTE_TERMINAL (terms.active[n].term), VTE_PTY_DEFAULT, NULL, argv, environ, G_SPAWN_DEFAULT, NULL, NULL, NULL, -1, NULL, NULL, NULL);
		}

		terms.active[n].spawned++;
	}
}


void
term_switch (long n, char *cmd, int window_i)
{
	if (n > terms.n_active)
		return;

	if (!terms.active[n].term) {
		GtkWidget *term;

		term = vte_terminal_new();
		g_object_ref (G_OBJECT(term));
		gtk_widget_show(term);
		gtk_widget_set_hexpand (term, TRUE);
		gtk_widget_set_vexpand (term, TRUE);


		g_signal_connect_after (G_OBJECT (term), "child-exited", G_CALLBACK (term_died), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "unrealize", G_CALLBACK (term_unrealized), (void *) n);
		g_signal_connect (G_OBJECT(term), "window_title_changed", G_CALLBACK(temu_window_title_changed), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "realize", G_CALLBACK (term_realized), (void *) n);

		terms.active[n].cmd = cmd;
		terms.active[n].term = term;
		terms.alive++;

		term_set_window (n, window_i);

		term_config(term, window_i);
	}

	if (window_i != terms.active[n].window) {
		window_i = terms.active[n].window;
		gtk_window_present (GTK_WINDOW(windows[window_i].window));
	}

	gtk_notebook_set_current_page (windows[window_i].notebook, gtk_notebook_page_num (windows[window_i].notebook, terms.active[n].term));
	temu_window_title_change (VTE_TERMINAL(terms.active[n].term), n);
	gtk_widget_grab_focus (GTK_WIDGET(terms.active[n].term));
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
		if (term == VTE_TERMINAL(terms.active[n].term)) {
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
term_key_event (GtkEventControllerKey *key_controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data)
{
	window_t *window = (window_t *) user_data;
	bind_t	*cur;
	GtkWidget *widget;

	//state &= 0xED;
	state &= GDK_MODIFIER_MASK ^ (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK | GDK_BUTTON4_MASK | GDK_BUTTON5_MASK);
#if DEBUG
	gchar *name = gtk_accelerator_name(keyval, state);
	debugf ("keyval: %d (%s), state: 0x%x, %d, '%s'", keyval, gdk_keyval_name(keyval), state, state, name);
	g_free(name);
#endif

	for (cur = terms.keys; cur; cur = cur->next) {
		//fprintf (stderr, "key_min: %d (%s), key_max: %d (%s), state: 0x%x (%s)\n", cur->key_min, gdk_keyval_name(cur->key_min), cur->key_max, gdk_keyval_name(cur->key_max), cur->state, gtk_accelerator_name(0, cur->state));
		if ((keyval >= cur->key_min) && (keyval <= cur->key_max)) {
			if (state == cur->state) {
				switch (cur->action) {
					case BIND_ACT_SWITCH:
						term_switch (cur->base + (keyval - cur->key_min), cur->cmd, window - &windows[0]);
						break;
					case BIND_ACT_CUT:
						debugf("Cut?");
						widget = gtk_notebook_get_nth_page(window->notebook, gtk_notebook_get_current_page(window->notebook));
						vte_terminal_copy_clipboard_format (VTE_TERMINAL(widget), VTE_FORMAT_TEXT);
						break;
					case BIND_ACT_PASTE:
						debugf("Paste");
						widget = gtk_notebook_get_nth_page(window->notebook, gtk_notebook_get_current_page(window->notebook));
						vte_terminal_paste_clipboard (VTE_TERMINAL(widget));
						break;
					case BIND_ACT_MENU:
						gtk_popover_set_has_arrow (GTK_POPOVER(window->menu), TRUE);
						gtk_widget_set_halign(window->menu, GTK_ALIGN_CENTER);
						gtk_widget_set_valign(window->menu, GTK_ALIGN_CENTER);
						gtk_popover_set_pointing_to(GTK_POPOVER(window->menu), NULL);
						gtk_popover_popup(GTK_POPOVER(window->menu));
						break;
					case BIND_ACT_NEXT_TERM:
						gtk_notebook_next_page(GTK_NOTEBOOK(window->notebook));
						break;
					case BIND_ACT_PREV_TERM:
						gtk_notebook_prev_page(GTK_NOTEBOOK(window->notebook));
						break;
				}
				debugf("action: %d", cur->action);
				return TRUE;
			}
		}
	}

	return FALSE;
}

static
gboolean term_button_event (GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data)
{
	window_t *window = (window_t *) user_data;

	debugf("Got button: %d", gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)));

	GdkEvent *event = gtk_gesture_get_last_event (gesture, sequence);
	if (gdk_event_triggers_context_menu(event)) {
		GdkRectangle rect = { width: 1, height: 1 };
		double x, y;

		gtk_gesture_get_point(gesture, sequence, &x, &y);

		rect.x = x;
		rect.y = y;
		rect.height = 1;
		rect.width = 1;

		debugf ("Event should trigger context menu, x: %f / %d, y: %f / %d", x, rect.x, y, rect.y);

		gtk_widget_set_halign(window->menu, GTK_ALIGN_CENTER);
		gtk_widget_set_valign(window->menu, GTK_ALIGN_CENTER);
		gtk_popover_set_has_arrow (GTK_POPOVER(window->menu), FALSE);
		gtk_popover_set_pointing_to(GTK_POPOVER(window->menu), &rect);
		debugf("");
		gtk_popover_set_position(GTK_POPOVER(window->menu), GTK_POS_BOTTOM);
		debugf("");
		gtk_widget_realize(window->menu);
		debugf("window->menu: %p", window->menu);
		gtk_popover_popup(GTK_POPOVER(window->menu));
		int ret = gtk_widget_grab_focus(window->menu);
		debugf("grab_focus: %d", ret);
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
		if (bind->state) {
			printf("Parsing '%s' as accelerator, result: 0x%x\n", subs[1], bind->state);
		} else {
			fprintf(stderr, "Error: Unable to parse '%s' as GTK Accelerator, skipping bind: %s %s %s-%s %s\n", subs[1], subs[0], subs[1], subs[2], subs[4], subs[6]);
			return;
		}
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
	char bind_str[128] = { 0 };
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

	// The new style is actually a GTK accelerator string, but with only the modifier component.
	snprintf(bind_str, sizeof(bind_str) - 1, "%s%s", subs[1], subs[2]);

	gtk_accelerator_parse(bind_str, &bind->key_max, &bind->state);
	debugf("Parsing '%s' as accelerator, result: state: 0x%x, keyval: 0x%x", bind_str, bind->state, bind->key_max);
	if (bind->key_max) {
		bind->key_min = bind->key_max;
	} else {
		int ret = gtk_accelerator_parse(subs[1], NULL, &bind->state);
		bind->key_min = bind->key_max = strtol(subs[2], NULL, 0);
		debugf("Parsing '%s' as partial accelerator, result: state: 0x%x, keyval: 0x%x, ret: %d", bind_str, bind->state, bind->key_max, ret);
		if (!ret) {
			fprintf(stderr, "Error: Unable to parse '%s' as GTK Accelerator, skipping bind: %s %s %s\n", subs[1], subs[0], subs[1], subs[2]);
			return;
		} else if (!bind->key_max) {
			fprintf(stderr, "Error: Unable to parse '%s' as key, skipping bind: %s %s %s\n", subs[2], subs[0], subs[1], subs[2]);
			return;
		}
	}

	debugf ("Binding: keyval: 0x%x, state: 0x%x, action: %d (%s %s %s)\n", bind->key_min, bind->state, bind->action, subs[0], subs[1], subs[2]);
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
				snprintf (terms.color_schemes[n_color_scheme].action, sizeof(terms.color_schemes[n_color_scheme].action), "color_scheme.%d", n_color_scheme);
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
					if (!terms.rewrap_on_resize) {
						fprintf(stderr, "NOT SUPPORTED: rewrap_on_resize = 0\n");
					}
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

	printf("Building a new window...\n");

	window = gtk_application_window_new(app);
	debugf("");
	gtk_window_set_default_size (GTK_WINDOW(window), start_width, start_height);

	debugf("");
	notebook = gtk_notebook_new();
	debugf("");
	g_object_set (G_OBJECT (notebook), "scrollable", TRUE, "enable-popup", TRUE, NULL);
	debugf("");
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK(notebook), FALSE);
	debugf("");
	gtk_notebook_set_show_border (GTK_NOTEBOOK(notebook), FALSE);
	debugf("");
#if GTK4
	gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(notebook));
#else
#endif
	gtk_widget_show(GTK_WIDGET(notebook));

	windows[i].window = GTK_WIDGET(window);
	windows[i].notebook = GTK_NOTEBOOK(notebook);
	debugf("windows[%d].notebook: %p", i, windows[i].notebook);

	gtk_widget_set_can_focus(notebook, true);

	windows[i].key_controller = gtk_event_controller_key_new();
	gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(windows[i].key_controller));
	gtk_event_controller_set_propagation_phase(windows[i].key_controller, GTK_PHASE_CAPTURE);
	g_signal_connect (windows[i].key_controller, "key-pressed", G_CALLBACK (term_key_event), &windows[i]);

	GtkGesture *gesture = gtk_gesture_click_new();
	g_signal_connect(gesture, "begin", G_CALLBACK(term_button_event), (void *) &windows[i]);
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE(gesture), 0);
	gtk_widget_add_controller(GTK_WIDGET(windows[i].window), GTK_EVENT_CONTROLLER(gesture));

	g_signal_connect (notebook, "switch_page", G_CALLBACK (term_switch_page), GTK_NOTEBOOK(notebook));

	rebuild_menus();

	gtk_window_present_with_time (GTK_WINDOW(window), time(NULL));

	printf("Window should be visible...\n");

	return i;
}

void destroy_window (int i)
{
	if (windows[i].window) {
		debugf("Destroying window[%d].", i);
		debugf("unparent windows[%d].menu: %p", i, windows[i].menu);
		gtk_widget_unparent(GTK_WIDGET(windows[i].menu));
		debugf("");
		gtk_application_remove_window(app, GTK_WINDOW(windows[i].window));
		gtk_window_destroy(GTK_WINDOW(windows[i].window));
		debugf("");
		windows[i].notebook = NULL;
		windows[i].window = NULL;
		windows[i].menu = NULL;
		windows[i].key_controller = NULL;
	}
}

static void activate (GtkApplication *app, gpointer user_data)
{
	bind_t *cur;
	char *cmd = NULL;

	temu_parse_config ();
	if (!terms.n_active) {
		fprintf (stderr, "Unable to read config file, or no terminals defined.\n");
		exit (0);
	}
	terms.active = calloc(terms.n_active, sizeof(*terms.active));

	for (cur = terms.keys; cur; cur = cur->next) {
		if (cur->base == 0) {
			cmd = cur->cmd;
			break;
		}
	}

	term_switch (0, cmd, 0);
}

int main(int argc, char *argv[], char *envp[])
{
	int i;

	g_set_application_name("zterm");
	//gtk_init(&argc, &argv);
	app = gtk_application_new ("org.aehallh.zterm", 0);
	g_application_set_application_id (G_APPLICATION(app), "com.aehallh.zterm");
	g_application_set_flags (G_APPLICATION(app), G_APPLICATION_NON_UNIQUE);

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

	chdir(getenv("HOME"));

	g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);

	g_application_run(G_APPLICATION (app), argc, argv);

	printf("Exiting, can free here. (%d)\n", terms.n_active);
	for (i = 0; i < terms.n_active; i++) {
		if (terms.active[i].term) {
			int page_num = gtk_notebook_page_num(windows[terms.active[i].window].notebook, GTK_WIDGET(terms.active[i].term));
			gtk_widget_hide (GTK_WIDGET(terms.active[i].term));
			gtk_notebook_remove_page(windows[terms.active[i].window].notebook, page_num);
		}
	}

	for (i = 0; i < MAX_WINDOWS; i++) {
		destroy_window (i);
	}

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
