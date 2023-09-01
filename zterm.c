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

extern char **environ;
#ifdef HAVE_LIBBSD
#  include <bsd/string.h>
#endif

GdkRGBA colors[256] = {
#include "256colors.h"
};

int _vfprintf(FILE *io, const char *fmt, va_list args)
{
	char buf[32] = { 0 }; // With some extra space, because.
	int millisec;
	struct tm *tm_info;
	struct timeval tv;

	gettimeofday(&tv, NULL);

	millisec = lrint(tv.tv_usec/1000.0);
	if (millisec >= 1000) {
	    millisec -= 1000;
	    tv.tv_sec++;
	}

	tm_info = localtime(&tv.tv_sec);

	strftime(buf, sizeof(buf) - 1, "%Y:%m:%d %H:%M:%S", tm_info);
	fprintf(io, "%s.%03d: ", buf, millisec);

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
int _fnullf(FILE *io, const char *fmt, ...)
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

	debugf("%s realized: %d, scale factor: %d\n", name, gtk_widget_get_realized(widget), gtk_widget_get_scale_factor(widget));
	gtk_widget_get_preferred_size(widget, &minimum, &natural);
	debugf("%s preferred minimum width x height: %d x %d\n", name, minimum.width, minimum.height);
	debugf("%s preferred natural width x height: %d x %d\n", name, natural.width, natural.height);
	gtk_widget_get_size_request(widget, &x, &y);
	debugf("%s size request width x height: %d x %d\n", name, x, y);
	debugf("%s allocated width x height: %d x %d (%x x %x)\n", name, gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget), gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget));
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
		fprintf (stderr, "Found %d active tabs, but %d terms alive.\n", active, terms.alive);
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
term_died (VteTerminal *term, gpointer user_data)
{
	int status = 0, n = -1, window_i = -1;

	int pid = waitpid (-1, &status, 0);

	for (int i = 0; i < terms.n_active; i++) {
		if (terms.active[i].term == GTK_WIDGET(term)) {
			n = i;
			window_i = terms.active[n].window;
			break;
		}
	}

	debugf("Term %d died: %d, pid %d, errno %d, strerror %s", n, status, pid, errno, strerror(errno));

	// If the user hits the close button for the window, the terminal may be unrealized before term_died is called.
	if (n == -1 || window_i == -1) {
		debugf("Unable to find term that died. (%d / %p / %p), terms.active[0].term: %p", user_data, term, GTK_WIDGET(term), terms.active[0].term);
		prune_windows ();
		debugf("");
		return FALSE;
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
		debugf("Attempting to exit...");
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
	gtk_widget_set_visible(term, true);
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
			fprintf (stderr, "Unable to load font '%s'\n", terms.font);
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

	if (terms.color_schemes[windows[window_i].color_scheme].name[0]) {
		vte_terminal_set_colors (VTE_TERMINAL (term), &terms.color_schemes[windows[window_i].color_scheme].foreground, &terms.color_schemes[windows[window_i].color_scheme].background, &colors[0], MIN(256, sizeof (colors) / sizeof(colors[0])));
	} else {
		vte_terminal_set_colors (VTE_TERMINAL (term), NULL, NULL, &colors[0], MIN(256, sizeof (colors) / sizeof(colors[0])));
	}

	char_width = vte_terminal_get_char_width(VTE_TERMINAL (term));
	char_height = vte_terminal_get_char_height(VTE_TERMINAL (term));

	debugf("setting size request: %d x %d", char_width * 2, char_height * 2);
	gtk_widget_set_size_request (windows[window_i].window, char_width * 2, char_height * 2);
}

static void spawn_callback (VteTerminal *term, GPid pid, GError *error, gpointer user_data)
{
	debugf("term: %p, pid: %d, error: %p, user_data: %p", term, pid, error, user_data);
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
		if (terms.active[n].cmd) {
			char *argv[] = {
				"/bin/sh",
				"-c",
				terms.active[n].cmd,
				NULL
			};
			vte_terminal_spawn_async (VTE_TERMINAL (terms.active[n].term), VTE_PTY_DEFAULT, NULL, argv, environ, G_SPAWN_DEFAULT, NULL, NULL, NULL, -1, NULL, NULL, NULL);
		} else {
			struct passwd *pass = getpwuid(getuid());

			char *argv[] = {
				pass->pw_shell,
				"--login",
				NULL
			};
			debugf("term: %p, shell: '%s'", VTE_TERMINAL (terms.active[n].term), pass->pw_shell);
			vte_terminal_spawn_async (VTE_TERMINAL (terms.active[n].term), VTE_PTY_DEFAULT, NULL, argv, environ, G_SPAWN_DEFAULT, NULL, NULL, NULL, 5000, NULL, spawn_callback, NULL);
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


void
term_switch (long n, char *cmd, int window_i)
{
	if (n >= terms.n_active) {
		fprintf(stderr, "ERROR!  Attempting to switch to term %ld, while terms.n_active is %d.\n", n, terms.n_active);
		return;
	}

	if (!terms.active[n].term) {
		GtkWidget *term;

		term = vte_terminal_new();
		g_object_ref (G_OBJECT(term));
		gtk_widget_set_visible(term, true);
		gtk_widget_set_hexpand (term, TRUE);
		gtk_widget_set_vexpand (term, TRUE);


		g_signal_connect_after (G_OBJECT (term), "child-exited", G_CALLBACK (term_died), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "unrealize", G_CALLBACK (term_unrealized), (void *) n);
		g_signal_connect (G_OBJECT(term), "window_title_changed", G_CALLBACK(temu_window_title_changed), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "realize", G_CALLBACK (term_realized), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "show", G_CALLBACK (term_show), (void *) n);
		g_signal_connect_after (G_OBJECT (term), "map", G_CALLBACK (term_map), (void *) n);

		terms.active[n].cmd = cmd;
		terms.active[n].term = term;
		terms.alive++;

		term_set_window (n, window_i);

		term_config(term, window_i);

		gtk_window_present_with_time (GTK_WINDOW(windows[window_i].window), GDK_CURRENT_TIME);
	}

	if (window_i != terms.active[n].window) {
		window_i = terms.active[n].window;
		gtk_window_present (GTK_WINDOW(windows[window_i].window));
	}

	gtk_notebook_set_current_page (windows[window_i].notebook, gtk_notebook_page_num (windows[window_i].notebook, terms.active[n].term));
	temu_window_title_change (VTE_TERMINAL(terms.active[n].term), n);
	gtk_widget_grab_focus (GTK_WIDGET(terms.active[n].term));
}

#undef FUNC_DEBUG
#define FUNC_DEBUG false
static void
term_switch_page (GtkNotebook *notebook, GtkWidget *page, gint page_num, gpointer user_data)
{
	VteTerminal *term;
	long n, found = 0;

	term = VTE_TERMINAL(gtk_notebook_get_nth_page (notebook, page_num));

	debugf("page_num: %d, current_page: %d, page: %p, term: %p, notebook: %p, user_data: %p",
			page_num, gtk_notebook_get_current_page (notebook), page, term, notebook, user_data);

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
#undef FUNC_DEBUG
#define FUNC_DEBUG true

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

	gtk_popover_set_has_arrow (GTK_POPOVER(window->menu), TRUE);
	gtk_widget_set_halign(window->menu, GTK_ALIGN_START);
	gtk_widget_set_valign(window->menu, GTK_ALIGN_START);
	gtk_popover_set_pointing_to(GTK_POPOVER(window->menu), &rect);
	gtk_popover_popup(GTK_POPOVER(window->menu));
}

#undef FUNC_DEBUG
#define FUNC_DEBUG false
static gboolean
term_key_event (GtkEventControllerKey *key_controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data)
{
	window_t *window = (window_t *) user_data;
	bind_t	*cur;
	GtkWidget *widget;

	gchar *name = gtk_accelerator_name(keyval, state);
	debugf ("keyval: %d (%s), state: 0x%x, %d, '%s'", keyval, gdk_keyval_name(keyval), state, state, name);
	g_free(name);

	for (cur = terms.keys; cur; cur = cur->next) {
		//fprintf (stderr, "key_min: %d (%s), key_max: %d (%s), state: 0x%x (%s)\n", cur->key_min, gdk_keyval_name(cur->key_min), cur->key_max, gdk_keyval_name(cur->key_max), cur->state, gtk_accelerator_name(0, cur->state));
		if ((keyval >= cur->key_min) && (keyval <= cur->key_max)) {
			if ((state & bind_mask) == cur->state) {
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
						show_menu(window, -1, -1);
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
#undef FUNC_DEBUG
#define FUNC_DEBUG false

static gboolean term_button_event (GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data)
{
	long int n = (long int) user_data;

	window_t *window = &windows[terms.active[n].window];

	return window_button_event(gesture, sequence, window);
}

static
gboolean window_button_event (GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data)
{
	window_t *window = (window_t *) user_data;

	debugf("Got button: %d", gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)));

	GdkEvent *event = gtk_gesture_get_last_event (gesture, sequence);
	if (gdk_event_triggers_context_menu(event)) {
		double x, y;

		gtk_gesture_get_point(gesture, sequence, &x, &y);

		show_menu(window, x, y);
		return TRUE;
	}

	return FALSE;
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

		if (target_width != width || target_height != height) {
			//debugf("Compute size: current: %dx%d, target: %dx%d, char: %dx%d, bounds: %dx%d", width, height, target_width, target_height, char_width, char_height, bounds_width, bounds_height);

			// Set both the window 'default' size, and the toplevel size.
			// Without the default size being set, there are problems on x11
			// with the correct size not getting set, which causes some very
			// odd loops, resulting in monitor sized windows.
			gtk_window_set_default_size (GTK_WINDOW(user_data), target_width, target_height);
			gdk_toplevel_size_set_size(size, target_width, target_height);
		}
	}
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

	debugf("Building a new window...");

	window = gtk_application_window_new(app);

	debugf("setting default size: %d x %d", start_width, start_height);
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

	gtk_window_present_with_time (GTK_WINDOW(window), GDK_CURRENT_TIME);

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
	debugf ("Using VTE: %s (%s)", vte_get_features(), vte_get_user_shell());
	terms.audible_bell = TRUE;
	terms.font_scale = 1;
	terms.scroll_on_output = FALSE;
	terms.scroll_on_keystroke = TRUE;
	terms.rewrap_on_resize = TRUE;
	terms.scrollback_lines = 512;
	terms.allow_bold = FALSE;
	terms.bold_is_bright = TRUE;
	terms.mouse_autohide = TRUE;

	chdir(getenv("HOME"));

	g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);

	g_application_run(G_APPLICATION (app), argc, argv);

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
