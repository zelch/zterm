#include "zterm.h"
#include <regex.h>
#include <string.h>
#ifdef HAVE_LIBBSD
#  include <bsd/string.h>
#endif

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
			debugf("Parsing '%s' as accelerator, result: 0x%x", subs[1], bind->state);
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

	// We need to find the highest terminal number that this binding can use.
	// We then make sure that n_active is, at minimum, that number.
	{
		int n = bind->base + (bind->key_max - bind->key_min) + 1;
		terms.n_active = MAX(terms.n_active, n);
	}
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

	debugf ("Binding: keyval: 0x%x, state: 0x%x, action: %d (%s %s %s)", bind->key_min, bind->state, bind->action, subs[0], subs[1], subs[2]);
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

static void
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

static void
free_subs (char *subs[], size_t count)
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
