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

/* ==================== Generic list-settings Apply/OK/Cancel/Reset ==================== */

typedef struct ListSettingsOps {
	void *ctx;
	void (*commit) (void *ctx);
	void (*snapshot) (void *ctx);
	void (*restore_config) (void *ctx);
	void (*restore_working) (void *ctx);
	void (*refresh_ui) (void *ctx);
	void (*destroy) (void *ctx);
	void (*after_commit) (void *ctx); /* optional; may be NULL */
} ListSettingsOps;

static void list_settings_apply (GtkButton *btn, ListSettingsOps *ops)
{
	debugf ("ops: %p, btn: %p", ops, btn);
	ops->commit (ops->ctx);
	zterm_save_config ();
	if (ops->after_commit)
		ops->after_commit (ops->ctx);
	ops->snapshot (ops->ctx);
}

static void list_settings_ok (GtkButton *btn, ListSettingsOps *ops)
{
	debugf ("ops: %p, btn: %p", ops, btn);
	list_settings_apply (btn, ops);
	ops->destroy (ops->ctx);
}

static void list_settings_cancel (GtkButton *btn, ListSettingsOps *ops)
{
	ops->restore_config (ops->ctx);
	ops->restore_working (ops->ctx);
	ops->refresh_ui (ops->ctx);
	ops->destroy (ops->ctx);
}

static void list_settings_reset (GtkButton *btn, ListSettingsOps *ops)
{
	ops->restore_working (ops->ctx);
	ops->refresh_ui (ops->ctx);
}

/* Color Scheme Editor */

typedef struct {
	GtkWidget *dialog;
	GtkWidget *name_entry;
	GtkWidget *fg_button;
	GtkWidget *bg_button;
	int		   scheme_index;
	long int   parent_window;
	void	  *list_dialog; /* ColorSchemeListDialog* when from list: OK updates working_schemes only */
	GdkRGBA	   original_fg;
	GdkRGBA	   original_bg;
	char	   original_name[32];
	bool	   is_new_scheme;
} ColorSchemeEditDialog;

typedef struct {
	GtkWidget	   *dialog;
	GtkWidget	   *list_box;
	long int		window_n;
	color_scheme_t	working_schemes[MAX_COLOR_SCHEMES];
	color_scheme_t	original_schemes[MAX_COLOR_SCHEMES];
	ListSettingsOps ops;
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
	if (!name || strlen (name) == 0) {
		gtk_window_destroy (GTK_WINDOW (edit->dialog));
		free (edit);
		return;
	}
	int			   idx = edit->scheme_index;
	const GdkRGBA *fg  = gtk_color_dialog_button_get_rgba (GTK_COLOR_DIALOG_BUTTON (edit->fg_button));
	const GdkRGBA *bg  = gtk_color_dialog_button_get_rgba (GTK_COLOR_DIALOG_BUTTON (edit->bg_button));

	ColorSchemeListDialog *list_dialog = (ColorSchemeListDialog *) edit->list_dialog;
	if (list_dialog) {
		/* Update working copy only; list Apply/OK will save */
		color_scheme_t *dest = &list_dialog->working_schemes[idx];
		strlcpy (dest->name, name, sizeof (dest->name));
		snprintf (dest->action, sizeof (dest->action), "color_scheme.%d", idx);
		dest->foreground = *fg;
		dest->background = *bg;
		refresh_color_scheme_list (list_dialog);
	} else {
		strlcpy (terms.color_schemes[idx].name, name, sizeof (terms.color_schemes[idx].name));
		snprintf (terms.color_schemes[idx].action, sizeof (terms.color_schemes[idx].action), "color_scheme.%d", idx);
		terms.color_schemes[idx].foreground = *fg;
		terms.color_schemes[idx].background = *bg;
		zterm_save_config ();
		rebuild_menus ();
	}
	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	free (edit);
}

static void show_color_scheme_edit_dialog (int scheme_index, long int parent_window, ColorSchemeListDialog *list_dialog)
{
	ColorSchemeEditDialog *edit = g_new0 (ColorSchemeEditDialog, 1);
	edit->scheme_index			= scheme_index;
	edit->parent_window			= parent_window;
	edit->list_dialog			= list_dialog;

	const color_scheme_t *src = list_dialog ? list_dialog->working_schemes : terms.color_schemes;
	GdkRGBA				  foreground, background;
	if (src[scheme_index].name[0]) {
		foreground = src[scheme_index].foreground;
		background = src[scheme_index].background;
		strlcpy (edit->original_name, src[scheme_index].name, sizeof (edit->original_name));
		edit->is_new_scheme = false;
	} else {
		gdk_rgba_parse (&foreground, "#ffffff");
		gdk_rgba_parse (&background, "#000000");
		edit->original_name[0] = '\0';
		edit->is_new_scheme	   = true;
	}
	edit->original_fg = foreground;
	edit->original_bg = background;

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), (scheme_index < MAX_COLOR_SCHEMES && src[scheme_index].name[0])
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
	gtk_editable_set_width_chars (GTK_EDITABLE (edit->name_entry), 24);
	gtk_editable_set_text (GTK_EDITABLE (edit->name_entry), src[scheme_index].name[0] ? src[scheme_index].name : "");
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

	gtk_window_set_default_size (GTK_WINDOW (dialog), 520, 320);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void color_scheme_add_clicked (GtkButton *button, gpointer user_data)
{
	ColorSchemeListDialog *list_dialog = (ColorSchemeListDialog *) user_data;
	int					   idx		   = -1;
	for (int i = 0; i < MAX_COLOR_SCHEMES; i++) {
		if (!list_dialog->working_schemes[i].name[0]) {
			idx = i;
			break;
		}
	}
	if (idx == -1)
		return;
	show_color_scheme_edit_dialog (idx, list_dialog->window_n, list_dialog);
}

static void color_scheme_edit_clicked (GtkButton *button, gpointer user_data)
{
	int					   scheme_index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "scheme_index"));
	ColorSchemeListDialog *list_dialog	= (ColorSchemeListDialog *) user_data;
	show_color_scheme_edit_dialog (scheme_index, list_dialog->window_n, list_dialog);
}

static void color_scheme_delete_clicked (GtkButton *button, gpointer user_data)
{
	int					   scheme_index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "scheme_index"));
	ColorSchemeListDialog *list_dialog	= (ColorSchemeListDialog *) user_data;
	color_scheme_t		  *w			= list_dialog->working_schemes;

	memset (&w[scheme_index], 0, sizeof (color_scheme_t));
	for (int i = scheme_index; i < MAX_COLOR_SCHEMES - 1; i++) {
		w[i] = w[i + 1];
		if (w[i].name[0])
			snprintf (w[i].action, sizeof (w[i].action), "color_scheme.%d", i);
	}
	memset (&w[MAX_COLOR_SCHEMES - 1], 0, sizeof (color_scheme_t));
	refresh_color_scheme_list (list_dialog);
}

static void refresh_color_scheme_list (ColorSchemeListDialog *list_dialog)
{
	GtkWidget *child = gtk_widget_get_first_child (list_dialog->list_box);
	while (child) {
		GtkWidget *next = gtk_widget_get_next_sibling (child);
		gtk_list_box_remove (GTK_LIST_BOX (list_dialog->list_box), child);
		child = next;
	}

	const color_scheme_t *w = list_dialog->working_schemes;
	for (int i = 0; i < MAX_COLOR_SCHEMES && w[i].name[0]; i++) {
		GtkWidget *row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_widget_set_margin_start (row_box, 6);
		gtk_widget_set_margin_end (row_box, 6);
		gtk_widget_set_margin_top (row_box, 3);
		gtk_widget_set_margin_bottom (row_box, 3);

		GtkWidget *preview = gtk_drawing_area_new ();
		gtk_widget_set_size_request (preview, 60, 24);
		gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (preview),
										(GtkDrawingAreaDrawFunc) (void (*) (void)) gtk_widget_get_first_child, NULL, NULL);

		char css_name[64];
		snprintf (css_name, sizeof (css_name), "color_preview%d", i);
		gtk_widget_set_name (preview, css_name);

		char			css_str[256];
		GtkCssProvider *provider = gtk_css_provider_new ();
		snprintf (css_str, sizeof (css_str), "#%s { background-color: %s; border: 1px solid %s; }", css_name,
				  gdk_rgba_to_string (&w[i].background), gdk_rgba_to_string (&w[i].foreground));
		gtk_css_provider_load_from_string (provider, css_str);
		gtk_style_context_add_provider_for_display (gdk_display_get_default (), GTK_STYLE_PROVIDER (provider),
													GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref (provider);

		gtk_box_append (GTK_BOX (row_box), preview);

		GtkWidget *name_label = gtk_label_new (w[i].name);
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

static void color_scheme_commit (void *ctx)
{
	ColorSchemeListDialog *list_dialog = (ColorSchemeListDialog *) ctx;
	memcpy (terms.color_schemes, list_dialog->working_schemes, sizeof (terms.color_schemes));
}

static void color_scheme_snapshot (void *ctx)
{
	ColorSchemeListDialog *list_dialog = (ColorSchemeListDialog *) ctx;
	memcpy (list_dialog->original_schemes, list_dialog->working_schemes, sizeof (list_dialog->original_schemes));
}

static void color_scheme_restore_config (void *ctx)
{
	ColorSchemeListDialog *list_dialog = (ColorSchemeListDialog *) ctx;
	memcpy (terms.color_schemes, list_dialog->original_schemes, sizeof (terms.color_schemes));
}

static void color_scheme_restore_working (void *ctx)
{
	ColorSchemeListDialog *list_dialog = (ColorSchemeListDialog *) ctx;
	memcpy (list_dialog->working_schemes, list_dialog->original_schemes, sizeof (list_dialog->working_schemes));
}

static void color_scheme_refresh_ui (void *ctx)
{
	refresh_color_scheme_list ((ColorSchemeListDialog *) ctx);
}

static void color_scheme_destroy (void *ctx)
{
	ColorSchemeListDialog *list_dialog = (ColorSchemeListDialog *) ctx;
	GtkWidget			  *dialog	   = list_dialog->dialog;
	free (list_dialog);
	gtk_window_destroy (GTK_WINDOW (dialog));
}

static void color_scheme_after_commit (void *ctx)
{
	(void) ctx;
	rebuild_menus ();
}

static void show_color_scheme_editor (GtkButton *button, gpointer user_data)
{
	PrefsDialog			  *prefs	   = (PrefsDialog *) user_data;
	ColorSchemeListDialog *list_dialog = g_new0 (ColorSchemeListDialog, 1);
	list_dialog->window_n			   = prefs->window_n;
	memcpy (list_dialog->working_schemes, terms.color_schemes, sizeof (terms.color_schemes));
	memcpy (list_dialog->original_schemes, terms.color_schemes, sizeof (terms.color_schemes));
	list_dialog->ops.ctx			 = list_dialog;
	list_dialog->ops.commit			 = color_scheme_commit;
	list_dialog->ops.snapshot		 = color_scheme_snapshot;
	list_dialog->ops.restore_config	 = color_scheme_restore_config;
	list_dialog->ops.restore_working = color_scheme_restore_working;
	list_dialog->ops.refresh_ui		 = color_scheme_refresh_ui;
	list_dialog->ops.destroy		 = color_scheme_destroy;
	list_dialog->ops.after_commit	 = color_scheme_after_commit;

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
	gtk_widget_set_size_request (scrolled, 420, 260);
	gtk_box_append (GTK_BOX (main_box), scrolled);

	list_dialog->list_box = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_dialog->list_box), GTK_SELECTION_NONE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list_dialog->list_box);

	refresh_color_scheme_list (list_dialog);

	/* Buttons: Add, then Reset | Cancel | Apply | OK */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *add_btn	  = gtk_button_new_with_mnemonic ("_Add");
	GtkWidget *reset_btn  = gtk_button_new_with_mnemonic ("_Reset");
	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *apply_btn  = gtk_button_new_with_mnemonic ("_Apply");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), add_btn);
	gtk_box_append (GTK_BOX (button_box), reset_btn);
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), apply_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	g_signal_connect (add_btn, "clicked", G_CALLBACK (color_scheme_add_clicked), list_dialog);
	g_signal_connect (reset_btn, "clicked", G_CALLBACK (list_settings_reset), &list_dialog->ops);
	g_signal_connect (cancel_btn, "clicked", G_CALLBACK (list_settings_cancel), &list_dialog->ops);
	g_signal_connect (apply_btn, "clicked", G_CALLBACK (list_settings_apply), &list_dialog->ops);
	g_signal_connect (ok_btn, "clicked", G_CALLBACK (list_settings_ok), &list_dialog->ops);
	debugf ("list_dialog: %p, ops: %p", list_dialog, &list_dialog->ops);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 540, 360);
	gtk_window_present (GTK_WINDOW (dialog));
}

/* ==================== Color Overrides Editor ==================== */

typedef struct {
	GtkWidget *dialog;
	GtkWidget *index_spin;
	GtkWidget *color_button;
	int		   override_index;
	long int   parent_window;
	void	  *list_dialog; /* ColorOverrideListDialog* when from list: OK updates working_overrides only */
} ColorOverrideEditDialog;

typedef struct {
	GtkWidget		 *dialog;
	GtkWidget		 *list_box;
	long int		  window_n;
	color_override_t *working_overrides;
	color_override_t *original_overrides;
	ListSettingsOps	  ops;
} ColorOverrideListDialog;

static void refresh_color_override_list (ColorOverrideListDialog *list_dialog);

static void color_override_edit_cancel (ColorOverrideEditDialog *edit)
{
	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	free (edit);
}

/* Apply override list to colors[] and all terminals (used on commit/restore_config). */
static void apply_color_overrides_to_terminals (color_override_t *list)
{
	for (color_override_t *cur = list; cur; cur = cur->next)
		colors[cur->index] = cur->color;
	for (int i = 0; i < terms.n_active; i++) {
		if (terms.active[i].term)
			term_config (terms.active[i].term, terms.active[i].window);
	}
}

static void color_override_edit_ok (ColorOverrideEditDialog *edit)
{
	int			   index = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (edit->index_spin));
	const GdkRGBA *color = gtk_color_dialog_button_get_rgba (GTK_COLOR_DIALOG_BUTTON (edit->color_button));

	if (index < 0 || index >= 256) {
		gtk_window_destroy (GTK_WINDOW (edit->dialog));
		free (edit);
		return;
	}

	ColorOverrideListDialog *list_dialog = (ColorOverrideListDialog *) edit->list_dialog;
	if (list_dialog) {
		color_override_t **p	 = &list_dialog->working_overrides;
		color_override_t  *found = NULL;
		for (color_override_t *cur = list_dialog->working_overrides; cur; cur = cur->next) {
			if (cur->index == index) {
				found = cur;
				break;
			}
			p = &cur->next;
		}
		if (found) {
			found->color = *color;
		} else {
			color_override_t *override = calloc (1, sizeof (color_override_t));
			override->index			   = index;
			override->color			   = *color;
			override->next			   = *p;
			*p						   = override;
		}
		refresh_color_override_list (list_dialog);
	} else {
		colors[index]			= *color;
		color_override_t *found = NULL;
		for (color_override_t *cur = terms.color_overrides; cur; cur = cur->next) {
			if (cur->index == index) {
				found = cur;
				break;
			}
		}
		if (found)
			found->color = *color;
		else {
			color_override_t *override = calloc (1, sizeof (color_override_t));
			override->index			   = index;
			override->color			   = *color;
			override->next			   = terms.color_overrides;
			terms.color_overrides	   = override;
		}
		apply_color_overrides_to_terminals (terms.color_overrides);
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
	edit->list_dialog			  = list_dialog;

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

	gtk_window_set_default_size (GTK_WINDOW (dialog), 480, 280);
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

	for (color_override_t *cur = list_dialog->working_overrides; cur; cur = cur->next) {
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

	color_override_t **prev = &list_dialog->working_overrides;
	for (color_override_t *cur = list_dialog->working_overrides; cur; cur = cur->next) {
		if (cur->index == override_index) {
			*prev = cur->next;
			free (cur);
			break;
		}
		prev = &cur->next;
	}
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

	for (color_override_t *cur = list_dialog->working_overrides; cur; cur = cur->next) {
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
		snprintf (css_name, sizeof (css_name), "color_override%d", cur->index);
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

static void color_override_commit (void *ctx)
{
	ColorOverrideListDialog *list_dialog = (ColorOverrideListDialog *) ctx;
	color_override_list_free (terms.color_overrides);
	terms.color_overrides = color_override_list_clone (list_dialog->working_overrides);
	apply_color_overrides_to_terminals (terms.color_overrides);
}

static void color_override_snapshot (void *ctx)
{
	ColorOverrideListDialog *list_dialog = (ColorOverrideListDialog *) ctx;
	color_override_list_free (list_dialog->original_overrides);
	list_dialog->original_overrides = color_override_list_clone (list_dialog->working_overrides);
}

static void color_override_restore_config (void *ctx)
{
	ColorOverrideListDialog *list_dialog = (ColorOverrideListDialog *) ctx;
	color_override_list_free (terms.color_overrides);
	terms.color_overrides = color_override_list_clone (list_dialog->original_overrides);
	apply_color_overrides_to_terminals (terms.color_overrides);
}

static void color_override_restore_working (void *ctx)
{
	ColorOverrideListDialog *list_dialog = (ColorOverrideListDialog *) ctx;
	color_override_list_free (list_dialog->working_overrides);
	list_dialog->working_overrides = color_override_list_clone (list_dialog->original_overrides);
}

static void color_override_refresh_ui (void *ctx)
{
	refresh_color_override_list ((ColorOverrideListDialog *) ctx);
}

static void color_override_destroy (void *ctx)
{
	ColorOverrideListDialog *list_dialog = (ColorOverrideListDialog *) ctx;
	GtkWidget				*dialog		 = list_dialog->dialog;
	color_override_list_free (list_dialog->working_overrides);
	color_override_list_free (list_dialog->original_overrides);
	free (list_dialog);
	gtk_window_destroy (GTK_WINDOW (dialog));
}

static void show_color_override_editor (GtkButton *button, gpointer user_data)
{
	PrefsDialog				*prefs		 = (PrefsDialog *) user_data;
	ColorOverrideListDialog *list_dialog = g_new0 (ColorOverrideListDialog, 1);
	list_dialog->window_n				 = prefs->window_n;
	list_dialog->working_overrides		 = color_override_list_clone (terms.color_overrides);
	list_dialog->original_overrides		 = color_override_list_clone (terms.color_overrides);
	list_dialog->ops.ctx				 = list_dialog;
	list_dialog->ops.commit				 = color_override_commit;
	list_dialog->ops.snapshot			 = color_override_snapshot;
	list_dialog->ops.restore_config		 = color_override_restore_config;
	list_dialog->ops.restore_working	 = color_override_restore_working;
	list_dialog->ops.refresh_ui			 = color_override_refresh_ui;
	list_dialog->ops.destroy			 = color_override_destroy;
	list_dialog->ops.after_commit		 = NULL;

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
	gtk_widget_set_size_request (scrolled, 420, 260);
	gtk_box_append (GTK_BOX (main_box), scrolled);

	list_dialog->list_box = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_dialog->list_box), GTK_SELECTION_NONE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list_dialog->list_box);

	refresh_color_override_list (list_dialog);

	/* Buttons: Add, then Reset | Cancel | Apply | OK */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *add_btn	  = gtk_button_new_with_mnemonic ("_Add");
	GtkWidget *reset_btn  = gtk_button_new_with_mnemonic ("_Reset");
	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *apply_btn  = gtk_button_new_with_mnemonic ("_Apply");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), add_btn);
	gtk_box_append (GTK_BOX (button_box), reset_btn);
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), apply_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	g_signal_connect (add_btn, "clicked", G_CALLBACK (color_override_add_clicked), list_dialog);
	g_signal_connect (reset_btn, "clicked", G_CALLBACK (list_settings_reset), &list_dialog->ops);
	g_signal_connect (cancel_btn, "clicked", G_CALLBACK (list_settings_cancel), &list_dialog->ops);
	g_signal_connect (apply_btn, "clicked", G_CALLBACK (list_settings_apply), &list_dialog->ops);
	g_signal_connect (ok_btn, "clicked", G_CALLBACK (list_settings_ok), &list_dialog->ops);
	debugf ("list_dialog: %p, ops: %p", list_dialog, &list_dialog->ops);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 540, 400);
	gtk_window_present (GTK_WINDOW (dialog));
}

/* ==================== Global Environment Editor ==================== */
/* Same format as per-terminal: list of "KEY=value" or "!VAR" strings */

typedef struct {
	GtkWidget	   *dialog;
	GtkWidget	   *list_box;
	GList		   *working_env; /* GList of gchar* */
	GList		   *original_env;
	ListSettingsOps ops;
} EnvListDialog;

typedef struct {
	GtkWidget	  *dialog;
	GtkWidget	  *name_entry;
	GtkWidget	  *value_entry;
	GtkWidget	  *unset_check;
	char		  *edit_string; /* string we're replacing, or NULL for add */
	EnvListDialog *list_dialog;
} EnvEditDialog;

static void refresh_env_list (EnvListDialog *list_dialog);

static char **env_list_to_strv (GList *list)
{
	guint  n  = g_list_length (list);
	char **sv = g_new (char *, n + 1);
	guint  i  = 0;
	for (GList *it = list; it; it = it->next)
		sv[i++] = g_strdup ((const char *) it->data);
	sv[i] = NULL;
	return sv;
}

static void env_edit_dialog_free (EnvEditDialog *edit)
{
	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	/* edit_string is not freed here: OK removes it from list and frees; Cancel leaves it in list */
	g_free (edit);
}

static void env_edit_ok (EnvEditDialog *edit)
{
	char *name = g_strdup (gtk_editable_get_text (GTK_EDITABLE (edit->name_entry)));
	g_strstrip (name);
	if (!name || name[0] == '\0') {
		g_free (name);
		GtkAlertDialog *alert = gtk_alert_dialog_new ("Name cannot be empty.");
		gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
		g_object_unref (alert);
		return;
	}
	bool		   unset	   = gtk_check_button_get_active (GTK_CHECK_BUTTON (edit->unset_check));
	EnvListDialog *list_dialog = edit->list_dialog;

	if (edit->edit_string) {
		list_dialog->working_env = g_list_remove (list_dialog->working_env, edit->edit_string);
		g_free (edit->edit_string);
	}

	const char *value		 = gtk_editable_get_text (GTK_EDITABLE (edit->value_entry));
	char	   *entry		 = unset ? g_strdup_printf ("!%s", name) : g_strdup_printf ("%s=%s", name, value ? value : "");
	list_dialog->working_env = g_list_append (list_dialog->working_env, entry);
	g_free (name);
	refresh_env_list (list_dialog);
	env_edit_dialog_free (edit);
}

static void env_edit_cancel (EnvEditDialog *edit)
{
	env_edit_dialog_free (edit);
}

static void show_env_edit_dialog (EnvListDialog *list_dialog, const char *edit_string)
{
	EnvEditDialog *edit = g_new0 (EnvEditDialog, 1);
	edit->list_dialog	= list_dialog;
	edit->edit_string	= (char *) edit_string; /* pointer into list, freed in env_edit_ok after remove */

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), edit_string ? "Edit Environment Variable" : "New Environment Variable");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (list_dialog->dialog));
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

	edit->name_entry = gtk_entry_new ();
	gtk_editable_set_width_chars (GTK_EDITABLE (edit->name_entry), 28);
	gtk_widget_set_hexpand (edit->name_entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), create_label ("Name:"), 0, row, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), edit->name_entry, 1, row++, 1, 1);

	edit->value_entry = gtk_entry_new ();
	gtk_editable_set_width_chars (GTK_EDITABLE (edit->value_entry), 28);
	gtk_widget_set_hexpand (edit->value_entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), create_label ("Value:"), 0, row, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), edit->value_entry, 1, row++, 1, 1);

	edit->unset_check = gtk_check_button_new_with_label ("Unset (remove from spawn env)");
	gtk_grid_attach (GTK_GRID (grid), edit->unset_check, 0, row++, 2, 1);

	if (edit_string) {
		if (edit_string[0] == '!' && edit_string[1] != '\0') {
			gtk_editable_set_text (GTK_EDITABLE (edit->name_entry), edit_string + 1);
			gtk_editable_set_text (GTK_EDITABLE (edit->value_entry), "");
			gtk_check_button_set_active (GTK_CHECK_BUTTON (edit->unset_check), TRUE);
		} else {
			const char *eq = strchr (edit_string, '=');
			if (eq && eq > edit_string) {
				char *name = g_strndup (edit_string, (gsize) (eq - edit_string));
				gtk_editable_set_text (GTK_EDITABLE (edit->name_entry), name);
				gtk_editable_set_text (GTK_EDITABLE (edit->value_entry), eq + 1);
				g_free (name);
				gtk_check_button_set_active (GTK_CHECK_BUTTON (edit->unset_check), FALSE);
			}
		}
	}

	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	g_signal_connect_swapped (cancel_btn, "clicked", G_CALLBACK (env_edit_cancel), edit);
	g_signal_connect_swapped (ok_btn, "clicked", G_CALLBACK (env_edit_ok), edit);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 420, 200);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void env_add_clicked (GtkButton *button, gpointer user_data)
{
	show_env_edit_dialog ((EnvListDialog *) user_data, NULL);
}

static void env_edit_clicked (GtkButton *button, gpointer user_data)
{
	const char	  *s		   = (const char *) g_object_get_data (G_OBJECT (button), "env_string");
	EnvListDialog *list_dialog = (EnvListDialog *) user_data;
	show_env_edit_dialog (list_dialog, s);
}

static void env_delete_clicked (GtkButton *button, gpointer user_data)
{
	EnvListDialog *list_dialog = (EnvListDialog *) user_data;
	char		  *s		   = (char *) g_object_get_data (G_OBJECT (button), "env_string");
	list_dialog->working_env   = g_list_remove (list_dialog->working_env, s);
	g_free (s);
	refresh_env_list (list_dialog);
}

static void refresh_env_list (EnvListDialog *list_dialog)
{
	GtkWidget *child = gtk_widget_get_first_child (list_dialog->list_box);
	while (child) {
		GtkWidget *next = gtk_widget_get_next_sibling (child);
		gtk_list_box_remove (GTK_LIST_BOX (list_dialog->list_box), child);
		child = next;
	}

	for (GList *it = list_dialog->working_env; it; it = it->next) {
		const char *s		= (const char *) it->data;
		GtkWidget  *row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_widget_set_margin_start (row_box, 6);
		gtk_widget_set_margin_end (row_box, 6);
		gtk_widget_set_margin_top (row_box, 3);
		gtk_widget_set_margin_bottom (row_box, 3);

		GtkWidget *label = gtk_label_new (s);
		gtk_widget_set_halign (label, GTK_ALIGN_START);
		gtk_widget_set_hexpand (label, TRUE);
		gtk_label_set_selectable (GTK_LABEL (label), TRUE);
		gtk_box_append (GTK_BOX (row_box), label);

		GtkWidget *edit_btn = gtk_button_new_from_icon_name ("document-edit-symbolic");
		gtk_widget_set_tooltip_text (edit_btn, "Edit");
		g_object_set_data (G_OBJECT (edit_btn), "env_string", (gpointer) s);
		g_signal_connect (edit_btn, "clicked", G_CALLBACK (env_edit_clicked), list_dialog);
		gtk_box_append (GTK_BOX (row_box), edit_btn);

		GtkWidget *delete_btn = gtk_button_new_from_icon_name ("edit-delete-symbolic");
		gtk_widget_set_tooltip_text (delete_btn, "Delete");
		g_object_set_data (G_OBJECT (delete_btn), "env_string", (gpointer) s);
		g_signal_connect (delete_btn, "clicked", G_CALLBACK (env_delete_clicked), list_dialog);
		gtk_box_append (GTK_BOX (row_box), delete_btn);

		gtk_list_box_append (GTK_LIST_BOX (list_dialog->list_box), row_box);
	}
}

static void env_commit (void *ctx)
{
	EnvListDialog *list_dialog = (EnvListDialog *) ctx;
	char		 **sv		   = env_list_to_strv (list_dialog->working_env);
	zterm_set_global_env ((const char **) sv);
	g_strfreev (sv);
}

static void env_snapshot (void *ctx)
{
	EnvListDialog *list_dialog = (EnvListDialog *) ctx;
	g_list_free_full (list_dialog->original_env, g_free);
	list_dialog->original_env = g_list_copy_deep (list_dialog->working_env, (GCopyFunc) g_strdup, g_free);
}

static void env_restore_config (void *ctx)
{
	EnvListDialog *list_dialog = (EnvListDialog *) ctx;
	char		 **sv		   = env_list_to_strv (list_dialog->original_env);
	zterm_set_global_env ((const char **) sv);
	g_strfreev (sv);
}

static void env_restore_working (void *ctx)
{
	EnvListDialog *list_dialog = (EnvListDialog *) ctx;
	g_list_free_full (list_dialog->working_env, g_free);
	list_dialog->working_env = g_list_copy_deep (list_dialog->original_env, (GCopyFunc) g_strdup, g_free);
}

static void env_refresh_ui (void *ctx)
{
	refresh_env_list ((EnvListDialog *) ctx);
}

static void env_destroy (void *ctx)
{
	EnvListDialog *list_dialog = (EnvListDialog *) ctx;
	GtkWidget	  *dialog	   = list_dialog->dialog;
	g_list_free_full (list_dialog->working_env, g_free);
	g_list_free_full (list_dialog->original_env, g_free);
	g_free (list_dialog);
	gtk_window_destroy (GTK_WINDOW (dialog));
}

static void show_env_editor (GtkButton *button, gpointer user_data)
{
	PrefsDialog	  *prefs	   = (PrefsDialog *) user_data;
	EnvListDialog *list_dialog = g_new0 (EnvListDialog, 1);
	list_dialog->working_env   = NULL;
	list_dialog->original_env  = NULL;
	if (terms.env) {
		for (int i = 0; terms.env[i] != NULL; i++)
			list_dialog->working_env = g_list_append (list_dialog->working_env, g_strdup (terms.env[i]));
	}
	list_dialog->original_env		 = g_list_copy_deep (list_dialog->working_env, (GCopyFunc) g_strdup, g_free);
	list_dialog->ops.ctx			 = list_dialog;
	list_dialog->ops.commit			 = env_commit;
	list_dialog->ops.snapshot		 = env_snapshot;
	list_dialog->ops.restore_config	 = env_restore_config;
	list_dialog->ops.restore_working = env_restore_working;
	list_dialog->ops.refresh_ui		 = env_refresh_ui;
	list_dialog->ops.destroy		 = env_destroy;
	list_dialog->ops.after_commit	 = NULL;

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), "Global Environment");
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

	GtkWidget *info_label = gtk_label_new ("Set or unset environment variables for all new terminal spawns. Set: name=value. "
										   "Unset: variable is removed from spawn env.");
	gtk_label_set_wrap (GTK_LABEL (info_label), TRUE);
	gtk_box_append (GTK_BOX (main_box), info_label);

	GtkWidget *scrolled = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand (scrolled, TRUE);
	gtk_widget_set_size_request (scrolled, 420, 260);
	gtk_box_append (GTK_BOX (main_box), scrolled);

	list_dialog->list_box = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_dialog->list_box), GTK_SELECTION_NONE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list_dialog->list_box);

	refresh_env_list (list_dialog);

	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *add_btn	  = gtk_button_new_with_mnemonic ("_Add");
	GtkWidget *reset_btn  = gtk_button_new_with_mnemonic ("_Reset");
	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *apply_btn  = gtk_button_new_with_mnemonic ("_Apply");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), add_btn);
	gtk_box_append (GTK_BOX (button_box), reset_btn);
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), apply_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	g_signal_connect (add_btn, "clicked", G_CALLBACK (env_add_clicked), list_dialog);
	g_signal_connect (reset_btn, "clicked", G_CALLBACK (list_settings_reset), &list_dialog->ops);
	g_signal_connect (cancel_btn, "clicked", G_CALLBACK (list_settings_cancel), &list_dialog->ops);
	g_signal_connect (apply_btn, "clicked", G_CALLBACK (list_settings_apply), &list_dialog->ops);
	g_signal_connect (ok_btn, "clicked", G_CALLBACK (list_settings_ok), &list_dialog->ops);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 540, 400);
	gtk_window_present (GTK_WINDOW (dialog));
}

/* ==================== Ignored Key Modifiers (bind_ignore) Editor ==================== */
/* List of modifier states to ignore when matching key/mouse bindings (e.g. Mod2 for NumLock). */

typedef struct {
	GtkWidget	   *dialog;
	GtkWidget	   *list_box;
	GList		   *working_list; /* GList of gchar* (state strings like "Mod2") */
	GList		   *original_list;
	ListSettingsOps ops;
} BindIgnoreListDialog;

typedef struct {
	GtkWidget			 *dialog;
	GtkWidget			 *state_entry;
	char				 *edit_string; /* current row string we're replacing, or NULL for add */
	BindIgnoreListDialog *list_dialog;
} BindIgnoreEditDialog;

static void refresh_bind_ignore_list (BindIgnoreListDialog *list_dialog);

static bool bind_ignore_parse_state (const char *state_input, guint *out_state)
{
	guint key = 0, state = 0;
	gtk_accelerator_parse (state_input, &key, &state);
	if (key != 0 || state == 0)
		return false;
	*out_state = state;
	return true;
}

static void bind_ignore_edit_dialog_free (BindIgnoreEditDialog *edit)
{
	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	g_free (edit->edit_string);
	g_free (edit);
}

static void bind_ignore_edit_ok (BindIgnoreEditDialog *edit)
{
	char *input = g_strstrip (g_strdup (gtk_editable_get_text (GTK_EDITABLE (edit->state_entry))));
	if (!input || input[0] == '\0') {
		g_free (input);
		GtkAlertDialog *alert = gtk_alert_dialog_new ("Enter a modifier state (e.g. Mod2, Control+Mod1).");
		gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
		g_object_unref (alert);
		return;
	}
	guint state;
	if (!bind_ignore_parse_state (input, &state)) {
		g_free (input);
		GtkAlertDialog *alert =
		  gtk_alert_dialog_new ("Invalid: value must be modifier state only (e.g. Mod2 or Control+Mod1), no key.");
		gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
		g_object_unref (alert);
		return;
	}
	BindIgnoreListDialog *list_dialog = edit->list_dialog;
	if (edit->edit_string) {
		list_dialog->working_list = g_list_remove (list_dialog->working_list, edit->edit_string);
		g_free (edit->edit_string);
		edit->edit_string = NULL;
	}
	list_dialog->working_list = g_list_append (list_dialog->working_list, input);
	refresh_bind_ignore_list (list_dialog);
	bind_ignore_edit_dialog_free (edit);
}

static void bind_ignore_edit_cancel (BindIgnoreEditDialog *edit)
{
	bind_ignore_edit_dialog_free (edit);
}

static void show_bind_ignore_edit_dialog (BindIgnoreListDialog *list_dialog, const char *edit_string)
{
	BindIgnoreEditDialog *edit = g_new0 (BindIgnoreEditDialog, 1);
	edit->list_dialog		   = list_dialog;
	edit->edit_string		   = edit_string ? g_strdup (edit_string) : NULL;

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), edit_string ? "Edit Ignored Modifiers" : "Add Ignored Modifiers");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (list_dialog->dialog));
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

	edit->state_entry = gtk_entry_new ();
	gtk_editable_set_width_chars (GTK_EDITABLE (edit->state_entry), 28);
	gtk_widget_set_hexpand (edit->state_entry, TRUE);
	gtk_entry_set_placeholder_text (GTK_ENTRY (edit->state_entry), "e.g. Mod2 or Control+Mod1");
	gtk_grid_attach (GTK_GRID (grid), create_label ("Modifier state:"), 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), edit->state_entry, 1, 0, 1, 1);
	if (edit_string)
		gtk_editable_set_text (GTK_EDITABLE (edit->state_entry), edit_string);

	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	g_signal_connect_swapped (cancel_btn, "clicked", G_CALLBACK (bind_ignore_edit_cancel), edit);
	g_signal_connect_swapped (ok_btn, "clicked", G_CALLBACK (bind_ignore_edit_ok), edit);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 420, 120);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void bind_ignore_add_clicked (GtkButton *button, gpointer user_data)
{
	show_bind_ignore_edit_dialog ((BindIgnoreListDialog *) user_data, NULL);
}

static void bind_ignore_edit_clicked (GtkButton *button, gpointer user_data)
{
	const char			 *s			  = (const char *) g_object_get_data (G_OBJECT (button), "bind_ignore_string");
	BindIgnoreListDialog *list_dialog = (BindIgnoreListDialog *) user_data;
	show_bind_ignore_edit_dialog (list_dialog, s);
}

static void bind_ignore_delete_clicked (GtkButton *button, gpointer user_data)
{
	BindIgnoreListDialog *list_dialog = (BindIgnoreListDialog *) user_data;
	char				 *s			  = (char *) g_object_get_data (G_OBJECT (button), "bind_ignore_string");
	list_dialog->working_list		  = g_list_remove (list_dialog->working_list, s);
	g_free (s);
	refresh_bind_ignore_list (list_dialog);
}

static void refresh_bind_ignore_list (BindIgnoreListDialog *list_dialog)
{
	GtkWidget *child = gtk_widget_get_first_child (list_dialog->list_box);
	while (child) {
		GtkWidget *next = gtk_widget_get_next_sibling (child);
		gtk_list_box_remove (GTK_LIST_BOX (list_dialog->list_box), child);
		child = next;
	}

	for (GList *it = list_dialog->working_list; it; it = it->next) {
		const char *s		= (const char *) it->data;
		GtkWidget  *row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_widget_set_margin_start (row_box, 6);
		gtk_widget_set_margin_end (row_box, 6);
		gtk_widget_set_margin_top (row_box, 3);
		gtk_widget_set_margin_bottom (row_box, 3);

		GtkWidget *label = gtk_label_new (s);
		gtk_widget_set_halign (label, GTK_ALIGN_START);
		gtk_widget_set_hexpand (label, TRUE);
		gtk_label_set_selectable (GTK_LABEL (label), TRUE);
		gtk_box_append (GTK_BOX (row_box), label);

		GtkWidget *edit_btn = gtk_button_new_from_icon_name ("document-edit-symbolic");
		gtk_widget_set_tooltip_text (edit_btn, "Edit");
		g_object_set_data (G_OBJECT (edit_btn), "bind_ignore_string", (gpointer) s);
		g_signal_connect (edit_btn, "clicked", G_CALLBACK (bind_ignore_edit_clicked), list_dialog);
		gtk_box_append (GTK_BOX (row_box), edit_btn);

		GtkWidget *delete_btn = gtk_button_new_from_icon_name ("edit-delete-symbolic");
		gtk_widget_set_tooltip_text (delete_btn, "Delete");
		g_object_set_data (G_OBJECT (delete_btn), "bind_ignore_string", (gpointer) s);
		g_signal_connect (delete_btn, "clicked", G_CALLBACK (bind_ignore_delete_clicked), list_dialog);
		gtk_box_append (GTK_BOX (row_box), delete_btn);

		gtk_list_box_append (GTK_LIST_BOX (list_dialog->list_box), row_box);
	}
}

static void bind_ignore_commit (void *ctx)
{
	BindIgnoreListDialog *list_dialog = (BindIgnoreListDialog *) ctx;
	/* Free existing terms.ignores */
	while (terms.ignores) {
		bind_ignore_t *next = terms.ignores->next;
		free (terms.ignores);
		terms.ignores = next;
	}
	/* Build new list from working_list (in reverse so order matches config) */
	bind_ignore_t *head = NULL;
	for (GList *it = g_list_last (list_dialog->working_list); it; it = it->prev) {
		guint state;
		if (!bind_ignore_parse_state ((const char *) it->data, &state))
			continue;
		bind_ignore_t *ignore = calloc (1, sizeof (bind_ignore_t));
		ignore->state		  = state;
		ignore->next		  = head;
		head				  = ignore;
	}
	terms.ignores = head;
	zterm_apply_bind_ignores ();
}

static void bind_ignore_snapshot (void *ctx)
{
	BindIgnoreListDialog *list_dialog = (BindIgnoreListDialog *) ctx;
	g_list_free_full (list_dialog->original_list, g_free);
	list_dialog->original_list = g_list_copy_deep (list_dialog->working_list, (GCopyFunc) g_strdup, g_free);
}

static void bind_ignore_restore_config (void *ctx)
{
	BindIgnoreListDialog *list_dialog = (BindIgnoreListDialog *) ctx;
	/* Restore terms.ignores from original_list */
	while (terms.ignores) {
		bind_ignore_t *next = terms.ignores->next;
		free (terms.ignores);
		terms.ignores = next;
	}
	bind_ignore_t *head = NULL;
	for (GList *it = g_list_last (list_dialog->original_list); it; it = it->prev) {
		guint state;
		if (!bind_ignore_parse_state ((const char *) it->data, &state))
			continue;
		bind_ignore_t *ignore = calloc (1, sizeof (bind_ignore_t));
		ignore->state		  = state;
		ignore->next		  = head;
		head				  = ignore;
	}
	terms.ignores = head;
	zterm_apply_bind_ignores ();
}

static void bind_ignore_restore_working (void *ctx)
{
	BindIgnoreListDialog *list_dialog = (BindIgnoreListDialog *) ctx;
	g_list_free_full (list_dialog->working_list, g_free);
	list_dialog->working_list = g_list_copy_deep (list_dialog->original_list, (GCopyFunc) g_strdup, g_free);
}

static void bind_ignore_refresh_ui (void *ctx)
{
	refresh_bind_ignore_list ((BindIgnoreListDialog *) ctx);
}

static void bind_ignore_destroy (void *ctx)
{
	BindIgnoreListDialog *list_dialog = (BindIgnoreListDialog *) ctx;
	GtkWidget			 *dialog	  = list_dialog->dialog;
	g_list_free_full (list_dialog->working_list, g_free);
	g_list_free_full (list_dialog->original_list, g_free);
	g_free (list_dialog);
	gtk_window_destroy (GTK_WINDOW (dialog));
}

static void show_bind_ignore_editor (GtkButton *button, gpointer user_data)
{
	PrefsDialog			 *prefs		  = (PrefsDialog *) user_data;
	BindIgnoreListDialog *list_dialog = g_new0 (BindIgnoreListDialog, 1);
	list_dialog->working_list		  = NULL;
	list_dialog->original_list		  = NULL;
	for (bind_ignore_t *cur = terms.ignores; cur; cur = cur->next) {
		gchar *state_str		  = gtk_accelerator_name (0, cur->state);
		list_dialog->working_list = g_list_append (list_dialog->working_list, state_str);
	}
	list_dialog->original_list		 = g_list_copy_deep (list_dialog->working_list, (GCopyFunc) g_strdup, g_free);
	list_dialog->ops.ctx			 = list_dialog;
	list_dialog->ops.commit			 = bind_ignore_commit;
	list_dialog->ops.snapshot		 = bind_ignore_snapshot;
	list_dialog->ops.restore_config	 = bind_ignore_restore_config;
	list_dialog->ops.restore_working = bind_ignore_restore_working;
	list_dialog->ops.refresh_ui		 = bind_ignore_refresh_ui;
	list_dialog->ops.destroy		 = bind_ignore_destroy;
	list_dialog->ops.after_commit	 = NULL;

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), "Ignored Key Modifiers");
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

	GtkWidget *info_label =
	  gtk_label_new ("Modifier states listed here are ignored when matching key and mouse bindings (e.g. Mod2 for NumLock).");
	gtk_label_set_wrap (GTK_LABEL (info_label), TRUE);
	gtk_box_append (GTK_BOX (main_box), info_label);

	GtkWidget *scrolled = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand (scrolled, TRUE);
	gtk_widget_set_size_request (scrolled, 420, 200);
	gtk_box_append (GTK_BOX (main_box), scrolled);

	list_dialog->list_box = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_dialog->list_box), GTK_SELECTION_NONE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list_dialog->list_box);

	refresh_bind_ignore_list (list_dialog);

	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *add_btn	  = gtk_button_new_with_mnemonic ("_Add");
	GtkWidget *reset_btn  = gtk_button_new_with_mnemonic ("_Reset");
	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *apply_btn  = gtk_button_new_with_mnemonic ("_Apply");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), add_btn);
	gtk_box_append (GTK_BOX (button_box), reset_btn);
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), apply_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	g_signal_connect (add_btn, "clicked", G_CALLBACK (bind_ignore_add_clicked), list_dialog);
	g_signal_connect (reset_btn, "clicked", G_CALLBACK (list_settings_reset), &list_dialog->ops);
	g_signal_connect (cancel_btn, "clicked", G_CALLBACK (list_settings_cancel), &list_dialog->ops);
	g_signal_connect (apply_btn, "clicked", G_CALLBACK (list_settings_apply), &list_dialog->ops);
	g_signal_connect (ok_btn, "clicked", G_CALLBACK (list_settings_ok), &list_dialog->ops);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 480, 340);
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

	gtk_window_set_default_size (GTK_WINDOW (dialog), 460, 220);
	gtk_window_present (GTK_WINDOW (dialog));

	/* Focus the window to receive key events */
	gtk_widget_grab_focus (dialog);
}

/* ==================== Key Bindings Editor ==================== */

static const char *bind_action_names[] = {"SWITCH",	   "CUT",		"CUT_HTML", "PASTE",   "MENU",
										  "NEXT_TERM", "PREV_TERM", "OPEN_URI", "CUT_URI", NULL};

/* Per-row widget refs so we can update visibility/sensitivity when action changes without refreshing the list */
typedef struct {
	GtkWidget *base_spin;
	GtkWidget *configure_btn;
	GtkWidget *max_key_btn;
} KeyBindRowWidgets;

typedef struct KeyBindListDialog_s {
	GtkWidget	   *dialog;
	GtkWidget	   *column_view;
	GListStore	   *store;
	long int		window_n;
	GHashTable	   *bind_row_widgets; /* bind_t* -> KeyBindRowWidgets* (cleared on refresh) */
	bind_t		   *working_keys;	  /* working copy; list dialog edits this */
	bind_t		   *original_keys;	  /* snapshot for Reset/Cancel */
	ListSettingsOps ops;			  /* commit/snapshot/restore/destroy/refresh for generic Apply/OK/Cancel/Reset */
} KeyBindListDialog;

static void refresh_key_bind_list (KeyBindListDialog *list_dialog);
static void show_terminal_config_editor_for_range (GtkWidget *parent_dialog, long int window_n, int range_start, int range_end);

/* GObject wrapper for bind_t to use with GListStore */
#define KEY_BIND_ITEM_TYPE (key_bind_item_get_type ())
G_DECLARE_FINAL_TYPE (KeyBindItem, key_bind_item, KEY, BIND_ITEM, GObject)

enum {
	KEY_BIND_ITEM_PROP_KEY_LABEL = 1,
	KEY_BIND_ITEM_PROP_MAX_KEY_LABEL,
	N_KEY_BIND_ITEM_PROPERTIES
};

struct _KeyBindItem {
	GObject parent_instance;
	bind_t *bind;
	char   *key_label;	   /* accelerator string for Key column */
	char   *max_key_label; /* key name or "—" for Max key column */
};

G_DEFINE_TYPE (KeyBindItem, key_bind_item, G_TYPE_OBJECT)

static void key_bind_item_update_labels (KeyBindItem *item);
static void key_bind_item_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void key_bind_item_finalize (GObject *object);

static void key_bind_item_class_init (KeyBindItemClass *klass)
{
	GObjectClass *obj_class = G_OBJECT_CLASS (klass);
	obj_class->get_property = key_bind_item_get_property;
	obj_class->finalize		= key_bind_item_finalize;
	g_object_class_install_property (
	  obj_class, KEY_BIND_ITEM_PROP_KEY_LABEL,
	  g_param_spec_string ("key-label", "Key label", "Accelerator string for key column", "", G_PARAM_READABLE));
	g_object_class_install_property (
	  obj_class, KEY_BIND_ITEM_PROP_MAX_KEY_LABEL,
	  g_param_spec_string ("max-key-label", "Max key label", "Key name for range end column", "", G_PARAM_READABLE));
}

static void key_bind_item_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	KeyBindItem *item = KEY_BIND_ITEM (object);
	switch (prop_id) {
		case KEY_BIND_ITEM_PROP_KEY_LABEL:
			g_value_set_string (value, item->key_label ? item->key_label : "");
			break;
		case KEY_BIND_ITEM_PROP_MAX_KEY_LABEL:
			g_value_set_string (value, item->max_key_label ? item->max_key_label : "—");
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void key_bind_item_finalize (GObject *object)
{
	KeyBindItem *item = KEY_BIND_ITEM (object);
	g_free (item->key_label);
	g_free (item->max_key_label);
	G_OBJECT_CLASS (key_bind_item_parent_class)->finalize (object);
}

static void key_bind_item_init (KeyBindItem *self)
{
}

static void key_bind_item_update_labels (KeyBindItem *item)
{
	if (!item->bind)
		return;
	g_free (item->key_label);
	item->key_label = gtk_accelerator_name (item->bind->key_min, item->bind->state);
	g_free (item->max_key_label);
	if (item->bind->action == BIND_ACT_SWITCH && item->bind->key_max != item->bind->key_min) {
		const char *name	= gdk_keyval_name (item->bind->key_max);
		item->max_key_label = g_strdup (name ? name : "—");
	} else {
		item->max_key_label = g_strdup ("—");
	}
	g_object_notify (G_OBJECT (item), "key-label");
	g_object_notify (G_OBJECT (item), "max-key-label");
}

static KeyBindItem *key_bind_item_new (bind_t *bind)
{
	KeyBindItem *item = g_object_new (KEY_BIND_ITEM_TYPE, NULL);
	item->bind		  = bind;
	key_bind_item_update_labels (item);
	return item;
}

/* Check for overlapping bindings in list (excluding exclude_bind). Returns false if overlap found (and shows alert). */
static bool key_bind_check_overlap (bind_t *list, bind_t *exclude_bind, guint key_min, guint state, guint key_max,
									GtkWindow *parent_window)
{
	for (bind_t *cur = list; cur; cur = cur->next) {
		if (cur == exclude_bind)
			continue;
		if (cur->state != state)
			continue;
		if (key_max >= cur->key_min && cur->key_max >= key_min) {
			gchar		   *state_str_display = gtk_accelerator_name (0, state);
			const gchar	   *cur_key_min_name  = gdk_keyval_name (cur->key_min);
			const gchar	   *cur_key_max_name  = gdk_keyval_name (cur->key_max);
			GtkAlertDialog *alert;
			if (cur->key_min == cur->key_max)
				alert = gtk_alert_dialog_new ("Binding overlaps with existing: %s%s", state_str_display,
											  cur_key_min_name ? cur_key_min_name : "?");
			else
				alert =
				  gtk_alert_dialog_new ("Binding overlaps with existing: %s%s-%s", state_str_display,
										cur_key_min_name ? cur_key_min_name : "?", cur_key_max_name ? cur_key_max_name : "?");
			gtk_alert_dialog_show (alert, parent_window);
			g_object_unref (alert);
			g_free (state_str_display);
			return false;
		}
	}
	return true;
}

static void key_bind_add_clicked (GtkButton *button, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	bind_t			  *bind		   = calloc (1, sizeof (bind_t));
	bind->key_min				   = GDK_KEY_space;
	bind->state					   = 0;
	bind->key_max				   = GDK_KEY_space;
	bind->action				   = BIND_ACT_SWITCH;
	bind->base					   = 0;
	bind->next					   = list_dialog->working_keys;
	list_dialog->working_keys	   = bind;
	refresh_key_bind_list (list_dialog);
}

static void key_bind_delete_clicked (GtkButton *button, gpointer user_data)
{
	bind_t			  *bind		   = (bind_t *) g_object_get_data (G_OBJECT (button), "bind_ptr");
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;

	/* Remove from working list */
	bind_t **prev = &list_dialog->working_keys;
	for (bind_t *cur = list_dialog->working_keys; cur; cur = cur->next) {
		if (cur == bind) {
			*prev = cur->next;
			free (cur);
			break;
		}
		prev = &cur->next;
	}
	refresh_key_bind_list (list_dialog);
}

/* Find the KeyBindItem in the store that wraps this bind, or NULL. Caller must unref the returned item. */
static KeyBindItem *key_bind_list_find_item_for_bind (KeyBindListDialog *list_dialog, bind_t *bind)
{
	guint n = g_list_model_get_n_items (G_LIST_MODEL (list_dialog->store));
	for (guint i = 0; i < n; i++) {
		GObject		*obj  = g_list_model_get_item (G_LIST_MODEL (list_dialog->store), i);
		KeyBindItem *item = KEY_BIND_ITEM (obj);
		if (item->bind == bind) {
			return item; /* caller unrefs */
		}
		g_object_unref (obj);
	}
	return NULL;
}

static void refresh_key_bind_list (KeyBindListDialog *list_dialog)
{
	debugf ("keybind: refresh_key_bind_list entry");
	g_hash_table_remove_all (list_dialog->bind_row_widgets);
	debugf ("keybind: refresh_key_bind_list before remove_all");
	g_list_store_remove_all (list_dialog->store);
	debugf ("keybind: refresh_key_bind_list after remove_all");

	for (bind_t *cur = list_dialog->working_keys; cur; cur = cur->next) {
		KeyBindItem *item = key_bind_item_new (cur);
		g_list_store_append (list_dialog->store, item);
		debugf ("Added item %p to list %p", item, list_dialog->store);
		g_object_unref (item);
	}
	debugf ("keybind: refresh_key_bind_list done");
}

/* Key capture for the Key Bindings list: small window, closes on key capture, Escape, or focus loss. */
typedef struct {
	GtkWidget		  *window;
	GtkWidget		  *label;
	guint			   captured_key;
	guint			   captured_state;
	bind_t			  *bind;
	KeyBindListDialog *list_dialog;
	bool			   max_key_only;
	guint			   active_check_source_id; /* timeout to detect window deactivation */
	bool			   had_focus;			   /* true once window has been active (avoids close on first frame) */
} KeyCaptureForBind;

static void key_capture_for_bind_close (KeyCaptureForBind *cap)
{
	if (cap->active_check_source_id) {
		g_source_remove (cap->active_check_source_id);
		cap->active_check_source_id = 0;
	}
	if (cap->window) {
		g_object_set_data (G_OBJECT (cap->window), "key-capture-cap", NULL);
		gtk_window_destroy (GTK_WINDOW (cap->window));
		cap->window = NULL;
	}
	free (cap);
}

/* Called periodically; close when window loses activation (Alt+Tab, another app, etc.). */
static gboolean key_capture_check_active (gpointer user_data)
{
	KeyCaptureForBind *cap = (KeyCaptureForBind *) user_data;
	if (!cap->window)
		return G_SOURCE_REMOVE;
	if (gtk_window_is_active (GTK_WINDOW (cap->window)))
		cap->had_focus = true;
	else if (cap->had_focus) {
		key_capture_for_bind_close (cap);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static void key_capture_for_bind_update_label (KeyCaptureForBind *cap)
{
	char label_text[256];
	if (cap->max_key_only) {
		if (cap->captured_key) {
			const gchar *name = gdk_keyval_name (cap->captured_key);
			snprintf (label_text, sizeof (label_text), "End key: %s\nRelease to set", name ? name : "?");
		} else {
			snprintf (label_text, sizeof (label_text),
					  "Press the key that ends the range (e.g. F12)\nEscape or click away to cancel");
		}
	} else {
		if (cap->captured_key) {
			gchar *accel = gtk_accelerator_name (cap->captured_key, cap->captured_state);
			snprintf (label_text, sizeof (label_text), "%s\nRelease to set", accel);
			g_free (accel);
		} else if (cap->captured_state) {
			gchar *state_str = gtk_accelerator_name (0, cap->captured_state);
			snprintf (label_text, sizeof (label_text), "Modifiers: %s\nPress a key, then release", state_str);
			g_free (state_str);
		} else {
			snprintf (label_text, sizeof (label_text), "Press key combination\nEscape or click away to cancel");
		}
	}
	gtk_label_set_text (GTK_LABEL (cap->label), label_text);
}

static void key_capture_focus_out (GtkEventControllerFocus *controller, gpointer user_data)
{
	GtkWidget		  *w		= gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (controller));
	GtkRoot			  *root		= gtk_widget_get_root (w);
	GtkWidget		  *toplevel = root ? GTK_WIDGET (root) : gtk_widget_get_ancestor (w, GTK_TYPE_WINDOW);
	KeyCaptureForBind *cap		= toplevel ? g_object_get_data (G_OBJECT (toplevel), "key-capture-cap") : NULL;
	debugf ("lost focus, w %p, root %p, toplevel %p, cap %p", w, root, toplevel, cap);
	if (cap)
		key_capture_for_bind_close (cap);
}

/* Apply captured key/state to bind; update item labels. Returns true if applied. */
static bool key_capture_apply (KeyCaptureForBind *cap)
{
	GtkWindow *parent = GTK_WINDOW (cap->list_dialog->dialog);
	if (cap->max_key_only) {
		if (!cap->captured_key || cap->captured_key == GDK_KEY_VoidSymbol)
			return false;
		guint key_max = cap->captured_key;
		if (key_max < cap->bind->key_min) {
			GtkAlertDialog *alert = gtk_alert_dialog_new ("End key must be >= start key.");
			gtk_alert_dialog_show (alert, parent);
			g_object_unref (alert);
			return false;
		}
		if (!key_bind_check_overlap (cap->list_dialog->working_keys, cap->bind, cap->bind->key_min, cap->bind->state, key_max,
									 parent))
			return false;
		cap->bind->key_max = key_max;
		/* terms.n_active is updated on Apply when we commit working_keys to terms.keys */
	} else {
		if (!cap->captured_key || cap->captured_key == GDK_KEY_VoidSymbol)
			return false;
		guint old_key	= cap->bind->key_min;
		guint new_key	= cap->captured_key;
		guint new_state = cap->captured_state;
		guint new_max	= cap->bind->key_max;
		if (new_key != old_key)
			new_max = new_key;
		if (!key_bind_check_overlap (cap->list_dialog->working_keys, cap->bind, new_key, new_state, new_max, parent))
			return false;
		cap->bind->key_min = new_key;
		cap->bind->state   = new_state;
		cap->bind->key_max = new_max;
		if (cap->bind->action != BIND_ACT_SWITCH)
			cap->bind->key_max = cap->bind->key_min;
		/* terms.n_active is updated on Apply when we commit working_keys to terms.keys */
	}
	KeyBindItem *item = key_bind_list_find_item_for_bind (cap->list_dialog, cap->bind);
	if (item) {
		key_bind_item_update_labels (item);
		g_object_unref (item);
	}
	return true;
}

/* Capture ends on first key-up or first keydown that is not a modifier. Escape / focus loss = abort. */
static gboolean key_capture_for_bind_key_pressed (GtkEventControllerKey *controller, guint keyval, guint keycode,
												  GdkModifierType state, gpointer user_data)
{
	KeyCaptureForBind *cap = (KeyCaptureForBind *) user_data;

	if (keyval == GDK_KEY_Escape) {
		key_capture_for_bind_close (cap);
		return TRUE;
	}

	if (cap->max_key_only) {
		if (keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R || keyval == GDK_KEY_Control_L ||
			keyval == GDK_KEY_Control_R || keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R || keyval == GDK_KEY_Super_L ||
			keyval == GDK_KEY_Super_R || keyval == GDK_KEY_Meta_L || keyval == GDK_KEY_Meta_R || keyval == GDK_KEY_Hyper_L ||
			keyval == GDK_KEY_Hyper_R)
			return TRUE;
		cap->captured_key = keyval;
		if (key_capture_apply (cap))
			key_capture_for_bind_close (cap);
		return TRUE;
	}

	/* Main key: modifier-only → update state; non-modifier → record key+state */
	if (keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R || keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R ||
		keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R || keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R ||
		keyval == GDK_KEY_Meta_L || keyval == GDK_KEY_Meta_R || keyval == GDK_KEY_Hyper_L || keyval == GDK_KEY_Hyper_R) {
		cap->captured_state = state & key_bind_mask;
		key_capture_for_bind_update_label (cap);
		return TRUE;
	}
	cap->captured_key	= keyval;
	cap->captured_state = state & key_bind_mask;
	key_capture_for_bind_update_label (cap);
	return TRUE;
}

static gboolean key_capture_for_bind_key_released (GtkEventControllerKey *controller, guint keyval, guint keycode,
												   GdkModifierType state, gpointer user_data)
{
	KeyCaptureForBind *cap = (KeyCaptureForBind *) user_data;
	/* Capture over on first key-up: apply if we have a key, then close */
	if (cap->captured_key && cap->captured_key != GDK_KEY_VoidSymbol) {
		key_capture_apply (cap);
		key_capture_for_bind_close (cap);
	}
	return TRUE;
}

static void show_key_capture_for_bind (GtkWindow *parent, bind_t *bind, KeyBindListDialog *list_dialog, bool max_key_only)
{
	KeyCaptureForBind *cap = g_new0 (KeyCaptureForBind, 1);
	cap->bind			   = bind;
	cap->list_dialog	   = list_dialog;
	cap->max_key_only	   = max_key_only;

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), max_key_only ? "Capture range end key" : "Capture key");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	g_object_set_data (G_OBJECT (dialog), "key-capture-cap", cap);
	cap->window					= dialog;
	cap->had_focus				= false;
	cap->active_check_source_id = g_timeout_add (150, key_capture_check_active, cap);

	GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (main_box, 24);
	gtk_widget_set_margin_end (main_box, 24);
	gtk_widget_set_margin_top (main_box, 24);
	gtk_widget_set_margin_bottom (main_box, 24);
	gtk_window_set_child (GTK_WINDOW (dialog), main_box);

	cap->label = gtk_label_new ("");
	gtk_label_set_justify (GTK_LABEL (cap->label), GTK_JUSTIFY_CENTER);
	gtk_label_set_wrap (GTK_LABEL (cap->label), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (cap->label), 40);
	gtk_widget_set_vexpand (cap->label, TRUE);
	gtk_box_append (GTK_BOX (main_box), cap->label);
	key_capture_for_bind_update_label (cap);

	GtkEventController *key_controller = gtk_event_controller_key_new ();
	gtk_widget_add_controller (dialog, key_controller);
	gtk_event_controller_set_propagation_phase (key_controller, GTK_PHASE_CAPTURE);
	g_signal_connect (key_controller, "key-pressed", G_CALLBACK (key_capture_for_bind_key_pressed), cap);
	g_signal_connect (key_controller, "key-released", G_CALLBACK (key_capture_for_bind_key_released), cap);

	GtkEventController *focus_controller = gtk_event_controller_focus_new ();
	gtk_widget_add_controller (dialog, focus_controller);
	g_signal_connect (focus_controller, "leave", G_CALLBACK (key_capture_focus_out), NULL);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 380, 120);
	gtk_window_present (GTK_WINDOW (dialog));
	gtk_widget_grab_focus (dialog);
}

static void key_bind_base_changed (GtkSpinButton *spin, gpointer user_data);

/* Synchronous teardown: clear the list store first so unbind runs while list_dialog is valid,
 * then free our data and destroy the window. No g_idle_add. */
/* List-settings callbacks for Key Bindings (ctx = KeyBindListDialog *) */
static void key_bind_commit (void *ctx)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) ctx;
	key_bind_list_free (terms.keys);
	terms.keys = key_bind_list_clone (list_dialog->working_keys);
	/* Update terms.n_active from SWITCH binds */
	for (bind_t *cur = terms.keys; cur; cur = cur->next) {
		if (cur->action == BIND_ACT_SWITCH) {
			int n = cur->base + (cur->key_max - cur->key_min) + 1;
			if (n > terms.n_active) {
				int old_n	   = terms.n_active;
				terms.n_active = n;
				terms.active   = realloc (terms.active, terms.n_active * sizeof (*terms.active));
				memset (&terms.active[old_n], 0, (terms.n_active - old_n) * sizeof (*terms.active));
			}
		}
	}
}

static void key_bind_snapshot (void *ctx)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) ctx;
	key_bind_list_free (list_dialog->original_keys);
	list_dialog->original_keys = key_bind_list_clone (list_dialog->working_keys);
}

static void key_bind_restore_config (void *ctx)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) ctx;
	key_bind_list_free (terms.keys);
	terms.keys = key_bind_list_clone (list_dialog->original_keys);
	/* Recompute terms.n_active from restored keys */
	int n_required = 0;
	for (bind_t *cur = terms.keys; cur; cur = cur->next) {
		if (cur->action == BIND_ACT_SWITCH) {
			int n = cur->base + (cur->key_max - cur->key_min) + 1;
			if (n > n_required)
				n_required = n;
		}
	}
	if (n_required > terms.n_active) {
		int old_n	   = terms.n_active;
		terms.n_active = n_required;
		terms.active   = realloc (terms.active, terms.n_active * sizeof (*terms.active));
		memset (&terms.active[old_n], 0, (terms.n_active - old_n) * sizeof (*terms.active));
	}
}

static void key_bind_restore_working (void *ctx)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) ctx;
	key_bind_list_free (list_dialog->working_keys);
	list_dialog->working_keys = key_bind_list_clone (list_dialog->original_keys);
}

static void key_bind_refresh_ui (void *ctx)
{
	refresh_key_bind_list ((KeyBindListDialog *) ctx);
}

static void key_bind_destroy (void *ctx)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) ctx;
	GtkWidget		  *dialog	   = list_dialog->dialog;
	g_list_store_remove_all (list_dialog->store);
	g_hash_table_destroy (list_dialog->bind_row_widgets);
	key_bind_list_free (list_dialog->working_keys);
	key_bind_list_free (list_dialog->original_keys);
	free (list_dialog);
	gtk_window_destroy (GTK_WINDOW (dialog));
}

static void key_bind_after_commit (void *ctx)
{
	(void) ctx;
	rebuild_menus ();
}

/* In-place: Action dropdown — update bind and this row's widgets only; no list refresh */
static void key_bind_action_changed_in_list (GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	bind_t			  *bind		   = g_object_get_data (G_OBJECT (dropdown), "bind_ptr");
	if (!bind)
		return;
	guint pos = gtk_drop_down_get_selected (dropdown);
	if (pos == GTK_INVALID_LIST_POSITION || pos >= (sizeof (bind_action_names) / sizeof (bind_action_names[0])) - 1)
		return;
	bind->action = (bind_actions_t) pos;
	if (bind->action != BIND_ACT_SWITCH) {
		bind->key_max = bind->key_min;
		bind->base	  = 0;
	}
	KeyBindItem *item = key_bind_list_find_item_for_bind (list_dialog, bind);
	if (item) {
		key_bind_item_update_labels (item);
		g_object_unref (item);
	}
	KeyBindRowWidgets *row_w = g_hash_table_lookup (list_dialog->bind_row_widgets, bind);
	if (row_w) {
		if (row_w->base_spin) {
			g_signal_handlers_disconnect_by_func (row_w->base_spin, G_CALLBACK (key_bind_base_changed), list_dialog);
			gtk_spin_button_set_value (GTK_SPIN_BUTTON (row_w->base_spin), 1.0);
			gtk_widget_set_sensitive (row_w->base_spin, bind->action == BIND_ACT_SWITCH);
			g_signal_connect (row_w->base_spin, "value-changed", G_CALLBACK (key_bind_base_changed), list_dialog);
		}
		if (row_w->configure_btn)
			gtk_widget_set_visible (row_w->configure_btn, bind->action == BIND_ACT_SWITCH);
		if (row_w->max_key_btn)
			gtk_widget_set_visible (row_w->max_key_btn, bind->action == BIND_ACT_SWITCH);
	}
}

/* Each dropdown gets its own model via gtk_drop_down_new_from_strings (no shared model). */
static void setup_key_bind_action_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	debugf ("keybind: setup_key_bind_action_factory list_item=%p", (void *) list_item);
	GtkWidget *dropdown = gtk_drop_down_new_from_strings (bind_action_names);
	gtk_list_item_set_child (list_item, dropdown);
}

static void bind_key_bind_action_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	GtkWidget		  *dropdown	   = gtk_list_item_get_child (list_item);
	KeyBindItem		  *item		   = gtk_list_item_get_item (list_item);
	debugf ("keybind: bind_key_bind_action_factory list_item=%p item=%p", (void *) list_item, (void *) item);
	if (item && item->bind) {
		g_object_set_data (G_OBJECT (dropdown), "bind_ptr", item->bind);
		g_signal_handlers_disconnect_by_func (dropdown, G_CALLBACK (key_bind_action_changed_in_list), list_dialog);
		gtk_drop_down_set_selected (GTK_DROP_DOWN (dropdown), item->bind->action);
		g_signal_connect (dropdown, "notify::selected", G_CALLBACK (key_bind_action_changed_in_list), list_dialog);
	}
}

/* Key column: button showing accelerator; click starts key capture (modifier + key). */
static void key_bind_key_clicked (GtkButton *button, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	bind_t			  *bind		   = g_object_get_data (G_OBJECT (button), "bind_ptr");
	if (!bind)
		return;
	/* Move focus to the dialog so when the modal closes and we refresh (destroying this button),
	 * focus returns to a valid widget, not the destroyed row. */
	gtk_widget_grab_focus (list_dialog->dialog);
	show_key_capture_for_bind (GTK_WINDOW (list_dialog->dialog), bind, list_dialog, false);
}

static void key_bind_key_label_notify (KeyBindItem *item, GParamSpec *pspec, gpointer user_data)
{
	GtkButton *btn = GTK_BUTTON (user_data);
	gchar	  *str = NULL;
	g_object_get (item, "key-label", &str, NULL);
	gtk_button_set_label (btn, str ? str : "…");
	g_free (str);
}

static void setup_key_bind_key_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	debugf ("keybind: setup_key_bind_key_factory list_item=%p", (void *) list_item);
	GtkWidget *btn = gtk_button_new ();
	gtk_button_set_has_frame (GTK_BUTTON (btn), FALSE);
	gtk_widget_set_hexpand (btn, TRUE);
	gtk_widget_set_halign (btn, GTK_ALIGN_START);
	gtk_list_item_set_child (list_item, btn);
}

static void unbind_key_bind_key_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget	*btn  = gtk_list_item_get_child (list_item);
	KeyBindItem *item = gtk_list_item_get_item (list_item);
	if (item)
		g_signal_handlers_disconnect_by_data (item, btn);
}

static void bind_key_bind_key_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	GtkWidget		  *btn		   = gtk_list_item_get_child (list_item);
	KeyBindItem		  *item		   = gtk_list_item_get_item (list_item);
	debugf ("keybind: bind_key_bind_key_factory list_item=%p btn=%p item=%p", (void *) list_item, (void *) btn, (void *) item);
	if (item && item->bind) {
		gchar *str = NULL;
		g_object_get (item, "key-label", &str, NULL);
		gtk_button_set_label (GTK_BUTTON (btn), str ? str : "…");
		g_free (str);
		g_object_set_data (G_OBJECT (btn), "bind_ptr", item->bind);
		g_signal_handlers_disconnect_by_func (btn, G_CALLBACK (key_bind_key_clicked), list_dialog);
		g_signal_connect (btn, "clicked", G_CALLBACK (key_bind_key_clicked), list_dialog);
		g_signal_handlers_disconnect_by_data (item, btn);
		g_signal_connect (item, "notify::key-label", G_CALLBACK (key_bind_key_label_notify), btn);
	}
}

/* Max key column: button showing range end key (SWITCH only); click starts key capture for end key. */
static void key_bind_max_key_clicked (GtkButton *button, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	bind_t			  *bind		   = g_object_get_data (G_OBJECT (button), "bind_ptr");
	if (!bind || bind->action != BIND_ACT_SWITCH)
		return;
	gtk_widget_grab_focus (list_dialog->dialog);
	show_key_capture_for_bind (GTK_WINDOW (list_dialog->dialog), bind, list_dialog, true);
}

static void setup_key_bind_max_key_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget *btn = gtk_button_new ();
	gtk_button_set_has_frame (GTK_BUTTON (btn), FALSE);
	gtk_widget_set_halign (btn, GTK_ALIGN_START);
	gtk_list_item_set_child (list_item, btn);
}

static void key_bind_max_key_label_notify (KeyBindItem *item, GParamSpec *pspec, gpointer user_data)
{
	GtkButton *btn = GTK_BUTTON (user_data);
	gchar	  *str = NULL;
	g_object_get (item, "max-key-label", &str, NULL);
	gtk_button_set_label (btn, str ? str : "—");
	g_free (str);
}

static void unbind_key_bind_max_key_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	GtkWidget		  *btn		   = gtk_list_item_get_child (list_item);
	KeyBindItem		  *item		   = gtk_list_item_get_item (list_item);
	debugf ("keybind: unbind_key_bind_max_key_factory list_item=%p item=%p", (void *) list_item, (void *) item);
	if (item) {
		g_signal_handlers_disconnect_by_data (item, btn);
		if (item->bind)
			g_hash_table_remove (list_dialog->bind_row_widgets, item->bind);
	}
}

static void bind_key_bind_max_key_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	GtkWidget		  *btn		   = gtk_list_item_get_child (list_item);
	KeyBindItem		  *item		   = gtk_list_item_get_item (list_item);
	if (item && item->bind) {
		KeyBindRowWidgets *row_w = g_hash_table_lookup (list_dialog->bind_row_widgets, item->bind);
		if (!row_w) {
			row_w = g_new0 (KeyBindRowWidgets, 1);
			g_hash_table_insert (list_dialog->bind_row_widgets, item->bind, row_w);
		}
		row_w->max_key_btn = btn;
		gchar *str		   = NULL;
		g_object_get (item, "max-key-label", &str, NULL);
		gtk_button_set_label (GTK_BUTTON (btn), str ? str : "—");
		g_free (str);
		gtk_widget_set_visible (btn, item->bind->action == BIND_ACT_SWITCH);
		g_object_set_data (G_OBJECT (btn), "bind_ptr", item->bind);
		g_signal_handlers_disconnect_by_func (btn, G_CALLBACK (key_bind_max_key_clicked), list_dialog);
		g_signal_connect (btn, "clicked", G_CALLBACK (key_bind_max_key_clicked), list_dialog);
		g_signal_handlers_disconnect_by_data (item, btn);
		g_signal_connect (item, "notify::max-key-label", G_CALLBACK (key_bind_max_key_label_notify), btn);
	}
}

/* In-place: Base spin (SWITCH only) */
static void key_bind_base_changed (GtkSpinButton *spin, gpointer user_data)
{
	(void) user_data;
	bind_t *bind = g_object_get_data (G_OBJECT (spin), "bind_ptr");
	if (!bind || bind->action != BIND_ACT_SWITCH)
		return;
	bind->base = gtk_spin_button_get_value_as_int (spin) - 1;
	/* No refresh: base is only shown in this spin, nothing else in the row depends on it */
}

static void setup_key_bind_base_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget *spin = gtk_spin_button_new_with_range (1, MAX_TABS, 1);
	gtk_list_item_set_child (list_item, spin);
}

static void unbind_key_bind_base_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	KeyBindItem		  *item		   = gtk_list_item_get_item (list_item);
	debugf ("keybind: unbind_key_bind_base_factory list_item=%p item=%p", (void *) list_item, (void *) item);
	if (item && item->bind)
		g_hash_table_remove (list_dialog->bind_row_widgets, item->bind);
}

static void bind_key_bind_base_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	GtkWidget		  *spin		   = gtk_list_item_get_child (list_item);
	KeyBindItem		  *item		   = gtk_list_item_get_item (list_item);
	if (item && item->bind) {
		KeyBindRowWidgets *row_w = g_hash_table_lookup (list_dialog->bind_row_widgets, item->bind);
		if (!row_w) {
			row_w = g_new0 (KeyBindRowWidgets, 1);
			g_hash_table_insert (list_dialog->bind_row_widgets, item->bind, row_w);
		}
		row_w->base_spin = spin;
		g_object_set_data (G_OBJECT (spin), "bind_ptr", item->bind);
		g_signal_handlers_disconnect_by_func (spin, G_CALLBACK (key_bind_base_changed), list_dialog);
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), (double) (item->bind->base + 1));
		gtk_widget_set_sensitive (spin, item->bind->action == BIND_ACT_SWITCH);
		g_signal_connect (spin, "value-changed", G_CALLBACK (key_bind_base_changed), list_dialog);
	}
}

/* Key bind list row: Delete, Configure (Configure only for SWITCH); editing is in-place on cells */
static void setup_key_bind_buttons_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget *box		  = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget *delete_btn = gtk_button_new_from_icon_name ("edit-delete-symbolic");
	gtk_widget_set_tooltip_text (delete_btn, "Delete");
	gtk_box_append (GTK_BOX (box), delete_btn);
	GtkWidget *configure_btn = gtk_button_new_from_icon_name ("utilities-terminal-symbolic");
	gtk_widget_set_tooltip_text (configure_btn, "Configure terminals for this binding's range");
	gtk_box_append (GTK_BOX (box), configure_btn);
	gtk_list_item_set_child (list_item, box);
}

static void key_bind_configure_clicked (GtkButton *button, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	bind_t			  *bind		   = (bind_t *) g_object_get_data (G_OBJECT (button), "bind_ptr");
	if (!bind || bind->action != BIND_ACT_SWITCH)
		return;
	int range_start = (int) bind->base;
	int range_end	= (int) (bind->base + (bind->key_max - bind->key_min));
	show_terminal_config_editor_for_range (list_dialog->dialog, list_dialog->window_n, range_start, range_end);
}

static void unbind_key_bind_buttons_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	KeyBindListDialog *list_dialog = (KeyBindListDialog *) user_data;
	KeyBindItem		  *item		   = gtk_list_item_get_item (list_item);
	debugf ("keybind: unbind_key_bind_buttons_factory list_item=%p item=%p", (void *) list_item, (void *) item);
	if (item && item->bind)
		g_hash_table_remove (list_dialog->bind_row_widgets, item->bind);
}

static void bind_key_bind_buttons_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	KeyBindListDialog *list_dialog	 = (KeyBindListDialog *) user_data;
	GtkWidget		  *box			 = gtk_list_item_get_child (list_item);
	KeyBindItem		  *item			 = gtk_list_item_get_item (list_item);
	GtkWidget		  *delete_btn	 = gtk_widget_get_first_child (box);
	GtkWidget		  *configure_btn = gtk_widget_get_next_sibling (delete_btn);

	if (item && item->bind) {
		KeyBindRowWidgets *row_w = g_hash_table_lookup (list_dialog->bind_row_widgets, item->bind);
		if (!row_w) {
			row_w = g_new0 (KeyBindRowWidgets, 1);
			g_hash_table_insert (list_dialog->bind_row_widgets, item->bind, row_w);
		}
		row_w->configure_btn = configure_btn;
		g_object_set_data (G_OBJECT (delete_btn), "bind_ptr", item->bind);
		g_object_set_data (G_OBJECT (configure_btn), "bind_ptr", item->bind);
		gtk_widget_set_visible (configure_btn, item->bind->action == BIND_ACT_SWITCH);
		g_signal_handlers_disconnect_by_func (delete_btn, G_CALLBACK (key_bind_delete_clicked), list_dialog);
		g_signal_handlers_disconnect_by_func (configure_btn, G_CALLBACK (key_bind_configure_clicked), list_dialog);
		g_signal_connect (delete_btn, "clicked", G_CALLBACK (key_bind_delete_clicked), list_dialog);
		g_signal_connect (configure_btn, "clicked", G_CALLBACK (key_bind_configure_clicked), list_dialog);
	}
}

static void show_key_bind_editor (GtkButton *button, gpointer user_data)
{
	PrefsDialog		  *prefs		 = (PrefsDialog *) user_data;
	KeyBindListDialog *list_dialog	 = g_new0 (KeyBindListDialog, 1);
	list_dialog->window_n			 = prefs->window_n;
	list_dialog->working_keys		 = key_bind_list_clone (terms.keys);
	list_dialog->original_keys		 = key_bind_list_clone (terms.keys);
	list_dialog->ops.ctx			 = list_dialog;
	list_dialog->ops.commit			 = key_bind_commit;
	list_dialog->ops.snapshot		 = key_bind_snapshot;
	list_dialog->ops.restore_config	 = key_bind_restore_config;
	list_dialog->ops.restore_working = key_bind_restore_working;
	list_dialog->ops.refresh_ui		 = key_bind_refresh_ui;
	list_dialog->ops.destroy		 = key_bind_destroy;
	list_dialog->ops.after_commit	 = key_bind_after_commit;

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

	list_dialog->bind_row_widgets = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	/* Create the GListStore and populate it */
	list_dialog->store = g_list_store_new (KEY_BIND_ITEM_TYPE);

	/* Create selection model */
	GtkNoSelection *selection = gtk_no_selection_new (G_LIST_MODEL (list_dialog->store));

	/* Create ColumnView */
	list_dialog->column_view = gtk_column_view_new (GTK_SELECTION_MODEL (selection));
	gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (list_dialog->column_view), TRUE);

	/* Action column: dropdown for in-place edit; each row gets its own model via new_from_strings */
	GtkListItemFactory *action_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (action_factory, "setup", G_CALLBACK (setup_key_bind_action_factory), NULL);
	g_signal_connect (action_factory, "bind", G_CALLBACK (bind_key_bind_action_factory), list_dialog);
	GtkColumnViewColumn *action_col = gtk_column_view_column_new ("Action", action_factory);
	gtk_column_view_column_set_resizable (action_col, TRUE);
	gtk_column_view_column_set_fixed_width (action_col, 120);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), action_col);

	/* Key column: modifier+key; click starts key capture */
	GtkListItemFactory *key_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (key_factory, "setup", G_CALLBACK (setup_key_bind_key_factory), NULL);
	g_signal_connect (key_factory, "bind", G_CALLBACK (bind_key_bind_key_factory), list_dialog);
	g_signal_connect (key_factory, "unbind", G_CALLBACK (unbind_key_bind_key_factory), list_dialog);
	GtkColumnViewColumn *key_col = gtk_column_view_column_new ("Key", key_factory);
	gtk_column_view_column_set_resizable (key_col, TRUE);
	gtk_column_view_column_set_expand (key_col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), key_col);

	/* Max key column: range end for SWITCH; click captures end key */
	GtkListItemFactory *max_key_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (max_key_factory, "setup", G_CALLBACK (setup_key_bind_max_key_factory), NULL);
	g_signal_connect (max_key_factory, "bind", G_CALLBACK (bind_key_bind_max_key_factory), list_dialog);
	g_signal_connect (max_key_factory, "unbind", G_CALLBACK (unbind_key_bind_max_key_factory), list_dialog);
	GtkColumnViewColumn *max_key_col = gtk_column_view_column_new ("Max key", max_key_factory);
	gtk_column_view_column_set_resizable (max_key_col, TRUE);
	gtk_column_view_column_set_fixed_width (max_key_col, 80);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), max_key_col);

	/* Base column: spin for in-place edit (SWITCH only) */
	GtkListItemFactory *base_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (base_factory, "setup", G_CALLBACK (setup_key_bind_base_factory), NULL);
	g_signal_connect (base_factory, "bind", G_CALLBACK (bind_key_bind_base_factory), list_dialog);
	g_signal_connect (base_factory, "unbind", G_CALLBACK (unbind_key_bind_base_factory), list_dialog);
	GtkColumnViewColumn *base_col = gtk_column_view_column_new ("Base", base_factory);
	gtk_column_view_column_set_resizable (base_col, TRUE);
	gtk_column_view_column_set_fixed_width (base_col, 60);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), base_col);

	/* Buttons column: Edit, Delete, Configure (Configure only for SWITCH) */
	GtkListItemFactory *buttons_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (buttons_factory, "setup", G_CALLBACK (setup_key_bind_buttons_factory), NULL);
	g_signal_connect (buttons_factory, "bind", G_CALLBACK (bind_key_bind_buttons_factory), list_dialog);
	g_signal_connect (buttons_factory, "unbind", G_CALLBACK (unbind_key_bind_buttons_factory), list_dialog);
	GtkColumnViewColumn *buttons_col = gtk_column_view_column_new ("", buttons_factory);
	/* No fixed width: column sizes to fit buttons exactly */
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), buttons_col);

	/* Scrolled window for column view; small minimum so user can shrink the dialog */
	GtkWidget *scrolled = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_overlay_scrolling (GTK_SCROLLED_WINDOW (scrolled), FALSE);
	gtk_widget_set_vexpand (scrolled, TRUE);
	gtk_widget_set_size_request (scrolled, 320, 160);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list_dialog->column_view);
	gtk_box_append (GTK_BOX (main_box), scrolled);

	refresh_key_bind_list (list_dialog);

	/* Buttons: Add, then Reset | Cancel | Apply | OK (GNOME convention) */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *add_btn	  = gtk_button_new_with_mnemonic ("_Add");
	GtkWidget *reset_btn  = gtk_button_new_with_mnemonic ("_Reset");
	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *apply_btn  = gtk_button_new_with_mnemonic ("_Apply");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), add_btn);
	gtk_box_append (GTK_BOX (button_box), reset_btn);
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), apply_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	g_signal_connect (add_btn, "clicked", G_CALLBACK (key_bind_add_clicked), list_dialog);
	g_signal_connect (reset_btn, "clicked", G_CALLBACK (list_settings_reset), &list_dialog->ops);
	g_signal_connect (cancel_btn, "clicked", G_CALLBACK (list_settings_cancel), &list_dialog->ops);
	g_signal_connect (apply_btn, "clicked", G_CALLBACK (list_settings_apply), &list_dialog->ops);
	g_signal_connect (ok_btn, "clicked", G_CALLBACK (list_settings_ok), &list_dialog->ops);
	debugf ("list_dialog: %p, ops: %p", list_dialog, &list_dialog->ops);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 740, 500);
	gtk_window_present (GTK_WINDOW (dialog));
}

/* ==================== Terminal Configuration Editor ==================== */

/* Clone/free for terminal_config_t array [0..MAX_TABS-1]. Used for working/original copies in list dialog. */
static void clear_terminal_config_slot (terminal_config_t *tc)
{
	if (tc->argv) {
		g_strfreev ((char **) tc->argv);
		tc->argv = NULL;
	}
	if (tc->env) {
		g_strfreev ((char **) tc->env);
		tc->env = NULL;
	}
	free ((char *) tc->working_directory);
	tc->working_directory = NULL;
}

static terminal_config_t *terminal_config_array_clone (terminal_config_t *src)
{
	if (!src)
		return NULL;
	terminal_config_t *dst = calloc (MAX_TABS, sizeof (terminal_config_t));
	for (int i = 0; i < MAX_TABS; i++) {
		if (src[i].argv)
			dst[i].argv = (const char **) g_strdupv ((gchar **) src[i].argv);
		if (src[i].env)
			dst[i].env = (const char **) g_strdupv ((gchar **) src[i].env);
		if (src[i].working_directory)
			dst[i].working_directory = strdup (src[i].working_directory);
	}
	return dst;
}

static void terminal_config_array_free (terminal_config_t *arr)
{
	if (!arr)
		return;
	for (int i = 0; i < MAX_TABS; i++)
		clear_terminal_config_slot (&arr[i]);
	free (arr);
}

/* Set one slot in an array (caller's array; used for working copy edits). */
static void set_terminal_config_slot (terminal_config_t *arr, int index, const char **argv, const char **env,
									  const char *working_directory)
{
	if (!arr || index < 0 || index >= MAX_TABS)
		return;
	clear_terminal_config_slot (&arr[index]);
	arr[index].argv				 = argv ? (const char **) g_strdupv ((gchar **) argv) : NULL;
	arr[index].env				 = env ? (const char **) g_strdupv ((gchar **) env) : NULL;
	arr[index].working_directory = working_directory ? strdup (working_directory) : NULL;
}

static bool terminal_config_entry_equal (terminal_config_t *a, terminal_config_t *b)
{
	if ((a->argv == NULL) != (b->argv == NULL))
		return false;
	if (a->argv != NULL && !g_strv_equal (a->argv, b->argv))
		return false;
	if ((a->env == NULL) != (b->env == NULL))
		return false;
	if (a->env != NULL && !g_strv_equal (a->env, b->env))
		return false;
	if (a->working_directory == NULL && b->working_directory == NULL)
		return true;
	if (a->working_directory == NULL || b->working_directory == NULL)
		return false;
	return strcmp (a->working_directory, b->working_directory) == 0;
}

/* Parse ranges string (1-based user input) "1", "3-8", "1-2, 9" into 0-based indices. Returns false on parse error. */
static bool parse_ranges_string (const char *str, int *out_indices, int max_indices, int *out_count)
{
	*out_count = 0;
	if (!str || strlen (str) == 0)
		return true;
	gchar **parts = g_strsplit (str, ",", -1);
	for (int i = 0; parts[i] != NULL && *out_count < max_indices; i++) {
		g_strstrip (parts[i]);
		if (parts[i][0] == '\0')
			continue;
		int start = -1, end = -1;
		if (strchr (parts[i], '-')) {
			if (sscanf (parts[i], "%d-%d", &start, &end) != 2 || start < 1 || end < start || end > MAX_TABS) {
				g_strfreev (parts);
				return false;
			}
		} else {
			if (sscanf (parts[i], "%d", &start) != 1 || start < 1 || start > MAX_TABS) {
				g_strfreev (parts);
				return false;
			}
			end = start;
		}
		for (int j = start; j <= end && *out_count < max_indices; j++)
			out_indices[(*out_count)++] = j - 1;
	}
	g_strfreev (parts);
	return true;
}

/* Format a single range for display (0-based start/end -> 1-based display) */
static void format_range (int start, int end, GString *out)
{
	if (start == end)
		g_string_append_printf (out, "%d", start + 1);
	else
		g_string_append_printf (out, "%d-%d", start + 1, end + 1);
}

#define TERMINAL_CONFIG_ITEM_TYPE (terminal_config_item_get_type ())
G_DECLARE_FINAL_TYPE (TerminalConfigItem, terminal_config_item, TERMINAL, CONFIG_ITEM, GObject)

struct _TerminalConfigItem {
	GObject parent_instance;
	int		start;
	int		end;
};

G_DEFINE_TYPE (TerminalConfigItem, terminal_config_item, G_TYPE_OBJECT)

static void terminal_config_item_class_init (TerminalConfigItemClass *)
{
}

static void terminal_config_item_init (TerminalConfigItem *self)
{
}

TerminalConfigItem *terminal_config_item_new (int start, int end)
{
	TerminalConfigItem *item = g_object_new (TERMINAL_CONFIG_ITEM_TYPE, NULL);
	item->start				 = start;
	item->end				 = end;
	return item;
}

typedef struct TerminalConfigListDialog_s TerminalConfigListDialog;

typedef struct {
	TerminalConfigListDialog *list_dialog;
	int						  column_type;
} TerminalConfigCellBindData;

typedef struct {
	GtkWidget *dialog;
	GtkWidget *ranges_entry;
	GtkWidget *cmd_entry;
	GtkWidget *directory_entry;
	GtkWidget *env_entry;
	int		   edit_start; /* -1 for add */
	int		   edit_end;
	long int   parent_window;
	GtkWidget *list_dialog;
} TerminalConfigEditDialog;

struct TerminalConfigListDialog_s {
	GtkWidget		  *dialog;
	GtkWidget		  *column_view;
	GtkWidget		  *filter_check;
	GtkWidget		  *filter_min_spin;
	GtkWidget		  *filter_max_spin;
	GListStore		  *store;
	long int		   window_n;
	int				   filter_start; /* -1 = show all */
	int				   filter_end;
	terminal_config_t *working_configs;	 /* working copy; list edits this */
	terminal_config_t *original_configs; /* snapshot for Reset/Cancel */
	ListSettingsOps	   ops;
	TerminalConfigCellBindData
	  cell_bind_data[4]; /* 0=ranges, 1=command, 2=directory, 3=env; must outlive stack for bind callbacks */
};

static void refresh_terminal_config_list (TerminalConfigListDialog *list_dialog);
static void terminal_config_cell_editing_done (GtkEntry *entry, gpointer unused);

static void terminal_config_edit_cancel (TerminalConfigEditDialog *edit)
{
	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	free (edit);
}

static void terminal_config_edit_ok (TerminalConfigEditDialog *edit)
{
	const char *ranges_str = gtk_editable_get_text (GTK_EDITABLE (edit->ranges_entry));
	const char *cmd_str	   = gtk_editable_get_text (GTK_EDITABLE (edit->cmd_entry));
	const char *dir_str	   = gtk_editable_get_text (GTK_EDITABLE (edit->directory_entry));
	const char *env_str	   = gtk_editable_get_text (GTK_EDITABLE (edit->env_entry));

	int indices[MAX_TABS];
	int n_indices = 0;
	if (!parse_ranges_string (ranges_str, indices, MAX_TABS, &n_indices) || n_indices == 0) {
		GtkAlertDialog *alert = gtk_alert_dialog_new ("Invalid terminal ranges. Use e.g. 1, 3-8, or 1-2, 9");
		gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
		g_object_unref (alert);
		return;
	}

	gchar **argv = NULL;
	gchar **env	 = NULL;
	if (cmd_str && strlen (cmd_str) > 0) {
		gint	argc  = 0;
		GError *error = NULL;
		if (!g_shell_parse_argv (cmd_str, &argc, &argv, &error)) {
			if (error) {
				GtkAlertDialog *alert = gtk_alert_dialog_new ("Invalid command: %s", error->message);
				gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
				g_object_unref (alert);
				g_error_free (error);
			}
			return;
		}
	}
	if (env_str && strlen (env_str) > 0) {
		gint	argc  = 0;
		GError *error = NULL;
		if (!g_shell_parse_argv (env_str, &argc, &env, &error)) {
			if (error) {
				GtkAlertDialog *alert = gtk_alert_dialog_new ("Invalid environment: %s", error->message);
				gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
				g_object_unref (alert);
				g_error_free (error);
			}
			return;
		}
	}
	const char *dir = (dir_str && strlen (dir_str) > 0) ? dir_str : NULL;

	TerminalConfigListDialog *list =
	  edit->list_dialog
		? (TerminalConfigListDialog *) g_object_get_data (G_OBJECT (edit->list_dialog), "terminal_config_list_dialog")
		: NULL;

	if (list && list->working_configs) {
		/* Apply to working copy only; list Apply/OK will commit and save */
		if (edit->edit_start >= 0) {
			for (int i = edit->edit_start; i <= edit->edit_end && i < MAX_TABS; i++)
				set_terminal_config_slot (list->working_configs, i, NULL, NULL, NULL);
		}
		for (int i = 0; i < n_indices; i++)
			set_terminal_config_slot (list->working_configs, indices[i], (const char **) argv, (const char **) env, dir);
		if (argv)
			g_strfreev (argv);
		if (env)
			g_strfreev (env);
		refresh_terminal_config_list (list);
	} else {
		/* Standalone edit (no list dialog): apply to terms and save immediately */
		zterm_ensure_terminal_configs ();
		if (edit->edit_start >= 0) {
			for (int i = edit->edit_start; i <= edit->edit_end && i < MAX_TABS; i++)
				zterm_set_terminal_config (i, NULL, NULL, NULL);
		}
		for (int i = 0; i < n_indices; i++)
			zterm_set_terminal_config (indices[i], (const char **) argv, (const char **) env, dir);
		if (argv)
			g_strfreev (argv);
		if (env)
			g_strfreev (env);
		zterm_save_config ();
	}
	gtk_window_destroy (GTK_WINDOW (edit->dialog));
	free (edit);
}

static void show_terminal_config_edit_dialog (int edit_start, int edit_end, long int parent_window, GtkWidget *list_dialog)
{
	TerminalConfigEditDialog *edit = g_new0 (TerminalConfigEditDialog, 1);
	edit->edit_start			   = edit_start;
	edit->edit_end				   = edit_end;
	edit->parent_window			   = parent_window;
	edit->list_dialog			   = list_dialog;

	GtkWidget *dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), (edit_start >= 0) ? "Edit Terminal Configuration" : "New Terminal Configuration");
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

	gtk_grid_attach (GTK_GRID (grid), create_label ("Terminals (ranges):"), 0, row, 1, 1);
	edit->ranges_entry = gtk_entry_new ();
	gtk_editable_set_width_chars (GTK_EDITABLE (edit->ranges_entry), 28);
	gtk_entry_set_placeholder_text (GTK_ENTRY (edit->ranges_entry), "e.g. 1, 3-8, or 1-2, 9");
	if (edit_start >= 0) {
		GString *s = g_string_new ("");
		format_range (edit_start, edit_end, s);
		gtk_editable_set_text (GTK_EDITABLE (edit->ranges_entry), s->str);
		g_string_free (s, TRUE);
	} else {
		TerminalConfigListDialog *list = g_object_get_data (G_OBJECT (list_dialog), "terminal_config_list_dialog");
		if (list && list->filter_start >= 0) {
			GString *s = g_string_new ("");
			format_range (list->filter_start, list->filter_end, s);
			gtk_editable_set_text (GTK_EDITABLE (edit->ranges_entry), s->str);
			g_string_free (s, TRUE);
		}
	}
	gtk_widget_set_hexpand (edit->ranges_entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), edit->ranges_entry, 1, row++, 1, 1);

	gtk_grid_attach (GTK_GRID (grid), create_label ("Command (optional):"), 0, row, 1, 1);
	edit->cmd_entry = gtk_entry_new ();
	gtk_editable_set_width_chars (GTK_EDITABLE (edit->cmd_entry), 28);
	gtk_entry_set_placeholder_text (GTK_ENTRY (edit->cmd_entry), "e.g. /bin/zsh -l");
	TerminalConfigListDialog *list_for_edit =
	  list_dialog ? (TerminalConfigListDialog *) g_object_get_data (G_OBJECT (list_dialog), "terminal_config_list_dialog") : NULL;
	terminal_config_t *config_src =
	  (list_for_edit && list_for_edit->working_configs) ? list_for_edit->working_configs : terms.terminal_configs;
	if (edit_start >= 0 && config_src && edit_start < MAX_TABS && config_src[edit_start].argv) {
		gchar *cmd_str = g_strjoinv (" ", (gchar **) config_src[edit_start].argv);
		gtk_editable_set_text (GTK_EDITABLE (edit->cmd_entry), cmd_str);
		g_free (cmd_str);
	}
	gtk_widget_set_hexpand (edit->cmd_entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), edit->cmd_entry, 1, row++, 1, 1);

	gtk_grid_attach (GTK_GRID (grid), create_label ("Working directory (optional):"), 0, row, 1, 1);
	edit->directory_entry = gtk_entry_new ();
	gtk_editable_set_width_chars (GTK_EDITABLE (edit->directory_entry), 32);
	gtk_entry_set_placeholder_text (GTK_ENTRY (edit->directory_entry), "e.g. /home/user/project");
	if (edit_start >= 0 && config_src && edit_start < MAX_TABS && config_src[edit_start].working_directory) {
		gtk_editable_set_text (GTK_EDITABLE (edit->directory_entry), config_src[edit_start].working_directory);
	}
	gtk_widget_set_hexpand (edit->directory_entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), edit->directory_entry, 1, row++, 1, 1);

	gtk_grid_attach (GTK_GRID (grid), create_label ("Environment (optional):"), 0, row, 1, 1);
	edit->env_entry = gtk_entry_new ();
	gtk_editable_set_width_chars (GTK_EDITABLE (edit->env_entry), 24);
	gtk_entry_set_placeholder_text (GTK_ENTRY (edit->env_entry), "e.g. VAR=val");
	if (edit_start >= 0 && config_src && edit_start < MAX_TABS && config_src[edit_start].env) {
		gchar *env_str = g_strjoinv (" ", (gchar **) config_src[edit_start].env);
		gtk_editable_set_text (GTK_EDITABLE (edit->env_entry), env_str);
		g_free (env_str);
	}
	gtk_widget_set_hexpand (edit->env_entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), edit->env_entry, 1, row++, 1, 1);

	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);
	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);
	g_signal_connect_swapped (cancel_btn, "clicked", G_CALLBACK (terminal_config_edit_cancel), edit);
	g_signal_connect_swapped (ok_btn, "clicked", G_CALLBACK (terminal_config_edit_ok), edit);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 560, 380);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void terminal_config_add_clicked (GtkButton *button, gpointer user_data)
{
	TerminalConfigListDialog *list_dialog = (TerminalConfigListDialog *) user_data;
	show_terminal_config_edit_dialog (-1, -1, list_dialog->window_n, list_dialog->dialog);
}

static void terminal_config_delete_clicked (GtkButton *button, gpointer user_data)
{
	TerminalConfigItem		 *item		  = (TerminalConfigItem *) g_object_get_data (G_OBJECT (button), "terminal_config_item");
	TerminalConfigListDialog *list_dialog = (TerminalConfigListDialog *) user_data;
	if (!item)
		return;
	if (list_dialog->working_configs) {
		for (int i = item->start; i <= item->end && i < MAX_TABS; i++)
			set_terminal_config_slot (list_dialog->working_configs, i, NULL, NULL, NULL);
		refresh_terminal_config_list (list_dialog);
	} else {
		if (!terms.terminal_configs)
			return;
		zterm_ensure_terminal_configs ();
		for (int i = item->start; i <= item->end && i < MAX_TABS; i++)
			zterm_set_terminal_config (i, NULL, NULL, NULL);
		zterm_save_config ();
		refresh_terminal_config_list (list_dialog);
	}
}

static void refresh_terminal_config_list (TerminalConfigListDialog *list_dialog)
{
	g_list_store_remove_all (list_dialog->store);
	terminal_config_t *src = list_dialog->working_configs ? list_dialog->working_configs : terms.terminal_configs;
	if (!src)
		return;
	int fs		 = list_dialog->filter_start;
	int fe		 = list_dialog->filter_end;
	int filtered = (fs >= 0);
	for (int i = 0; i < MAX_TABS; i++) {
		terminal_config_t *tc = &src[i];
		if (tc->argv == NULL && tc->working_directory == NULL && tc->env == NULL)
			continue;
		int end_i = i;
		while (end_i + 1 < MAX_TABS && terminal_config_entry_equal (tc, &src[end_i + 1]))
			end_i++;
		if (filtered && (end_i < fs || i > fe))
			; /* skip: no overlap with [fs, fe] */
		else {
			TerminalConfigItem *item = terminal_config_item_new (i, end_i);
			g_list_store_append (list_dialog->store, item);
			debugf ("Added item %p to list %p", item, list_dialog->store);
			g_object_unref (item);
		}
		i = end_i;
	}
}

/* ListSettingsOps for Terminal Configuration: Apply/Reset/Cancel/OK pattern */
static void terminal_config_commit (void *ctx)
{
	TerminalConfigListDialog *list_dialog = (TerminalConfigListDialog *) ctx;
	zterm_ensure_terminal_configs ();
	for (int i = 0; i < MAX_TABS; i++) {
		terminal_config_t *w = &list_dialog->working_configs[i];
		zterm_set_terminal_config (i, w->argv, w->env, w->working_directory);
	}
}

static void terminal_config_snapshot (void *ctx)
{
	TerminalConfigListDialog *list_dialog = (TerminalConfigListDialog *) ctx;
	terminal_config_array_free (list_dialog->original_configs);
	list_dialog->original_configs = terminal_config_array_clone (list_dialog->working_configs);
}

static void terminal_config_restore_config (void *ctx)
{
	TerminalConfigListDialog *list_dialog = (TerminalConfigListDialog *) ctx;
	zterm_ensure_terminal_configs ();
	for (int i = 0; i < MAX_TABS; i++) {
		terminal_config_t *o = &list_dialog->original_configs[i];
		zterm_set_terminal_config (i, o->argv, o->env, o->working_directory);
	}
}

static void terminal_config_restore_working (void *ctx)
{
	TerminalConfigListDialog *list_dialog = (TerminalConfigListDialog *) ctx;
	terminal_config_array_free (list_dialog->working_configs);
	list_dialog->working_configs = terminal_config_array_clone (list_dialog->original_configs);
}

static void terminal_config_refresh_ui (void *ctx)
{
	refresh_terminal_config_list ((TerminalConfigListDialog *) ctx);
}

static void terminal_config_destroy (void *ctx)
{
	TerminalConfigListDialog *list_dialog = (TerminalConfigListDialog *) ctx;
	terminal_config_array_free (list_dialog->working_configs);
	list_dialog->working_configs = NULL;
	terminal_config_array_free (list_dialog->original_configs);
	list_dialog->original_configs = NULL;
	gtk_window_destroy (GTK_WINDOW (list_dialog->dialog));
	free (list_dialog);
}

/* In-place editable cell: entry that commits on activate (Enter) or focus leave. column_type: 0=ranges, 1=command, 2=directory,
 * 3=env */
static void terminal_config_entry_focus_leave (GtkEventControllerFocus *ctrl, gpointer unused)
{
	GtkWidget *entry = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (ctrl));
	terminal_config_cell_editing_done (GTK_ENTRY (entry), NULL);
}

static void setup_terminal_config_entry_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget *entry = gtk_entry_new ();
	gtk_editable_set_width_chars (GTK_EDITABLE (entry), 12);
	gtk_widget_set_hexpand (entry, TRUE);
	g_signal_connect (entry, "activate", G_CALLBACK (terminal_config_cell_editing_done), NULL);
	GtkEventController *focus = gtk_event_controller_focus_new ();
	g_signal_connect (focus, "leave", G_CALLBACK (terminal_config_entry_focus_leave), NULL);
	gtk_widget_add_controller (entry, focus);
	gtk_list_item_set_child (list_item, entry);
}

static void terminal_config_editable_cell_bind (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	TerminalConfigCellBindData *bind_data = (TerminalConfigCellBindData *) user_data;
	GtkWidget				   *entry	  = gtk_list_item_get_child (list_item);
	TerminalConfigItem		   *item	  = gtk_list_item_get_item (list_item);
	char						display[512];
	display[0] = '\0';
	terminal_config_t *src =
	  bind_data->list_dialog->working_configs ? bind_data->list_dialog->working_configs : terms.terminal_configs;
	if (item && src && item->start < MAX_TABS) {
		terminal_config_t *tc = &src[item->start];
		switch (bind_data->column_type) {
			case 0: {
				GString *s = g_string_new ("");
				format_range (item->start, item->end, s);
				g_snprintf (display, sizeof (display), "%s", s->str);
				g_string_free (s, TRUE);
				break;
			}
			case 1:
				if (tc->argv && tc->argv[0]) {
					gchar *j = g_strjoinv (" ", (gchar **) tc->argv);
					g_snprintf (display, sizeof (display), "%s", j ? j : "");
					g_free (j);
				} else
					g_snprintf (display, sizeof (display), "(default)");
				break;
			case 2:
				g_snprintf (display, sizeof (display), "%s",
							tc->working_directory && tc->working_directory[0] ? tc->working_directory : "(default)");
				break;
			case 3:
				if (tc->env && tc->env[0]) {
					gchar *j = g_strjoinv (" ", (gchar **) tc->env);
					g_snprintf (display, sizeof (display), "%s", j ? j : "");
					g_free (j);
				} else
					g_snprintf (display, sizeof (display), "(default)");
				break;
		}
	}
	gtk_editable_set_text (GTK_EDITABLE (entry), display);
	g_object_set_data (G_OBJECT (entry), "terminal_config_item", item);
	g_object_set_data (G_OBJECT (entry), "terminal_config_list_dialog", bind_data->list_dialog);
	g_object_set_data (G_OBJECT (entry), "column_type", GINT_TO_POINTER (bind_data->column_type));
}

static void terminal_config_cell_apply (TerminalConfigListDialog *list_dialog, TerminalConfigItem *item, int column_type,
										const char *new_text)
{
	terminal_config_t *arr		   = list_dialog->working_configs;
	bool			   use_working = (arr != NULL);
	if (!use_working) {
		zterm_ensure_terminal_configs ();
		arr = terms.terminal_configs;
	}
	if (!item || item->start < 0 || item->start >= MAX_TABS || !arr)
		return;
	terminal_config_t *tc		= &arr[item->start];
	gchar			 **argv_new = NULL;
	gchar			 **env_new	= NULL;
	const char		  *dir_new	= NULL;

	if (column_type == 0) {
		int indices[MAX_TABS];
		int n = 0;
		if (!new_text || !parse_ranges_string (new_text, indices, MAX_TABS, &n) || n == 0) {
			GtkAlertDialog *alert = gtk_alert_dialog_new ("Invalid terminal ranges. Use e.g. 1, 3-8, or 1-2, 9");
			gtk_alert_dialog_show (alert, GTK_WINDOW (list_dialog->dialog));
			g_object_unref (alert);
			return;
		}
		gchar **argv_dup = tc->argv ? g_strdupv ((gchar **) tc->argv) : NULL;
		gchar **env_dup	 = tc->env ? g_strdupv ((gchar **) tc->env) : NULL;
		char   *dir_dup	 = tc->working_directory ? strdup (tc->working_directory) : NULL;
		for (int i = item->start; i <= item->end && i < MAX_TABS; i++) {
			if (use_working)
				set_terminal_config_slot (arr, i, NULL, NULL, NULL);
			else
				zterm_set_terminal_config (i, NULL, NULL, NULL);
		}
		for (int i = 0; i < n; i++) {
			if (use_working)
				set_terminal_config_slot (arr, indices[i], (const char **) argv_dup, (const char **) env_dup, dir_dup);
			else
				zterm_set_terminal_config (indices[i], (const char **) argv_dup, (const char **) env_dup, dir_dup);
		}
		g_strfreev (argv_dup);
		g_strfreev (env_dup);
		free (dir_dup);
	} else if (column_type == 1) {
		if (new_text && strlen (new_text) > 0) {
			gint	argc = 0;
			GError *err	 = NULL;
			if (!g_shell_parse_argv (new_text, &argc, &argv_new, &err)) {
				if (err) {
					GtkAlertDialog *alert = gtk_alert_dialog_new ("Invalid command: %s", err->message);
					gtk_alert_dialog_show (alert, GTK_WINDOW (list_dialog->dialog));
					g_object_unref (alert);
					g_error_free (err);
				}
				return;
			}
		}
		for (int i = item->start; i <= item->end && i < MAX_TABS; i++) {
			if (use_working)
				set_terminal_config_slot (arr, i, (const char **) argv_new, tc->env, tc->working_directory);
			else
				zterm_set_terminal_config (i, (const char **) argv_new, tc->env, tc->working_directory);
		}
		g_strfreev (argv_new);
	} else if (column_type == 2) {
		dir_new = (new_text && strlen (new_text) > 0) ? new_text : NULL;
		for (int i = item->start; i <= item->end && i < MAX_TABS; i++) {
			if (use_working)
				set_terminal_config_slot (arr, i, tc->argv, tc->env, dir_new);
			else
				zterm_set_terminal_config (i, tc->argv, tc->env, dir_new);
		}
	} else if (column_type == 3) {
		if (new_text && strlen (new_text) > 0) {
			gint	argc = 0;
			GError *err	 = NULL;
			if (!g_shell_parse_argv (new_text, &argc, &env_new, &err)) {
				if (err) {
					GtkAlertDialog *alert = gtk_alert_dialog_new ("Invalid environment: %s", err->message);
					gtk_alert_dialog_show (alert, GTK_WINDOW (list_dialog->dialog));
					g_object_unref (alert);
					g_error_free (err);
				}
				return;
			}
		}
		for (int i = item->start; i <= item->end && i < MAX_TABS; i++) {
			if (use_working)
				set_terminal_config_slot (arr, i, tc->argv, (const char **) env_new, tc->working_directory);
			else
				zterm_set_terminal_config (i, tc->argv, (const char **) env_new, tc->working_directory);
		}
		g_strfreev (env_new);
	}
	if (!use_working)
		zterm_save_config ();
	refresh_terminal_config_list (list_dialog);
}

static void terminal_config_cell_editing_done (GtkEntry *entry, gpointer unused)
{
	TerminalConfigItem		 *item		  = g_object_get_data (G_OBJECT (entry), "terminal_config_item");
	TerminalConfigListDialog *list_dialog = g_object_get_data (G_OBJECT (entry), "terminal_config_list_dialog");
	int						  column_type = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (entry), "column_type"));
	if (!item || !list_dialog)
		return;
	const char *text = gtk_editable_get_text (GTK_EDITABLE (entry));
	terminal_config_cell_apply (list_dialog, item, column_type, text);
}

/* Terminal config list: only Delete button (edit is in-place on cells) */
static void setup_terminal_config_buttons_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget *box		  = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget *delete_btn = gtk_button_new_from_icon_name ("edit-delete-symbolic");
	gtk_widget_set_tooltip_text (delete_btn, "Delete");
	gtk_box_append (GTK_BOX (box), delete_btn);
	gtk_list_item_set_child (list_item, box);
}

static void terminal_config_buttons_factory (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	TerminalConfigListDialog *list_dialog = (TerminalConfigListDialog *) user_data;
	GtkWidget				 *box		  = gtk_list_item_get_child (list_item);
	TerminalConfigItem		 *item		  = gtk_list_item_get_item (list_item);
	GtkWidget				 *delete_btn  = gtk_widget_get_first_child (box);
	if (item) {
		g_object_set_data (G_OBJECT (delete_btn), "terminal_config_item", item);
		g_signal_handlers_disconnect_by_func (delete_btn, G_CALLBACK (terminal_config_delete_clicked), list_dialog);
		g_signal_connect (delete_btn, "clicked", G_CALLBACK (terminal_config_delete_clicked), list_dialog);
	}
}

static void terminal_config_filter_changed (GtkWidget *widget, gpointer user_data)
{
	TerminalConfigListDialog *list_dialog = (TerminalConfigListDialog *) user_data;
	if (gtk_check_button_get_active (GTK_CHECK_BUTTON (list_dialog->filter_check))) {
		int min_val = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (list_dialog->filter_min_spin));
		int max_val = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (list_dialog->filter_max_spin));
		if (min_val > max_val) {
			gtk_spin_button_set_value (GTK_SPIN_BUTTON (list_dialog->filter_max_spin), (double) min_val);
			max_val = min_val;
		}
		list_dialog->filter_start = min_val - 1;
		list_dialog->filter_end	  = max_val - 1;
	} else {
		list_dialog->filter_start = -1;
		list_dialog->filter_end	  = -1;
	}
	refresh_terminal_config_list (list_dialog);
}

static void show_terminal_config_editor_impl (GtkWidget *parent_window, long int window_n, int filter_start, int filter_end)
{
	TerminalConfigListDialog *list_dialog = g_new0 (TerminalConfigListDialog, 1);
	list_dialog->window_n				  = window_n;
	list_dialog->filter_start			  = filter_start;
	list_dialog->filter_end				  = filter_end;
	zterm_ensure_terminal_configs ();
	list_dialog->working_configs	 = terminal_config_array_clone (terms.terminal_configs);
	list_dialog->original_configs	 = terminal_config_array_clone (terms.terminal_configs);
	list_dialog->ops.ctx			 = list_dialog;
	list_dialog->ops.commit			 = terminal_config_commit;
	list_dialog->ops.snapshot		 = terminal_config_snapshot;
	list_dialog->ops.restore_config	 = terminal_config_restore_config;
	list_dialog->ops.restore_working = terminal_config_restore_working;
	list_dialog->ops.refresh_ui		 = terminal_config_refresh_ui;
	list_dialog->ops.destroy		 = terminal_config_destroy;
	list_dialog->ops.after_commit	 = NULL;

	GtkWidget *dialog = gtk_window_new ();
	if (filter_start >= 0) {
		char title_buf[80];
		snprintf (title_buf, sizeof (title_buf), "Terminal Configuration (terminals %d–%d)", filter_start + 1, filter_end + 1);
		gtk_window_set_title (GTK_WINDOW (dialog), title_buf);
	} else {
		gtk_window_set_title (GTK_WINDOW (dialog), "Terminal Configuration");
	}
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent_window));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	list_dialog->dialog = dialog;
	g_object_set_data (G_OBJECT (dialog), "terminal_config_list_dialog", list_dialog);

	GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (main_box, 12);
	gtk_widget_set_margin_end (main_box, 12);
	gtk_widget_set_margin_top (main_box, 12);
	gtk_widget_set_margin_bottom (main_box, 12);
	gtk_window_set_child (GTK_WINDOW (dialog), main_box);

	/* Filter row: checkbox and min–max (1-based in UI) */
	GtkWidget *filter_row	  = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	list_dialog->filter_check = gtk_check_button_new_with_label ("Filter to range:");
	gtk_box_append (GTK_BOX (filter_row), list_dialog->filter_check);
	list_dialog->filter_min_spin = gtk_spin_button_new_with_range (1, MAX_TABS, 1);
	list_dialog->filter_max_spin = gtk_spin_button_new_with_range (1, MAX_TABS, 1);
	if (filter_start >= 0) {
		gtk_check_button_set_active (GTK_CHECK_BUTTON (list_dialog->filter_check), TRUE);
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (list_dialog->filter_min_spin), (double) (filter_start + 1));
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (list_dialog->filter_max_spin), (double) (filter_end + 1));
	} else {
		gtk_check_button_set_active (GTK_CHECK_BUTTON (list_dialog->filter_check), FALSE);
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (list_dialog->filter_min_spin), 1.0);
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (list_dialog->filter_max_spin), (double) MAX_TABS);
	}
	gtk_box_append (GTK_BOX (filter_row), list_dialog->filter_min_spin);
	gtk_box_append (GTK_BOX (filter_row), gtk_label_new ("–"));
	gtk_box_append (GTK_BOX (filter_row), list_dialog->filter_max_spin);
	g_signal_connect (list_dialog->filter_check, "toggled", G_CALLBACK (terminal_config_filter_changed), list_dialog);
	g_signal_connect (list_dialog->filter_min_spin, "value-changed", G_CALLBACK (terminal_config_filter_changed), list_dialog);
	g_signal_connect (list_dialog->filter_max_spin, "value-changed", G_CALLBACK (terminal_config_filter_changed), list_dialog);
	gtk_box_append (GTK_BOX (main_box), filter_row);

	list_dialog->store		  = g_list_store_new (TERMINAL_CONFIG_ITEM_TYPE);
	GtkNoSelection *selection = gtk_no_selection_new (G_LIST_MODEL (list_dialog->store));
	list_dialog->column_view  = gtk_column_view_new (GTK_SELECTION_MODEL (selection));
	gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (list_dialog->column_view), TRUE);

	list_dialog->cell_bind_data[0] = (TerminalConfigCellBindData) {list_dialog, 0};
	list_dialog->cell_bind_data[1] = (TerminalConfigCellBindData) {list_dialog, 1};
	list_dialog->cell_bind_data[2] = (TerminalConfigCellBindData) {list_dialog, 2};
	list_dialog->cell_bind_data[3] = (TerminalConfigCellBindData) {list_dialog, 3};

	GtkListItemFactory *ranges_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (ranges_factory, "setup", G_CALLBACK (setup_terminal_config_entry_factory), NULL);
	g_signal_connect (ranges_factory, "bind", G_CALLBACK (terminal_config_editable_cell_bind), &list_dialog->cell_bind_data[0]);
	GtkColumnViewColumn *ranges_col = gtk_column_view_column_new ("Terminals", ranges_factory);
	gtk_column_view_column_set_resizable (ranges_col, TRUE);
	gtk_column_view_column_set_fixed_width (ranges_col, 100);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), ranges_col);

	GtkListItemFactory *command_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (command_factory, "setup", G_CALLBACK (setup_terminal_config_entry_factory), NULL);
	g_signal_connect (command_factory, "bind", G_CALLBACK (terminal_config_editable_cell_bind), &list_dialog->cell_bind_data[1]);
	GtkColumnViewColumn *command_col = gtk_column_view_column_new ("Command", command_factory);
	gtk_column_view_column_set_resizable (command_col, TRUE);
	gtk_column_view_column_set_expand (command_col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), command_col);

	GtkListItemFactory *directory_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (directory_factory, "setup", G_CALLBACK (setup_terminal_config_entry_factory), NULL);
	g_signal_connect (directory_factory, "bind", G_CALLBACK (terminal_config_editable_cell_bind),
					  &list_dialog->cell_bind_data[2]);
	GtkColumnViewColumn *directory_col = gtk_column_view_column_new ("Directory", directory_factory);
	gtk_column_view_column_set_resizable (directory_col, TRUE);
	gtk_column_view_column_set_expand (directory_col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), directory_col);

	GtkListItemFactory *env_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (env_factory, "setup", G_CALLBACK (setup_terminal_config_entry_factory), NULL);
	g_signal_connect (env_factory, "bind", G_CALLBACK (terminal_config_editable_cell_bind), &list_dialog->cell_bind_data[3]);
	GtkColumnViewColumn *env_col = gtk_column_view_column_new ("Environment", env_factory);
	gtk_column_view_column_set_resizable (env_col, TRUE);
	gtk_column_view_column_set_fixed_width (env_col, 140);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), env_col);

	GtkListItemFactory *buttons_factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (buttons_factory, "setup", G_CALLBACK (setup_terminal_config_buttons_factory), NULL);
	g_signal_connect (buttons_factory, "bind", G_CALLBACK (terminal_config_buttons_factory), list_dialog);
	GtkColumnViewColumn *buttons_col = gtk_column_view_column_new ("", buttons_factory);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (list_dialog->column_view), buttons_col);

	GtkWidget *scrolled = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand (scrolled, TRUE);
	gtk_widget_set_size_request (scrolled, 540, 280);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list_dialog->column_view);
	gtk_box_append (GTK_BOX (main_box), scrolled);

	refresh_terminal_config_list (list_dialog);

	/* Buttons: Add, then Reset | Cancel | Apply | OK (same pattern as Key Bindings, Color Schemes, etc.) */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);
	GtkWidget *add_btn	  = gtk_button_new_with_mnemonic ("_Add");
	GtkWidget *reset_btn  = gtk_button_new_with_mnemonic ("_Reset");
	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *apply_btn  = gtk_button_new_with_mnemonic ("_Apply");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), add_btn);
	gtk_box_append (GTK_BOX (button_box), reset_btn);
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), apply_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);
	g_signal_connect (add_btn, "clicked", G_CALLBACK (terminal_config_add_clicked), list_dialog);
	g_signal_connect (reset_btn, "clicked", G_CALLBACK (list_settings_reset), &list_dialog->ops);
	g_signal_connect (cancel_btn, "clicked", G_CALLBACK (list_settings_cancel), &list_dialog->ops);
	g_signal_connect (apply_btn, "clicked", G_CALLBACK (list_settings_apply), &list_dialog->ops);
	g_signal_connect (ok_btn, "clicked", G_CALLBACK (list_settings_ok), &list_dialog->ops);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 600, 400);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void show_terminal_config_editor (GtkButton *button, gpointer user_data)
{
	PrefsDialog *prefs = (PrefsDialog *) user_data;
	show_terminal_config_editor_impl (prefs->dialog, prefs->window_n, -1, -1);
}

static void show_terminal_config_editor_for_range (GtkWidget *parent_dialog, long int window_n, int range_start, int range_end)
{
	show_terminal_config_editor_impl (parent_dialog, window_n, range_start, range_end);
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
	GtkWidget	   *dialog;
	GtkWidget	   *list_box;
	long int		window_n;
	bind_button_t  *working_buttons;
	bind_button_t  *original_buttons;
	ListSettingsOps ops;
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

	bind_actions_t	action = (action_idx == 0) ? BIND_ACT_OPEN_URI : BIND_ACT_CUT_URI;
	GdkModifierType state  = 0;
	gtk_accelerator_parse (state_str, NULL, &state);

	ButtonBindListDialog *list_dialog = edit->list_dialog;
	bind_button_t		 *check_list  = list_dialog ? list_dialog->working_buttons : terms.buttons;

	for (bind_button_t *cur = check_list; cur; cur = cur->next) {
		if (cur != edit->editing_bind && cur->button == button && cur->state == state) {
			GtkAlertDialog *alert = gtk_alert_dialog_new ("A binding for this button and modifier combination already exists.");
			gtk_alert_dialog_show (alert, GTK_WINDOW (edit->dialog));
			g_object_unref (alert);
			return;
		}
	}

	bind_button_t *bind;
	if (edit->editing_bind) {
		bind = edit->editing_bind;
	} else if (list_dialog) {
		bind						 = calloc (1, sizeof (bind_button_t));
		bind->next					 = list_dialog->working_buttons;
		list_dialog->working_buttons = bind;
	} else {
		bind		  = calloc (1, sizeof (bind_button_t));
		bind->next	  = terms.buttons;
		terms.buttons = bind;
	}

	bind->action = action;
	bind->button = button;
	bind->state	 = state;

	if (list_dialog)
		refresh_button_bind_list (list_dialog);
	else
		zterm_save_config ();

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
	gtk_editable_set_width_chars (GTK_EDITABLE (edit->state_entry), 20);
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

	gtk_window_set_default_size (GTK_WINDOW (dialog), 500, 280);
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

	bind_button_t **prev = &list_dialog->working_buttons;
	for (bind_button_t *cur = list_dialog->working_buttons; cur; cur = cur->next) {
		if (cur == bind) {
			*prev = cur->next;
			free (cur);
			break;
		}
		prev = &cur->next;
	}
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

	for (bind_button_t *cur = list_dialog->working_buttons; cur; cur = cur->next) {
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

static void button_bind_commit (void *ctx)
{
	ButtonBindListDialog *list_dialog = (ButtonBindListDialog *) ctx;
	button_bind_list_free (terms.buttons);
	terms.buttons = button_bind_list_clone (list_dialog->working_buttons);
}

static void button_bind_snapshot (void *ctx)
{
	ButtonBindListDialog *list_dialog = (ButtonBindListDialog *) ctx;
	button_bind_list_free (list_dialog->original_buttons);
	list_dialog->original_buttons = button_bind_list_clone (list_dialog->working_buttons);
}

static void button_bind_restore_config (void *ctx)
{
	ButtonBindListDialog *list_dialog = (ButtonBindListDialog *) ctx;
	button_bind_list_free (terms.buttons);
	terms.buttons = button_bind_list_clone (list_dialog->original_buttons);
}

static void button_bind_restore_working (void *ctx)
{
	ButtonBindListDialog *list_dialog = (ButtonBindListDialog *) ctx;
	button_bind_list_free (list_dialog->working_buttons);
	list_dialog->working_buttons = button_bind_list_clone (list_dialog->original_buttons);
}

static void button_bind_refresh_ui (void *ctx)
{
	refresh_button_bind_list ((ButtonBindListDialog *) ctx);
}

static void button_bind_destroy (void *ctx)
{
	ButtonBindListDialog *list_dialog = (ButtonBindListDialog *) ctx;
	GtkWidget			 *dialog	  = list_dialog->dialog;
	button_bind_list_free (list_dialog->working_buttons);
	button_bind_list_free (list_dialog->original_buttons);
	free (list_dialog);
	gtk_window_destroy (GTK_WINDOW (dialog));
}

static void show_button_bind_editor (GtkButton *button, gpointer user_data)
{
	PrefsDialog			 *prefs		  = (PrefsDialog *) user_data;
	ButtonBindListDialog *list_dialog = g_new0 (ButtonBindListDialog, 1);
	list_dialog->window_n			  = prefs->window_n;
	list_dialog->working_buttons	  = button_bind_list_clone (terms.buttons);
	list_dialog->original_buttons	  = button_bind_list_clone (terms.buttons);
	list_dialog->ops.ctx			  = list_dialog;
	list_dialog->ops.commit			  = button_bind_commit;
	list_dialog->ops.snapshot		  = button_bind_snapshot;
	list_dialog->ops.restore_config	  = button_bind_restore_config;
	list_dialog->ops.restore_working  = button_bind_restore_working;
	list_dialog->ops.refresh_ui		  = button_bind_refresh_ui;
	list_dialog->ops.destroy		  = button_bind_destroy;
	list_dialog->ops.after_commit	  = NULL;

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
	gtk_widget_set_size_request (scrolled, 420, 260);
	gtk_box_append (GTK_BOX (main_box), scrolled);

	list_dialog->list_box = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_dialog->list_box), GTK_SELECTION_NONE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list_dialog->list_box);

	refresh_button_bind_list (list_dialog);

	/* Buttons: Add, then Reset | Cancel | Apply | OK */
	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (button_box, GTK_ALIGN_END);
	gtk_widget_set_margin_top (button_box, 12);
	gtk_box_append (GTK_BOX (main_box), button_box);

	GtkWidget *add_btn	  = gtk_button_new_with_mnemonic ("_Add");
	GtkWidget *reset_btn  = gtk_button_new_with_mnemonic ("_Reset");
	GtkWidget *cancel_btn = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *apply_btn  = gtk_button_new_with_mnemonic ("_Apply");
	GtkWidget *ok_btn	  = gtk_button_new_with_mnemonic ("_OK");
	gtk_box_append (GTK_BOX (button_box), add_btn);
	gtk_box_append (GTK_BOX (button_box), reset_btn);
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), apply_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	g_signal_connect (add_btn, "clicked", G_CALLBACK (button_bind_add_clicked), list_dialog);
	g_signal_connect (reset_btn, "clicked", G_CALLBACK (list_settings_reset), &list_dialog->ops);
	g_signal_connect (cancel_btn, "clicked", G_CALLBACK (list_settings_cancel), &list_dialog->ops);
	g_signal_connect (apply_btn, "clicked", G_CALLBACK (list_settings_apply), &list_dialog->ops);
	g_signal_connect (ok_btn, "clicked", G_CALLBACK (list_settings_ok), &list_dialog->ops);
	debugf ("list_dialog: %p, ops: %p", list_dialog, &list_dialog->ops);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 540, 360);
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
	gtk_editable_set_width_chars (GTK_EDITABLE (prefs->font_entry), 32);
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
	gtk_editable_set_width_chars (GTK_EDITABLE (prefs->word_char_entry), 24);
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

	/* Environment button */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Environment:"), 0, row, 1, 1);
	GtkWidget *env_btn = gtk_button_new_with_label ("Edit Global Environment...");
	g_signal_connect (env_btn, "clicked", G_CALLBACK (show_env_editor), prefs);
	gtk_grid_attach (GTK_GRID (grid), env_btn, 1, row++, 2, 1);

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

	/* Ignored key modifiers (bind_ignore) button */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Ignored key modifiers:"), 0, row, 1, 1);
	GtkWidget *bind_ignore_btn = gtk_button_new_with_label ("Edit...");
	g_signal_connect (bind_ignore_btn, "clicked", G_CALLBACK (show_bind_ignore_editor), prefs);
	gtk_grid_attach (GTK_GRID (grid), bind_ignore_btn, 1, row++, 2, 1);

	/* Terminal Configuration button */
	gtk_grid_attach (GTK_GRID (grid), create_label ("Terminal Configuration:"), 0, row, 1, 1);
	GtkWidget *terminal_config_btn = gtk_button_new_with_label ("Configure Terminals...");
	g_signal_connect (terminal_config_btn, "clicked", G_CALLBACK (show_terminal_config_editor), prefs);
	gtk_grid_attach (GTK_GRID (grid), terminal_config_btn, 1, row++, 2, 1);

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
	GtkWidget *reset_btn   = gtk_button_new_with_mnemonic ("_Reset");
	GtkWidget *cancel_btn  = gtk_button_new_with_mnemonic ("_Cancel");
	GtkWidget *apply_btn   = gtk_button_new_with_mnemonic ("_Apply");
	GtkWidget *ok_btn	   = gtk_button_new_with_mnemonic ("_OK");

	/* Button order: Reset | Cancel | Apply | OK (GNOME convention; Preview first) */
	gtk_box_append (GTK_BOX (button_box), preview_btn);
	gtk_box_append (GTK_BOX (button_box), reset_btn);
	gtk_box_append (GTK_BOX (button_box), cancel_btn);
	gtk_box_append (GTK_BOX (button_box), apply_btn);
	gtk_box_append (GTK_BOX (button_box), ok_btn);

	/* Connect button signals */
	g_signal_connect_swapped (preview_btn, "clicked", G_CALLBACK (prefs_preview_clicked), prefs);
	g_signal_connect_swapped (reset_btn, "clicked", G_CALLBACK (prefs_revert_clicked), prefs);
	g_signal_connect_swapped (cancel_btn, "clicked", G_CALLBACK (prefs_cancel_clicked), prefs);
	g_signal_connect_swapped (apply_btn, "clicked", G_CALLBACK (prefs_apply_clicked), prefs);
	g_signal_connect_swapped (ok_btn, "clicked", G_CALLBACK (prefs_ok_clicked), prefs);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 540, 520);
	gtk_window_present (GTK_WINDOW (dialog));
}

// vim: set ts=4 sw=4 noexpandtab :
