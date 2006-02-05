#ifndef TEMU_SCREEN_h
#define TEMU_SCREEN_h 1

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_UNICHAR_UNKNOWN_GLYPH	((gunichar)0xfffd)

#define TEMU_SCREEN_MAX_COLORS	257
#define TEMU_SCREEN_COLOR_BITS	9
#define TEMU_SCREEN_FG_DEFAULT	256
#define TEMU_SCREEN_BG_DEFAULT	256

typedef struct _TemuScreenPrivate	TemuScreenPrivate;
typedef struct _TemuScreen		TemuScreen;

typedef struct _temu_cell temu_cell_t;
typedef gunichar temu_char_t;
typedef struct temu_attr temu_attr_t;
typedef struct temu_line_attr temu_line_attr_t;
typedef struct temu_scr_attr temu_scr_attr_t;

struct temu_attr {
	guint fg:TEMU_SCREEN_COLOR_BITS;
	guint bg:TEMU_SCREEN_COLOR_BITS;

	guint cursor:1;
	guint selected:1;

	guint negative:1;
	guint hidden:1;
	guint overstrike:1;
	guint overline:1;

	guint wide:1;
	guint extended:1;	/* use offset to UCS-4 string */

	guint bold:2;		/* 0: normal, 1: bold, 2: dim */
	guint underline:2;	/* 0: none, 1: underline, 2: double underline */

	guint blink:2;		/* 0: static, 1: slow blink, 2: fast blink */
	guint frame:2;		/* 0: none, 1: box, 2: circle */
	guint italic:2;		/* 0: normal, 1: italic, 2: gothic */
	guint font:4;		/* 0-9: font number to use */
};

struct temu_line_attr {
	guint selected:1;	/* there are selected characters in the line */
	guint wide:1;
	guint dhl:2;		/* 0: not double-height, 1: top half, 2: bottom half */
	guint wrapped:1;	/* line wrapped at the end */
};

struct temu_scr_attr {
	guint negative:1;
};

struct _temu_cell {
	struct {
		temu_char_t glyph;
		guint32 offset;
	} ch;
	temu_attr_t attr;
};

struct _TemuScreen {
	GtkWidget widget;

	gint font_width, font_height;

	TemuScreenPrivate *priv;
};

typedef struct _TemuScreenClass	TemuScreenClass;
struct _TemuScreenClass {
	GtkWidgetClass parent_class;
};

#define TEMU_TYPE_SCREEN	(temu_screen_get_type())

#define TEMU_SCREEN(obj)		(GTK_CHECK_CAST((obj), TEMU_TYPE_SCREEN, TemuScreen))
#define TEMU_IS_SCREEN(obj)	GTK_CHECK_TYPE((obj), TEMU_TYPE_SCREEN)

#define TEMU_SCREEN_CLASS(klass)	(GTK_CHECK_CLASS_CAST((klass), TEMU_TYPE_SCREEN, TemuScreenClass))
#define TEMU_IS_SCREEN_CLASS(obj)	GTK_CHECK_CLASS_TYPE((obj), TEMU_TYPE_SCREEN)
#define TEMU_SCREEN_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), TEMU_TYPE_SCREEN, TemuScreenClass))

GType		temu_screen_get_type		(void) G_GNUC_CONST;

GtkWidget*	temu_screen_new			(void);

void		temu_screen_set_size		(TemuScreen *screen,
						 gint width,
						 gint height,
						 gint scrollback);
gint		temu_screen_get_cols		(TemuScreen *screen);
gint		temu_screen_get_rows		(TemuScreen *screen);

void		temu_screen_get_base_geometry_hints(TemuScreen *screen,
						 GdkGeometry *geom,
						 GdkWindowHints *mask);

void		temu_screen_set_color (TemuScreen *screen, guint n, GdkColor *color);
void		temu_screen_set_font (TemuScreen *screen, const char *font);
void		temu_screen_set_font_description(TemuScreen *screen, PangoFontDescription *desc);

/*
 * Text add/set functions _DO NOT WRAP_ themselves.
 * 'written' tells how many characters made it.
 */
gint		temu_screen_set_cell_text	(TemuScreen *screen, gint x, gint y, const temu_cell_t *cells, gint length, gint *written);

gint		temu_screen_set_utf8_text	(TemuScreen *screen, gint x, gint y, const gchar *text, temu_attr_t attr, temu_attr_t colors, gint length, gint *written);
gint		temu_screen_set_ucs4_text	(TemuScreen *screen, gint x, gint y, const gunichar *text, temu_attr_t attr, temu_attr_t colors, gint length, gint *written);

const temu_cell_t *temu_screen_get_cell		(TemuScreen *screen, gint x, gint y);
void		temu_screen_set_cell		(TemuScreen *screen, gint x, gint y, const temu_cell_t *cell);

/* global/line attributes */
void		temu_screen_set_screen_attr	(TemuScreen *screen, temu_scr_attr_t *attr);
temu_scr_attr_t	*temu_screen_get_screen_attr	(TemuScreen *screen);

void		temu_screen_set_line_attr	(TemuScreen *screen, gint line, temu_line_attr_t *attr);
temu_line_attr_t	*temu_screen_get_line_attr	(TemuScreen *screen, gint line);

/* cell to fill resizes with */
void		temu_screen_set_resize_cell	(TemuScreen *screen, const temu_cell_t *cell);

/* scroll/clear */
void		temu_screen_do_scroll		(TemuScreen *screen, gint rows, gint y, gint height, const temu_cell_t *cell);
void		temu_screen_fill_rect		(TemuScreen *screen, gint x, gint y, gint width, gint height, const temu_cell_t *cell);

/* scrollback */
void		temu_screen_scroll_offset(TemuScreen *screen, gint offset);
void		temu_screen_scroll_back(TemuScreen *screen, gint lines);
#define		temu_screen_scroll_forward(screen,lines) \
			temu_screen_scroll_back(screen, -(lines))
void		temu_screen_scroll_top(TemuScreen *screen);
#define		temu_screen_scroll_bottom(screen) \
			temu_screen_scroll_offset(screen, 0)
gint temu_screen_scroll_offset_max(TemuScreen *screen);

/* move text around */
void		temu_screen_move_rect		(TemuScreen *screen, gint x, gint y, gint width, gint height, gint dx, gint dy, const temu_cell_t *cell);

/* beep! */
void		temu_screen_emit_bell		(TemuScreen *screen);
G_END_DECLS

/* Show/Hide the pointer. */
void		temu_screen_show_pointer	(TemuScreen *screen);
void		temu_screen_hide_pointer	(TemuScreen *screen);

#endif
