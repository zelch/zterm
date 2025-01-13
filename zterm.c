#define _GNU_SOURCE
#include "zterm.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <vte/vte.h>
#include <stdbool.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/time.h>
#include <ctype.h>
#define PCRE2_CODE_UNIT_WIDTH 0
#include <pcre2.h>

extern char **environ;

GdkRGBA colors[256] = {
#include "256colors.h"
};

int _vfprintf(FILE *io, const char *fmt, va_list args)
{
	// Only bother with timestamps if STDIN is a tty, otherwise we're probably running in a context that ends up in syslog/the systemd journal with it's own timestamp.
	if (isatty(0)) {
		char buf[32] = { 0 }; // With some extra space, because.
		int millisec;
		const struct tm *tm_info;
		struct timeval tv;

		gettimeofday(&tv, NULL);

		millisec = lrint(tv.tv_usec/1000.0);
		if (millisec >= 1000) {
			millisec -= 1000;
			tv.tv_sec++;
		}

		tm_info = localtime(&tv.tv_sec);

		strftime(buf, sizeof(buf) - 1, "%Y-%m-%d %H:%M:%S", tm_info);
		fprintf(io, "%s.%03d: ", buf, millisec);
	}

	int ret = vfprintf(io, fmt, args);
	fflush(io);
	return ret;
}

int _fprintf(bool print, FILE *io, const char *fmt, ...)
{
	va_list args;

	if (!print) {
		return 0;
	}

	va_start (args, fmt);

	int ret = _vfprintf(io, fmt, args);

	va_end(args);

	return ret;
}

// This exists just to ensure that the arguments are always evaluated.
int _fnullf(const FILE *io, const char *fmt, ...)
{
	return 0;
}

int start_width = 1024;
int start_height = 768;

int char_width = 0;
int char_height = 0;

unsigned int bind_mask = (GDK_MODIFIER_MASK & ~GDK_LOCK_MASK) ^ (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK | GDK_BUTTON4_MASK | GDK_BUTTON5_MASK);;

terms_t terms;
window_t windows[MAX_WINDOWS];

GtkApplication *app;

static gboolean button_event (GtkGesture *gesture, GdkEventSequence *sequence, int64_t term_n, window_t *window);
static gboolean term_button_event (GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data);
static gboolean window_button_event (GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data);
int new_window (void);
void destroy_window (int i);
void add_button(GtkWidget *widget, long int term_n, int window_i);

static void
print_widget_size(GtkWidget *widget, const char *name)
{
	int x, y;
	GtkRequisition minimum, natural;

	debugf("%s realized: %d, scale factor: %d", name, gtk_widget_get_realized(widget), gtk_widget_get_scale_factor(widget));
	gtk_widget_get_preferred_size(widget, &minimum, &natural);
	debugf("%s preferred minimum width x height: %dx%d", name, minimum.width, minimum.height);
	debugf("%s preferred natural width x height: %dx%d", name, natural.width, natural.height);
	gtk_widget_get_size_request(widget, &x, &y);
	debugf("%s size request width x height: %dx%d", name, x, y);
	debugf("%s allocated width x height: %dx%d (%x x %x)", name, gtk_widget_get_width(widget), gtk_widget_get_height(widget), gtk_widget_get_width(widget), gtk_widget_get_height(widget));
}


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

bool
term_find (GtkWidget *term, int *i)
{
	for (*i = 0; *i < terms.n_active; *i = *i + 1) {
		if (terms.active[*i].term == GTK_WIDGET(term)) {
			return true;
		}
	}

	return false; // Default to terminal 0.
}

static void
temu_window_title_change (VteTerminal *terminal, long int n)
{
	char window_str[16] = { 0 };
	const char *title_str;
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

#if VTE_CHECK_VERSION(0, 77, 0)
	title_str = vte_terminal_get_termprop_string_by_id(terminal, VTE_PROPERTY_ID_XTERM_TITLE, NULL);
#else
	title_str = vte_terminal_get_window_title(terminal);
#endif

	if (snprintf(new_str, sizeof(new_str) - 1, "%s%s [%ld]", window_str, title_str ? title_str : "zterm", n + 1) < 0) {
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
		errorf("Found %d active tabs, but %d terms alive.", active, terms.alive);
	}

	if (active <= 0) {
		debugf("Attempting to exit...");
		g_application_quit(G_APPLICATION(app));
	}

	if (pruned) {
		debugf("Pruned %d windows.", pruned);

		rebuild_menus();
	}
	debugf("");
}

static gboolean
term_died (VteTerminal *term, int status)
{
	int n = -1, window_i = -1;

	if (!term_find(GTK_WIDGET(term), &n)) {
		return false;
	}
	window_i = terms.active[n].window;

	if (WIFEXITED(status)) {
		infof("Term %d exited.  Exit code: %d", n, WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		infof("Term %d killed.  Signal: %d", n, WTERMSIG(status));
	} else {
		infof("Term %d died.  Status: %d", n, status);
	}

	// If the user hits the close button for the window, the terminal may be unrealized before term_died is called.
	if (n == -1 || window_i == -1) {
		debugf("Unable to find term that died. (%d / %p / %p), terms.active[0].term: %p", status, term, GTK_WIDGET(term), terms.active[0].term);
		prune_windows ();
		debugf("");
		return false;
	}

	debugf ("Removing dead term %d from window %d.", n, window_i);
	int i = gtk_notebook_page_num(windows[window_i].notebook, GTK_WIDGET(term));
	if (gtk_notebook_get_current_page(windows[window_i].notebook) == i) {
		gtk_notebook_prev_page (windows[window_i].notebook);
	}
	gtk_widget_set_visible(GTK_WIDGET(term), false);
	if (i != -1) {
		debugf("About to remove notebook page %d", i);
		gtk_notebook_remove_page(windows[window_i].notebook, i);
		debugf("");
	}

	prune_windows ();
	debugf("");

	return true;
}

static gboolean
term_unrealized (VteTerminal *term, gpointer user_data)
{
	int n = (long) user_data;

	debugf("Got unrealize for term %d.", n);
	if (terms.active[n].moving) {
		debugf("Not destroying term %d because we are moving it.", n);
		return false;
	}

	g_object_unref (G_OBJECT(term));

	terms.active[n].spawned--;
	terms.active[n].term = NULL;
	terms.alive--;

	if (!terms.alive) {
		debugf("Attempting to exit...");
		g_application_quit(G_APPLICATION(app));
	}

	debugf("");

	return true;
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
	gtk_widget_set_visible(term, true);
	gtk_widget_grab_focus(term);

	terms.active[n].moving--;

	prune_windows ();
	temu_reorder ();
}

void
term_config (GtkWidget *term, int window_i)
{
	static bool manage_fc_timestamp = false;

	if (terms.font) {
		/*
		 * This is all to workaround https://gitlab.gnome.org/GNOME/gtk/-/issues/7039
		 *
		 * The very short version: Check to see if gtk-fontconfig-timestamp has been set.
		 *
		 * If it has, we can pretend that gtk is going to do the right thing.
		 *
		 * It won't until the first bug described in the issue is fixed, but
		 * that's not too hard for the to workaround. (Make a change, then make
		 * another change.  The first change will then be picked up correctly.)
		 *
		 * If it has not been set, then we need to do it ourselves.
		 *
		 * Use a static variable to check if we are still managing it.
		 *
		 * NOTE: Sadly, just setting gtk-fontconfig-timestamp to different
		 * values in sequence isn't enough.
		 *
		 * FcConfigUptoDate needs to show as false during both increments.
		 *
		 * Sadly, we don't really have any way to trigger that.
		 *
		 * Without talking to the fontconfig layer of pango ourselves, we don't
		 * really have any good options here, except for the user to make a
		 * fontconfig change, hit reload config settings, make a second change,
		 * and then hit reload config settings again.
		 */
		int timestamp = 0;
		g_object_get(gtk_settings_get_default(), "gtk-fontconfig-timestamp", &timestamp, NULL);

		if (timestamp == 0 || manage_fc_timestamp) {
			manage_fc_timestamp = true;
			timestamp++;

			g_object_set(gtk_settings_get_default(), "gtk-fontconfig-timestamp", timestamp, NULL);
		}

		PangoFontDescription *font = pango_font_description_from_string(terms.font);
		if (font) {
			vte_terminal_set_font (VTE_TERMINAL (term), font);
			pango_font_description_free (font);
		} else {
			errorf("Unable to load font '%s'", terms.font);
		}
	}
	vte_terminal_set_word_char_exceptions (VTE_TERMINAL (term), terms.word_char_exceptions);
	vte_terminal_set_audible_bell (VTE_TERMINAL (term), terms.audible_bell);
	vte_terminal_set_font_scale (VTE_TERMINAL (term), terms.font_scale);
	vte_terminal_set_scroll_on_output (VTE_TERMINAL (term), terms.scroll_on_output);
	vte_terminal_set_scroll_on_keystroke (VTE_TERMINAL (term), terms.scroll_on_keystroke);
	vte_terminal_set_bold_is_bright (VTE_TERMINAL (term), terms.bold_is_bright);
	vte_terminal_set_cursor_blink_mode (VTE_TERMINAL (term), VTE_CURSOR_BLINK_OFF);
	vte_terminal_set_cursor_shape (VTE_TERMINAL (term), VTE_CURSOR_SHAPE_BLOCK);
	vte_terminal_set_scrollback_lines (VTE_TERMINAL (term), terms.scrollback_lines);
	vte_terminal_set_mouse_autohide (VTE_TERMINAL (term), terms.mouse_autohide);
	vte_terminal_set_enable_sixel (VTE_TERMINAL (term), true);
	vte_terminal_set_allow_hyperlink (VTE_TERMINAL (term), true);
#if VTE_CHECK_VERSION(0, 77, 0)
	vte_terminal_set_enable_legacy_osc777 (VTE_TERMINAL (term), true);
#endif

	// FIXME: Should this be in the config?
	VteRegex *regex;
	regex = vte_regex_new_for_match("([a-z]+://(\\w+(:\\w+)@)?[^\\s]*)", -1, PCRE2_NEVER_BACKSLASH_C | PCRE2_UTF | PCRE2_MULTILINE | PCRE2_CASELESS, NULL);
	if (regex) {
		int ret = vte_terminal_match_add_regex(VTE_TERMINAL (term), regex, 0);
		debugf("regex: %p, ret: %d", regex, ret);
	} else {
		debugf("regex failed to compile, we should add error handling.");
	}

	if (terms.color_schemes[windows[window_i].color_scheme].name[0]) {
		vte_terminal_set_colors (VTE_TERMINAL (term), &terms.color_schemes[windows[window_i].color_scheme].foreground, &terms.color_schemes[windows[window_i].color_scheme].background, &colors[0], MIN(256, sizeof (colors) / sizeof(colors[0])));
	} else {
		vte_terminal_set_colors (VTE_TERMINAL (term), NULL, NULL, &colors[0], MIN(256, sizeof (colors) / sizeof(colors[0])));
	}

	char_width = vte_terminal_get_char_width(VTE_TERMINAL (term));
	char_height = vte_terminal_get_char_height(VTE_TERMINAL (term));

	debugf("setting size request: %dx%d", char_width * 2, char_height * 2);
	gtk_widget_set_size_request (windows[window_i].window, char_width * 2, char_height * 2);
}

static void spawn_callback (VteTerminal *term, GPid pid, GError *error, gpointer user_data)
{
	debugf("term: %p, pid: %d, error: %p, user_data: %p", term, pid, error, user_data);
	if (error != NULL) {
		errorf("error: domain: 0x%x, code: 0x%x, message: %s", error->domain, error->code, error->message);
		term_died(term, -1); // This is a horrible hack.
	}
}

static gboolean
term_spawn (gpointer data)
{
	int n = (long int) data;
	int realized = gtk_widget_get_realized(terms.active[n].term);

	debugf("For terminal %d, spawned: %d, realized: %d, cmd: %s.", n, terms.active[n].spawned, realized, terms.active[n].cmd);

	if (!realized) {
		return true;
	}

	if (!terms.active[n].spawned) {
		char **env = environ;
		if (terms.active[n].env != NULL) {
			env = terms.active[n].env;
		}

		if (terms.active[n].cmd) {
			char *argv[] = {
				"/bin/sh",
				"-c",
				terms.active[n].cmd,
				NULL
			};
			debugf("Spawning with args: %s %s %s %s", argv[0], argv[1], argv[2], argv[3]);
			vte_terminal_spawn_async (VTE_TERMINAL (terms.active[n].term), VTE_PTY_DEFAULT, NULL, argv, env, G_SPAWN_DEFAULT, NULL, NULL, NULL, -1, NULL, spawn_callback, NULL);
		} else if (terms.active[n].argv != NULL && terms.active[n].argv[0] != NULL) {
			debugf("Spawning with: %p '%s'", terms.active[n].argv, terms.active[n].argv[0]);
			for (int i = 0; terms.active[n].argv[i] != NULL; i++) {
				debugf("argv[%d]: '%s'", i, terms.active[n].argv[i]);
			}
			vte_terminal_spawn_async (VTE_TERMINAL (terms.active[n].term), VTE_PTY_DEFAULT, NULL, terms.active[n].argv, env, G_SPAWN_DEFAULT, NULL, NULL, NULL, -1, NULL, spawn_callback, NULL);

		} else {
			struct passwd *pass = getpwuid(getuid());

			char *argv[] = {
				pass->pw_shell,
				"--login",
				NULL
			};
			debugf("term: %p, shell: '%s'", VTE_TERMINAL (terms.active[n].term), pass->pw_shell);
			debugf("Spawning with args: %s %s", argv[0], argv[1]);
			vte_terminal_spawn_async (VTE_TERMINAL (terms.active[n].term), VTE_PTY_DEFAULT, NULL, argv, env, G_SPAWN_DEFAULT, NULL, NULL, NULL, 5000, NULL, spawn_callback, NULL);
		}

		terms.active[n].spawned++;

		add_button(GTK_WIDGET(terms.active[n].term), n, -1);

		// Workaround a bug where the cursor may not be drawn when we first switch to a new terminal.
		vte_terminal_set_cursor_blink_mode (VTE_TERMINAL (terms.active[n].term), VTE_CURSOR_BLINK_ON);
		vte_terminal_set_cursor_blink_mode (VTE_TERMINAL (terms.active[n].term), VTE_CURSOR_BLINK_OFF);

		return false;
	}

	return true;
}

static void
term_show (GtkWidget *widget, void *data)
{
	int n = (long int) data;

	debugf("Show for terminal %d, spawned: %d, cmd: %s.", n, terms.active[n].spawned, terms.active[n].cmd);
}

static void
term_realized (GtkWidget *widget, void *data)
{
	int n = (long int) data;

	debugf("For terminal %d, spawned: %d, cmd: %s.", n, terms.active[n].spawned, terms.active[n].cmd);

	if (!terms.active[n].spawned) {
		g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, &term_spawn, data, NULL);
	}
}

static void
term_map (GtkWidget *widget, void *data)
{
	int n = (long int) data;

	debugf("For terminal %d, spawned: %d, cmd: %s.", n, terms.active[n].spawned, terms.active[n].cmd);
	// print_widget_size(GTK_WIDGET(widget), "term");
}


static void term_hover_uri_changed (VteTerminal *term, gchar *uri, GdkRectangle *bbox, gpointer user_data)
{
	int64_t n = (int64_t) user_data;

	debugf("uri: %s, n: %ld", uri, n);

	if (terms.active[n].hyperlink_uri != NULL) {
		free(terms.active[n].hyperlink_uri);
		terms.active[n].hyperlink_uri = NULL;
	}

	if (uri != NULL) {
		terms.active[n].hyperlink_uri = strdup(uri);
	}
}

static void term_resize_window (VteTerminal *term, guint width, guint height, gpointer user_data)
{
	int64_t n = (int64_t) user_data;

	debugf("target size: %dx%d, term %d", width, height, n);
}

static void term_increase_font_size (VteTerminal *term, gpointer user_data)
{
	int64_t n = (int64_t) user_data;

	debugf("term %d", n);
}

static void term_decrease_font_size (VteTerminal *term, gpointer user_data)
{
	int64_t n = (int64_t) user_data;

	debugf("term %d", n);
}

static gboolean term_setup_context_menu (VteTerminal *term, VteEventContext *context, gpointer user_data)
{
	int64_t n = (int64_t) user_data;
	window_t *window = &windows[terms.active[n].window];

	debugf("context: %p, term %d", context, n);
	if (window->menu_hyperlink_uri) {
		free(window->menu_hyperlink_uri);
		window->menu_hyperlink_uri = NULL;
	}

	if (!context) {
		vte_terminal_set_context_menu_model (term, NULL);
		window->menu_x = window->menu_y = 0;
		return true;
	}

	vte_event_context_get_coordinates(context, &window->menu_x, &window->menu_y);
	if (terms.active[n].hyperlink_uri) {
		window->menu_hyperlink_uri = strdup(terms.active[n].hyperlink_uri);
		debugf("Setting menu hyperlink uri: %s", window->menu_hyperlink_uri);
	}

	vte_terminal_set_context_menu_model (term, window->menu_model);

	debugf("Done.");

	return true;
}

#if VTE_CHECK_VERSION(0, 77, 0)
static void term_termprop_changed (VteTerminal *term, int id, VtePropertyType type, VtePropertyFlags flags, const char *name, int64_t n)
{
	const char *resolved_name;
	int prop = 0;
	gboolean bvalue;
	int64_t ivalue;
	uint64_t uivalue;
	double dvalue;
	GdkRGBA color_value;
	const char *svalue;
	const uint8_t *data_value;
	size_t value_len;

	if (vte_query_termprop(name, &resolved_name, &prop, &type, &flags)) {
		switch (type) {
			case VTE_PROPERTY_INVALID:
			case VTE_PROPERTY_VALUELESS:
				debugf("termprop: %s, type: VALUELESS %d, term: %d", name, type, n);
				break;
			case VTE_PROPERTY_BOOL:
				vte_terminal_get_termprop_bool_by_id(term, prop, &bvalue);
				debugf("termprop: %s, type: BOOL %d, value: %d, term: %d", name, type, bvalue, n);
				break;
			case VTE_PROPERTY_INT:
				vte_terminal_get_termprop_int_by_id(term, prop, &ivalue);
				debugf("termprop: %s, type: INT %d, value: %d, term: %d", name, type, ivalue, n);
				break;
			case VTE_PROPERTY_UINT:
				vte_terminal_get_termprop_uint_by_id(term, prop, &uivalue);
				debugf("termprop: %s, type: UINT %d, value: %d, term: %d", name, type, uivalue, n);
				break;
			case VTE_PROPERTY_DOUBLE:
				vte_terminal_get_termprop_double_by_id(term, prop, &dvalue);
				debugf("termprop: %s, type: DOUBLE %d, value: %d, term: %d", name, type, dvalue, n);
				break;
			case VTE_PROPERTY_RGB:
			case VTE_PROPERTY_RGBA:
				vte_terminal_get_termprop_rgba_by_id(term, prop, &color_value);
				debugf("termprop: %s, type: RGBA %d, value: _, term: %d", name, type, n);
				break;
			case VTE_PROPERTY_STRING:
				svalue = vte_terminal_get_termprop_string_by_id(term, prop, &value_len);
				debugf("termprop: %s, type: STRING %d, value: %s, len: %d, term: %d", name, type, svalue, value_len, n);
				break;
			case VTE_PROPERTY_DATA:
			case VTE_PROPERTY_UUID:
				data_value = vte_terminal_get_termprop_data_by_id(term, prop, &value_len);
				debugf("termprop: %s, type: DATA %d, value: %s, len: %d, term: %d", name, type, data_value, value_len, n);
				break;
			case VTE_PROPERTY_URI:
				GUri *uri_value = vte_terminal_ref_termprop_uri_by_id(term, prop);
				if (uri_value == NULL) {
					debugf("Unable to get URI.");
					break;
				}
				svalue = g_uri_to_string(uri_value);
				debugf("termprop: %s, type: URI %d, value: %s, term: %d", name, type, svalue, n);
				free((void *) svalue);
				g_uri_unref(uri_value);
				break;
		}
	} else {
		debugf("Unable to get termprop info for %s on term %d", name, n);
	}
}

static gboolean term_termprops_changed (VteTerminal *term, int const *props, int n_props, gpointer user_data)
{
	int64_t n = (int64_t) user_data;
	const char *name;
	VtePropertyType type;
	VtePropertyFlags flags;

	debugf("%d termprops changed on term %d.", n_props, n);
	for (int i = 0; i < n_props; i++) {
		if (vte_query_termprop_by_id(props[i], &name, &type, &flags)) {
			debugf("Prop %d / %s: type %d, flags %d", props[i], name, type, flags);
			term_termprop_changed (term, props[i], type, flags, name, n);
		}
	}

	return false;
}
#endif


void
term_switch (long n, char *cmd, char **argv, char **env, int window_i)
{
	if (n >= terms.n_active) {
		errorf("ERROR!  Attempting to switch to term %ld, while terms.n_active is %d.", n, terms.n_active);
		return;
	}

	if (!terms.active[n].term) {
		GtkWidget *term;

		term = vte_terminal_new();
		g_object_ref (G_OBJECT(term));
		gtk_widget_set_visible(term, true);
		gtk_widget_set_hexpand (term, true);
		gtk_widget_set_vexpand (term, true);


		g_signal_connect_after (G_OBJECT (term), "child-exited", G_CALLBACK (term_died), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "unrealize", G_CALLBACK (term_unrealized), (void *) n);
		g_signal_connect (G_OBJECT(term), "window_title_changed", G_CALLBACK(temu_window_title_changed), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "realize", G_CALLBACK (term_realized), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "show", G_CALLBACK (term_show), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "map", G_CALLBACK (term_map), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "hyperlink_hover_uri_changed", G_CALLBACK (term_hover_uri_changed), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "resize_window", G_CALLBACK (term_resize_window), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "increase_font_size", G_CALLBACK (term_increase_font_size), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "decrease_font_size", G_CALLBACK (term_decrease_font_size), (void *) n);
		g_signal_connect (G_OBJECT (term), "setup_context_menu", G_CALLBACK (term_setup_context_menu), (void *) n);
#if VTE_CHECK_VERSION(0, 77, 0)
		g_signal_connect (G_OBJECT (term), "termprops_changed", G_CALLBACK (term_termprops_changed), (void *) n);
#endif

		terms.active[n].cmd = cmd;
		terms.active[n].argv = argv;
		terms.active[n].env = env;
		terms.active[n].term = term;
		terms.alive++;

		term_set_window (n, window_i);

		term_config(term, window_i);

		gtk_window_present (GTK_WINDOW(windows[window_i].window));
	}

	if (window_i != terms.active[n].window) {
		window_i = terms.active[n].window;
		gtk_window_present (GTK_WINDOW(windows[window_i].window));
	}

	gtk_notebook_set_current_page (windows[window_i].notebook, gtk_notebook_page_num (windows[window_i].notebook, terms.active[n].term));
	temu_window_title_change (VTE_TERMINAL(terms.active[n].term), n);
	gtk_widget_grab_focus (GTK_WIDGET(terms.active[n].term));
}

/*
#undef FUNC_DEBUG
#define FUNC_DEBUG false
*/
static void
term_switch_page (GtkNotebook *notebook, GtkWidget *page, gint page_num, gpointer user_data)
{
	VteTerminal *term;
	int i;

	term = VTE_TERMINAL(gtk_notebook_get_nth_page (notebook, page_num));

	debugf("page_num: %d, current_page: %d, page: %p, term: %p, notebook: %p, user_data: %p",
			page_num, gtk_notebook_get_current_page (notebook), page, term, notebook, user_data);

	if (term_find(GTK_WIDGET(term), &i)) {
		long n = i;
		temu_window_title_changed (term, (void *) n);
		gtk_widget_grab_focus (GTK_WIDGET(term));
	}
}
/*
#undef FUNC_DEBUG
#define FUNC_DEBUG true
*/

static void show_menu(window_t *window, double x, double y)
{
	GdkRectangle rect = { .width = 1, .height = 1 };

	if (x == -1 && y == -1) {
		GdkDisplay *display = gdk_display_get_default();
		GdkSeat *seat = gdk_display_get_default_seat(display);
		GdkDevice *pointer = gdk_seat_get_pointer(seat);
		GdkModifierType mask;
		GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(GTK_WINDOW(window->window)));

		gdk_surface_get_device_position(GDK_SURFACE(surface), pointer, &x, &y, &mask);
	}

	rect.x = x;
	rect.y = y;

	if (window->menu_hyperlink_uri) {
		free(window->menu_hyperlink_uri);
		window->menu_hyperlink_uri = NULL;
	}

	GtkWidget *widget = gtk_notebook_get_nth_page(window->notebook, gtk_notebook_get_current_page(window->notebook));
	int n;
	term_find(widget, &n);

	window->menu_x = x;
	window->menu_y = y;
	if (terms.active[n].hyperlink_uri) {
		window->menu_hyperlink_uri = strdup(terms.active[n].hyperlink_uri);
		debugf("Setting menu hyperlink uri: %s", window->menu_hyperlink_uri);
	}

	gtk_popover_set_has_arrow (GTK_POPOVER(window->menu), true);
	gtk_widget_set_halign(window->menu, GTK_ALIGN_START);
	gtk_widget_set_valign(window->menu, GTK_ALIGN_START);
	gtk_popover_set_pointing_to(GTK_POPOVER(window->menu), &rect);
	gtk_popover_popup(GTK_POPOVER(window->menu));
}

static gboolean
term_key_event (GtkEventControllerKey *key_controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data)
{
	window_t *window = (window_t *) user_data;
	bind_t	*cur;
	GtkWidget *widget;
	guint keyval_lower = keyval;

	/*
	 * Key bindings that involve shift are a problem, because we may get the
	 * upper case keyval (C instead of c), and yet modern gtk_accelerator_parse
	 * will always convert the keyval to lowercase.
	 *
	 * We could do something outright absurd, like using gtk_accelerator_name
	 * followed by gtk_accelerator_parse to normalize the key press, but the
	 * performance impact of that makes it feel like a bad plan.
	 *
	 * So if it's an alpha character, make a lower case version, and check the
	 * binding against that as well.
	 *
	 * Note: isalpha's argument is an int, however it is documented that it
	 * must be either an unsigned int, or the constant EOF.
	 *
	 * Notably, it segfaults for some values above 0xFF on my system.
	 */
	if (keyval < 0xFF && isalpha(keyval)) {
		keyval_lower = tolower(keyval);
	}

#if 0
	gchar *name = gtk_accelerator_name(keyval, state);
	gchar *label = gtk_accelerator_get_label(keyval, state);
	debugf ("keyval: %d (%s), state: 0x%x, %d, name: '%s', label: '%s'", keyval, gdk_keyval_name(keyval), state, state, name, label);
	g_free(label);
	g_free(name);
#endif

	for (cur = terms.keys; cur; cur = cur->next) {
#if 0
		gchar *name = gtk_accelerator_name(cur->key_min, cur->state);
		debugf("key_min: %d (%s), key_max: %d (%s), state: 0x%x (%s)", cur->key_min, gdk_keyval_name(cur->key_min), cur->key_max, gdk_keyval_name(cur->key_max), cur->state, name);
		g_free(name);
#endif
		if (((keyval >= cur->key_min) && (keyval <= cur->key_max)) ||
			((keyval_lower >= cur->key_min) && (keyval_lower <= cur->key_max))) {
			if ((state & bind_mask) == cur->state) {
				switch (cur->action) {
					case BIND_ACT_SWITCH:
						term_switch (cur->base + (keyval - cur->key_min), cur->cmd, cur->argv, cur->env, window - &windows[0]);
						break;
					case BIND_ACT_CUT:
						debugf("Cut text");
						widget = gtk_notebook_get_nth_page(window->notebook, gtk_notebook_get_current_page(window->notebook));
						vte_terminal_copy_clipboard_format (VTE_TERMINAL(widget), VTE_FORMAT_TEXT);
						break;
					case BIND_ACT_CUT_HTML:
						debugf("Cut HTML");
						widget = gtk_notebook_get_nth_page(window->notebook, gtk_notebook_get_current_page(window->notebook));
						vte_terminal_copy_clipboard_format (VTE_TERMINAL(widget), VTE_FORMAT_HTML);
						break;
					case BIND_ACT_PASTE:
						debugf("Paste");
						widget = gtk_notebook_get_nth_page(window->notebook, gtk_notebook_get_current_page(window->notebook));
						vte_terminal_paste_clipboard (VTE_TERMINAL(widget));
						break;
					case BIND_ACT_MENU:
						show_menu(window, -1, -1);
						break;
					case BIND_ACT_NEXT_TERM:
						gtk_notebook_next_page(GTK_NOTEBOOK(window->notebook));
						break;
					case BIND_ACT_PREV_TERM:
						gtk_notebook_prev_page(GTK_NOTEBOOK(window->notebook));
						break;
					case BIND_ACT_OPEN_URI:
					case BIND_ACT_CUT_URI:
						int n;

						widget = gtk_notebook_get_nth_page(window->notebook, gtk_notebook_get_current_page(window->notebook));

						if (term_find(widget, &n)) {
							process_uri(n, window, cur->action, -1, -1, false);
							return true;
						}
						break;
					default:
						debugf("Fell into impossible key binding case.");
						return false;
				}
				//debugf("action: %d", cur->action);
				return true;
			}
		}
	}

	return false;
}

static gboolean term_button_event (GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data)
{
	long int n = (long int) user_data;

	window_t *window = &windows[terms.active[n].window];

	// debugf("Got button: %d, n: %ld", gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)), n);

	return button_event(gesture, sequence, n, window);
}

static
gboolean window_button_event (GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data)
{
	window_t *window = (window_t *) user_data;

	debugf("Got window button: %d", gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)));

	return button_event (gesture, sequence, -1, window);
}

static void file_launch_callback (GObject *source, GAsyncResult *result, gpointer data)
{
	GtkFileLauncher *launcher = (GtkFileLauncher *) data;
	GError *error = NULL;

	debugf();
	bool ret = gtk_file_launcher_launch_finish(launcher, result, &error);
	debugf("ret: %d, error: %p, result: %p", ret, error, result);
	if (error) {
		debugf("Error opening URI: domain: %x, code: %d, message: %s", error->domain, error->code, error->message);
		g_free(error->message);
		g_free(error);
	}

}

gboolean process_uri(int64_t term_n, window_t *window, bind_actions_t action, double x, double y, bool menu)
{
	GError *error = NULL;
	const char *uri;

	/*
	 * There are two different kinds of URIs for us to deal with.
	 *
	 * The first is stuff that's just printed in URI format.
	 *
	 * The second is URIs given via OSC codes, such as how `eza --hyperlink` works.
	 *
	 * And we get called in roughly three different contexts, key bindings, mouse bindings, and menus.
	 *
	 * For the first case, a text URI, we must use vte_terminal_check_match_at
	 * with the coordinates of the mouse pointer.
	 *
	 * For the key binding case, we'll need to find the current location of the
	 * pointer.
	 *
	 * For the mouse binding case, we'll be passed the location the click
	 * happened.
	 *
	 * For the menu case, we recorded the location of the pointer at the time
	 * that the menu was opened.
	 *
	 *
	 * For the second case, a URI given OSC codes, the only time we know about
	 * it is when we get a signal with the URI in it, which happens at
	 * mouseover time, or when it leaves, with no URI.
	 *
	 * In that case, both the key binding and mouse binding cases look at the
	 * terminal's hyperlink_uri variable, and the menu case looks at the
	 * window's menu_hyperlink_uri variable, which gets set from the terminal's
	 * hyperlink_uri variable at the time the window is opened.
	 *
	 * It would be bloody _great_ if printed URIs and URIs via OSC code would
	 * work roughly the same way, either with a signal with the URI on mouse
	 * over, or a function to check for a URI via OSC code with coordinates
	 * passed to it.
	 *
	 * But that's not what we actually have, ah well.
	 */
	if (menu) {
		// We can't rely on what the current state is, because we're in a menu.
		if (window->menu_hyperlink_uri) {
			uri = window->menu_hyperlink_uri;
			debugf("window menu hyperlink: %s", uri);
		} else {
			int tag = 0;
			uri = vte_terminal_check_match_at (VTE_TERMINAL (terms.active[term_n].term), x, y, &tag);
			debugf("menu coord link: %s", uri);
		}
	} else {
		if (terms.active[term_n].hyperlink_uri) {
			uri = terms.active[term_n].hyperlink_uri;
			debugf("Terminal hyperlink: %s", uri);
		} else {
			int tag = 0;

			// We likely won't have coordinates, so look at where the mouse pointer is right now.
			if (x == -1 && y == -1) {
				debugf("Trying to find the cursor.");
				GdkDisplay *display = gdk_display_get_default();
				GdkSeat *seat = gdk_display_get_default_seat(display);
				GdkDevice *pointer = gdk_seat_get_pointer(seat);
				GdkModifierType mask;
				GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(GTK_WINDOW(window->window)));

				gdk_surface_get_device_position(GDK_SURFACE(surface), pointer, &x, &y, &mask);
			}

			uri = vte_terminal_check_match_at (VTE_TERMINAL (terms.active[term_n].term), x, y, &tag);
			debugf("Match URI: %s, tag: %d, at %fx%f", uri, tag, x, y);
		}
	}

	debugf("URI: %s, at %fx%f", uri, x, y);

	if (!uri) {
		debugf("No URI found.");
		return false;
	}

	if (!g_uri_is_valid(uri, 0, &error)) {
		debugf("Received invalid URI: %s, error: domain: 0x%x, code: 0x%x, message: %s", uri, error->domain, error->code, error->message);
		g_error_free(error);
		return false;
	}


	switch (action) {
		case BIND_ACT_CUT_URI:
			GdkDisplay *display = gdk_display_get_default();
			GdkClipboard *clipboard = gdk_display_get_clipboard(display);
			gdk_clipboard_set_text(clipboard, uri);

			debugf("COPY URI: %s", uri);
			break;
		case BIND_ACT_OPEN_URI:
			GFile *file = g_file_new_for_uri(uri);

			if (!file) {
				debugf("uri: %s, file: %p", uri, file);
				return false;
			}

#if 0
			debugf("raw uri: %s", uri);
			debugf("path: %s", g_file_get_path(file));
			debugf("parse_name: %s", g_file_get_parse_name(file));
			debugf("uri: %s", g_file_get_uri(file));
			debugf("uri_scheme: %s", g_file_get_uri_scheme(file));
			debugf("basename: %s", g_file_get_basename(file));
#endif
			GtkFileLauncher *launcher = gtk_file_launcher_new(file);

			if (!launcher) {
				return false;
			}

			debugf("launcher: %p", launcher);
			debugf("window: %p, GTK_WINDOW(window): %p", window->window, GTK_WINDOW(window->window));
			gtk_file_launcher_launch(launcher, GTK_WINDOW(window->window), NULL, file_launch_callback, launcher);
			break;
		default:
			errorf("Bad action %d!", action);
			break;
	}

	return true;
}

static gboolean button_event (GtkGesture *gesture, GdkEventSequence *sequence, int64_t term_n, window_t *window)
{
	GdkEvent *event = gtk_gesture_get_last_event (gesture, sequence);

	double x, y;

	gtk_gesture_get_point(gesture, sequence, &x, &y);

	if (term_n >= 0) {
		GdkModifierType state = gdk_event_get_modifier_state(event);
		int button = gdk_button_event_get_button(event);

		for (bind_button_t *cur = terms.buttons; cur; cur = cur->next) {
			if (cur->button == button) {
				if ((state & bind_mask) == cur->state) {
					switch (cur->action) {
						case BIND_ACT_OPEN_URI:
						case BIND_ACT_CUT_URI:
							return process_uri(term_n, window, cur->action, x, y, false);
						default:
							debugf("Got impossible button binding action.");
							return false;
					}
				}
			}
		}
	}

	return false;
}

void add_button(GtkWidget *widget, long int term_n, int window_i)
{
	GtkGesture *gesture = gtk_gesture_click_new();
	if (term_n >= 0) {
		g_signal_connect(gesture, "begin", G_CALLBACK(term_button_event), (void *) term_n);
	} else {
		g_signal_connect(gesture, "begin", G_CALLBACK(window_button_event), (void *) &windows[window_i]);
	}
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE(gesture), 0);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(gesture));
}

#undef FUNC_DEBUG
#define FUNC_DEBUG false
// Attempt to ensure that our window size is correct for our font.
// This is definitely inferior to geometry hints, but geometry hints no longer
// exist for GTK4.
void window_compute_size (GdkToplevel *self, GdkToplevelSize *size, gpointer user_data)
{
	int bounds_width, bounds_height;
	int width, height;
	int target_width, target_height;
	int min_width, min_height;


#define EXTRA_WIDTH		2
#define EXTRA_HEIGHT	2

	gdk_toplevel_size_get_bounds(size, &bounds_width, &bounds_height);

	gtk_window_get_default_size (GTK_WINDOW(user_data), &width, &height);
	target_width = width;
	target_height = height;

	debugf("bounds: %dx%d, target: %dx%d, char: %dx%d", bounds_width, bounds_height, width, height, char_width, char_height);

	if (char_width != 0 && char_height != 0) {
		// The minimum window size is 4 characters in a square, plus the 'extra' width and height.
		// The extra width and extra height exist to make it easier to see stuff at the edge of the window.
		min_width = (char_width * 4) + EXTRA_WIDTH;
		min_height = (char_height * 4) + EXTRA_HEIGHT;

		//debugf("width /: %d, width %%: %d, height /: %d, height %%: %d", width / char_width, width % char_width, height / char_height, height % char_height);

		// These round down to the nearest character size.
		// The commented out versions round up instead.
		if ((target_width % char_width) != EXTRA_WIDTH) {
			//target_width += (char_width - (target_width % char_width)) + EXTRA_WIDTH;
			target_width -= (target_width % char_width) - EXTRA_WIDTH;
		}
		if ((target_height % char_height) != EXTRA_HEIGHT) {
			//target_height += (char_height - (target_height % char_height)) + EXTRA_HEIGHT;
			target_height -= (target_height % char_height) - EXTRA_HEIGHT;
		}

		// Set the minimum size.
		gdk_toplevel_size_set_min_size(size, min_width, min_height);

		// Ensure that the target size is within the minimum and maximum sizes.
		target_width = MAX(min_width, MIN(target_width, bounds_width));
		target_height = MAX(min_height, MIN(target_height, bounds_height));
		debugf("Compute size: current: %dx%d, target: %dx%d, char: %dx%d, bounds: %dx%d", width, height, target_width, target_height, char_width, char_height, bounds_width, bounds_height);
#if FUNC_DEBUG
		print_widget_size(GTK_WIDGET(user_data), "window");
#endif

		if (target_width != width || target_height != height) {

			// Set both the window 'default' size, and the toplevel size.
			// Without the default size being set, there are problems on x11
			// with the correct size not getting set, which causes some very
			// odd loops, resulting in monitor sized windows.
			gtk_window_set_default_size (GTK_WINDOW(user_data), target_width, target_height);
			gdk_toplevel_size_set_size(size, target_width, target_height);
		}
	}
}
#undef FUNC_DEBUG
#define FUNC_DEBUG true

int new_window (void)
{
	GtkWidget *window, *notebook;
	long int i;

	debugf("in new_window...");

	for (i = 0; i < MAX_WINDOWS; i++) {
		if (!windows[i].window) {
			break;
		}
	}

	if (i == MAX_WINDOWS) {
		errorf("ERROR: Unable to allocate new window.");
		return -1;
	}

	debugf("Building a new window...");

	window = gtk_application_window_new(app);

	debugf("setting default size: %dx%d", start_width, start_height);
	gtk_window_set_default_size (GTK_WINDOW(window), start_width, start_height);

	debugf("Setting size request: 24x24");
	gtk_widget_set_size_request (GTK_WIDGET(window), 24, 24);

	debugf("");
	notebook = gtk_notebook_new();
	debugf("");
	g_object_set (G_OBJECT (notebook), "scrollable", true, "enable-popup", true, NULL);
	gtk_notebook_popup_enable(GTK_NOTEBOOK(notebook));
	debugf("");
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK(notebook), false);
	debugf("");
	gtk_notebook_set_show_border (GTK_NOTEBOOK(notebook), false);
	debugf("");
	gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(notebook));
	gtk_widget_set_visible(GTK_WIDGET(notebook), true);

	windows[i].window = GTK_WIDGET(window);
	windows[i].notebook = GTK_NOTEBOOK(notebook);
	debugf("windows[%d].notebook: %p", i, windows[i].notebook);

	gtk_widget_set_can_focus(notebook, true);

	windows[i].key_controller = gtk_event_controller_key_new();
	gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(windows[i].key_controller));
	gtk_event_controller_set_propagation_phase(windows[i].key_controller, GTK_PHASE_CAPTURE);
	g_signal_connect (windows[i].key_controller, "key-pressed", G_CALLBACK (term_key_event), &windows[i]);

	add_button(GTK_WIDGET(windows[i].window), -1, i);
	add_button(GTK_WIDGET(windows[i].notebook), -1, i);

	g_signal_connect (notebook, "switch_page", G_CALLBACK (term_switch_page), GTK_NOTEBOOK(notebook));

	rebuild_menus();

	gtk_window_present (GTK_WINDOW(window));

	debugf("Window should be visible...");

	debugf("Old window present location...");

	print_widget_size(GTK_WIDGET(window), "window");


	GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(GTK_WINDOW(window)));
	GdkToplevel *toplevel = GDK_TOPLEVEL(surface);
	g_signal_connect (toplevel, "compute-size", G_CALLBACK (window_compute_size), window);
	debugf("compute-size attached to toplevel");

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
	exec_t *exec = (exec_t *) user_data;

	temu_parse_config ();
	if (!terms.n_active) {
		errorf("Unable to read config file, or no terminals defined.");
		exit (0);
	}
	terms.active = calloc(terms.n_active, sizeof(*terms.active));

	if (exec->cmd || exec->argv) {
		term_switch (0, exec->cmd, exec->argv, exec->env, 0);
	} else {
		for (cur = terms.keys; cur; cur = cur->next) {
			if (cur->base == 0) {
				break;
			}
		}

		if (cur != NULL) {
			term_switch (0, cur->cmd, cur->argv, cur->env, 0);
		} else {
			term_switch (0, NULL, NULL, NULL, 0);
		}

	}
}

int main(int argc, char *argv[], char *envp[])
{
	exec_t exec = { NULL };
	int i;
	char *shell;

	g_set_application_name("zterm");
	app = gtk_application_new ("org.aehallh.zterm", 0);
	g_application_set_application_id (G_APPLICATION(app), "com.aehallh.zterm");
	g_application_set_flags (G_APPLICATION(app), G_APPLICATION_NON_UNIQUE);

	memset (&terms, 0, sizeof (terms));
	terms.envp = envp;
	shell = vte_get_user_shell();
	debugf ("Using VTE: %s (%s)", vte_get_features(), shell);
	free(shell);
	terms.audible_bell = true;
	terms.font_scale = 1;
	terms.scroll_on_output = false;
	terms.scroll_on_keystroke = true;
	terms.rewrap_on_resize = true;
	terms.scrollback_lines = 512;
	terms.allow_bold = false;
	terms.bold_is_bright = true;
	terms.mouse_autohide = true;

	if (chdir(getenv("HOME")) != 0) {
		errorf("Unable to chdir to %s: %s", getenv("HOME"), strerror(errno));
	}

	if (argc > 1) {
		debugf("argc: %d", argc);
		exec.argv = calloc(argc + 1, sizeof (char *));
		for (int i = 0; i < argc; i++) {
			exec.argv[i] = argv[i + 1];
		}
		exec.env = envp;
	}

	g_signal_connect (app, "activate", G_CALLBACK (activate), &exec);

	g_application_run(G_APPLICATION (app), 0, NULL);

	debugf("Exiting, can free here. (%d)", terms.n_active);
	for (i = 0; i < terms.n_active; i++) {
		if (terms.active[i].term) {
			int page_num = gtk_notebook_page_num(windows[terms.active[i].window].notebook, GTK_WIDGET(terms.active[i].term));
			gtk_widget_set_visible(GTK_WIDGET(terms.active[i].term), false);
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

// vim: set ts=4 sw=4 noexpandtab :
