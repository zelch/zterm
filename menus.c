#include "zterm.h"
#include <alloca.h>

void do_reload_config (GSimpleAction *self, GVariant *parameter, gpointer user_data)
{
	int old_n_active = terms.n_active;

	temu_parse_config ();
	if (!terms.n_active) {
		errorf ("Unable to read config file, or no terminals defined.");
		return;
	}

	// We actively don't want to worry about the number shrinking.  That just makes too much pain.
	// But we absolutely have to handle it growing.
	if (terms.n_active > old_n_active) {
		debugf ("old_n_active: %d, terms.n_active: %d", old_n_active, terms.n_active);
		terms.active = realloc (terms.active, terms.n_active * sizeof (*terms.active));
		memset (&terms.active[old_n_active], 0, (terms.n_active - old_n_active) * sizeof (*terms.active));
	}

	for (int i = 0; i < terms.n_active; i++) {
		if (terms.active[i].term) {
			term_config (terms.active[i].term, terms.active[i].window);
			// vte_terminal_set_geometry_hints_for_window(VTE_TERMINAL (terms.active[i].term), GTK_WINDOW
			// (windows[terms.active[i].window].window));
		}
	}

	rebuild_menus ();
}

void do_copy (GSimpleAction *self, GVariant *parameter, gpointer data)
{
	long int i = (long int) data;

	debugf ("parameter: %p, user_data: %p", parameter, data);

	GtkWidget *widget = gtk_notebook_get_nth_page (windows[i].notebook, gtk_notebook_get_current_page (windows[i].notebook));
	vte_terminal_copy_clipboard_format (VTE_TERMINAL (widget), VTE_FORMAT_TEXT);
}

void do_copy_uri (GSimpleAction *self, GVariant *parameter, gpointer data)
{
	long int window_i = (long int) data;
	int		 n;

	debugf ("parameter: %p, user_data: %p", parameter, data);

	window_t *window = &windows[window_i];

	GtkWidget *widget = gtk_notebook_get_nth_page (window->notebook, gtk_notebook_get_current_page (window->notebook));

	if (term_find (widget, &n)) {
		process_uri (n, window, BIND_ACT_CUT_URI, window->menu_x, window->menu_y, true);
	}
}

void do_open_uri (GSimpleAction *self, GVariant *parameter, gpointer data)
{
	long int window_i = (long int) data;
	int		 n;

	debugf ("parameter: %p, user_data: %p", parameter, data);

	window_t *window = &windows[window_i];

	GtkWidget *widget = gtk_notebook_get_nth_page (window->notebook, gtk_notebook_get_current_page (window->notebook));

	if (term_find (widget, &n)) {
		process_uri (n, window, BIND_ACT_OPEN_URI, window->menu_x, window->menu_y, true);
	}
}

void do_paste (GSimpleAction *self, GVariant *parameter, gpointer data)
{
	long int i = (long int) data;

	debugf ("parameter: %p, user_data: %p", parameter, data);

	GtkWidget *widget = gtk_notebook_get_nth_page (windows[i].notebook, gtk_notebook_get_current_page (windows[i].notebook));
	vte_terminal_paste_clipboard (VTE_TERMINAL (widget));
}

void do_t_decorate (GSimpleAction *self, GVariant *parameter, gpointer data)
{
	long int i = (long int) data;

	gboolean decorated = gtk_window_get_decorated (GTK_WINDOW (windows[i].window));
	debugf ("setting decorated: %d", !decorated);
	gtk_window_set_decorated (GTK_WINDOW (windows[i].window), !decorated);
}

void do_t_fullscreen (GSimpleAction *self, GVariant *parameter, gpointer data)
{
	long int i = (long int) data;

	debugf ("");

	if (gtk_window_is_fullscreen (GTK_WINDOW (windows[i].window))) {
		gtk_window_unfullscreen (GTK_WINDOW (windows[i].window));
	} else {
		gtk_window_fullscreen (GTK_WINDOW (windows[i].window));
	}
}

void do_t_tabbar (GSimpleAction *self, GVariant *parameter, gpointer data)
{
	long int i = (long int) data;

	debugf ("");

	gboolean show_tabs = gtk_notebook_get_show_tabs (GTK_NOTEBOOK (windows[i].notebook));
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (windows[i].notebook), !show_tabs);
}

void do_show_terms (GSimpleAction *self, GVariant *parameter, gpointer data)
{
	long int window_i = (long int) data;
	gboolean ret;

	ret = gtk_widget_activate_action_variant (GTK_WIDGET (windows[window_i].notebook), "menu.popup", NULL);
	debugf ("gtk_widget_activate_action_variant: %d", ret);
}

void do_next_term (GSimpleAction *self, GVariant *parameter, gpointer data)
{
	long int i = (long int) data;

	debugf ("");

	gtk_notebook_next_page (GTK_NOTEBOOK (windows[i].notebook));
}

void do_prev_term (GSimpleAction *self, GVariant *parameter, gpointer data)
{
	long int i = (long int) data;

	debugf ("");

	gtk_notebook_prev_page (GTK_NOTEBOOK (windows[i].notebook));
}

void do_move_to_window (GSimpleAction *self, GVariant *parameter, gpointer data)
{
	long int   window_i		= ((long int) data) & ((1 << 8) - 1);
	long int   new_window_i = (long int) data >> 8;
	int		   i;
	GtkWidget *widget;

	debugf ("window_i: %d, new_window_i: %d, data: 0x%x", window_i, new_window_i, data);

	widget = gtk_notebook_get_nth_page (windows[window_i].notebook, gtk_notebook_get_current_page (windows[window_i].notebook));

	if (term_find (widget, &i)) {
		term_set_window (i, new_window_i);
		term_switch (i, NULL, NULL, NULL, window_i);
	}
}

void do_set_window_color_scheme (GSimpleAction *self, GVariant *parameter, gpointer data)
{
	long int color_scheme = ((long int) data) >> 8;
	long int window_i	  = ((long int) data) & ((1 << 8) - 1);
	int		 i;

	debugf ("");

	windows[window_i].color_scheme = color_scheme;

	for (i = 0; i < terms.n_active; i++) {
		if (terms.active[i].term && terms.active[i].window == window_i) {
			vte_terminal_set_colors (VTE_TERMINAL (terms.active[i].term),
									 &terms.color_schemes[windows[window_i].color_scheme].foreground,
									 &terms.color_schemes[windows[window_i].color_scheme].background, &colors[0],
									 MIN (256, sizeof (colors) / sizeof (colors[0])));
		}
	}
}

/*
 * Ideally, we would free menu_hyperlink_uri here.
 *
 * Sadly, if someone opens the menu, and then selects something like 'Copy
 * URI', we get the closed signal before we get told that a menu item was
 * selected.
 *
 * As such, this is far less useful at the moment.
 */
static void menu_closed (GtkPopover *menu, gpointer user_data)
{
	long int i = (long int) user_data;

	debugf ("window %d", i);
}

// This is really strdupa.
// It really must be a macro, not a function, so that alloca gets called with
// the caller function's stack.
// And we're using the gcc statement expression syntax, because nothing else
// gives us the ability to have temporary variables, return values, and the
// ability to have a return value.
#define dupstr(str)                                                                                                              \
	({                                                                                                                           \
		char *in  = str;                                                                                                         \
		char *new = alloca (strlen (in) + 1);                                                                                    \
		char *ret = strcpy (new, in);                                                                                            \
		ret;                                                                                                                     \
	})

typedef struct {
	GActionEntry entry;
	gpointer	 user_data;
} ZActionEntry;

static void z_menu_append (GMenu *menu, ZActionEntry *actions, int *n_actions, char *prefix, char *label, char *_name,
						   void (*function) (GSimpleAction *, GVariant *, gpointer), long int _user_data)
{
	char buf[70] = {0};
	snprintf (buf, sizeof (buf) - 1, "%s%s", prefix, _name);
	g_menu_append (menu, label, buf);
	debugf ("label: %s, action: %s, name: %s", label, buf, _name);
	ZActionEntry tmp = {
		.entry = {_name, function},
			.user_data = (gpointer) _user_data
	  };
	actions[(*n_actions)++] = tmp;
	debugf ("action: %d, name: %s", *n_actions - 1, actions[*n_actions - 1].entry.name);
}

static void rebuild_window_menu (long int window_n)
{
	ZActionEntry add_actions[64];
	int			 n_add_actions = 0;

	debugf ("windows[%d].menu: %p", window_n, windows[window_n].menu);
	if (windows[window_n].menu != NULL) {
		gtk_widget_unparent (GTK_WIDGET (windows[window_n].menu));
		windows[window_n].menu = NULL;
	}

	GMenu *main = g_menu_new ();

	GMenu *actions = g_menu_new ();
	z_menu_append (actions, add_actions, &n_add_actions, "menu.", "_Copy", "copy", do_copy, window_n);
	z_menu_append (actions, add_actions, &n_add_actions, "menu.", "_Paste", "paste", do_paste, window_n);
	z_menu_append (actions, add_actions, &n_add_actions, "menu.", "Copy _URI", "copy_uri", do_copy_uri, window_n);
	z_menu_append (actions, add_actions, &n_add_actions, "menu.", "_Open URI", "open_uri", do_open_uri, window_n);
	g_menu_append_section (main, "Actions", G_MENU_MODEL (actions));

	GMenu *terminals = g_menu_new ();
	z_menu_append (terminals, add_actions, &n_add_actions, "menu.", "_Show Terminals", "show_terminals", do_show_terms, window_n);
	z_menu_append (terminals, add_actions, &n_add_actions, "menu.", "_Previous Terminal", "prev_terminal", do_prev_term,
				   window_n);
	z_menu_append (terminals, add_actions, &n_add_actions, "menu.", "_Next Terminal", "next_terminal", do_next_term, window_n);
	g_menu_append_section (main, "Terminals", G_MENU_MODEL (terminals));

	GMenu *config = g_menu_new ();
	z_menu_append (config, add_actions, &n_add_actions, "menu.", "_Decorations", "decorations", do_t_decorate, window_n);
	z_menu_append (config, add_actions, &n_add_actions, "menu.", "_Fullscreen", "fullscreen", do_t_fullscreen, window_n);
	z_menu_append (config, add_actions, &n_add_actions, "menu.", "_Tab bar", "tab_bar", do_t_tabbar, window_n);
	z_menu_append (config, add_actions, &n_add_actions, "menu.", "_Reload config file", "reload_config", do_reload_config,
				   window_n);
	g_menu_append_section (main, "Config", G_MENU_MODEL (config));

	/*
	z_menu_append(config, add_actions, &n_add_actions, "menu.", "_", "", do_, window_n);
	z_menu_append(config, add_actions, &n_add_actions, "menu.", "_", "", do_, window_n);
	z_menu_append(config, add_actions, &n_add_actions, "menu.", "_", "", do_, window_n);
	*/

	GMenu *schemes = g_menu_new ();
	for (long int j = 0; j < MAX_COLOR_SCHEMES && terms.color_schemes[j].name[0]; j++) {
		debugf ("name: %s, action: %s", terms.color_schemes[j].name, terms.color_schemes[j].action);
		z_menu_append (schemes, add_actions, &n_add_actions, "menu.", terms.color_schemes[j].name, terms.color_schemes[j].action,
					   do_set_window_color_scheme, ((j << 8) + window_n));
	}
	g_menu_append_section (main, "Color schemes", G_MENU_MODEL (schemes));

	GMenu *window = g_menu_new ();

	long first_empty = -1;
	for (long n = 0; n < MAX_WINDOWS; n++) {
		if (n == window_n) { // Don't offer to move to the same window, that's just weird.
			continue;
		} else if (windows[n].window) {
			char action[64] = {0};
			char title[64]	= {0};

			snprintf (title, sizeof (title), "Move to window _%ld", n);
			snprintf (action, sizeof (action), "window.move_%ld", n);
			char *_title  = dupstr (title);
			char *_action = dupstr (action);
			z_menu_append (window, add_actions, &n_add_actions, "menu.", _title, _action, do_move_to_window,
						   ((n << 8) + window_n));
		} else if (first_empty == -1) {
			first_empty = n;
		}
	}

	if (first_empty != -1) {
		char action[64] = {0};
		char title[64]	= {0};

		snprintf (title, sizeof (title), "Move to _new window %ld", first_empty);
		snprintf (action, sizeof (action), "window.move_%ld", first_empty);

		char *_title  = dupstr (title);
		char *_action = dupstr (action);
		z_menu_append (window, add_actions, &n_add_actions, "menu.", _title, _action, do_move_to_window,
					   ((first_empty << 8) + window_n));
	} else {
		debugf ("first_empty: %d", first_empty);
		for (long n = 0; n < MAX_WINDOWS; n++) {
			debugf ("windows[%d].window: %p", n, windows[n].window);
		}
	}

	g_menu_append_section (main, "Window", G_MENU_MODEL (window));

	windows[window_n].menu_model = G_MENU_MODEL (main);
	GtkWidget *menu				 = gtk_popover_menu_new_from_model (G_MENU_MODEL (main));
	gtk_popover_set_autohide (GTK_POPOVER (menu), TRUE);
	gtk_popover_set_has_arrow (GTK_POPOVER (menu), FALSE);
	gtk_popover_set_position (GTK_POPOVER (menu), GTK_POS_BOTTOM);
	gtk_widget_set_halign (menu, GTK_ALIGN_START);
	gtk_widget_set_valign (menu, GTK_ALIGN_END);

	GSimpleActionGroup *group = g_simple_action_group_new ();

	for (int i = 0; i < n_add_actions; i++) {
		g_action_map_add_action_entries (G_ACTION_MAP (group), &add_actions[i].entry, 1, add_actions[i].user_data);
		debugf ("action: %d, name: %s", i, add_actions[i].entry.name);
	}
	gtk_widget_insert_action_group (windows[window_n].window, "menu", G_ACTION_GROUP (group));

	windows[window_n].menu = menu;
	gtk_widget_set_parent (menu, windows[window_n].window);
	debugf ("windows[%d].menu: %p", window_n, windows[window_n].menu);
	g_signal_connect_after (menu, "closed", G_CALLBACK (menu_closed), (void *) window_n);

	return;
}

void rebuild_menus (void)
{
	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (windows[i].window) {
			rebuild_window_menu (i);
		}
	}
}

// vim: set ts=4 sw=4 noexpandtab :
