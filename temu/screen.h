#ifndef TEMU_SCREEN_h
#define TEMU_SCREEN_h 1

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_UNICHAR_UNKNOWN_GLYPH	((gunichar)0xfffd)

#define TEMU_SCREEN_MAX_COLORS	17
#define TEMU_SCREEN_FG_DEFAULT	16
#define TEMU_SCREEN_BG_DEFAULT	16

typedef struct _TemuScreenPrivate	TemuScreenPrivate;
typedef struct _TemuScreen		TemuScreen;

typedef struct _temu_cell temu_cell_t;
typedef gunichar temu_char_t;
typedef guint temu_attr_t;

/* Attribute compression, since there are so many tri-state attributes */
#define GET_ATTR(attr,PREFIX) (((attr) / A_##PREFIX##_DIV) % A_##PREFIX##_MOD)
#define GET_ATTR_BASE(attr,base)	((attr) % A_##base##_DIV)
#define SET_ATTR(attr,PREFIX,val) ((attr) = (attr) \
			- ((attr) % (A_##PREFIX##_DIV * A_##PREFIX##_MOD)) \
			+ ((attr) % (A_##PREFIX##_DIV)) + ((val) * A_##PREFIX##_DIV))

#define GET_LINE_ATTR(screen,line,PREFIX) \
		GET_ATTR(temu_screen_get_line_attr(screen, line), PREFIX)
#define SET_LINE_ATTR(screen,line,PREFIX,val) do {			\
		guint _attr = temu_screen_get_line_attr(screen, line);	\
		SET_ATTR(_attr, PREFIX, (val));				\
		temu_screen_set_line_attr(screen, line, _attr);		\
	} while(0)

#define GET_SCREEN_ATTR(screen,PREFIX) \
		GET_ATTR(temu_screen_get_screen_attr(screen), PREFIX)
#define SET_SCREEN_ATTR(screen,PREFIX,val) do {				\
		guint _attr = temu_screen_get_screen_attr(screen);	\
		SET_ATTR(_attr, PREFIX, (val));				\
		temu_screen_set_screen_attr(screen, _attr);		\
	} while(0)

#define ATTR1(NAME,MOD)		A_##NAME##_DIV = 1, A_##NAME##_MOD = MOD
#define ATTR(NAME,MOD,PREV)	A_##NAME##_DIV = (A_##PREV##_DIV * A_##PREV##_MOD), A_##NAME##_MOD = MOD
enum {
	/* FG and BG are BASE attributes */
	ATTR1(FG,		TEMU_SCREEN_MAX_COLORS),
	ATTR(BG,		TEMU_SCREEN_MAX_COLORS,	FG),
	ATTR(BASE,		1,			BG),

	ATTR(CURSOR,		2,			BASE),
	ATTR(SELECTED,		2,			CURSOR),
	ATTR(NEGATIVE,		2,			SELECTED),
	ATTR(HIDDEN,		2,			NEGATIVE),
	ATTR(OVERSTRIKE,	2,			HIDDEN),
	ATTR(OVERLINE,		2,			OVERSTRIKE),
	ATTR(WIDE,		4,			OVERLINE),
	ATTR(BOLD,		3,			WIDE),
	ATTR(UNDERLINE,		3,			BOLD),
	ATTR(BLINK,		3,			UNDERLINE),
	ATTR(FRAME,		3,			BLINK),
	ATTR(ITALIC,		3,			FRAME),
	ATTR(FONT,		10,			ITALIC),

	ATTR(PRIVATE,		1,			FONT)
};

enum {
	ATTR1(LINE_WIDE,	2),
	ATTR(LINE_DHL,		3,			LINE_WIDE),
	ATTR(LINE_UPDATE,	1,			LINE_DHL),

	ATTR(LINE_WRAPPED,	2,			LINE_UPDATE),
	ATTR(LINE_SELECTED,	2,			LINE_WRAPPED),

	ATTR(LINE_PRIVATE,	1,			LINE_SELECTED)
};

enum {
	ATTR1(SCREEN_NEGATIVE,		2),
	ATTR(SCREEN_UPDATE,	1,			SCREEN_NEGATIVE),

	ATTR(SCREEN_PRIVATE,	1,			SCREEN_UPDATE)
};
#undef ATTR
#undef ATTR1

struct _temu_cell {
	temu_char_t glyph;
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

void		temu_screen_set_font_description(TemuScreen *screen, PangoFontDescription *desc);

/*
 * Text add/set functions _DO NOT WRAP_ themselves.
 * 'written' tells how many characters made it.
 */
gint		temu_screen_set_cell_text	(TemuScreen *screen, gint x, gint y, const temu_cell_t *cells, gint length, gint *written);

gint		temu_screen_set_utf8_text	(TemuScreen *screen, gint x, gint y, const gchar *text, temu_attr_t attr, gint length, gint *written);
gint		temu_screen_set_ucs4_text	(TemuScreen *screen, gint x, gint y, const gunichar *text, temu_attr_t attr, gint length, gint *written);

const temu_cell_t *temu_screen_get_cell		(TemuScreen *screen, gint x, gint y);
void		temu_screen_set_cell		(TemuScreen *screen, gint x, gint y, const temu_cell_t *cell);

/* global/line attributes */
void		temu_screen_set_screen_attr	(TemuScreen *screen, guint attr);
guint		temu_screen_get_screen_attr	(TemuScreen *screen);

void		temu_screen_set_line_attr	(TemuScreen *screen, gint line, guint attr);
guint		temu_screen_get_line_attr	(TemuScreen *screen, gint line);

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

#endif
