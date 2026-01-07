#include "zterm.h"

/* Preferences Dialog Implementation */

typedef struct {
	GtkWidget *dialog;
	GtkWidget *font_entry;
	GtkWidget *font_button;
	GtkWidget *font_scale_spin;
	GtkWidget *word_char_entry;
	GtkWidget *size_width_spin;
	GtkWidget *size_height_spin;
	GtkWidget *scrollback_spin;
	GtkWidget *audible_bell_check;
	GtkWidget *scroll_on_output_check;
	GtkWidget *scroll_on_keystroke_check;
	GtkWidget *bold_is_bright_check;
	GtkWidget *mouse_autohide_check;
	long int   window_n;

	/* Original values for revert */
	char  *original_font;
	char  *original_word_char_exceptions;
	double original_font_scale;
	int	   original_start_width;
	int	   original_start_height;
	int	   original_scrollback_lines;
	bool   original_audible_bell;
	bool   original_scroll_on_output;
	bool   original_scroll_on_keystroke;
	bool   original_bold_is_bright;
	bool   original_mouse_autohide;
} PrefsDialog;

static void apply_preferences (PrefsDialog *prefs)
{
	/* Get font */
	const char *font = gtk_editable_get_text (GTK_EDITABLE (prefs->font_entry));
	if (font && strlen (font) > 0) {
		if (terms.font)
			free (terms.font);
		terms.font = strdup (font);
	}

	/* Get font scale */
	terms.font_scale = gtk_spin_button_get_value (GTK_SPIN_BUTTON (prefs->font_scale_spin));

	/* Get word char exceptions */
	const char *word_chars = gtk_editable_get_text (GTK_EDITABLE (prefs->word_char_entry));
	if (terms.word_char_exceptions)
		free (terms.word_char_exceptions);
	terms.word_char_exceptions = strdup (word_chars ? word_chars : "");

	/* Get window size */
	start_width	 = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (prefs->size_width_spin));
	start_height = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (prefs->size_height_spin));

	/* Get scrollback lines */
	terms.scrollback_lines = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (prefs->scrollback_spin));

	/* Get boolean settings */
	terms.audible_bell		  = gtk_check_button_get_active (GTK_CHECK_BUTTON (prefs->audible_bell_check));
	terms.scroll_on_output	  = gtk_check_button_get_active (GTK_CHECK_BUTTON (prefs->scroll_on_output_check));
	terms.scroll_on_keystroke = gtk_check_button_get_active (GTK_CHECK_BUTTON (prefs->scroll_on_keystroke_check));
	terms.bold_is_bright	  = gtk_check_button_get_active (GTK_CHECK_BUTTON (prefs->bold_is_bright_check));
	terms.mouse_autohide	  = gtk_check_button_get_active (GTK_CHECK_BUTTON (prefs->mouse_autohide_check));

	/* Apply settings to all terminals */
	for (int i = 0; i < terms.n_active; i++) {
		if (terms.active[i].term) {
			term_config (terms.active[i].term, terms.active[i].window);
		}
	}

	/* Save configuration */
	zterm_save_config ();
}

/* Apply settings without saving (for preview) */
static void preview_preferences (PrefsDialog *prefs)
{
	/* Get font */
	const char *font = gtk_editable_get_text (GTK_EDITABLE (prefs->font_entry));
	if (font && strlen (font) > 0) {
		if (terms.font)
			free (terms.font);
		terms.font = strdup (font);
	}

	/* Get font scale */
	terms.font_scale = gtk_spin_button_get_value (GTK_SPIN_BUTTON (prefs->font_scale_spin));

	/* Get word char exceptions */
	const char *word_chars = gtk_editable_get_text (GTK_EDITABLE (prefs->word_char_entry));
	if (terms.word_char_exceptions)
		free (terms.word_char_exceptions);
	terms.word_char_exceptions = strdup (word_chars ? word_chars : "");

	/* Get window size (doesn't affect existing windows, only new ones) */
	start_width	 = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (prefs->size_width_spin));
	start_height = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (prefs->size_height_spin));

	/* Get scrollback lines */
	terms.scrollback_lines = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (prefs->scrollback_spin));

	/* Get boolean settings */
	terms.audible_bell		  = gtk_check_button_get_active (GTK_CHECK_BUTTON (prefs->audible_bell_check));
	terms.scroll_on_output	  = gtk_check_button_get_active (GTK_CHECK_BUTTON (prefs->scroll_on_output_check));
	terms.scroll_on_keystroke = gtk_check_button_get_active (GTK_CHECK_BUTTON (prefs->scroll_on_keystroke_check));
	terms.bold_is_bright	  = gtk_check_button_get_active (GTK_CHECK_BUTTON (prefs->bold_is_bright_check));
	terms.mouse_autohide	  = gtk_check_button_get_active (GTK_CHECK_BUTTON (prefs->mouse_autohide_check));

	/* Apply settings to all terminals */
	for (int i = 0; i < terms.n_active; i++) {
		if (terms.active[i].term) {
			term_config (terms.active[i].term, terms.active[i].window);
		}
	}
}

/* Revert to original settings */
static void revert_preferences (PrefsDialog *prefs)
{
	/* Restore original values to the terms structure */
	if (terms.font)
		free (terms.font);
	terms.font = prefs->original_font ? strdup (prefs->original_font) : NULL;

	if (terms.word_char_exceptions)
		free (terms.word_char_exceptions);
	terms.word_char_exceptions = prefs->original_word_char_exceptions ? strdup (prefs->original_word_char_exceptions) : NULL;

	terms.font_scale		  = prefs->original_font_scale;
	start_width				  = prefs->original_start_width;
	start_height			  = prefs->original_start_height;
	terms.scrollback_lines	  = prefs->original_scrollback_lines;
	terms.audible_bell		  = prefs->original_audible_bell;
	terms.scroll_on_output	  = prefs->original_scroll_on_output;
	terms.scroll_on_keystroke = prefs->original_scroll_on_keystroke;
	terms.bold_is_bright	  = prefs->original_bold_is_bright;
	terms.mouse_autohide	  = prefs->original_mouse_autohide;

	/* Update dialog widgets to show original values */
	gtk_editable_set_text (GTK_EDITABLE (prefs->font_entry), prefs->original_font ? prefs->original_font : "");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (prefs->font_scale_spin), prefs->original_font_scale);
	gtk_editable_set_text (GTK_EDITABLE (prefs->word_char_entry),
						   prefs->original_word_char_exceptions ? prefs->original_word_char_exceptions : "");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (prefs->size_width_spin), prefs->original_start_width);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (prefs->size_height_spin), prefs->original_start_height);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (prefs->scrollback_spin), prefs->original_scrollback_lines);
	gtk_check_button_set_active (GTK_CHECK_BUTTON (prefs->audible_bell_check), prefs->original_audible_bell);
	gtk_check_button_set_active (GTK_CHECK_BUTTON (prefs->scroll_on_output_check), prefs->original_scroll_on_output);
	gtk_check_button_set_active (GTK_CHECK_BUTTON (prefs->scroll_on_keystroke_check), prefs->original_scroll_on_keystroke);
	gtk_check_button_set_active (GTK_CHECK_BUTTON (prefs->bold_is_bright_check), prefs->original_bold_is_bright);
	gtk_check_button_set_active (GTK_CHECK_BUTTON (prefs->mouse_autohide_check), prefs->original_mouse_autohide);

	/* Apply reverted settings to all terminals */
	for (int i = 0; i < terms.n_active; i++) {
		if (terms.active[i].term) {
			term_config (terms.active[i].term, terms.active[i].window);
		}
	}
}

static void free_prefs_dialog (PrefsDialog *prefs)
{
	if (prefs->original_font) {
		free (prefs->original_font);
		prefs->original_font = NULL;
	}
	if (prefs->original_word_char_exceptions) {
		free (prefs->original_word_char_exceptions);
		prefs->original_word_char_exceptions = NULL;
	}
	free (prefs);
}

static void prefs_cancel_clicked (PrefsDialog *prefs)
{
	/* Revert to original settings before closing */
	revert_preferences (prefs);
	gtk_window_destroy (GTK_WINDOW (prefs->dialog));
	free_prefs_dialog (prefs);
}

static void prefs_preview_clicked (PrefsDialog *prefs)
{
	preview_preferences (prefs);
}

static void prefs_revert_clicked (PrefsDialog *prefs)
{
	revert_preferences (prefs);
}

static void prefs_apply_clicked (PrefsDialog *prefs)
{
	apply_preferences (prefs);
	/* Update original values after applying so revert goes back to last applied state */
	if (prefs->original_font)
		free (prefs->original_font);
	prefs->original_font = terms.font ? strdup (terms.font) : NULL;

	if (prefs->original_word_char_exceptions)
		free (prefs->original_word_char_exceptions);
	prefs->original_word_char_exceptions = terms.word_char_exceptions ? strdup (terms.word_char_exceptions) : NULL;

	prefs->original_font_scale			= terms.font_scale;
	prefs->original_start_width			= start_width;
	prefs->original_start_height		= start_height;
	prefs->original_scrollback_lines	= terms.scrollback_lines;
	prefs->original_audible_bell		= terms.audible_bell;
	prefs->original_scroll_on_output	= terms.scroll_on_output;
	prefs->original_scroll_on_keystroke = terms.scroll_on_keystroke;
	prefs->original_bold_is_bright		= terms.bold_is_bright;
	prefs->original_mouse_autohide		= terms.mouse_autohide;
}

static void prefs_ok_clicked (PrefsDialog *prefs)
{
	apply_preferences (prefs);
	gtk_window_destroy (GTK_WINDOW (prefs->dialog));
	free_prefs_dialog (prefs);
}

static GtkWidget *create_label (const char *text)
{
	GtkWidget *label = gtk_label_new (text);
	gtk_widget_set_halign (label, GTK_ALIGN_END);
	gtk_widget_set_margin_end (label, 6);
	return label;
}

static void font_dialog_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
	PrefsDialog			 *prefs		= (PrefsDialog *) user_data;
	GtkFontDialog		 *dialog	= GTK_FONT_DIALOG (source);
	PangoFontDescription *font_desc = gtk_font_dialog_choose_font_finish (dialog, result, NULL);

	if (font_desc) {
		char *font_str = pango_font_description_to_string (font_desc);
		gtk_editable_set_text (GTK_EDITABLE (prefs->font_entry), font_str);
		g_free (font_str);
		pango_font_description_free (font_desc);
	}
}

static void font_button_clicked (GtkButton *button, gpointer user_data)
{
	PrefsDialog	  *prefs  = (PrefsDialog *) user_data;
	GtkFontDialog *dialog = gtk_font_dialog_new ();

	gtk_font_dialog_set_modal (dialog, TRUE);
	gtk_font_dialog_set_title (dialog, "Choose Font");

	/* Set initial font from entry */
	const char			 *current_font = gtk_editable_get_text (GTK_EDITABLE (prefs->font_entry));
	PangoFontDescription *initial	   = NULL;
	if (current_font && strlen (current_font) > 0) {
		initial = pango_font_description_from_string (current_font);
	}

	gtk_font_dialog_choose_font (dialog, GTK_WINDOW (prefs->dialog), initial, NULL, font_dialog_cb, prefs);

	if (initial) {
		pango_font_description_free (initial);
	}
}

/* Color Scheme Editor */

typedef struct {
	GtkWidget *dialog;
	GtkWidget *name_entry;
	GtkWidget *fg_button;
	GtkWidget *bg_button;
	int		   scheme_index;
	long int   parent_window; /* Parent window index */
	GdkRGBA	   original_fg;	  /* For revert functionality */
	GdkRGBA	   original_bg;
	char	   original_name[32];
	bool	   is_new_scheme;
} ColorSchemeEditDialog;

typedef struct {
	GtkWidget *dialog;
	GtkWidget *list_box;
	long int   window_n;
} ColorSchemeListDialog;

static void refresh_color_scheme_list (ColorSchemeListDialog *list_dialog);

/* Apply colors to all terminals in a window for preview */
static void apply_color_scheme_to_window (long int window_n, const GdkRGBA *fg, const GdkRGBA *bg)
{
	for (int i = 0; i < terms.n_active; i++) {
		if (terms.active[i].term && terms.active[i].window == window_n) {
			vte_terminal_set_colors (VTE_TERMINAL (terms.active[i].term), fg, bg, &colors[0],
									 MIN (256, sizeof (colors) / sizeof (colors[0])));
		}
	}
}

/* Revert to original colors */
static void color_scheme_edit_revert (ColorSchemeEditDialog *edit)
{
	/* Reset button colors to original */
	gtk_color_dialog_button_set_rgba (GTK_COLOR_DIALOG_BUTTON (edit->fg_button), &edit->original_fg);
	gtk_color_dialog_button_set_rgba (GTK_COLOR_DIALOG_BUTTON (edit->bg_button), &edit->original_bg);
	gtk_editable_set_text (GTK_EDITABLE (edit->name_entry), edit->original_name);

	/* Revert terminal colors */
	apply_color_scheme_to_window (edit->parent_window, &edit->original_fg, &edit->original_bg);
}

/* Preview current colors on terminal */
static void color_scheme_edit_preview (ColorSchemeEditDialog *edit)
{
	const GdkRGBA *fg = gtk_color_dialog_button_get_rgba (GTK_COLOR_DIALOG_BUTTON (edit->fg_button));
	const GdkRGBA *bg = gtk_color_dialog_button_get_rgba (GTK_COLOR_DIALOG_BUTTON (edit->bg_button));

	apply_color_scheme_to_window (edit->parent_window, fg, bg);
}

static void color_scheme_edit_cancel (ColorSchemeEditDialog *edit)
{
	/* Revert to original colors before closing */
	if (!edit->is_new_scheme) {
		apply_color_scheme_to_window (edit->parent_window, &edit->original_fg, &edit->original_bg);
	} else {
		/* For new schemes, revert to the window's current color scheme */
		int scheme_idx = windows[edit->parent_window].color_scheme;
		if (terms.color_schemes[scheme_idx].name[0]) {
			apply_color_scheme_to_window (edit->parent_window, &terms.color_schemes[scheme_idx].foreground,
										  &terms.color_schemes[scheme_idx].background);
		}
	}

	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	free (edit);
}

static void color_scheme_edit_ok (ColorSchemeEditDialog *edit)
{
	const char *name = gtk_editable_get_text (GTK_EDITABLE (edit->name_entry));

	if (name && strlen (name) > 0) {
		int idx = edit->scheme_index;

		/* Get colors from buttons */
		const GdkRGBA *fg = gtk_color_dialog_button_get_rgba (GTK_COLOR_DIALOG_BUTTON (edit->fg_button));
		const GdkRGBA *bg = gtk_color_dialog_button_get_rgba (GTK_COLOR_DIALOG_BUTTON (edit->bg_button));

		/* Update or add the color scheme */
		strlcpy (terms.color_schemes[idx].name, name, sizeof (terms.color_schemes[idx].name));
		snprintf (terms.color_schemes[idx].action, sizeof (terms.color_schemes[idx].action), "color_scheme.%d", idx);
		terms.color_schemes[idx].foreground = *fg;
		terms.color_schemes[idx].background = *bg;

		/* Save and rebuild menus */
		zterm_save_config ();
		debugf ("Calling rebuild_menus");
		rebuild_menus ();
	}

	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	free (edit);
}

static void show_color_scheme_edit_dialog (int scheme_index, long int parent_window)
{
	ColorSchemeEditDialog *edit = g_new0 (ColorSchemeEditDialog, 1);
	edit->scheme_index			= scheme_index;
	edit->parent_window			= parent_window;

	/* Initialize colors and store originals for revert */
	GdkRGBA foreground, background;
	if (terms.color_schemes[scheme_index].name[0]) {
		foreground = terms.color_schemes[scheme_index].foreground;
		background = terms.color_schemes[scheme_index].background;
		strlcpy (edit->original_name, terms.color_schemes[scheme_index].name, sizeof (edit->original_name));
		edit->is_new_scheme = false;
	} else {
		/* Default colors for new scheme */
		gdk_rgba_parse (&foreground, "#ffffff");
		gdk_rgba_parse (&background, "#000000");
		edit->original_name[0] = '\0';
		edit->is_new_scheme	   = true;
	}
	edit->original_fg = foreground;
	edit->original_bg = background;

	/* Create dialog window */
	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), scheme_index < MAX_COLOR_SCHEMES && terms.color_schemes[scheme_index].name[0]
												 ? "Edit Color Scheme"
												 : "New Color Scheme");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (windows[parent_window].window));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	edit->dialog = dialog;

	GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (main_box, 12);
	gtk_widget_set_margin_end (main_box, 12);
	gtk_widget_set_margin_top (main_box, 12);
	gtk_widget_set_margin_bottom (main_box, 12);
	gtk_window_set_child (GTK_WINDOW (dialog), main_box);

	GtkWidget *grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_box_append (GTK_BOX (main_box), grid);

	int row = 0;

	/* Name */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Name:"), 0, row, 1, 1);
	edit->name_entry = gtk_entry_new ();
	gtk_editable_set_text (GTK_EDITABLE (edit->name_entry),
						   terms.color_schemes[scheme_index].name[0] ? terms.color_schemes[scheme_index].name : "");
	gtk_widget_set_hexpand (edit->name_entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), edit->name_entry, 1, row++, 1, 1);

	/* Foreground color */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Foreground:"), 0, row, 1, 1);
	GtkColorDialog *fg_color_dialog = gtk_color_dialog_new ();
	edit->fg_button					= gtk_color_dialog_button_new (fg_color_dialog);
	gtk_color_dialog_button_set_rgba (GTK_COLOR_DIALOG_BUTTON (edit->fg_button), &foreground);
	gtk_grid_attach (GTK_GRID (grid), edit->fg_button, 1, row++, 1, 1);

	/* Background color */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Background:"), 0, row, 1, 1);
	GtkColorDialog *bg_color_dialog = gtk_color_dialog_new ();
	edit->bg_button					= gtk_color_dialog_button_new (bg_color_dialog);
	gtk_color_dialog_button_set_rgba (GTK_COLOR_DIALOG_BUTTON (edit->bg_button), &background);
	gtk_grid_attach (GTK_GRID (grid), edit->bg_button, 1, row++, 1, 1);

	/* Buttons */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *preview_btn = gtk_button_new_with_mnemonic ("_Preview");
	GtkWidget *revert_btn  = gtk_button_new_with_mnemonic ("_Revert");
	GtkWidget *cancel_btn  = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *ok_btn	   = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), preview_btn);
	gtk_box_append (GTK_BOX (button_box), revert_btn);
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	g_signal_connect_swapped (preview_btn, "clicked", G_CALLBACK (color_scheme_edit_preview), edit);
	g_signal_connect_swapped (revert_btn, "clicked", G_CALLBACK (color_scheme_edit_revert), edit);
	g_signal_connect_swapped (cancel_btn, "clicked", G_CALLBACK (color_scheme_edit_cancel), edit);
	g_signal_connect_swapped (ok_btn, "clicked", G_CALLBACK (color_scheme_edit_ok), edit);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 400, -1);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void color_scheme_add_clicked (GtkButton *button, gpointer user_data)
{
	ColorSchemeListDialog *list_dialog = (ColorSchemeListDialog *) user_data;

	/* Find first empty slot */
	int idx = -1;
	for (int i = 0; i < MAX_COLOR_SCHEMES; i++) {
		if (!terms.color_schemes[i].name[0]) {
			idx = i;
			break;
		}
	}

	if (idx == -1) {
		/* No room for more schemes */
		return;
	}

	show_color_scheme_edit_dialog (idx, list_dialog->window_n);
}

static void color_scheme_edit_clicked (GtkButton *button, gpointer user_data)
{
	int					   scheme_index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "scheme_index"));
	ColorSchemeListDialog *list_dialog	= (ColorSchemeListDialog *) user_data;

	show_color_scheme_edit_dialog (scheme_index, list_dialog->window_n);
}

static void color_scheme_delete_clicked (GtkButton *button, gpointer user_data)
{
	int					   scheme_index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "scheme_index"));
	ColorSchemeListDialog *list_dialog	= (ColorSchemeListDialog *) user_data;

	/* Clear the scheme */
	memset (&terms.color_schemes[scheme_index], 0, sizeof (color_scheme_t));

	/* Compact the array */
	for (int i = scheme_index; i < MAX_COLOR_SCHEMES - 1; i++) {
		terms.color_schemes[i] = terms.color_schemes[i + 1];
		if (terms.color_schemes[i].name[0]) {
			snprintf (terms.color_schemes[i].action, sizeof (terms.color_schemes[i].action), "color_scheme.%d", i);
		}
	}
	memset (&terms.color_schemes[MAX_COLOR_SCHEMES - 1], 0, sizeof (color_scheme_t));

	zterm_save_config ();
	debugf ("Calling rebuild_menus");
	rebuild_menus ();
	refresh_color_scheme_list (list_dialog);
}

static void refresh_color_scheme_list (ColorSchemeListDialog *list_dialog)
{
	/* Clear existing children */
	GtkWidget *child = gtk_widget_get_first_child (list_dialog->list_box);
	while (child) {
		GtkWidget *next = gtk_widget_get_next_sibling (child);
		gtk_list_box_remove (GTK_LIST_BOX (list_dialog->list_box), child);
		child = next;
	}

	/* Add rows for each color scheme */
	for (int i = 0; i < MAX_COLOR_SCHEMES && terms.color_schemes[i].name[0]; i++) {
		GtkWidget *row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_widget_set_margin_start (row_box, 6);
		gtk_widget_set_margin_end (row_box, 6);
		gtk_widget_set_margin_top (row_box, 3);
		gtk_widget_set_margin_bottom (row_box, 3);

		/* Color preview */
		GtkWidget *preview = gtk_drawing_area_new ();
		gtk_widget_set_size_request (preview, 60, 24);
		gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (preview),
										(GtkDrawingAreaDrawFunc) (void (*) (void)) gtk_widget_get_first_child, NULL, NULL);

		/* Create a simple colored box using CSS */
		char css_name[64];
		snprintf (css_name, sizeof (css_name), "colorpreview%d", i);
		gtk_widget_set_name (preview, css_name);

		char			css_str[256];
		GtkCssProvider *provider = gtk_css_provider_new ();
		snprintf (css_str, sizeof (css_str), "#%s { background-color: %s; border: 1px solid %s; }", css_name,
				  gdk_rgba_to_string (&terms.color_schemes[i].background),
				  gdk_rgba_to_string (&terms.color_schemes[i].foreground));
		gtk_css_provider_load_from_string (provider, css_str);
		gtk_style_context_add_provider_for_display (gdk_display_get_default (), GTK_STYLE_PROVIDER (provider),
													GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref (provider);

		gtk_box_append (GTK_BOX (row_box), preview);

		/* Name label */
		GtkWidget *name_label = gtk_label_new (terms.color_schemes[i].name);
		gtk_widget_set_hexpand (name_label, TRUE);
		gtk_widget_set_halign (name_label, GTK_ALIGN_START);
		gtk_box_append (GTK_BOX (row_box), name_label);

		/* Edit button */
		GtkWidget *edit_btn = gtk_button_new_from_icon_name ("document-edit-symbolic");
		gtk_widget_set_tooltip_text (edit_btn, "Edit");
		g_object_set_data (G_OBJECT (edit_btn), "scheme_index", GINT_TO_POINTER (i));
		g_signal_connect (edit_btn, "clicked", G_CALLBACK (color_scheme_edit_clicked), list_dialog);
		gtk_box_append (GTK_BOX (row_box), edit_btn);

		/* Delete button */
		GtkWidget *delete_btn = gtk_button_new_from_icon_name ("edit-delete-symbolic");
		gtk_widget_set_tooltip_text (delete_btn, "Delete");
		g_object_set_data (G_OBJECT (delete_btn), "scheme_index", GINT_TO_POINTER (i));
		g_signal_connect (delete_btn, "clicked", G_CALLBACK (color_scheme_delete_clicked), list_dialog);
		gtk_box_append (GTK_BOX (row_box), delete_btn);

		gtk_list_box_append (GTK_LIST_BOX (list_dialog->list_box), row_box);
	}
}

static void color_scheme_list_close (ColorSchemeListDialog *list_dialog)
{
	gtk_window_destroy (GTK_WINDOW (list_dialog->dialog));
	free (list_dialog);
}

static void show_color_scheme_editor (GtkButton *button, gpointer user_data)
{
	PrefsDialog			  *prefs	   = (PrefsDialog *) user_data;
	ColorSchemeListDialog *list_dialog = g_new0 (ColorSchemeListDialog, 1);
	list_dialog->window_n			   = prefs->window_n;

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), "Color Schemes");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (prefs->dialog));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	list_dialog->dialog = dialog;

	GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (main_box, 12);
	gtk_widget_set_margin_end (main_box, 12);
	gtk_widget_set_margin_top (main_box, 12);
	gtk_widget_set_margin_bottom (main_box, 12);
	gtk_window_set_child (GTK_WINDOW (dialog), main_box);

	/* Scrolled window for list */
	GtkWidget *scrolled = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand (scrolled, TRUE);
	gtk_widget_set_size_request (scrolled, 350, 200);
	gtk_box_append (GTK_BOX (main_box), scrolled);

	list_dialog->list_box = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_dialog->list_box), GTK_SELECTION_NONE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list_dialog->list_box);

	refresh_color_scheme_list (list_dialog);

	/* Buttons */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *add_btn	 = gtk_button_new_with_mnemonic ("_Add");
	GtkWidget *close_btn = gtk_button_new_with_mnemonic ("_Close");
	gtk_box_append (GTK_BOX (button_box), add_btn);
	gtk_box_append (GTK_BOX (button_box), close_btn);

	g_signal_connect (add_btn, "clicked", G_CALLBACK (color_scheme_add_clicked), list_dialog);
	g_signal_connect_swapped (close_btn, "clicked", G_CALLBACK (color_scheme_list_close), list_dialog);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 500, 300);
	gtk_window_present (GTK_WINDOW (dialog));
}

/* ==================== Color Overrides Editor ==================== */

typedef struct {
	GtkWidget *dialog;
	GtkWidget *index_spin;
	GtkWidget *color_button;
	int		   override_index; /* -1 for new */
	long int   parent_window;
} ColorOverrideEditDialog;

typedef struct {
	GtkWidget *dialog;
	GtkWidget *list_box;
	long int   window_n;
} ColorOverrideListDialog;

static void refresh_color_override_list (ColorOverrideListDialog *list_dialog);

static void color_override_edit_cancel (ColorOverrideEditDialog *edit)
{
	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	free (edit);
}

static void color_override_edit_ok (ColorOverrideEditDialog *edit)
{
	int			   index = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (edit->index_spin));
	const GdkRGBA *color = gtk_color_dialog_button_get_rgba (GTK_COLOR_DIALOG_BUTTON (edit->color_button));

	if (index >= 0 && index < 256) {
		/* Update the color */
		colors[index] = *color;

		/* Track the override */
		color_override_t *found = NULL;
		for (color_override_t *cur = terms.color_overrides; cur; cur = cur->next) {
			if (cur->index == index) {
				found = cur;
				break;
			}
		}

		if (found) {
			found->color = *color;
		} else {
			color_override_t *override = calloc (1, sizeof (color_override_t));
			override->index			   = index;
			override->color			   = *color;
			override->next			   = terms.color_overrides;
			terms.color_overrides	   = override;
		}

		/* Apply to all terminals */
		for (int i = 0; i < terms.n_active; i++) {
			if (terms.active[i].term) {
				term_config (terms.active[i].term, terms.active[i].window);
			}
		}

		zterm_save_config ();
	}

	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	free (edit);
}

static void show_color_override_edit_dialog (int override_index, GdkRGBA *initial_color, long int parent_window,
											 ColorOverrideListDialog *list_dialog)
{
	ColorOverrideEditDialog *edit = g_new0 (ColorOverrideEditDialog, 1);
	edit->override_index		  = override_index;
	edit->parent_window			  = parent_window;

	GdkRGBA color;
	if (initial_color) {
		color = *initial_color;
	} else {
		color = colors[override_index >= 0 ? override_index : 0];
	}

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), override_index >= 0 ? "Edit Color Override" : "New Color Override");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (windows[parent_window].window));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	edit->dialog = dialog;

	GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (main_box, 12);
	gtk_widget_set_margin_end (main_box, 12);
	gtk_widget_set_margin_top (main_box, 12);
	gtk_widget_set_margin_bottom (main_box, 12);
	gtk_window_set_child (GTK_WINDOW (dialog), main_box);

	GtkWidget *grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_box_append (GTK_BOX (main_box), grid);

	int row = 0;

	/* Color Index */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Color Index (0-255):"), 0, row, 1, 1);
	edit->index_spin = gtk_spin_button_new_with_range (0, 255, 1);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (edit->index_spin), override_index >= 0 ? override_index : 0);
	gtk_grid_attach (GTK_GRID (grid), edit->index_spin, 1, row++, 1, 1);

	/* Color */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Color:"), 0, row, 1, 1);
	GtkColorDialog *color_dialog = gtk_color_dialog_new ();
	edit->color_button			 = gtk_color_dialog_button_new (color_dialog);
	gtk_color_dialog_button_set_rgba (GTK_COLOR_DIALOG_BUTTON (edit->color_button), &color);
	gtk_grid_attach (GTK_GRID (grid), edit->color_button, 1, row++, 1, 1);

	/* Buttons */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	g_signal_connect_swapped (cancel_btn, "clicked", G_CALLBACK (color_override_edit_cancel), edit);
	g_signal_connect_swapped (ok_btn, "clicked", G_CALLBACK (color_override_edit_ok), edit);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 400, -1);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void color_override_add_clicked (GtkButton *button, gpointer user_data)
{
	ColorOverrideListDialog *list_dialog = (ColorOverrideListDialog *) user_data;
	show_color_override_edit_dialog (-1, NULL, list_dialog->window_n, list_dialog);
}

static void color_override_edit_clicked (GtkButton *button, gpointer user_data)
{
	int						 override_index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "override_index"));
	ColorOverrideListDialog *list_dialog	= (ColorOverrideListDialog *) user_data;

	for (color_override_t *cur = terms.color_overrides; cur; cur = cur->next) {
		if (cur->index == override_index) {
			show_color_override_edit_dialog (override_index, &cur->color, list_dialog->window_n, list_dialog);
			return;
		}
	}
}

static void color_override_delete_clicked (GtkButton *button, gpointer user_data)
{
	int						 override_index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "override_index"));
	ColorOverrideListDialog *list_dialog	= (ColorOverrideListDialog *) user_data;

	/* Remove from linked list */
	color_override_t **prev = &terms.color_overrides;
	for (color_override_t *cur = terms.color_overrides; cur; cur = cur->next) {
		if (cur->index == override_index) {
			*prev = cur->next;
			free (cur);
			break;
		}
		prev = &cur->next;
	}

	zterm_save_config ();
	refresh_color_override_list (list_dialog);
}

static void refresh_color_override_list (ColorOverrideListDialog *list_dialog)
{
	/* Clear existing children */
	GtkWidget *child = gtk_widget_get_first_child (list_dialog->list_box);
	while (child) {
		GtkWidget *next = gtk_widget_get_next_sibling (child);
		gtk_list_box_remove (GTK_LIST_BOX (list_dialog->list_box), child);
		child = next;
	}

	/* Add rows for each color override */
	for (color_override_t *cur = terms.color_overrides; cur; cur = cur->next) {
		GtkWidget *row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_widget_set_margin_start (row_box, 6);
		gtk_widget_set_margin_end (row_box, 6);
		gtk_widget_set_margin_top (row_box, 3);
		gtk_widget_set_margin_bottom (row_box, 3);

		/* Index label */
		char index_str[16];
		snprintf (index_str, sizeof (index_str), "Color %d:", cur->index);
		GtkWidget *index_label = gtk_label_new (index_str);
		gtk_widget_set_size_request (index_label, 80, -1);
		gtk_widget_set_halign (index_label, GTK_ALIGN_START);
		gtk_box_append (GTK_BOX (row_box), index_label);

		/* Color preview using CSS */
		GtkWidget *preview = gtk_drawing_area_new ();
		gtk_widget_set_size_request (preview, 60, 24);

		char css_name[64];
		snprintf (css_name, sizeof (css_name), "coloroverride%d", cur->index);
		gtk_widget_set_name (preview, css_name);

		char			css_str[256];
		GtkCssProvider *provider = gtk_css_provider_new ();
		snprintf (css_str, sizeof (css_str), "#%s { background-color: %s; border: 1px solid #888; }", css_name,
				  gdk_rgba_to_string (&cur->color));
		gtk_css_provider_load_from_string (provider, css_str);
		gtk_style_context_add_provider_for_display (gdk_display_get_default (), GTK_STYLE_PROVIDER (provider),
													GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref (provider);

		gtk_widget_set_hexpand (preview, TRUE);
		gtk_box_append (GTK_BOX (row_box), preview);

		/* Edit button */
		GtkWidget *edit_btn = gtk_button_new_from_icon_name ("document-edit-symbolic");
		gtk_widget_set_tooltip_text (edit_btn, "Edit");
		g_object_set_data (G_OBJECT (edit_btn), "override_index", GINT_TO_POINTER (cur->index));
		g_signal_connect (edit_btn, "clicked", G_CALLBACK (color_override_edit_clicked), list_dialog);
		gtk_box_append (GTK_BOX (row_box), edit_btn);

		/* Delete button */
		GtkWidget *delete_btn = gtk_button_new_from_icon_name ("edit-delete-symbolic");
		gtk_widget_set_tooltip_text (delete_btn, "Delete");
		g_object_set_data (G_OBJECT (delete_btn), "override_index", GINT_TO_POINTER (cur->index));
		g_signal_connect (delete_btn, "clicked", G_CALLBACK (color_override_delete_clicked), list_dialog);
		gtk_box_append (GTK_BOX (row_box), delete_btn);

		gtk_list_box_append (GTK_LIST_BOX (list_dialog->list_box), row_box);
	}
}

static void color_override_list_close (ColorOverrideListDialog *list_dialog)
{
	gtk_window_destroy (GTK_WINDOW (list_dialog->dialog));
	free (list_dialog);
}

static void show_color_override_editor (GtkButton *button, gpointer user_data)
{
	PrefsDialog				*prefs		 = (PrefsDialog *) user_data;
	ColorOverrideListDialog *list_dialog = g_new0 (ColorOverrideListDialog, 1);
	list_dialog->window_n				 = prefs->window_n;

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), "Color Overrides");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (prefs->dialog));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	list_dialog->dialog = dialog;

	GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (main_box, 12);
	gtk_widget_set_margin_end (main_box, 12);
	gtk_widget_set_margin_top (main_box, 12);
	gtk_widget_set_margin_bottom (main_box, 12);
	gtk_window_set_child (GTK_WINDOW (dialog), main_box);

	/* Info label */
	GtkWidget *info_label = gtk_label_new ("Override colors in the 256-color palette (0-255)");
	gtk_box_append (GTK_BOX (main_box), info_label);

	/* Scrolled window for list */
	GtkWidget *scrolled = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand (scrolled, TRUE);
	gtk_widget_set_size_request (scrolled, 350, 200);
	gtk_box_append (GTK_BOX (main_box), scrolled);

	list_dialog->list_box = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_dialog->list_box), GTK_SELECTION_NONE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list_dialog->list_box);

	refresh_color_override_list (list_dialog);

	/* Buttons */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *add_btn	 = gtk_button_new_with_mnemonic ("_Add");
	GtkWidget *close_btn = gtk_button_new_with_mnemonic ("_Close");
	gtk_box_append (GTK_BOX (button_box), add_btn);
	gtk_box_append (GTK_BOX (button_box), close_btn);

	g_signal_connect (add_btn, "clicked", G_CALLBACK (color_override_add_clicked), list_dialog);
	g_signal_connect_swapped (close_btn, "clicked", G_CALLBACK (color_override_list_close), list_dialog);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 500, 350);
	gtk_window_present (GTK_WINDOW (dialog));
}

/* ==================== Key/Button Capture Helper ==================== */

typedef struct {
	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *state_entry; /* Entry to update with captured state */
	GtkWidget *key_entry;	/* Entry to update with captured key (NULL for button capture) */
	GtkWidget *button_spin; /* Spin button to update with captured button (NULL for key capture) */
	guint	   captured_key;
	guint	   captured_state;
	guint	   captured_button;
	bool	   capture_key;			/* true for key, false for button */
	bool	   waiting_for_release; /* For button capture, wait for release */
} CaptureDialog;

static void capture_dialog_close (CaptureDialog *capture)
{
	gtk_window_destroy (GTK_WINDOW (capture->dialog));
	free (capture);
}

static void capture_dialog_ok (CaptureDialog *capture)
{
	/* Update the entries with captured values */
	gchar *state_str = gtk_accelerator_name (0, capture->captured_state);
	gtk_editable_set_text (GTK_EDITABLE (capture->state_entry), state_str);
	g_free (state_str);

	if (capture->capture_key && capture->key_entry && capture->captured_key) {
		const gchar *key_name = gdk_keyval_name (capture->captured_key);
		if (key_name) {
			gtk_editable_set_text (GTK_EDITABLE (capture->key_entry), key_name);
		}
	} else if (!capture->capture_key && capture->button_spin && capture->captured_button) {
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (capture->button_spin), capture->captured_button);
	}

	capture_dialog_close (capture);
}

static void update_capture_label (CaptureDialog *capture)
{
	char label_text[256];

	if (capture->capture_key) {
		if (capture->captured_key) {
			gchar *accel = gtk_accelerator_name (capture->captured_key, capture->captured_state);
			snprintf (label_text, sizeof (label_text), "Captured: %s\n\nPress new key combination or click OK", accel);
			g_free (accel);
		} else if (capture->captured_state) {
			gchar *state_str = gtk_accelerator_name (0, capture->captured_state);
			snprintf (label_text, sizeof (label_text), "Modifiers: %s\n\nPress a key to complete", state_str);
			g_free (state_str);
		} else {
			snprintf (label_text, sizeof (label_text), "Press the key combination you want to bind...");
		}
	} else {
		if (capture->captured_button) {
			gchar *state_str = gtk_accelerator_name (0, capture->captured_state);
			snprintf (label_text, sizeof (label_text), "Captured: %sButton%d\n\nClick new button or click OK", state_str,
					  capture->captured_button);
			g_free (state_str);
		} else if (capture->captured_state) {
			gchar *state_str = gtk_accelerator_name (0, capture->captured_state);
			snprintf (label_text, sizeof (label_text), "Modifiers: %s\n\nClick a mouse button to complete", state_str);
			g_free (state_str);
		} else {
			snprintf (label_text, sizeof (label_text),
					  "Click the mouse button (with modifiers) you want to bind...\n\n"
					  "Hold modifier keys and click a mouse button");
		}
	}

	gtk_label_set_text (GTK_LABEL (capture->label), label_text);
}

static gboolean capture_key_pressed (GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state,
									 gpointer user_data)
{
	CaptureDialog *capture = (CaptureDialog *) user_data;

	/* Ignore lone modifier keys - just update state display */
	if (keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R || keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R ||
		keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R || keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R ||
		keyval == GDK_KEY_Meta_L || keyval == GDK_KEY_Meta_R || keyval == GDK_KEY_Hyper_L || keyval == GDK_KEY_Hyper_R) {
		capture->captured_state = state & key_bind_mask;
		update_capture_label (capture);
		return TRUE;
	}

	/* Escape cancels */
	if (keyval == GDK_KEY_Escape && capture->captured_state == 0) {
		capture_dialog_close (capture);
		return TRUE;
	}

	capture->captured_key	= keyval;
	capture->captured_state = state & key_bind_mask;
	update_capture_label (capture);

	return TRUE;
}

static void capture_button_pressed (GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
	CaptureDialog  *capture = (CaptureDialog *) user_data;
	GdkEvent	   *event	= gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (gesture));
	GdkModifierType state	= gdk_event_get_modifier_state (event);
	int				button	= gdk_button_event_get_button (event);

	/* Button 1 with no modifiers on OK/Cancel buttons should work normally */
	/* Check if click is on one of the dialog buttons */
	if (button == 1 && (state & button_bind_mask) == 0) {
		/* Let the click through to buttons if we already have a capture */
		if (capture->captured_button != 0) {
			return; /* Don't claim, let button handle it */
		}
	}

	capture->captured_button = button;
	capture->captured_state	 = state & button_bind_mask;
	update_capture_label (capture);

	gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void show_capture_dialog (GtkWidget *state_entry, GtkWidget *key_entry, GtkWidget *button_spin, bool capture_key,
								 long int parent_window)
{
	CaptureDialog *capture = g_new0 (CaptureDialog, 1);
	capture->state_entry   = state_entry;
	capture->key_entry	   = key_entry;
	capture->button_spin   = button_spin;
	capture->capture_key   = capture_key;

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), capture_key ? "Capture Key" : "Capture Mouse Button");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (windows[parent_window].window));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	capture->dialog = dialog;

	GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (main_box, 24);
	gtk_widget_set_margin_end (main_box, 24);
	gtk_widget_set_margin_top (main_box, 24);
	gtk_widget_set_margin_bottom (main_box, 24);
	gtk_window_set_child (GTK_WINDOW (dialog), main_box);

	/* Instruction/status label */
	capture->label = gtk_label_new ("");
	gtk_label_set_justify (GTK_LABEL (capture->label), GTK_JUSTIFY_CENTER);
	gtk_widget_set_vexpand (capture->label, TRUE);
	gtk_box_append (GTK_BOX (main_box), capture->label);
	update_capture_label (capture);

	/* Buttons */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	g_signal_connect_swapped (cancel_btn, "clicked", G_CALLBACK (capture_dialog_close), capture);
	g_signal_connect_swapped (ok_btn, "clicked", G_CALLBACK (capture_dialog_ok), capture);

	/* Key controller for the whole window */
	if (capture_key) {
		GtkEventController *key_controller = gtk_event_controller_key_new ();
		gtk_widget_add_controller (dialog, key_controller);
		gtk_event_controller_set_propagation_phase (key_controller, GTK_PHASE_CAPTURE);
		g_signal_connect (key_controller, "key-pressed", G_CALLBACK (capture_key_pressed), capture);
	}

	/* Button gesture for mouse capture */
	if (!capture_key) {
		GtkGesture *click_gesture = gtk_gesture_click_new ();
		gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click_gesture), 0); /* Listen to all buttons */
		gtk_widget_add_controller (dialog, GTK_EVENT_CONTROLLER (click_gesture));
		gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (click_gesture), GTK_PHASE_CAPTURE);
		g_signal_connect (click_gesture, "pressed", G_CALLBACK (capture_button_pressed), capture);
	}

	gtk_window_set_default_size (GTK_WINDOW (dialog), 400, 180);
	gtk_window_present (GTK_WINDOW (dialog));

	/* Focus the window to receive key events */
	gtk_widget_grab_focus (dialog);
}

/* ==================== Key Bindings Editor ==================== */

static const char *bind_action_names[] = {"SWITCH",	   "CUT",		"CUT_HTML", "PASTE",   "MENU",
										  "NEXT_TERM", "PREV_TERM", "OPEN_URI", "CUT_URI", NULL};

typedef struct {
	GtkWidget				   *dialog;
	GtkWidget				   *action_combo;
	GtkWidget				   *state_entry;
	GtkWidget				   *key_entry;
	GtkWidget				   *base_spin;
	GtkWidget				   *key_max_entry;
	GtkWidget				   *base_label;
	GtkWidget				   *key_max_label;
	GtkWidget				   *cmd_label;
	GtkWidget				   *cmd_entry;
	GtkWidget				   *env_label;
	GtkWidget				   *env_entry;
	bind_t					   *editing_bind; /* NULL for new */
	long int					parent_window;
	struct KeyBindListDialog_s *list_dialog; /* For refreshing list after edit */
} KeyBindEditDialog;

typedef struct KeyBindListDialog_s {
	GtkWidget  *dialog;
	GtkWidget  *column_view;
	GListStore *store;
	long int	window_n;
} KeyBindListDialog;

static void refresh_key_bind_list (KeyBindListDialog *list_dialog);

/* GObject wrapper for bind_t to use with GListStore */
#define KEY_BIND_ITEM_TYPE (key_bind_item_get_type ())
G_DECLARE_FINAL_TYPE (KeyBindItem, key_bind_item, KEY, BIND_ITEM, GObject)

struct _KeyBindItem {
	GObject parent_instance;
	bind_t *bind;
};

G_DEFINE_TYPE (KeyBindItem, key_bind_item, G_TYPE_OBJECT)

static void key_bind_item_class_init (KeyBindItemClass *klass)
{
}

static void key_bind_item_init (KeyBindItem *self)
{
}

static KeyBindItem *key_bind_item_new (bind_t *bind)
{
	KeyBindItem *item = g_object_new (KEY_BIND_ITEM_TYPE, NULL);
	item->bind		  = bind;
	return item;
}

static void key_bind_edit_cancel (KeyBindEditDialog *edit)
{
	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	free (edit);
}

static void key_bind_edit_ok (KeyBindEditDialog *edit)
{
	int			action_idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (edit->action_combo));
	const char *state_str  = gtk_editable_get_text (GTK_EDITABLE (edit->state_entry));
	const char *key_str	   = gtk_editable_get_text (GTK_EDITABLE (edit->key_entry));

	if (!key_str || strlen (key_str) == 0) {
		GtkAlertDialog *alert = gtk_alert_dialog_new ("Key is required.");
		gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
		g_object_unref (alert);
		return;
	}

	/* Parse key_min early for validation */
	guint key_min = 0;
	guint state	  = 0;
	char  bind_str[256];
	snprintf (bind_str, sizeof (bind_str), "%s%s", state_str ? state_str : "", key_str);
	gtk_accelerator_parse (bind_str, &key_min, &state);

	if (key_min == 0) {
		/* Try parsing state separately */
		gtk_accelerator_parse (state_str ? state_str : "", NULL, &state);
		key_min = gdk_keyval_from_name (key_str);
	}

	if (key_min == 0 || key_min == GDK_KEY_VoidSymbol) {
		GtkAlertDialog *alert = gtk_alert_dialog_new ("Invalid key: '%s'", key_str);
		gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
		g_object_unref (alert);
		return;
	}

	/* Validate SWITCH-specific fields */
	guint key_max = key_min;
	if (action_idx == BIND_ACT_SWITCH) {
		const char *key_max_str = gtk_editable_get_text (GTK_EDITABLE (edit->key_max_entry));
		if (key_max_str && strlen (key_max_str) > 0) {
			key_max = gdk_keyval_from_name (key_max_str);
			if (key_max == 0 || key_max == GDK_KEY_VoidSymbol) {
				GtkAlertDialog *alert = gtk_alert_dialog_new ("Invalid key max: '%s'", key_max_str);
				gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
				g_object_unref (alert);
				return;
			}
			if (key_max < key_min) {
				GtkAlertDialog *alert =
				  gtk_alert_dialog_new ("Key max (%s) must be greater than or equal to key min (%s).", key_max_str, key_str);
				gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
				g_object_unref (alert);
				return;
			}
		}

		int base = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (edit->base_spin));
		if (base < 0) {
			GtkAlertDialog *alert = gtk_alert_dialog_new ("Base terminal must be non-negative.");
			gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
			g_object_unref (alert);
			return;
		}
	}

	/* Check for duplicate or overlapping bindings */
	for (bind_t *cur = terms.keys; cur; cur = cur->next) {
		/* Skip the binding we're editing */
		if (cur == edit->editing_bind) {
			continue;
		}

		/* Must have same modifier state to conflict */
		if (cur->state != state) {
			continue;
		}

		/* Check for overlap: ranges [key_min, key_max] and [cur->key_min, cur->key_max] */
		/* Two ranges overlap if: max1 >= min2 AND max2 >= min1 */
		if (key_max >= cur->key_min && cur->key_max >= key_min) {
			gchar		*state_str_display = gtk_accelerator_name (0, state);
			const gchar *cur_key_min_name  = gdk_keyval_name (cur->key_min);
			const gchar *cur_key_max_name  = gdk_keyval_name (cur->key_max);

			GtkAlertDialog *alert;
			if (cur->key_min == cur->key_max) {
				alert = gtk_alert_dialog_new ("Binding overlaps with existing binding: %s%s", state_str_display,
											  cur_key_min_name ? cur_key_min_name : "?");
			} else {
				alert =
				  gtk_alert_dialog_new ("Binding overlaps with existing binding: %s%s-%s", state_str_display,
										cur_key_min_name ? cur_key_min_name : "?", cur_key_max_name ? cur_key_max_name : "?");
			}
			gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
			g_object_unref (alert);
			g_free (state_str_display);
			return;
		}
	}

	bind_t *bind;
	if (edit->editing_bind) {
		bind = edit->editing_bind;
	} else {
		bind	   = calloc (1, sizeof (bind_t));
		bind->next = terms.keys;
		terms.keys = bind;
	}

	bind->action  = (bind_actions_t) action_idx;
	bind->key_min = key_min;
	bind->state	  = state;

	if (bind->action == BIND_ACT_SWITCH) {
		bind->base				= gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (edit->base_spin));
		const char *key_max_str = gtk_editable_get_text (GTK_EDITABLE (edit->key_max_entry));
		if (key_max_str && strlen (key_max_str) > 0) {
			bind->key_max = gdk_keyval_from_name (key_max_str);
		} else {
			bind->key_max = bind->key_min;
		}

		/* Parse and save command */
		const char *cmd_str = gtk_editable_get_text (GTK_EDITABLE (edit->cmd_entry));
		if (bind->argv) {
			g_strfreev (bind->argv);
			bind->argv = NULL;
		}
		if (cmd_str && strlen (cmd_str) > 0) {
			gint	argc  = 0;
			gchar **argv  = NULL;
			GError *error = NULL;
			if (g_shell_parse_argv (cmd_str, &argc, &argv, &error)) {
				bind->argv = argv;
			} else {
				if (error) {
					g_error_free (error);
				}
			}
		}

		/* Parse and save environment */
		const char *env_str = gtk_editable_get_text (GTK_EDITABLE (edit->env_entry));
		if (bind->env) {
			g_strfreev (bind->env);
			bind->env = NULL;
		}
		if (env_str && strlen (env_str) > 0) {
			gint	argc  = 0;
			gchar **argv  = NULL;
			GError *error = NULL;
			if (g_shell_parse_argv (env_str, &argc, &argv, &error)) {
				bind->env = argv;
			} else {
				if (error) {
					g_error_free (error);
				}
			}
		}

		/* Update n_active if needed */
		int n = bind->base + (bind->key_max - bind->key_min) + 1;
		if (n > terms.n_active) {
			int old_n_active = terms.n_active;
			terms.n_active	 = n;
			terms.active	 = realloc (terms.active, terms.n_active * sizeof (*terms.active));
			memset (&terms.active[old_n_active], 0, (terms.n_active - old_n_active) * sizeof (*terms.active));
		}
	} else {
		bind->key_max = bind->key_min;
		bind->base	  = 0;
		/* Clear argv and env for non-switch bindings */
		if (bind->argv) {
			g_strfreev (bind->argv);
			bind->argv = NULL;
		}
		if (bind->env) {
			g_strfreev (bind->env);
			bind->env = NULL;
		}
	}

	zterm_save_config ();
	debugf ("Calling rebuild_menus");
	rebuild_menus ();

	/* Refresh the list dialog if available */
	if (edit->list_dialog) {
		refresh_key_bind_list (edit->list_dialog);
	}

	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	free (edit);
}

static void key_bind_action_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
	KeyBindEditDialog *edit		  = (KeyBindEditDialog *) user_data;
	int				   action_idx = gtk_drop_down_get_selected (dropdown);
	bool			   is_switch  = (action_idx == BIND_ACT_SWITCH);

	gtk_widget_set_visible (edit->base_label, is_switch);
	gtk_widget_set_visible (edit->base_spin, is_switch);
	gtk_widget_set_visible (edit->key_max_label, is_switch);
	gtk_widget_set_visible (edit->key_max_entry, is_switch);
	gtk_widget_set_visible (edit->cmd_label, is_switch);
	gtk_widget_set_visible (edit->cmd_entry, is_switch);
	gtk_widget_set_visible (edit->env_label, is_switch);
	gtk_widget_set_visible (edit->env_entry, is_switch);
}

static void key_capture_btn_clicked (GtkButton *button, gpointer user_data)
{
	KeyBindEditDialog *edit = (KeyBindEditDialog *) user_data;
	show_capture_dialog (edit->state_entry, edit->key_entry, NULL, true, edit->parent_window);
}

static void show_key_bind_edit_dialog (bind_t *editing_bind, long int parent_window, KeyBindListDialog *list_dialog)
{
	KeyBindEditDialog *edit = g_new0 (KeyBindEditDialog, 1);
	edit->editing_bind		= editing_bind;
	edit->parent_window		= parent_window;
	edit->list_dialog		= list_dialog;

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), editing_bind ? "Edit Key Binding" : "New Key Binding");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (windows[parent_window].window));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	edit->dialog = dialog;

	GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (main_box, 12);
	gtk_widget_set_margin_end (main_box, 12);
	gtk_widget_set_margin_top (main_box, 12);
	gtk_widget_set_margin_bottom (main_box, 12);
	gtk_window_set_child (GTK_WINDOW (dialog), main_box);

	GtkWidget *grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_box_append (GTK_BOX (main_box), grid);

	int row = 0;

	/* Action */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Action:"), 0, row, 1, 1);
	GtkStringList *action_list = gtk_string_list_new ((const char *const *) bind_action_names);
	edit->action_combo		   = gtk_drop_down_new (G_LIST_MODEL (action_list), NULL);
	if (editing_bind) {
		gtk_drop_down_set_selected (GTK_DROP_DOWN (edit->action_combo), editing_bind->action);
	}
	g_signal_connect (edit->action_combo, "notify::selected", G_CALLBACK (key_bind_action_changed), edit);
	gtk_grid_attach (GTK_GRID (grid), edit->action_combo, 1, row++, 1, 1);

	/* State (modifiers) */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Modifiers:"), 0, row, 1, 1);
	edit->state_entry = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (edit->state_entry), "e.g. <Control><Shift>");
	if (editing_bind) {
		gchar *state_str = gtk_accelerator_name (0, editing_bind->state);
		gtk_editable_set_text (GTK_EDITABLE (edit->state_entry), state_str);
		g_free (state_str);
	}
	gtk_grid_attach (GTK_GRID (grid), edit->state_entry, 1, row++, 1, 1);

	/* Key */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Key:"), 0, row, 1, 1);
	GtkWidget *key_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	edit->key_entry	   = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (edit->key_entry), "e.g. c, F1, 1");
	if (editing_bind) {
		const gchar *key_str = gdk_keyval_name (editing_bind->key_min);
		if (key_str) {
			gtk_editable_set_text (GTK_EDITABLE (edit->key_entry), key_str);
		}
	}
	gtk_widget_set_hexpand (edit->key_entry, TRUE);
	gtk_box_append (GTK_BOX (key_box), edit->key_entry);

	/* Capture button for key */
	GtkWidget *capture_btn = gtk_button_new_with_label ("Capture...");
	g_object_set_data (G_OBJECT (capture_btn), "state_entry", edit->state_entry);
	g_object_set_data (G_OBJECT (capture_btn), "key_entry", edit->key_entry);
	g_object_set_data (G_OBJECT (capture_btn), "parent_window", GINT_TO_POINTER (parent_window));
	g_signal_connect (capture_btn, "clicked", G_CALLBACK (key_capture_btn_clicked), edit);
	gtk_box_append (GTK_BOX (key_box), capture_btn);

	gtk_grid_attach (GTK_GRID (grid), key_box, 1, row++, 1, 1);

	/* Base (for SWITCH only) */
	edit->base_label = create_label ("Base Terminal:");
	gtk_grid_attach (GTK_GRID (grid), edit->base_label, 0, row, 1, 1);
	edit->base_spin = gtk_spin_button_new_with_range (0, 100, 1);
	if (editing_bind && editing_bind->action == BIND_ACT_SWITCH) {
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (edit->base_spin), editing_bind->base);
	}
	gtk_grid_attach (GTK_GRID (grid), edit->base_spin, 1, row++, 1, 1);

	/* Key Max (for SWITCH only) */
	edit->key_max_label = create_label ("Key Max (optional):");
	gtk_grid_attach (GTK_GRID (grid), edit->key_max_label, 0, row, 1, 1);
	edit->key_max_entry = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (edit->key_max_entry), "e.g. 9, F12");
	if (editing_bind && editing_bind->action == BIND_ACT_SWITCH && editing_bind->key_max != editing_bind->key_min) {
		const gchar *key_max_str = gdk_keyval_name (editing_bind->key_max);
		if (key_max_str) {
			gtk_editable_set_text (GTK_EDITABLE (edit->key_max_entry), key_max_str);
		}
	}
	gtk_grid_attach (GTK_GRID (grid), edit->key_max_entry, 1, row++, 1, 1);

	/* Command (for SWITCH only) */
	edit->cmd_label = create_label ("Command (optional):");
	gtk_grid_attach (GTK_GRID (grid), edit->cmd_label, 0, row, 1, 1);
	edit->cmd_entry = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (edit->cmd_entry), "e.g. /bin/bash --login");
	if (editing_bind && editing_bind->action == BIND_ACT_SWITCH && editing_bind->argv) {
		gchar *cmd_str = g_strjoinv (" ", editing_bind->argv);
		gtk_editable_set_text (GTK_EDITABLE (edit->cmd_entry), cmd_str);
		g_free (cmd_str);
	}
	gtk_widget_set_hexpand (edit->cmd_entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), edit->cmd_entry, 1, row++, 1, 1);

	/* Environment (for SWITCH only) */
	edit->env_label = create_label ("Environment (optional):");
	gtk_grid_attach (GTK_GRID (grid), edit->env_label, 0, row, 1, 1);
	edit->env_entry = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (edit->env_entry), "e.g. VAR1=val1 VAR2=val2");
	if (editing_bind && editing_bind->action == BIND_ACT_SWITCH && editing_bind->env) {
		gchar *env_str = g_strjoinv (" ", editing_bind->env);
		gtk_editable_set_text (GTK_EDITABLE (edit->env_entry), env_str);
		g_free (env_str);
	}
	gtk_widget_set_hexpand (edit->env_entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), edit->env_entry, 1, row++, 1, 1);

	/* Set initial visibility of switch-only fields */
	/* For new bindings, default action is SWITCH (index 0), so fields should be visible */
	bool is_switch = editing_bind ? (editing_bind->action == BIND_ACT_SWITCH) : true;
	gtk_widget_set_visible (edit->base_label, is_switch);
	gtk_widget_set_visible (edit->base_spin, is_switch);
	gtk_widget_set_visible (edit->key_max_label, is_switch);
	gtk_widget_set_visible (edit->key_max_entry, is_switch);
	gtk_widget_set_visible (edit->cmd_label, is_switch);
	gtk_widget_set_visible (edit->cmd_entry, is_switch);
	gtk_widget_set_visible (edit->env_label, is_switch);
	gtk_widget_set_visible (edit->env_entry, is_switch);

	/* Buttons */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	g_signal_connect_swapped (cancel_btn, "clicked", G_CALLBACK (key_bind_edit_cancel), edit);
	g_signal_connect_swapped (ok_btn, "clicked", G_CALLBACK (key_bind_edit_ok), edit);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 450, -1);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void key_bind_add_clicked (GtkButton *button, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	show_key_bind_edit_dialog (NULL, list_dialog->window_n, list_dialog);
}

static void key_bind_edit_clicked (GtkButton *button, gpointer user_data)
{
	bind_t			  *bind		   = (bind_t *) g_object_get_data (G_OBJECT (button), "bind_ptr");
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;

	if (bind) {
		show_key_bind_edit_dialog (bind, list_dialog->window_n, list_dialog);
	}
}

static void key_bind_delete_clicked (GtkButton *button, gpointer user_data)
{
	bind_t			  *bind		   = (bind_t *) g_object_get_data (G_OBJECT (button), "bind_ptr");
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;

	/* Remove from linked list */
	bind_t **prev = &terms.keys;
	for (bind_t *cur = terms.keys; cur; cur = cur->next) {
		if (cur == bind) {
			*prev = cur->next;
			if (cur->argv)
				g_strfreev (cur->argv);
			if (cur->env)
				g_strfreev (cur->env);
			free (cur);
			break;
		}
		prev = &cur->next;
	}

	zterm_save_config ();
	debugf ("Calling rebuild_menus");
	rebuild_menus ();
	refresh_key_bind_list (list_dialog);
}

static void refresh_key_bind_list (KeyBindListDialog *list_dialog)
{
	/* Clear and repopulate the store */
	g_list_store_remove_all (list_dialog->store);

	for (bind_t *cur = terms.keys; cur; cur = cur->next) {
		KeyBindItem *item = key_bind_item_new (cur);
		g_list_store_append (list_dialog->store, item);
		g_object_unref (item);
	}
}

static void key_bind_list_close (KeyBindListDialog *list_dialog)
{
	gtk_window_destroy (GTK_WINDOW (list_dialog->dialog));
	/* Note: store is owned by the selection model which is owned by the column view,
	 * so it will be freed when the dialog is destroyed */
	free (list_dialog);
}

static void setup_label_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_list_item_set_child (list_item, label);
}

static void bind_action_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget	*label = gtk_list_item_get_child (list_item);
	KeyBindItem *item  = gtk_list_item_get_item (list_item);
	if (item && item->bind) {
		const char *action_name = (item->bind->action < sizeof (bind_action_names) / sizeof (bind_action_names[0]) - 1)
									? bind_action_names[item->bind->action]
									: "UNKNOWN";
		gtk_label_set_text (GTK_LABEL (label), action_name);
	}
}

static void bind_binding_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget	*label = gtk_list_item_get_child (list_item);
	KeyBindItem *item  = gtk_list_item_get_item (list_item);
	if (item && item->bind) {
		char   binding_str[128];
		gchar *accel = gtk_accelerator_name (item->bind->key_min, item->bind->state);
		if (item->bind->action == BIND_ACT_SWITCH && item->bind->key_max != item->bind->key_min) {
			const gchar *key_max_name = gdk_keyval_name (item->bind->key_max);
			snprintf (binding_str, sizeof (binding_str), "%s..%s", accel, key_max_name ? key_max_name : "?");
		} else {
			snprintf (binding_str, sizeof (binding_str), "%s", accel);
		}
		g_free (accel);
		gtk_label_set_text (GTK_LABEL (label), binding_str);
	}
}

static void bind_base_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget	*label = gtk_list_item_get_child (list_item);
	KeyBindItem *item  = gtk_list_item_get_item (list_item);
	if (item && item->bind) {
		char base_str[16] = "";
		if (item->bind->action == BIND_ACT_SWITCH) {
			snprintf (base_str, sizeof (base_str), "%d", item->bind->base);
		}
		gtk_label_set_text (GTK_LABEL (label), base_str);
	}
}

static void bind_command_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget	*label = gtk_list_item_get_child (list_item);
	KeyBindItem *item  = gtk_list_item_get_item (list_item);
	if (item && item->bind) {
		if (item->bind->action == BIND_ACT_SWITCH && item->bind->argv) {
			gchar *cmd_str = g_strjoinv (" ", item->bind->argv);
			gtk_label_set_text (GTK_LABEL (label), cmd_str);
			g_free (cmd_str);
		} else {
			gtk_label_set_text (GTK_LABEL (label), "");
		}
	}
}

static void setup_buttons_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);

	GtkWidget *edit_btn = gtk_button_new_from_icon_name ("document-edit-symbolic");
	gtk_widget_set_tooltip_text (edit_btn, "Edit");
	gtk_box_append (GTK_BOX (box), edit_btn);

	GtkWidget *delete_btn = gtk_button_new_from_icon_name ("edit-delete-symbolic");
	gtk_widget_set_tooltip_text (delete_btn, "Delete");
	gtk_box_append (GTK_BOX (box), delete_btn);

	gtk_list_item_set_child (list_item, box);
}

static void bind_buttons_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	GtkWidget		  *box		   = gtk_list_item_get_child (list_item);
	KeyBindItem		  *item		   = gtk_list_item_get_item (list_item);

	GtkWidget *edit_btn	  = gtk_widget_get_first_child (box);
	GtkWidget *delete_btn = gtk_widget_get_next_sibling (edit_btn);

	if (item && item->bind) {
		g_object_set_data (G_OBJECT (edit_btn), "bind_ptr", item->bind);
		g_object_set_data (G_OBJECT (delete_btn), "bind_ptr", item->bind);

		/* Disconnect any previous handlers */
		g_signal_handlers_disconnect_by_func (edit_btn, G_CALLBACK (key_bind_edit_clicked), list_dialog);
		g_signal_handlers_disconnect_by_func (delete_btn, G_CALLBACK (key_bind_delete_clicked), list_dialog);

		g_signal_connect (edit_btn, "clicked", G_CALLBACK (key_bind_edit_clicked), list_dialog);
		g_signal_connect (delete_btn, "clicked", G_CALLBACK (key_bind_delete_clicked), list_dialog);
	}
}

static void show_key_bind_editor (GtkButton *button, gpointer user_data)
{
	PrefsDialog		  *prefs	   = (PrefsDialog *) user_data;
	KeyBindListDialog *list_dialog = g_new0 (KeyBindListDialog, 1);
	list_dialog->window_n		   = prefs->window_n;

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), "Key Bindings");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (prefs->dialog));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	list_dialog->dialog = dialog;

	GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (main_box, 12);
	gtk_widget_set_margin_end (main_box, 12);
	gtk_widget_set_margin_top (main_box, 12);
	gtk_widget_set_margin_bottom (main_box, 12);
	gtk_window_set_child (GTK_WINDOW (dialog), main_box);

	/* Create the GListStore and populate it */
	list_dialog->store = g_list_store_new (KEY_BIND_ITEM_TYPE);

	/* Create selection model */
	GtkNoSelection *selection = gtk_no_selection_new (G_LIST_MODEL (list_dialog->store));

	/* Create ColumnView */
	list_dialog->column_view = gtk_column_view_new (GTK_SELECTION_MODEL (selection));
	gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (list_dialog->column_view), TRUE);

	/* Action column */
	GtkListItemFactory *action_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (action_factory, "setup", G_CALLBACK (setup_label_factory), NULL);
	g_signal_connect (action_factory, "bind", G_CALLBACK (bind_action_factory), NULL);
	GtkColumnViewColumn *action_col = gtk_column_view_column_new ("Action", action_factory);
	gtk_column_view_column_set_resizable (action_col, TRUE);
	gtk_column_view_column_set_fixed_width (action_col, 100);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), action_col);

	/* Binding column */
	GtkListItemFactory *binding_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (binding_factory, "setup", G_CALLBACK (setup_label_factory), NULL);
	g_signal_connect (binding_factory, "bind", G_CALLBACK (bind_binding_factory), NULL);
	GtkColumnViewColumn *binding_col = gtk_column_view_column_new ("Binding", binding_factory);
	gtk_column_view_column_set_resizable (binding_col, TRUE);
	gtk_column_view_column_set_fixed_width (binding_col, 180);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), binding_col);

	/* Base column */
	GtkListItemFactory *base_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (base_factory, "setup", G_CALLBACK (setup_label_factory), NULL);
	g_signal_connect (base_factory, "bind", G_CALLBACK (bind_base_factory), NULL);
	GtkColumnViewColumn *base_col = gtk_column_view_column_new ("Base", base_factory);
	gtk_column_view_column_set_resizable (base_col, TRUE);
	gtk_column_view_column_set_fixed_width (base_col, 60);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), base_col);

	/* Command column */
	GtkListItemFactory *cmd_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (cmd_factory, "setup", G_CALLBACK (setup_label_factory), NULL);
	g_signal_connect (cmd_factory, "bind", G_CALLBACK (bind_command_factory), NULL);
	GtkColumnViewColumn *cmd_col = gtk_column_view_column_new ("Command", cmd_factory);
	gtk_column_view_column_set_resizable (cmd_col, TRUE);
	gtk_column_view_column_set_expand (cmd_col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), cmd_col);

	/* Buttons column */
	GtkListItemFactory *buttons_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (buttons_factory, "setup", G_CALLBACK (setup_buttons_factory), NULL);
	g_signal_connect (buttons_factory, "bind", G_CALLBACK (bind_buttons_factory), list_dialog);
	GtkColumnViewColumn *buttons_col = gtk_column_view_column_new ("", buttons_factory);
	gtk_column_view_column_set_fixed_width (buttons_col, 80);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), buttons_col);

	/* Scrolled window for column view */
	GtkWidget *scrolled = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand (scrolled, TRUE);
	gtk_widget_set_size_request (scrolled, 600, 300);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list_dialog->column_view);
	gtk_box_append (GTK_BOX (main_box), scrolled);

	refresh_key_bind_list (list_dialog);

	/* Buttons */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *add_btn	 = gtk_button_new_with_mnemonic ("_Add");
	GtkWidget *close_btn = gtk_button_new_with_mnemonic ("_Close");
	gtk_box_append (GTK_BOX (button_box), add_btn);
	gtk_box_append (GTK_BOX (button_box), close_btn);

	g_signal_connect (add_btn, "clicked", G_CALLBACK (key_bind_add_clicked), list_dialog);
	g_signal_connect_swapped (close_btn, "clicked", G_CALLBACK (key_bind_list_close), list_dialog);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 700, 450);
	gtk_window_present (GTK_WINDOW (dialog));
}

/* ==================== Mouse Button Bindings Editor ==================== */

typedef struct {
	GtkWidget					  *dialog;
	GtkWidget					  *action_combo;
	GtkWidget					  *state_entry;
	GtkWidget					  *button_spin;
	bind_button_t				  *editing_bind; /* NULL for new */
	long int					   parent_window;
	struct ButtonBindListDialog_s *list_dialog; /* For refreshing list after edit */
} ButtonBindEditDialog;

typedef struct ButtonBindListDialog_s {
	GtkWidget *dialog;
	GtkWidget *list_box;
	long int   window_n;
} ButtonBindListDialog;

static void refresh_button_bind_list (ButtonBindListDialog *list_dialog);

static void button_bind_edit_cancel (ButtonBindEditDialog *edit)
{
	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	free (edit);
}

static void button_bind_edit_ok (ButtonBindEditDialog *edit)
{
	int			action_idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (edit->action_combo));
	const char *state_str  = gtk_editable_get_text (GTK_EDITABLE (edit->state_entry));
	int			button	   = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (edit->button_spin));

	/* Only OPEN_URI and CUT_URI are valid for button bindings */
	bind_actions_t action;
	if (action_idx == 0) {
		action = BIND_ACT_OPEN_URI;
	} else {
		action = BIND_ACT_CUT_URI;
	}

	/* Parse state */
	GdkModifierType state = 0;
	gtk_accelerator_parse (state_str, NULL, &state);

	/* Check for duplicate binding (same button + state combination) */
	for (bind_button_t *cur = terms.buttons; cur; cur = cur->next) {
		if (cur != edit->editing_bind && cur->button == button && cur->state == state) {
			/* Duplicate found - don't add */
			GtkAlertDialog *alert = gtk_alert_dialog_new ("A binding for this button and modifier combination already exists.");
			gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
			g_object_unref (alert);
			return;
		}
	}

	bind_button_t *bind;
	if (edit->editing_bind) {
		bind = edit->editing_bind;
	} else {
		bind		  = calloc (1, sizeof (bind_button_t));
		bind->next	  = terms.buttons;
		terms.buttons = bind;
	}

	bind->action = action;
	bind->button = button;
	bind->state	 = state;

	zterm_save_config ();

	/* Refresh the list dialog if available */
	if (edit->list_dialog) {
		refresh_button_bind_list (edit->list_dialog);
	}

	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	free (edit);
}

static void button_capture_btn_clicked (GtkButton *button, gpointer user_data)
{
	ButtonBindEditDialog *edit = (ButtonBindEditDialog *) user_data;
	show_capture_dialog (edit->state_entry, NULL, edit->button_spin, false, edit->parent_window);
}

static void show_button_bind_edit_dialog (bind_button_t *editing_bind, long int parent_window, ButtonBindListDialog *list_dialog)
{
	ButtonBindEditDialog *edit = g_new0 (ButtonBindEditDialog, 1);
	edit->editing_bind		   = editing_bind;
	edit->parent_window		   = parent_window;
	edit->list_dialog		   = list_dialog;

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), editing_bind ? "Edit Mouse Binding" : "New Mouse Binding");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (windows[parent_window].window));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	edit->dialog = dialog;

	GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (main_box, 12);
	gtk_widget_set_margin_end (main_box, 12);
	gtk_widget_set_margin_top (main_box, 12);
	gtk_widget_set_margin_bottom (main_box, 12);
	gtk_window_set_child (GTK_WINDOW (dialog), main_box);

	GtkWidget *grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_box_append (GTK_BOX (main_box), grid);

	int row = 0;

	/* Action - only OPEN_URI and CUT_URI for buttons */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Action:"), 0, row, 1, 1);
	const char	  *button_actions[] = {"OPEN_URI", "CUT_URI", NULL};
	GtkStringList *action_list		= gtk_string_list_new (button_actions);
	edit->action_combo				= gtk_drop_down_new (G_LIST_MODEL (action_list), NULL);
	if (editing_bind) {
		gtk_drop_down_set_selected (GTK_DROP_DOWN (edit->action_combo), editing_bind->action == BIND_ACT_CUT_URI ? 1 : 0);
	}
	gtk_grid_attach (GTK_GRID (grid), edit->action_combo, 1, row++, 1, 1);

	/* State (modifiers) */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Modifiers:"), 0, row, 1, 1);
	edit->state_entry = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (edit->state_entry), "e.g. <Control>");
	if (editing_bind) {
		gchar *state_str = gtk_accelerator_name (0, editing_bind->state);
		gtk_editable_set_text (GTK_EDITABLE (edit->state_entry), state_str);
		g_free (state_str);
	}
	gtk_grid_attach (GTK_GRID (grid), edit->state_entry, 1, row++, 1, 1);

	/* Button number */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Button (1-5):"), 0, row, 1, 1);
	GtkWidget *button_num_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	edit->button_spin		  = gtk_spin_button_new_with_range (1, 5, 1);
	if (editing_bind) {
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (edit->button_spin), editing_bind->button);
	} else {
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (edit->button_spin), 1);
	}
	gtk_box_append (GTK_BOX (button_num_box), edit->button_spin);

	/* Capture button for mouse button */
	GtkWidget *capture_btn = gtk_button_new_with_label ("Capture...");
	g_signal_connect (capture_btn, "clicked", G_CALLBACK (button_capture_btn_clicked), edit);
	gtk_box_append (GTK_BOX (button_num_box), capture_btn);

	gtk_grid_attach (GTK_GRID (grid), button_num_box, 1, row++, 1, 1);

	/* Buttons */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	g_signal_connect_swapped (cancel_btn, "clicked", G_CALLBACK (button_bind_edit_cancel), edit);
	g_signal_connect_swapped (ok_btn, "clicked", G_CALLBACK (button_bind_edit_ok), edit);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 400, -1);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void button_bind_add_clicked (GtkButton *button, gpointer user_data)
{
	ButtonBindListDialog *list_dialog = (ButtonBindListDialog *) user_data;
	show_button_bind_edit_dialog (NULL, list_dialog->window_n, list_dialog);
}

static void button_bind_edit_clicked (GtkButton *button, gpointer user_data)
{
	bind_button_t		 *bind		  = (bind_button_t *) g_object_get_data (G_OBJECT (button), "bind_ptr");
	ButtonBindListDialog *list_dialog = (ButtonBindListDialog *) user_data;

	if (bind) {
		show_button_bind_edit_dialog (bind, list_dialog->window_n, list_dialog);
	}
}

static void button_bind_delete_clicked (GtkButton *button, gpointer user_data)
{
	bind_button_t		 *bind		  = (bind_button_t *) g_object_get_data (G_OBJECT (button), "bind_ptr");
	ButtonBindListDialog *list_dialog = (ButtonBindListDialog *) user_data;

	/* Remove from linked list */
	bind_button_t **prev = &terms.buttons;
	for (bind_button_t *cur = terms.buttons; cur; cur = cur->next) {
		if (cur == bind) {
			*prev = cur->next;
			free (cur);
			break;
		}
		prev = &cur->next;
	}

	zterm_save_config ();
	refresh_button_bind_list (list_dialog);
}

static void refresh_button_bind_list (ButtonBindListDialog *list_dialog)
{
	/* Clear existing children */
	GtkWidget *child = gtk_widget_get_first_child (list_dialog->list_box);
	while (child) {
		GtkWidget *next = gtk_widget_get_next_sibling (child);
		gtk_list_box_remove (GTK_LIST_BOX (list_dialog->list_box), child);
		child = next;
	}

	/* Add rows for each button binding */
	for (bind_button_t *cur = terms.buttons; cur; cur = cur->next) {
		GtkWidget *row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_widget_set_margin_start (row_box, 6);
		gtk_widget_set_margin_end (row_box, 6);
		gtk_widget_set_margin_top (row_box, 3);
		gtk_widget_set_margin_bottom (row_box, 3);

		/* Action label */
		const char *action_name	 = cur->action == BIND_ACT_OPEN_URI ? "OPEN_URI" : "CUT_URI";
		GtkWidget  *action_label = gtk_label_new (action_name);
		gtk_widget_set_size_request (action_label, 80, -1);
		gtk_widget_set_halign (action_label, GTK_ALIGN_START);
		gtk_box_append (GTK_BOX (row_box), action_label);

		/* Binding description */
		char   binding_str[128];
		gchar *state_str = gtk_accelerator_name (0, cur->state);
		snprintf (binding_str, sizeof (binding_str), "%sButton%d", state_str, cur->button);
		g_free (state_str);

		GtkWidget *binding_label = gtk_label_new (binding_str);
		gtk_widget_set_hexpand (binding_label, TRUE);
		gtk_widget_set_halign (binding_label, GTK_ALIGN_START);
		gtk_box_append (GTK_BOX (row_box), binding_label);

		/* Edit button */
		GtkWidget *edit_btn = gtk_button_new_from_icon_name ("document-edit-symbolic");
		gtk_widget_set_tooltip_text (edit_btn, "Edit");
		g_object_set_data (G_OBJECT (edit_btn), "bind_ptr", cur);
		g_signal_connect (edit_btn, "clicked", G_CALLBACK (button_bind_edit_clicked), list_dialog);
		gtk_box_append (GTK_BOX (row_box), edit_btn);

		/* Delete button */
		GtkWidget *delete_btn = gtk_button_new_from_icon_name ("edit-delete-symbolic");
		gtk_widget_set_tooltip_text (delete_btn, "Delete");
		g_object_set_data (G_OBJECT (delete_btn), "bind_ptr", cur);
		g_signal_connect (delete_btn, "clicked", G_CALLBACK (button_bind_delete_clicked), list_dialog);
		gtk_box_append (GTK_BOX (row_box), delete_btn);

		gtk_list_box_append (GTK_LIST_BOX (list_dialog->list_box), row_box);
	}
}

static void button_bind_list_close (ButtonBindListDialog *list_dialog)
{
	gtk_window_destroy (GTK_WINDOW (list_dialog->dialog));
	free (list_dialog);
}

static void show_button_bind_editor (GtkButton *button, gpointer user_data)
{
	PrefsDialog			 *prefs		  = (PrefsDialog *) user_data;
	ButtonBindListDialog *list_dialog = g_new0 (ButtonBindListDialog, 1);
	list_dialog->window_n			  = prefs->window_n;

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), "Mouse Button Bindings");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (prefs->dialog));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	list_dialog->dialog = dialog;

	GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (main_box, 12);
	gtk_widget_set_margin_end (main_box, 12);
	gtk_widget_set_margin_top (main_box, 12);
	gtk_widget_set_margin_bottom (main_box, 12);
	gtk_window_set_child (GTK_WINDOW (dialog), main_box);

	/* Scrolled window for list */
	GtkWidget *scrolled = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand (scrolled, TRUE);
	gtk_widget_set_size_request (scrolled, 350, 200);
	gtk_box_append (GTK_BOX (main_box), scrolled);

	list_dialog->list_box = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_dialog->list_box), GTK_SELECTION_NONE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list_dialog->list_box);

	refresh_button_bind_list (list_dialog);

	/* Buttons */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *add_btn	 = gtk_button_new_with_mnemonic ("_Add");
	GtkWidget *close_btn = gtk_button_new_with_mnemonic ("_Close");
	gtk_box_append (GTK_BOX (button_box), add_btn);
	gtk_box_append (GTK_BOX (button_box), close_btn);

	g_signal_connect (add_btn, "clicked", G_CALLBACK (button_bind_add_clicked), list_dialog);
	g_signal_connect_swapped (close_btn, "clicked", G_CALLBACK (button_bind_list_close), list_dialog);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 500, 300);
	gtk_window_present (GTK_WINDOW (dialog));
}

void do_preferences (GSimpleAction *self, GVariant *parameter, gpointer data)
{
	long int	 window_n = (long int) data;
	PrefsDialog *prefs	  = g_new0 (PrefsDialog, 1);
	prefs->window_n		  = window_n;

	/* Store original values for revert */
	prefs->original_font				 = terms.font ? strdup (terms.font) : NULL;
	prefs->original_word_char_exceptions = terms.word_char_exceptions ? strdup (terms.word_char_exceptions) : NULL;
	prefs->original_font_scale			 = terms.font_scale;
	prefs->original_start_width			 = start_width;
	prefs->original_start_height		 = start_height;
	prefs->original_scrollback_lines	 = terms.scrollback_lines;
	prefs->original_audible_bell		 = terms.audible_bell;
	prefs->original_scroll_on_output	 = terms.scroll_on_output;
	prefs->original_scroll_on_keystroke	 = terms.scroll_on_keystroke;
	prefs->original_bold_is_bright		 = terms.bold_is_bright;
	prefs->original_mouse_autohide		 = terms.mouse_autohide;

	/* Create window */
	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), "Preferences");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (windows[window_n].window));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	prefs->dialog = dialog;

	/* Create main vertical box */
	GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (main_box, 12);
	gtk_widget_set_margin_end (main_box, 12);
	gtk_widget_set_margin_top (main_box, 12);
	gtk_widget_set_margin_bottom (main_box, 12);
	gtk_window_set_child (GTK_WINDOW (dialog), main_box);

	/* Create grid for layout */
	GtkWidget *grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_box_append (GTK_BOX (main_box), grid);

	int row = 0;

	/* Font */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Font:"), 0, row, 1, 1);
	GtkWidget *font_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	prefs->font_entry	= gtk_entry_new ();
	gtk_editable_set_text (GTK_EDITABLE (prefs->font_entry), terms.font ? terms.font : "");
	gtk_widget_set_hexpand (prefs->font_entry, TRUE);
	gtk_box_append (GTK_BOX (font_box), prefs->font_entry);
	prefs->font_button = gtk_button_new_with_label ("Choose...");
	g_signal_connect (prefs->font_button, "clicked", G_CALLBACK (font_button_clicked), prefs);
	gtk_box_append (GTK_BOX (font_box), prefs->font_button);
	gtk_grid_attach (GTK_GRID (grid), font_box, 1, row++, 2, 1);

	/* Font Scale */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Font Scale:"), 0, row, 1, 1);
	prefs->font_scale_spin = gtk_spin_button_new_with_range (0.1, 10.0, 0.1);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (prefs->font_scale_spin), terms.font_scale);
	gtk_spin_button_set_digits (GTK_SPIN_BUTTON (prefs->font_scale_spin), 2);
	gtk_grid_attach (GTK_GRID (grid), prefs->font_scale_spin, 1, row++, 2, 1);

	/* Word Character Exceptions */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Word Char Exceptions:"), 0, row, 1, 1);
	prefs->word_char_entry = gtk_entry_new ();
	gtk_editable_set_text (GTK_EDITABLE (prefs->word_char_entry), terms.word_char_exceptions ? terms.word_char_exceptions : "");
	gtk_widget_set_hexpand (prefs->word_char_entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), prefs->word_char_entry, 1, row++, 2, 1);

	/* Window Size */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Default Window Size:"), 0, row, 1, 1);
	GtkWidget *size_box	   = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	prefs->size_width_spin = gtk_spin_button_new_with_range (100, 4096, 1);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (prefs->size_width_spin), start_width);
	gtk_box_append (GTK_BOX (size_box), prefs->size_width_spin);
	gtk_box_append (GTK_BOX (size_box), gtk_label_new ("x"));
	prefs->size_height_spin = gtk_spin_button_new_with_range (100, 4096, 1);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (prefs->size_height_spin), start_height);
	gtk_box_append (GTK_BOX (size_box), prefs->size_height_spin);
	gtk_grid_attach (GTK_GRID (grid), size_box, 1, row++, 2, 1);

	/* Scrollback Lines */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Scrollback Lines:"), 0, row, 1, 1);
	prefs->scrollback_spin = gtk_spin_button_new_with_range (0, 1000000, 100);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (prefs->scrollback_spin), terms.scrollback_lines);
	gtk_grid_attach (GTK_GRID (grid), prefs->scrollback_spin, 1, row++, 2, 1);

	/* Separator */
	GtkWidget *separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_margin_top (separator, 6);
	gtk_widget_set_margin_bottom (separator, 6);
	gtk_grid_attach (GTK_GRID (grid), separator, 0, row++, 3, 1);

	/* Boolean options */
	prefs->audible_bell_check = gtk_check_button_new_with_label ("Audible Bell");
	gtk_check_button_set_active (GTK_CHECK_BUTTON (prefs->audible_bell_check), terms.audible_bell);
	gtk_grid_attach (GTK_GRID (grid), prefs->audible_bell_check, 0, row++, 3, 1);

	prefs->scroll_on_output_check = gtk_check_button_new_with_label ("Scroll on Output");
	gtk_check_button_set_active (GTK_CHECK_BUTTON (prefs->scroll_on_output_check), terms.scroll_on_output);
	gtk_grid_attach (GTK_GRID (grid), prefs->scroll_on_output_check, 0, row++, 3, 1);

	prefs->scroll_on_keystroke_check = gtk_check_button_new_with_label ("Scroll on Keystroke");
	gtk_check_button_set_active (GTK_CHECK_BUTTON (prefs->scroll_on_keystroke_check), terms.scroll_on_keystroke);
	gtk_grid_attach (GTK_GRID (grid), prefs->scroll_on_keystroke_check, 0, row++, 3, 1);

	prefs->bold_is_bright_check = gtk_check_button_new_with_label ("Bold is Bright");
	gtk_check_button_set_active (GTK_CHECK_BUTTON (prefs->bold_is_bright_check), terms.bold_is_bright);
	gtk_grid_attach (GTK_GRID (grid), prefs->bold_is_bright_check, 0, row++, 3, 1);

	prefs->mouse_autohide_check = gtk_check_button_new_with_label ("Mouse Autohide");
	gtk_check_button_set_active (GTK_CHECK_BUTTON (prefs->mouse_autohide_check), terms.mouse_autohide);
	gtk_grid_attach (GTK_GRID (grid), prefs->mouse_autohide_check, 0, row++, 3, 1);

	/* Separator before color schemes */
	GtkWidget *separator2 = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_margin_top (separator2, 6);
	gtk_widget_set_margin_bottom (separator2, 6);
	gtk_grid_attach (GTK_GRID (grid), separator2, 0, row++, 3, 1);

	/* Color Schemes button */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Color Schemes:"), 0, row, 1, 1);
	GtkWidget *color_scheme_btn = gtk_button_new_with_label ("Edit Color Schemes...");
	g_signal_connect (color_scheme_btn, "clicked", G_CALLBACK (show_color_scheme_editor), prefs);
	gtk_grid_attach (GTK_GRID (grid), color_scheme_btn, 1, row++, 2, 1);

	/* Color Overrides button */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Color Overrides:"), 0, row, 1, 1);
	GtkWidget *color_override_btn = gtk_button_new_with_label ("Edit Color Overrides...");
	g_signal_connect (color_override_btn, "clicked", G_CALLBACK (show_color_override_editor), prefs);
	gtk_grid_attach (GTK_GRID (grid), color_override_btn, 1, row++, 2, 1);

	/* Key Bindings button */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Key Bindings:"), 0, row, 1, 1);
	GtkWidget *key_bind_btn = gtk_button_new_with_label ("Edit Key Bindings...");
	g_signal_connect (key_bind_btn, "clicked", G_CALLBACK (show_key_bind_editor), prefs);
	gtk_grid_attach (GTK_GRID (grid), key_bind_btn, 1, row++, 2, 1);

	/* Mouse Button Bindings button */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Mouse Bindings:"), 0, row, 1, 1);
	GtkWidget *button_bind_btn = gtk_button_new_with_label ("Edit Mouse Bindings...");
	g_signal_connect (button_bind_btn, "clicked", G_CALLBACK (show_button_bind_editor), prefs);
	gtk_grid_attach (GTK_GRID (grid), button_bind_btn, 1, row++, 2, 1);

	/* Button box */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *preview_btn = gtk_button_new_with_mnemonic ("_Preview");
	GtkWidget *revert_btn  = gtk_button_new_with_mnemonic ("_Revert");
	GtkWidget *cancel_btn  = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *apply_btn   = gtk_button_new_with_mnemonic ("_Apply");
	GtkWidget *ok_btn	   = gtk_button_new_with_mnemonic ("_OK");

	gtk_box_append (GTK_BOX (button_box), preview_btn);
	gtk_box_append (GTK_BOX (button_box), revert_btn);
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), apply_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	/* Connect button signals */
	g_signal_connect_swapped (preview_btn, "clicked", G_CALLBACK (prefs_preview_clicked), prefs);
	g_signal_connect_swapped (revert_btn, "clicked", G_CALLBACK (prefs_revert_clicked), prefs);
	g_signal_connect_swapped (cancel_btn, "clicked", G_CALLBACK (prefs_cancel_clicked), prefs);
	g_signal_connect_swapped (apply_btn, "clicked", G_CALLBACK (prefs_apply_clicked), prefs);
	g_signal_connect_swapped (ok_btn, "clicked", G_CALLBACK (prefs_ok_clicked), prefs);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 400, -1);
	gtk_window_present (GTK_WINDOW (dialog));
}

// vim: set ts=4 sw=4 noexpandtab :
