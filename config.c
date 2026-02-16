#include "zterm.h"
#include <libconfig.h>
#include <regex.h>
#include <string.h>

static config_t cfg;
static bool		cfg_initialized = false;

void zterm_free_terminal_configs (void)
{
	if (terms.terminal_configs == NULL) {
		return;
	}
	for (int i = 0; i < MAX_TABS; i++) {
		if (terms.terminal_configs[i].argv) {
			g_strfreev ((char **) terms.terminal_configs[i].argv);
			terms.terminal_configs[i].argv = NULL;
		}
		if (terms.terminal_configs[i].env) {
			g_strfreev ((char **) terms.terminal_configs[i].env);
			terms.terminal_configs[i].env = NULL;
		}
		free ((char *) terms.terminal_configs[i].working_directory);
		terms.terminal_configs[i].working_directory = NULL;
	}
	free (terms.terminal_configs);
	terms.terminal_configs = NULL;
}

void zterm_ensure_terminal_configs (void)
{
	if (terms.terminal_configs == NULL) {
		terms.terminal_configs = calloc (MAX_TABS, sizeof (terminal_config_t));
	}
}

static void clear_one_terminal_config (terminal_config_t *tc)
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

static bool same_terminal_config (terminal_config_t *a, terminal_config_t *b)
{
	if ((a->argv == NULL) != (b->argv == NULL))
		return false;
	if (a->argv != NULL && !g_strv_equal (a->argv, b->argv))
		return false;
	if ((a->env == NULL) != (b->env == NULL))
		return false;
	if (a->env != NULL && !g_strv_equal (a->env, b->env))
		return false;
	if (a->working_directory == NULL && b->working_directory == NULL) {
		return true;
	}
	if (a->working_directory == NULL || b->working_directory == NULL) {
		return false;
	}
	return strcmp (a->working_directory, b->working_directory) == 0;
}

static void add_terminal_config_to_list (config_setting_t *terminals_list, terminal_config_t *tc, int index, int end_index)
{
	config_setting_t *entry	 = config_setting_add (terminals_list, NULL, CONFIG_TYPE_GROUP);
	config_setting_t *ranges = config_setting_add (entry, "ranges", CONFIG_TYPE_LIST);
	if (index == end_index) {
		config_setting_t *elem = config_setting_add (ranges, NULL, CONFIG_TYPE_INT);
		config_setting_set_int (elem, index);
	} else {
		config_setting_t *range_group = config_setting_add (ranges, NULL, CONFIG_TYPE_GROUP);
		config_setting_t *start_set	  = config_setting_add (range_group, "start", CONFIG_TYPE_INT);
		config_setting_t *end_set	  = config_setting_add (range_group, "end", CONFIG_TYPE_INT);
		config_setting_set_int (start_set, index);
		config_setting_set_int (end_set, end_index);
	}
	if (tc->argv != NULL) {
		config_setting_t *cmd = config_setting_add (entry, "command", CONFIG_TYPE_ARRAY);
		for (int j = 0; tc->argv[j] != NULL; j++) {
			config_setting_t *arg = config_setting_add (cmd, NULL, CONFIG_TYPE_STRING);
			config_setting_set_string (arg, tc->argv[j]);
		}
	}
	if (tc->working_directory != NULL) {
		config_setting_t *dir = config_setting_add (entry, "directory", CONFIG_TYPE_STRING);
		config_setting_set_string (dir, tc->working_directory);
	}
	if (tc->env != NULL) {
		config_setting_t *env = config_setting_add (entry, "env", CONFIG_TYPE_ARRAY);
		for (int j = 0; tc->env[j] != NULL; j++) {
			config_setting_t *e = config_setting_add (env, NULL, CONFIG_TYPE_STRING);
			config_setting_set_string (e, tc->env[j]);
		}
	}
}

void zterm_set_terminal_config (int index, const char **argv, const char **env, const char *working_directory)
{
	if (index < 0 || index >= MAX_TABS) {
		return;
	}
	zterm_ensure_terminal_configs ();
	terminal_config_t *tc = &terms.terminal_configs[index];
	clear_one_terminal_config (tc);
	tc->argv			  = argv ? (const gchar **) g_strdupv ((gchar **) argv) : NULL;
	tc->env				  = env ? (const gchar **) g_strdupv ((gchar **) env) : NULL;
	tc->working_directory = working_directory ? strdup (working_directory) : NULL;
}

void zterm_set_terminal_config_range (int base, int count, const char **argv, const char **env)
{
	if (count <= 0 || base < 0) {
		return;
	}
	zterm_ensure_terminal_configs ();
	for (int i = 0; i < count && (base + i) < MAX_TABS; i++) {
		zterm_set_terminal_config (base + i, argv, env, NULL);
	}
}

static int zregcomp (regex_t *restrict preg, const char *restrict regex, int cflags)
{
	int ret = regcomp (preg, regex, cflags);
	if (ret) {
		char errbuf[128] = {0};

		regerror (ret, preg, errbuf, sizeof (errbuf) - 1);
		errorf ("regcomp failed: %d (%s) for regex '%s'", ret, errbuf, regex);
	}

	return ret;
}

static void zterm_parse_bind_switch (int base, char *state, char *key_min, char *key_max)
{
	bind_t *bind = calloc (1, sizeof (bind_t));
	bind->next	 = terms.keys;
	terms.keys	 = bind;

	bind->action = BIND_ACT_SWITCH;
	bind->base = base, bind->state = strtol (state, NULL, 0);
	// The new style is actually a GTK accelerator string, but with only the modifier component.
	if (bind->state == 0) {
		gtk_accelerator_parse (state, NULL, &bind->state);
		if (bind->state) {
			debugf ("Parsing '%s' as accelerator, result: 0x%x", state, bind->state);
		} else {
			errorf ("Error: Unable to parse '%s' as GTK Accelerator, skipping bind: %d %s %s-%s", state, base, state, key_min,
					key_max);
			return;
		}
	}
	bind->key_min = gdk_keyval_from_name (key_min);
	if (key_max)
		bind->key_max = gdk_keyval_from_name (key_max);
	else
		bind->key_max = bind->key_min;

	// We need to find the highest terminal number that this binding can use.
	// We then make sure that n_active is, at minimum, that number.
	{
		int n		   = bind->base + (bind->key_max - bind->key_min) + 1;
		terms.n_active = MAX (terms.n_active, n);
	}
}

static void temu_parse_bind_switch (char **subs)
{
	int base = strtol (subs[0], NULL, 0);
	zterm_parse_bind_switch (base, subs[1], subs[2], subs[4]);

	/* Legacy: optional command at end of line → store as terminal config for this binding's range */
	if (subs[6] && subs[6][0]) {
		int		key_min = gdk_keyval_from_name (subs[2]);
		int		key_max = subs[4] ? gdk_keyval_from_name (subs[4]) : key_min;
		int		count	= key_max - key_min + 1;
		gint	argc;
		char  **argv  = NULL;
		GError *error = NULL;
		if (g_shell_parse_argv (subs[6], &argc, &argv, &error)) {
			zterm_ensure_terminal_configs ();
			zterm_set_terminal_config_range (base, count, (const char **) argv, NULL);
			g_strfreev (argv);
		}
		g_clear_error (&error);
	}
}

static void zterm_parse_bind_action (char *action, char *state, char *key)
{
	char	bind_str[128] = {0};
	bind_t *bind		  = calloc (1, sizeof (bind_t));

	if (!strcasecmp (action, "CUT")) {
		bind->action = BIND_ACT_CUT;
	} else if (!strcasecmp (action, "CUT_HTML")) {
		bind->action = BIND_ACT_CUT_HTML;
	} else if (!strcasecmp (action, "CUT_URI")) {
		bind->action = BIND_ACT_CUT_URI;
	} else if (!strcasecmp (action, "PASTE")) {
		bind->action = BIND_ACT_PASTE;
	} else if (!strcasecmp (action, "MENU")) {
		bind->action = BIND_ACT_MENU;
	} else if (!strcasecmp (action, "NEXT_TERM")) {
		bind->action = BIND_ACT_NEXT_TERM;
	} else if (!strcasecmp (action, "PREV_TERM")) {
		bind->action = BIND_ACT_PREV_TERM;
	} else {
		errorf ("Unknown bind action '%s'.", action);
		free (bind);
		return;
	}

	bind->next = terms.keys;
	terms.keys = bind;

	// The new style is actually a GTK accelerator string, but with only the modifier component.
	snprintf (bind_str, sizeof (bind_str) - 1, "%s%s", state, key);

	gtk_accelerator_parse (bind_str, &bind->key_max, &bind->state);
	debugf ("Parsing '%s' as accelerator, result: state: 0x%x, keyval: 0x%x", bind_str, bind->state, bind->key_max);
	if (bind->key_max) {
		bind->key_min = bind->key_max;
	} else {
		int ret		  = gtk_accelerator_parse (state, NULL, &bind->state);
		bind->key_min = bind->key_max = strtol (key, NULL, 0);
		debugf ("Parsing '%s' as partial accelerator, result: state: 0x%x, keyval: 0x%x, ret: %d", bind_str, bind->state,
				bind->key_max, ret);
		if (!ret) {
			errorf ("Error: Unable to parse '%s' as GTK Accelerator, skipping bind: %s %s %s", state, action, state, key);
			return;
		} else if (!bind->key_max) {
			errorf ("Error: Unable to parse '%s' as key, skipping bind: %s %s %s", key, action, state, key);
			return;
		}
	}

	debugf ("Binding: keyval: 0x%x, state: 0x%x, action: %d (%s %s %s)", bind->key_min, bind->state, bind->action, action, state,
			key);
}

static void temu_parse_bind_action (char **subs)
{
	zterm_parse_bind_action (subs[0], subs[1], subs[2]);
}

static void zterm_parse_bind_button (char *action, char *state, int button)
{
	bind_button_t *bind = calloc (1, sizeof (bind_button_t));

	debugf ();
	if (!strcasecmp (action, "OPEN_URI")) {
		bind->action = BIND_ACT_OPEN_URI;
	} else if (!strcasecmp (action, "CUT_URI")) {
		bind->action = BIND_ACT_CUT_URI;
	} else {
		errorf ("Unknown bind action '%s'.", action);
		free (bind);
		return;
	}

	bind->button = button;

	bind->next	  = terms.buttons;
	terms.buttons = bind;

	int ret = gtk_accelerator_parse (state, NULL, &bind->state);
	debugf ("Parsing '%s' as partial accelerator, result: state: 0x%x, button: %d, ret: %d", state, bind->state, bind->button,
			ret);
	if (!ret) {
		bind->state = strtol (state, NULL, 0);
		if (bind->state) {
			debugf ("Parsing '%s' as numeric value, result: state: 0x%x, button: %d, ret: %d", state, bind->state, bind->button,
					ret);
			button_bind_mask |= bind->state;
		} else {
			errorf ("Error: Unable to parse '%s' as GTK Accelerator, skipping bind: %s %s %d", state, action, state, button);
			return;
		}

		errorf ("Error: Unable to parse '%s' as GTK Accelerator, skipping bind: %s %s %d", state, action, state, button);
		return;
	}

	debugf ("Binding: button: %d, state: 0x%x, action: %d (%s %s %d)", bind->button, bind->state, bind->action, action, state,
			button);

	gchar *name	 = gtk_accelerator_name (0, bind->state);
	gchar *label = gtk_accelerator_get_label (0, bind->state);
	debugf ("bind->state: 0x%x, %d, name: '%s', label: '%s'", bind->state, bind->state, name, label);
	g_free (label);
	g_free (name);
}

static void temu_parse_bind_button (char **subs)
{
	zterm_parse_bind_button (subs[0], subs[1], strtol (subs[2], NULL, 0));
}

static void zterm_parse_bind_ignore (char *state_input)
{
	guint key, state;

	gtk_accelerator_parse (state_input, &key, &state);

	debugf ("Parsing '%s' as accelerator, result: state: 0x%x, keyval: 0x%x", state_input, state, key);

	if (key != 0 || state == 0) {
		errorf ("Error: ignore value must only be states.");
		exit (1);
	}

	key_bind_mask &= ~state;
	button_bind_mask &= ~state;

	/* Track the ignore for saving */
	bind_ignore_t *ignore = calloc (1, sizeof (bind_ignore_t));
	ignore->state		  = state;
	ignore->next		  = terms.ignores;
	terms.ignores		  = ignore;
}

static void temu_parse_bind_ignore (char **subs)
{
	zterm_parse_bind_ignore (subs[0]);
}

static void zterm_free_settings (void)
{
	while (terms.keys != NULL) {
		bind_t *next = terms.keys->next;

		free (terms.keys);

		terms.keys = next;
	}

	while (terms.ignores != NULL) {
		bind_ignore_t *next = terms.ignores->next;
		free (terms.ignores);
		terms.ignores = next;
	}

	while (terms.color_overrides != NULL) {
		color_override_t *next = terms.color_overrides->next;
		free (terms.color_overrides);
		terms.color_overrides = next;
	}

	if (terms.env) {
		g_strfreev ((char **) terms.env);
		terms.env = NULL;
	}
}

void zterm_set_global_env (const char **env)
{
	if (terms.env)
		g_strfreev ((char **) terms.env);
	terms.env = env ? (const char **) g_strdupv ((char **) env) : NULL;
}

static void zterm_parse_color (int index, const char *value)
{
	if (index < 0 || index >= (int) (sizeof (colors) / sizeof (colors[0]))) {
		return;
	}

	gdk_rgba_parse (&colors[index], value);

	/* Track the color override for saving */
	color_override_t *override = calloc (1, sizeof (color_override_t));
	override->index			   = index;
	override->color			   = colors[index];
	override->next			   = terms.color_overrides;
	terms.color_overrides	   = override;
}

static void temu_parse_color (char **subs)
{
	int n = strtol (subs[0], NULL, 0);
	zterm_parse_color (n, subs[1]);
}

static void temu_parse_font (char **subs)
{
	if (terms.font)
		free (terms.font);
	terms.font = strdup (subs[0]);
}

static void temu_parse_size (char **subs)
{
	start_width	 = strtol (subs[0], NULL, 10);
	start_height = strtol (subs[1], NULL, 10);
}

static void gen_subs (char *str, char *subs[], regmatch_t matches[], size_t count)
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

static void free_subs (char *subs[], size_t count)
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
#	define REG_ENHANCED 0
#endif

bool temu_parse_config (void)
{
#define MATCHES 16
	regex_t	   bind_action, bind_button, bind_switch, bind_ignore, color, color_scheme, font, size, env, other;
	regmatch_t regexp_matches[MATCHES];
	char	  *subs[MATCHES] = {0};
	FILE	  *f;
	char	  *file = NULL;
	long	   f_len;
	int		   j, ret;
	char	  *t1, *t2;
	char	   conffile[512] = {0};
	size_t	   read;
	int		   n_color_scheme = 0;

	zregcomp (&bind_action, "^bind:[ \t]+([a-zA-Z_]+)[ \t]+([^\\s]+)[ \t]+([a-zA-Z0-9_]+)$", REG_ENHANCED | REG_EXTENDED);
	zregcomp (&bind_button, "^bind_button:[ \t]+([a-zA-Z_]+)[ \t]+([^\\s]+)[ \t]+([0-9_]+)$", REG_ENHANCED | REG_EXTENDED);
	zregcomp (&bind_switch, "^bind:[ \t]+([0-9]+)[ \t]+([^\\s]+)[ \t]+([a-zA-Z0-9_]+)(-([a-zA-Z0-9_]+))?([ \t]+(.*?)+)?$",
			  REG_ENHANCED | REG_EXTENDED);

	zregcomp (&bind_ignore, "^ignore_mod:[ \t]+([^ \t]+)$", REG_ENHANCED | REG_EXTENDED);
	zregcomp (&color, "^color:[ \t]+([0-9]+)[ \t]+(.*?)$", REG_ENHANCED | REG_EXTENDED);
	zregcomp (&color_scheme, "^color_scheme:[ \t]+([-a-zA-Z0-9_ ]*?)[ \t]+(#[0-9a-fA-F]+)[ \t]+(#[0-9a-fA-F]+)$",
			  REG_ENHANCED | REG_EXTENDED);
	zregcomp (&font, "^font:[ \t]+(.*?)$", REG_ENHANCED | REG_EXTENDED);
	zregcomp (&size, "^size:[ \t]+([0-9]+)x([0-9]+)$", REG_ENHANCED | REG_EXTENDED);
	zregcomp (&env, "^env:[ \t]+([^=]*?)=(.*?)$", REG_ENHANCED | REG_EXTENDED);
	zregcomp (&other, "^([^: ]*):[ \t]+(.*?)$", REG_ENHANCED | REG_EXTENDED);

	zterm_free_settings ();
	// FIXME: We need to correctly handle the case where this number changes with a reload, it's going to be a bit rough.
	terms.n_active = 0;

	snprintf (conffile, sizeof (conffile) - 1, "%s/.zterm/config", getenv ("HOME"));
	f = fopen (conffile, "r");
	if (!f)
		goto done;
	fseek (f, 0, SEEK_END);
	f_len = ftell (f);
	fseek (f, 0, SEEK_SET);
	file = calloc (1, f_len + 1);
	read = fread (file, f_len, 1, f);
	fclose (f);
	if (read != 1)
		goto done;

	t1 = file;
	while (*t1) {
		t2	  = strchr (t1, '\n');
		*t2++ = '\0';

		j = t1[0] == '#' ? 1 : 0;
		if (!j) {
			j = t1[0] == '\0' ? 1 : 0;
		}

#define add_regex(name)                                                                                                          \
	do {                                                                                                                         \
		if (!j) {                                                                                                                \
			ret = regexec (&name, t1, MATCHES, regexp_matches, 0);                                                               \
			if (!ret) {                                                                                                          \
				debugf ();                                                                                                       \
				gen_subs (t1, subs, regexp_matches, MATCHES);                                                                    \
				temu_parse_##name (subs);                                                                                        \
				free_subs (subs, MATCHES);                                                                                       \
				j++;                                                                                                             \
			}                                                                                                                    \
		}                                                                                                                        \
	} while (0)

		add_regex (bind_action);
		add_regex (bind_switch);
		add_regex (bind_ignore);
		add_regex (bind_button);
		add_regex (color);
		add_regex (font);
		add_regex (size);

		if (!j && n_color_scheme < (sizeof (terms.color_schemes) / sizeof (terms.color_schemes[0]))) {
			ret = regexec (&color_scheme, t1, MATCHES, regexp_matches, 0);
			if (!ret) {
				gen_subs (t1, subs, regexp_matches, MATCHES);
				strlcpy (terms.color_schemes[n_color_scheme].name, subs[0], sizeof (terms.color_schemes[n_color_scheme].name));
				snprintf (terms.color_schemes[n_color_scheme].action, sizeof (terms.color_schemes[n_color_scheme].action),
						  "color_scheme.%d", n_color_scheme);
				gdk_rgba_parse (&terms.color_schemes[n_color_scheme].foreground, subs[1]);
				gdk_rgba_parse (&terms.color_schemes[n_color_scheme].background, subs[2]);
				n_color_scheme++;
				free_subs (subs, MATCHES);
				j++;
			}
		}

		if (!j) {
			ret = regexec (&env, t1, MATCHES, regexp_matches, 0);
			if (!ret) {
				gen_subs (t1, subs, regexp_matches, MATCHES);
				setenv (subs[0], subs[1], 1);
				free_subs (subs, MATCHES);
				j++;
			}
		}

		if (!j) {
			ret = regexec (&other, t1, MATCHES, regexp_matches, 0);
			if (!ret) {
				gen_subs (t1, subs, regexp_matches, MATCHES);
				if (!strcmp (subs[0], "audible_bell")) {
					terms.audible_bell = atoi (subs[1]);
				} else if (!strcmp (subs[0], "word_char_exceptions")) {
					strlcpy (terms.word_char_exceptions, subs[1], sizeof (terms.word_char_exceptions));
				} else if (!strcmp (subs[0], "font_scale")) {
					terms.font_scale = atof (subs[1]);
				} else if (!strcmp (subs[0], "scroll_on_output")) {
					terms.scroll_on_output = atoi (subs[1]);
				} else if (!strcmp (subs[0], "scroll_on_keystroke")) {
					terms.scroll_on_keystroke = atoi (subs[1]);
				} else if (!strcmp (subs[0], "scrollback_lines")) {
					terms.scrollback_lines = atoi (subs[1]);
				} else if (!strcmp (subs[0], "bold_is_bright")) {
					terms.bold_is_bright = atoi (subs[1]);
				} else if (!strcmp (subs[0], "mouse_autohide")) {
					terms.mouse_autohide = atoi (subs[1]);
				} else {
					errorf ("Unable to parse line in config: '%s' (%s)", t1, subs[0]);
				}
				free_subs (subs, MATCHES);
				j++;
			}
		}

		if (!j) {
			errorf ("Unable to parse line in config: '%s'", t1);
		}

		t1 = t2;
	}

done:
	regfree (&bind_action);
	regfree (&bind_button);
	regfree (&bind_switch);
	regfree (&bind_ignore);
	regfree (&color);
	regfree (&color_scheme);
	regfree (&font);
	regfree (&size);
	regfree (&env);
	regfree (&other);
	free (file);

	return true;
}

static const char **get_config_str_vec_from_setting (config_setting_t *setting);

const char **get_config_str_vec (config_t *cfg, const char *path)
{
	config_setting_t *setting = config_lookup (cfg, path);
	return get_config_str_vec_from_setting (setting);
}

static const char **get_config_str_vec_from_setting (config_setting_t *setting)
{
	if (setting == NULL || config_setting_type (setting) != CONFIG_TYPE_ARRAY) {
		return NULL;
	}
	int			 n	 = config_setting_length (setting);
	const char **vec = g_new0 (const char *, n + 1);
	for (int i = 0; i < n; i++) {
		vec[i] = config_setting_get_string_elem (setting, i);
	}
	vec[n] = NULL;
	return (const char **) g_strdupv ((char **) vec);
}

static void zterm_parse_env (const char *name, const char *value)
{
	if (name == NULL || value == NULL) {
		return;
	}

	setenv (name, value, 1);
}

const char *zterm_config_file ()
{
	static char conffile[512] = {0};
	char	   *conf_dir	  = getenv ("XDG_CONFIG_HOME");

	if (conf_dir == NULL) {
		snprintf (conffile, sizeof (conffile) - 1, "%s/.config/zterm.conf", getenv ("HOME"));
	} else {
		snprintf (conffile, sizeof (conffile) - 1, "%s/zterm.conf", conf_dir);
	}

	return conffile;
}

bool zterm_parse_config ()
{
	if (cfg_initialized) {
		config_destroy (&cfg);
		cfg_initialized = false;
	}
	config_init (&cfg);
	cfg_initialized = true;

	const char *filename = zterm_config_file ();

	if (!config_read_file (&cfg, filename)) {
		errorf ("Unable to read config file '%s': %s at line %d", filename, config_error_text (&cfg), config_error_line (&cfg));
		return false;
	}

	zterm_free_terminal_configs ();

	/* Clear previous global env; build terms.env as "KEY=value" / "!VAR" (same as per-terminal) */
	if (terms.env) {
		g_strfreev ((char **) terms.env);
		terms.env = NULL;
	}
	config_setting_t *env_setting = config_lookup (&cfg, "env");
	if (env_setting != NULL) {
		GPtrArray *arr = g_ptr_array_new_with_free_func (g_free);
		if (config_setting_type (env_setting) == CONFIG_TYPE_GROUP) {
			int n = config_setting_length (env_setting);
			for (int i = 0; i < n; i++) {
				config_setting_t *item = config_setting_get_elem (env_setting, i);
				const char		 *name = config_setting_name (item);
				if (name == NULL)
					continue;
				if (config_setting_type (item) == CONFIG_TYPE_BOOL) {
					int val = config_setting_get_bool (item);
					if (!val)
						g_ptr_array_add (arr, g_strdup_printf ("!%s", name));
					else {
						zterm_parse_env (name, "1");
						g_ptr_array_add (arr, g_strdup_printf ("%s=1", name));
					}
				} else if (config_setting_type (item) == CONFIG_TYPE_STRING) {
					const char *value = config_setting_get_string (item);
					if (value != NULL) {
						zterm_parse_env (name, value);
						g_ptr_array_add (arr, g_strdup_printf ("%s=%s", name, value));
					}
				}
			}
		} else if (config_setting_type (env_setting) == CONFIG_TYPE_ARRAY) {
			int n = config_setting_length (env_setting);
			for (int i = 0; i < n; i++) {
				const char *s = config_setting_get_string_elem (env_setting, i);
				if (s == NULL)
					continue;
				if (s[0] == '!' && s[1] != '\0') {
					const char *varname = s + 1;
					const char *eq		= strchr (varname, '=');
					size_t		len		= eq != NULL ? (size_t) (eq - varname) : strlen (varname);
					if (len > 0)
						g_ptr_array_add (arr, g_strdup_printf ("!%.*s", (int) len, varname));
				} else {
					const char *eq = strchr (s, '=');
					if (eq != NULL && eq > s) {
						char *name = g_strndup (s, (gsize) (eq - s));
						zterm_parse_env (name, eq + 1);
						g_free (name);
						g_ptr_array_add (arr, g_strdup (s));
					}
				}
			}
		}
		if (arr->len > 0) {
			char **sv = g_new (char *, arr->len + 1);
			for (guint i = 0; i < arr->len; i++)
				sv[i] = g_strdup (g_ptr_array_index (arr, i));
			sv[arr->len] = NULL;
			terms.env	 = (const char **) sv;
		}
		g_ptr_array_free (arr, TRUE);
	}

	/* Parse simple settings */
	const char *str_value;
	int			int_value;
	double		double_value;

	if (config_lookup_string (&cfg, "font", &str_value)) {
		if (terms.font)
			free (terms.font);
		terms.font = strdup (str_value);
	}

	if (config_lookup_string (&cfg, "word_char_exceptions", &str_value)) {
		if (terms.word_char_exceptions)
			free (terms.word_char_exceptions);
		terms.word_char_exceptions = strdup (str_value);
	}

	if (config_lookup_bool (&cfg, "audible_bell", &int_value)) {
		terms.audible_bell = int_value ? true : false;
	}

	if (config_lookup_float (&cfg, "font_scale", &double_value)) {
		terms.font_scale = double_value;
	}

	if (config_lookup_bool (&cfg, "scroll_on_output", &int_value)) {
		terms.scroll_on_output = int_value ? true : false;
	}

	if (config_lookup_bool (&cfg, "scroll_on_keystroke", &int_value)) {
		terms.scroll_on_keystroke = int_value ? true : false;
	}

	if (config_lookup_int (&cfg, "scrollback_lines", &int_value)) {
		terms.scrollback_lines = int_value;
	}

	if (config_lookup_bool (&cfg, "bold_is_bright", &int_value)) {
		terms.bold_is_bright = int_value ? true : false;
	}

	if (config_lookup_bool (&cfg, "mouse_autohide", &int_value)) {
		terms.mouse_autohide = int_value ? true : false;
	}

	if (config_lookup_string (&cfg, "size", &str_value)) {
		sscanf (str_value, "%dx%d", &start_width, &start_height);
	}

	/* Parse color overrides */
	config_setting_t *color_list = config_lookup (&cfg, "color");
	if (color_list != NULL) {
		int n = config_setting_length (color_list);
		for (int i = 0; i < n; i++) {
			config_setting_t *color = config_setting_get_elem (color_list, i);
			int				  index;
			const char		 *value;
			if (config_setting_lookup_int (color, "index", &index) && config_setting_lookup_string (color, "value", &value)) {
				if (index >= 0 && index < (int) (sizeof (colors) / sizeof (colors[0]))) {
					zterm_parse_color (index, value);
				} else {
					errorf ("%s:%d Invalid color index %d, max is %d", config_setting_source_file (color),
							config_setting_source_line (color), index, (int) (sizeof (colors) / sizeof (colors[0])) - 1);
				}
			}
		}
	}

	/* Parse color schemes */
	config_setting_t *scheme_list = config_lookup (&cfg, "color_schemes");
	if (scheme_list != NULL) {
		int n = config_setting_length (scheme_list);
		for (int i = 0; i < n && i < MAX_COLOR_SCHEMES; i++) {
			config_setting_t *scheme = config_setting_get_elem (scheme_list, i);
			const char		 *fg, *bg, *name;

			if (!config_setting_lookup_string (scheme, "name", &name)) {
				errorf ("Color scheme with no name, skipping.");
				continue;
			}

			debugf ("Parsing color scheme '%s'", name);
			strlcpy (terms.color_schemes[i].name, name, sizeof (terms.color_schemes[i].name));
			snprintf (terms.color_schemes[i].action, sizeof (terms.color_schemes[i].action), "color_scheme.%d", i);

			if (config_setting_lookup_string (scheme, "foreground", &fg)) {
				gdk_rgba_parse (&terms.color_schemes[i].foreground, fg);
			}
			if (config_setting_lookup_string (scheme, "background", &bg)) {
				gdk_rgba_parse (&terms.color_schemes[i].background, bg);
			}
		}
		if (n > MAX_COLOR_SCHEMES) {
			errorf ("Too many color schemes defined, max is %d", MAX_COLOR_SCHEMES);
		}
	}

	/* Parse bind_action entries */
	config_setting_t *bind_action_list = config_lookup (&cfg, "bind_action");
	if (bind_action_list != NULL) {
		int n = config_setting_length (bind_action_list);
		for (int i = 0; i < n; i++) {
			config_setting_t *bind = config_setting_get_elem (bind_action_list, i);
			const char		 *action, *state, *key;
			if (config_setting_lookup_string (bind, "action", &action) && config_setting_lookup_string (bind, "state", &state) &&
				config_setting_lookup_string (bind, "key", &key)) {
				zterm_parse_bind_action ((char *) action, (char *) state, (char *) key);
			}
		}
	}

	/* Parse bind_button_action entries */
	config_setting_t *bind_button_list = config_lookup (&cfg, "bind_button_action");
	if (bind_button_list != NULL) {
		int n = config_setting_length (bind_button_list);
		for (int i = 0; i < n; i++) {
			config_setting_t *bind = config_setting_get_elem (bind_button_list, i);
			const char		 *action, *state;
			int				  button;
			if (config_setting_lookup_string (bind, "action", &action) && config_setting_lookup_string (bind, "state", &state) &&
				config_setting_lookup_int (bind, "button", &button)) {
				zterm_parse_bind_button ((char *) action, (char *) state, button);
			}
		}
	}

	/* Parse bind_switch entries (key bindings only; terminal command/env come from "terminals" or legacy per-entry cmd/env) */
	config_setting_t *bind_switch_list = config_lookup (&cfg, "bind_switch");
	if (bind_switch_list != NULL) {
		int n = config_setting_length (bind_switch_list);
		for (int i = 0; i < n; i++) {
			config_setting_t *bind = config_setting_get_elem (bind_switch_list, i);
			const char		 *key_min, *key_max = NULL, *state;
			int				  base;

			if (!config_setting_lookup_string (bind, "key_min", &key_min) ||
				!config_setting_lookup_string (bind, "state", &state) || !config_setting_lookup_int (bind, "base", &base)) {
				errorf ("%s:%d: Invalid bind_switch entry, missing required fields.", config_setting_source_file (bind),
						config_setting_source_line (bind));
				continue;
			}

			config_setting_lookup_string (bind, "key_max", &key_max);

			zterm_parse_bind_switch (base, (char *) state, (char *) key_min, (char *) key_max);

			/* Legacy: per-entry cmd/env in bind_switch → apply to terminal config for this binding's range */
			config_setting_t *cmd_setting = config_setting_get_member (bind, "cmd");
			config_setting_t *env_setting = config_setting_get_member (bind, "env");
			if (cmd_setting != NULL || env_setting != NULL) {
				const char **argv	   = get_config_str_vec_from_setting (cmd_setting);
				const char **env	   = get_config_str_vec_from_setting (env_setting);
				int			 key_min_k = gdk_keyval_from_name (key_min);
				int			 key_max_k = key_max ? gdk_keyval_from_name (key_max) : key_min_k;
				int			 count	   = key_max_k - key_min_k + 1;
				if (count > 0) {
					zterm_ensure_terminal_configs ();
					zterm_set_terminal_config_range (base, count, argv, env);
				}
				if (argv)
					g_strfreev ((char **) argv);
				if (env)
					g_strfreev ((char **) env);
			}
		}
	}

	/* Parse bind_ignore entries */
	config_setting_t *bind_ignore_list = config_lookup (&cfg, "bind_ignore");
	if (bind_ignore_list != NULL) {
		int n = config_setting_length (bind_ignore_list);
		for (int i = 0; i < n; i++) {
			config_setting_t *bind = config_setting_get_elem (bind_ignore_list, i);
			const char		 *state;
			if (config_setting_lookup_string (bind, "state", &state)) {
				zterm_parse_bind_ignore ((char *) state);
			}
		}
	}

	return true;
}

static const char *bind_action_to_string (bind_actions_t action)
{
	switch (action) {
		case BIND_ACT_CUT:
			return "CUT";
		case BIND_ACT_CUT_HTML:
			return "CUT_HTML";
		case BIND_ACT_CUT_URI:
			return "CUT_URI";
		case BIND_ACT_PASTE:
			return "PASTE";
		case BIND_ACT_MENU:
			return "MENU";
		case BIND_ACT_NEXT_TERM:
			return "NEXT_TERM";
		case BIND_ACT_PREV_TERM:
			return "PREV_TERM";
		case BIND_ACT_OPEN_URI:
			return "OPEN_URI";
		default:
			return NULL;
	}
}

static void set_config_string (config_t *cfg, const char *path, const char *value)
{
	config_setting_t *setting = config_lookup (cfg, path);
	if (setting == NULL) {
		config_setting_t *root = config_root_setting (cfg);
		setting				   = config_setting_add (root, path, CONFIG_TYPE_STRING);
	}
	if (setting != NULL && value != NULL) {
		config_setting_set_string (setting, value);
	}
}

static void set_config_bool (config_t *cfg, const char *path, bool value)
{
	config_setting_t *setting = config_lookup (cfg, path);
	if (setting == NULL) {
		config_setting_t *root = config_root_setting (cfg);
		setting				   = config_setting_add (root, path, CONFIG_TYPE_BOOL);
	}
	if (setting != NULL) {
		config_setting_set_bool (setting, value);
	}
}

static void set_config_int (config_t *cfg, const char *path, int value)
{
	config_setting_t *setting = config_lookup (cfg, path);
	if (setting == NULL) {
		config_setting_t *root = config_root_setting (cfg);
		setting				   = config_setting_add (root, path, CONFIG_TYPE_INT);
	}
	if (setting != NULL) {
		config_setting_set_int (setting, value);
	}
}

static void set_config_float (config_t *cfg, const char *path, double value)
{
	config_setting_t *setting = config_lookup (cfg, path);
	if (setting == NULL) {
		config_setting_t *root = config_root_setting (cfg);
		setting				   = config_setting_add (root, path, CONFIG_TYPE_FLOAT);
	}
	if (setting != NULL) {
		config_setting_set_float (setting, value);
	}
}

void zterm_save_config ()
{
	if (!cfg_initialized) {
		return;
	}

	/* Save simple settings */
	if (terms.font != NULL) {
		set_config_string (&cfg, "font", terms.font);
	}

	if (terms.word_char_exceptions != NULL) {
		set_config_string (&cfg, "word_char_exceptions", terms.word_char_exceptions);
	}

	set_config_bool (&cfg, "audible_bell", terms.audible_bell);
	set_config_float (&cfg, "font_scale", terms.font_scale);
	set_config_bool (&cfg, "scroll_on_output", terms.scroll_on_output);
	set_config_bool (&cfg, "scroll_on_keystroke", terms.scroll_on_keystroke);
	set_config_int (&cfg, "scrollback_lines", terms.scrollback_lines);
	set_config_bool (&cfg, "bold_is_bright", terms.bold_is_bright);
	set_config_bool (&cfg, "mouse_autohide", terms.mouse_autohide);

	/* Save size */
	char size_str[32];
	snprintf (size_str, sizeof (size_str), "%dx%d", start_width, start_height);
	set_config_string (&cfg, "size", size_str);

	/* Save color schemes */
	config_setting_t *scheme_list = config_lookup (&cfg, "color_schemes");
	if (scheme_list != NULL) {
		config_setting_remove (config_root_setting (&cfg), "color_schemes");
	}
	scheme_list = config_setting_add (config_root_setting (&cfg), "color_schemes", CONFIG_TYPE_LIST);
	for (int i = 0; i < MAX_COLOR_SCHEMES && terms.color_schemes[i].name[0]; i++) {
		config_setting_t *scheme = config_setting_add (scheme_list, NULL, CONFIG_TYPE_GROUP);
		config_setting_t *name	 = config_setting_add (scheme, "name", CONFIG_TYPE_STRING);
		config_setting_set_string (name, terms.color_schemes[i].name);

		char fg_str[32], bg_str[32];
		snprintf (fg_str, sizeof (fg_str), "#%02x%02x%02x", (int) (terms.color_schemes[i].foreground.red * 255),
				  (int) (terms.color_schemes[i].foreground.green * 255), (int) (terms.color_schemes[i].foreground.blue * 255));
		snprintf (bg_str, sizeof (bg_str), "#%02x%02x%02x", (int) (terms.color_schemes[i].background.red * 255),
				  (int) (terms.color_schemes[i].background.green * 255), (int) (terms.color_schemes[i].background.blue * 255));

		config_setting_t *fg = config_setting_add (scheme, "foreground", CONFIG_TYPE_STRING);
		config_setting_set_string (fg, fg_str);
		config_setting_t *bg = config_setting_add (scheme, "background", CONFIG_TYPE_STRING);
		config_setting_set_string (bg, bg_str);
	}

	/* Save bind_action entries (non-switch key bindings) */
	config_setting_t *bind_action_list = config_lookup (&cfg, "bind_action");
	if (bind_action_list != NULL) {
		config_setting_remove (config_root_setting (&cfg), "bind_action");
	}
	bind_action_list = config_setting_add (config_root_setting (&cfg), "bind_action", CONFIG_TYPE_LIST);
	for (bind_t *cur = terms.keys; cur; cur = cur->next) {
		if (cur->action == BIND_ACT_SWITCH) {
			continue; /* Switch bindings are saved separately */
		}
		const char *action_str = bind_action_to_string (cur->action);
		if (action_str == NULL) {
			continue;
		}

		config_setting_t *bind	  = config_setting_add (bind_action_list, NULL, CONFIG_TYPE_GROUP);
		config_setting_t *action  = config_setting_add (bind, "action", CONFIG_TYPE_STRING);
		config_setting_t *state	  = config_setting_add (bind, "state", CONFIG_TYPE_STRING);
		config_setting_t *key_set = config_setting_add (bind, "key", CONFIG_TYPE_STRING);

		config_setting_set_string (action, action_str);

		gchar *state_str = gtk_accelerator_name (0, cur->state);
		config_setting_set_string (state, state_str);
		g_free (state_str);

		const gchar *key_str = gdk_keyval_name (cur->key_min);
		if (key_str != NULL) {
			config_setting_set_string (key_set, key_str);
		}
	}

	/* Save bind_button_action entries */
	config_setting_t *bind_button_list = config_lookup (&cfg, "bind_button_action");
	if (bind_button_list != NULL) {
		config_setting_remove (config_root_setting (&cfg), "bind_button_action");
	}
	bind_button_list = config_setting_add (config_root_setting (&cfg), "bind_button_action", CONFIG_TYPE_LIST);
	for (bind_button_t *cur = terms.buttons; cur; cur = cur->next) {
		const char *action_str = bind_action_to_string (cur->action);
		if (action_str == NULL) {
			continue;
		}

		config_setting_t *bind	 = config_setting_add (bind_button_list, NULL, CONFIG_TYPE_GROUP);
		config_setting_t *action = config_setting_add (bind, "action", CONFIG_TYPE_STRING);
		config_setting_t *state	 = config_setting_add (bind, "state", CONFIG_TYPE_STRING);
		config_setting_t *button = config_setting_add (bind, "button", CONFIG_TYPE_INT);

		config_setting_set_string (action, action_str);

		gchar *state_str = gtk_accelerator_name (0, cur->state);
		config_setting_set_string (state, state_str);
		g_free (state_str);

		config_setting_set_int (button, cur->button);
	}

	/* Save bind_switch entries */
	config_setting_t *bind_switch_list = config_lookup (&cfg, "bind_switch");
	if (bind_switch_list != NULL) {
		config_setting_remove (config_root_setting (&cfg), "bind_switch");
	}
	bind_switch_list = config_setting_add (config_root_setting (&cfg), "bind_switch", CONFIG_TYPE_LIST);
	for (bind_t *cur = terms.keys; cur; cur = cur->next) {
		if (cur->action != BIND_ACT_SWITCH) {
			continue;
		}

		config_setting_t *bind	  = config_setting_add (bind_switch_list, NULL, CONFIG_TYPE_GROUP);
		config_setting_t *base	  = config_setting_add (bind, "base", CONFIG_TYPE_INT);
		config_setting_t *state	  = config_setting_add (bind, "state", CONFIG_TYPE_STRING);
		config_setting_t *key_min = config_setting_add (bind, "key_min", CONFIG_TYPE_STRING);

		config_setting_set_int (base, cur->base);

		gchar *state_str = gtk_accelerator_name (0, cur->state);
		config_setting_set_string (state, state_str);
		g_free (state_str);

		const gchar *key_min_str = gdk_keyval_name (cur->key_min);
		if (key_min_str != NULL) {
			config_setting_set_string (key_min, key_min_str);
		}

		if (cur->key_max != cur->key_min) {
			config_setting_t *key_max	  = config_setting_add (bind, "key_max", CONFIG_TYPE_STRING);
			const gchar		 *key_max_str = gdk_keyval_name (cur->key_max);
			if (key_max_str != NULL) {
				config_setting_set_string (key_max, key_max_str);
			}
		}
	}

	/* Save terminals: coalesce consecutive indices with identical config into start/end ranges */
	if (terms.terminal_configs != NULL) {
		config_setting_t *terminals_list = config_lookup (&cfg, "terminals");
		if (terminals_list != NULL) {
			config_setting_remove (config_root_setting (&cfg), "terminals");
		}
		terminals_list = config_setting_add (config_root_setting (&cfg), "terminals", CONFIG_TYPE_LIST);
		for (int i = 0; i < MAX_TABS; i++) {
			terminal_config_t *tc = &terms.terminal_configs[i];
			if (tc->argv == NULL && tc->working_directory == NULL && tc->env == NULL) {
				continue;
			}
			int end_i = i;
			while (end_i + 1 < MAX_TABS && same_terminal_config (tc, &terms.terminal_configs[end_i + 1])) {
				end_i++;
			}
			add_terminal_config_to_list (terminals_list, tc, i, end_i);
			i = end_i;
		}
	}

	/* Save bind_ignore entries */
	config_setting_t *bind_ignore_list = config_lookup (&cfg, "bind_ignore");
	if (bind_ignore_list != NULL) {
		config_setting_remove (config_root_setting (&cfg), "bind_ignore");
	}
	bind_ignore_list = config_setting_add (config_root_setting (&cfg), "bind_ignore", CONFIG_TYPE_LIST);
	for (bind_ignore_t *cur = terms.ignores; cur; cur = cur->next) {
		config_setting_t *bind	= config_setting_add (bind_ignore_list, NULL, CONFIG_TYPE_GROUP);
		config_setting_t *state = config_setting_add (bind, "state", CONFIG_TYPE_STRING);

		gchar *state_str = gtk_accelerator_name (0, cur->state);
		config_setting_set_string (state, state_str);
		g_free (state_str);
	}

	/* Save color overrides */
	config_setting_t *color_list = config_lookup (&cfg, "color");
	if (color_list != NULL) {
		config_setting_remove (config_root_setting (&cfg), "color");
	}
	color_list = config_setting_add (config_root_setting (&cfg), "color", CONFIG_TYPE_LIST);
	for (color_override_t *cur = terms.color_overrides; cur; cur = cur->next) {
		config_setting_t *color = config_setting_add (color_list, NULL, CONFIG_TYPE_GROUP);
		config_setting_t *index = config_setting_add (color, "index", CONFIG_TYPE_INT);
		config_setting_t *value = config_setting_add (color, "value", CONFIG_TYPE_STRING);

		config_setting_set_int (index, cur->index);

		char color_str[32];
		snprintf (color_str, sizeof (color_str), "#%02x%02x%02x", (int) (cur->color.red * 255), (int) (cur->color.green * 255),
				  (int) (cur->color.blue * 255));
		config_setting_set_string (value, color_str);
	}

	/* Save environment variables */
	config_setting_t *env_list = config_lookup (&cfg, "env");
	if (env_list != NULL) {
		config_setting_remove (config_root_setting (&cfg), "env");
	}
	env_list = config_setting_add (config_root_setting (&cfg), "env", CONFIG_TYPE_GROUP);
	if (terms.env) {
		for (int i = 0; terms.env[i] != NULL; i++) {
			const char *ent = terms.env[i];
			if (ent[0] == '!' && ent[1] != '\0') {
				config_setting_t *var = config_setting_add (env_list, ent + 1, CONFIG_TYPE_BOOL);
				if (var != NULL)
					config_setting_set_bool (var, 0);
			} else {
				const char *eq = strchr (ent, '=');
				if (eq != NULL && eq > ent) {
					char			 *name = g_strndup (ent, (gsize) (eq - ent));
					config_setting_t *var  = config_setting_add (env_list, name, CONFIG_TYPE_STRING);
					if (var != NULL)
						config_setting_set_string (var, eq + 1);
					g_free (name);
				}
			}
		}
	}

	const char *filename = zterm_config_file ();
	config_write_file (&cfg, filename);
}

// vim: set ts=4 sw=4 noexpandtab :
