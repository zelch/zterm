/*
 * vt52x emulation core
 */

#include <stdio.h>
#include <string.h>
#include <glib/gprintf.h>
#include <gdk/gdkkeysyms.h>
#include <unistd.h>

#include "screen.h"
#include "emul.h"

#define WARN_NOTIMPL		1
#undef SHOW_IMPL
#undef OUTPUT_EVERYTHING

#if WARN_NOTIMPL
#define NOTIMPL(id,desc,reason)		g_warning("%s (%s) not implemented%s" reason , id, desc, reason?"; confidence: ":"")
#define NOTIMPL_UNKNOWN(fmt,args...)	g_warning("UNKNOWN (" fmt ") not implemented" , ## args )
#else
#define NOTIMPL(id,desc,reason)		do{}while(0)
#define NOTIMPL_UNKNOWN(fmt,args...)	do{}while(0)
#endif

#if SHOW_IMPL
#define IMPL(id,desc,support)	fprintf(stderr, "Implemented %s: %s (" support ")\n", id, desc)
#else
#define IMPL(id,desc,support)	do{}while(0)
#endif

#define VTPARSE_MAX_PARAMS	17	/* We overwrite the last one endlessly */
#define VTPARSE_MAX_STR		1024

#define VT_GLYPH_BACKWARDS_QUESTION	0x61f

#define T	(S->owner)
#define TP	(T->priv)

#define WIDTH	(temu_screen_get_cols(T))
#define HEIGHT	(temu_screen_get_rows(T))

#define CURSOR_X_TRANS(x)	(x)
#define CURSOR_Y_TRANS(y)	(S->o_DECOM?((y) + S->scroll_top):(y))

#define CURSOR_X_RTRANS(x)	(x)
#define CURSOR_Y_RTRANS(y)	(S->o_DECOM?((y) - S->scroll_top):(y))

#define P(number) (S->parm[number])
#define PARM_DEF(number, def) do{ \
		if (P(number) == 0) \
			P(number) = (def); \
	} while(0)

#define ECELL		(S->cell)
#define ATTR		(S->attr)
#define ATTR_NORMAL()	do { \
		ATTR = 0; \
		SET_ATTR(ATTR, FG, TEMU_SCREEN_FG_DEFAULT); \
		SET_ATTR(ATTR, FG, TEMU_SCREEN_BG_DEFAULT); \
	} while(0)

#define E(strt,pre,intr,fin)	(((strt)<<24) | ((pre)<<16) | ((intr)<<8) |((fin)<<0))

enum C0 {
	C0_FIRST=0,
	C_NUL = C0_FIRST,
		C_SOH,	C_STX,	C_ETX,
	C_EOT,	C_ENQ,	C_ACK,	C_BEL,
	C_BS,	C_HT,	C_LF,	C_VT,
	C_FF,	C_CR,	C_SO,	C_SI,

	C_DLO,	C_DC1,	C_DC2,	C_DC3,
	C_DC4,	C_NAK,	C_SYN,	C_ETB,
	C_CAN,	C_EM,	C_SUB,	C_ESC,
	C_FS,	C_GS,	C_RS,	C_US,
	
	C0_PAST
};

enum C1 {
	C1_FIRST=0x80,
	C_80 = C1_FIRST,
		C_81,	C_82,	C_83,
	C_IND,	C_NEL,	C_SSA,	C_ESA,
	C_HTS,	C_HTJ,	C_VTS,	C_PLD,
	C_PLU,	C_RI,	C_SS2,	C_SS3,

	C_DCS,	C_PU1,	C_PU2,	C_STS,
	C_CRH,	C_MW,	C_SPA,	C_EPA,
	C_SOS,	C_99,	C_DECID,C_CSI,
	C_ST,	C_OSC,	C_PM,	C_APC,
	
	C1_PAST
};

#define C_DEL	0x7f

typedef enum _vtstate vtstate;
enum _vtstate {
	VT_GROUND,
	VT_ESC, VT_ESC_IGNORE,
	VT_CSI, VT_CSI_FIN, VT_CSI_IGNORE,
	VT_STR, VT_STR_FIN, VT_STR_IGNORE, VT_STR_DATA,
	VT_STRX
};

struct _TemuEmul {
	vtstate state;

	TemuScreen *owner;

	gboolean allow_resize;
	gboolean do_C1, do_C1_lo;
	gboolean send_C1_lo;

	temu_cset_t charset;
	temu_cell_t cell;
	temu_attr_t attr;

	gboolean scroll_height_full;
	gint scroll_top, scroll_height;
	gint saved_cursor_x, saved_cursor_y, saved_attr;
	gint cursor_x, cursor_y;
	gboolean cursor_redraw;

	guchar strt, pre, intr, fin;

	gint parm[VTPARSE_MAX_PARAMS];
	gint parms;

	gchar char_buf[6];
	gint chars;

	gint outmax;
	gchar *out;
	gint outs;

	gboolean tabclear;
	gint tabstops;
	guint8 *tabstop;

	gchar str[VTPARSE_MAX_STR];
	gint strs;

	/* */
	gboolean conformance;
	gboolean o_KAM, o_CRM, o_IRM, o_SRM, o_LNM, o_DECKPM;
	gboolean o_DECOM, o_DECAWM, o_DECNCSM;

};

static void vt52x_parse(TemuEmul *S, const gchar *text, gint len);
static void vt52x_C0(TemuEmul *S, guchar c0);
static void vt52x_C1(TemuEmul *S, guchar c1);
static void vt52x_esc(TemuEmul *S);
static void vt52x_csi(TemuEmul *S);
static void vt52x_dcs(TemuEmul *S);
static void vt52x_apc(TemuEmul *S);
static void vt52x_osc(TemuEmul *S);

static void emul_SM_ansi(TemuEmul *S, gboolean set);
static void emul_SM_dec(TemuEmul *S, gboolean set);

static void emul_init(TemuEmul *S);
static void emul_reset(TemuEmul *S);
static void emul_reset_soft(TemuEmul *S);

static void emul_resize(TemuEmul *S, gint width, gint height);

static void emul_cursor_cleared(TemuEmul *S);
static void emul_cursor_clear(TemuEmul *S);
static void emul_cursor_draw(TemuEmul *S);
static void emul_cursor_save(TemuEmul *S);
static void emul_cursor_restore(TemuEmul *S);

static void emul_tab_reset(TemuEmul *S, gboolean clear);
static void emul_tab_set(TemuEmul *S, gint col);
static void emul_tab_clear(TemuEmul *S, gint col);
static void emul_tab_next(TemuEmul *S);
static void emul_tab_prev(TemuEmul *S);

static void emul_move_cursor(TemuEmul *S, gint x, gint y);
static void emul_scroll(TemuEmul *S, gint rows);

static void emul_BS(TemuEmul *S);
static void emul_CR(TemuEmul *S);
static void emul_DA1(TemuEmul *S);
static void emul_DECBI(TemuEmul *S);
static void emul_DECFI(TemuEmul *S);
static void emul_DECRQSS(TemuEmul *S);
static void emul_DECSCL(TemuEmul *S);
static void emul_DSR_ansi(TemuEmul *S);
static void emul_ED(TemuEmul *S, gint func);
static void emul_EL(TemuEmul *S, gint func);
static void emul_ICH(TemuEmul *S, gint cols);
static void emul_IND(TemuEmul *S);
static void emul_LF(TemuEmul *S);
static void emul_RI(TemuEmul *S);

static void emul_SGR(TemuEmul *S);

static void emul_add_char(TemuEmul *S, guchar ch);
static void emul_add_glyph(TemuEmul *S, gunichar glyph);

/*
 *
 */

static void emul_rect_coords(TemuEmul *S, gint first, gint *x, gint *y, gint *w, gint *h)
{
	PARM_DEF(first, 1);
	PARM_DEF(first+1, 1);
	PARM_DEF(first+2, HEIGHT-1);
	PARM_DEF(first+3, WIDTH-1);

	*y = CURSOR_Y_TRANS(P(first)-1);
	*x = CURSOR_X_TRANS(P(first+1)-1);
	*h = P(first+2) - P(first) + 1;
	*w = P(first+3) - P(first+1) + 1;
}

/*
 *
 */

static void emul_screen_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data)
{
	TemuEmul *S = user_data;

	/* Uhm.  Hack. */
	if (S->scroll_height_full || (S->scroll_top+S->scroll_height) > HEIGHT)
		S->scroll_height = HEIGHT - S->scroll_top;

	emul_cursor_cleared(S);
	emul_move_cursor(S, S->cursor_x, S->cursor_y);
	emul_cursor_draw(S);
	S->cursor_redraw = FALSE;
}

TemuEmul *temu_emul_new(TemuScreen *screen)
{
	TemuEmul *S;

	S = g_new(TemuEmul, 1);
	S->owner = screen;

	emul_init(S);

	g_signal_connect(G_OBJECT(screen), "size-allocate",
		G_CALLBACK(emul_screen_size_allocate), S);

	return S;
}

void temu_emul_destroy(TemuEmul *emul)
{
	g_free(emul);
}

void temu_emul_emulate(TemuEmul *S, const gchar *text, gint length)
{
	vt52x_parse(S, text, length);

	if (S->cursor_redraw) {
		emul_cursor_draw(S);
		S->cursor_redraw = FALSE;
	}
}

/* *** KEYCODE TRANSLATION / RESPONSES *** */

/* Ensure there are at least two bytes free in the buffer. */
static gchar *emul_stpcpy_c1(TemuEmul *S, gchar *buffer, gint c1)
{
	if (!S->send_C1_lo) {
		*buffer++ = c1;
	} else {
		*buffer++ = C_ESC;
		*buffer++ = c1 - 0x40;
	}
	
	return buffer;
}

void emul_add_output(TemuEmul *S, const gchar *out, gint count)
{
	if ((S->outs+count) > S->outmax) {
		S->out = g_realloc(S->out, S->outs + count);
		S->outmax = S->outs + count;
	}

	memcpy(S->out + S->outs, out, count);
	S->outs += count;
}

gssize temu_emul_get_responses(TemuEmul *S, gchar *buffer, gint len)
{
	if (S->outs < len)
		len = S->outs;

	memcpy(buffer, S->out, len);
	S->outs -= len;

	g_memmove(S->out, S->out + len, S->outs);

	return len;
}

gboolean temu_emul_translate(TemuEmul *S, GdkEventKey *key, guchar buffer[16], gint *count)
{
	guchar *p = buffer;
	gint n;

#define STRCAT_C1(c1)	(p = emul_stpcpy_c1(S, p, (c1)))

#define STRCAT_MODIFIER(extra)	do{				\
		n = 1;						\
		if (key->state & GDK_CONTROL_MASK) n += 4;	\
		if (key->state & GDK_MOD1_MASK) n += 2;		\
		if (key->state & GDK_SHIFT_MASK) n += 1;	\
		if (n != 1) p += g_sprintf(p, "%s;%d", (extra), n);\
	} while(0)

	switch (key->keyval) {
	  case GDK_BackSpace:
		if (key->state & GDK_CONTROL_MASK) key->keyval = C_BS;
		else key->keyval = C_DEL;
		break;
	  case GDK_Return:
		key->keyval = C_CR;
		break;
	  case GDK_Tab:
		key->keyval = C_HT;
		break;

	  case GDK_Escape:
		*p++ = C_ESC;
		goto done;

	  case GDK_Insert:
		STRCAT_C1(C_CSI); *p++ = '2'; STRCAT_MODIFIER(""); *p++ = '~';
		goto done;
	  case GDK_Delete:
		STRCAT_C1(C_CSI); *p++ = '3'; STRCAT_MODIFIER(""); *p++ = '~';
		goto done;
	  case GDK_Page_Up:
		STRCAT_C1(C_CSI); *p++ = '5'; STRCAT_MODIFIER(""); *p++ = '~';
		goto done;
	  case GDK_Page_Down:
		STRCAT_C1(C_CSI); *p++ = '6'; STRCAT_MODIFIER(""); *p++ = '~';
		goto done;
	  case GDK_End:
		STRCAT_C1(C_CSI); STRCAT_MODIFIER("1"); *p++ = 'F';
		goto done;
	  case GDK_Home:
		STRCAT_C1(C_CSI); STRCAT_MODIFIER("1"); *p++ = 'H';
		goto done;

	  case GDK_Up:
		STRCAT_C1(C_CSI); STRCAT_MODIFIER("1"); *p++ = 'A';
		goto done;
	  case GDK_Down:
		STRCAT_C1(C_CSI); STRCAT_MODIFIER("1"); *p++ = 'B';
		goto done;
	  case GDK_Right:
		STRCAT_C1(C_CSI); STRCAT_MODIFIER("1"); *p++ = 'C';
		goto done;
	  case GDK_Left:
		STRCAT_C1(C_CSI); STRCAT_MODIFIER("1"); *p++ = 'D';
		goto done;

	  case GDK_F1 ... GDK_F35:	/* ... GDK_F20 */
		/* There are several breaks... */
		n = key->keyval - GDK_F1 + 11;
		if (key->keyval > GDK_F5) n++;
		if (key->keyval > GDK_F10) n++;
		if (key->keyval > GDK_F14) n++;
		if (key->keyval > GDK_F16) n += 2;

		STRCAT_C1(C_CSI);
		p += g_sprintf(p, "%d", n);
		STRCAT_MODIFIER("");
		*p++ += '~';
		goto done;

	  case GDK_at: case GDK_2: case GDK_space:
		if (key->state & GDK_CONTROL_MASK) {
			*p++ = '\0';
			goto done;
		}
		break;

	  case GDK_minus:
		if (key->state & GDK_CONTROL_MASK) key->keyval = '_' - GDK_A + 1;
		break;
	  case GDK_grave:
		if (key->state & GDK_CONTROL_MASK) key->keyval = '~' - GDK_a + 1;
		break;

	  case GDK_A ... GDK_underscore:
		if (key->state & GDK_CONTROL_MASK) key->keyval -= GDK_A - 1;
		break;
	  case GDK_a ... GDK_asciitilde:
		if (key->state & GDK_CONTROL_MASK) key->keyval -= GDK_a - 1;
		break;
	}

	if (key->keyval >= 0x00 && key->keyval <= 0xff) {
		if (key->state & GDK_MOD1_MASK) *p++ = C_ESC;
		*p++ = key->keyval;
		goto done;
	}

  done:
	*count = p - buffer;

	if (!S->o_SRM)
		emul_add_output(S, buffer, *count);

	return (*count)?TRUE:FALSE;
}

/* *** PARSER *** */

/* I would love to have this part auto-generated.
   That's not the way it turned out, though. */

#define BEGIN_STR(start,type) do {					\
		S->state = type;					\
		S->strt = start;					\
		S->pre = S->intr = S->fin = 0;				\
		memset(S->parm, 0, sizeof(S->parm));			\
		S->parms = 0;						\
		S->strs = 0;						\
	} while (0)

#define BEGIN_ESC() do {						\
		S->state = VT_ESC;					\
		S->strt = 0;						\
		S->pre = S->intr = S->fin = 0;				\
	} while (0);

#define global_C0_cases							\
	  case C_NUL:							\
		continue;						\
	  case C_ESC:							\
		BEGIN_ESC(); continue;					\
	  case (C_NUL+1) ... (C_CAN-1):					\
	  case (C_CAN+1) ... (C_SUB-1):					\
	  case (C_ESC+1) ... (C0_PAST-1):				\
		vt52x_C0(S, ch);					\
		continue;

#define data_C0()							\
	switch (ch) {							\
	  case C_CAN:							\
		S->state = VT_GROUND; continue;				\
	  case C_SUB:							\
		emul_add_glyph(S, VT_GLYPH_BACKWARDS_QUESTION);		\
		S->state = VT_GROUND; continue;				\
	}

#define nonground_C0()							\
	switch (ch) {							\
	  case C_CAN:							\
		S->state = VT_GROUND; continue;				\
	  case C_SUB:							\
		emul_add_glyph(S, VT_GLYPH_BACKWARDS_QUESTION);		\
		S->state = VT_GROUND; continue;				\
	  case C_DEL:							\
		continue;						\
	  global_C0_cases						\
	}

#define state_parms(PREFIX)						\
	nonground_C0();							\
	switch (ch) {							\
	  case '0' ... '9':						\
		S->parm[S->parms] *= 10;				\
		S->parm[S->parms] += ch - '0';				\
		continue;						\
	  case ';':							\
		if (S->parms < (VTPARSE_MAX_PARAMS-1))			\
			++S->parms;					\
		continue;						\
	  case '?':							\
	  case ':': case '<': case '=': case '>':			\
		if (S->pre && S->pre != ch) {				\
			S->state = PREFIX ## _IGNORE;			\
			continue;					\
		}							\
		S->pre = ch;						\
		continue;						\
	  case 0x20 ... 0x2f:						\
		if (S->intr && S->intr != ch) {				\
			S->state = PREFIX ## _IGNORE;			\
			continue;					\
		}							\
		S->intr = ch;						\
		continue;						\
	  default:							\
		S->state = PREFIX ## _FIN;				\
		goto PREFIX ## _fin;					\
	}

static void vt52x_parse(TemuEmul *S, const gchar *text, gint len)
{
	for (; len--; text++) {
		guchar ch = (guchar)*text;

#if OUTPUT_EVERYTHING
		write(STDOUT_FILENO, &ch, 1);
#endif

		switch (S->state) {
		  case VT_GROUND:
		    /* FIXME: Doesn't handle invalid sequences properly,
		       and may produce undesired effects, the extents of
		       which I am not fully aware.
		    */
		    if (S->chars) {
			/* We're in the middle of a multibyte sequence,
			  assume the host is kind enough to send them
			  without _real_ escapes between them.. */
			emul_add_char(S, ch);
			continue;
		    }

		    switch (ch) {
		      global_C0_cases
		      case C_CAN:
		        continue;
		      case C_SUB:
			emul_add_glyph(S, VT_GLYPH_BACKWARDS_QUESTION);
			continue;
		    }

		    if (S->do_C1)
		    if (ch >= 0x80 && ch <= 0x9f) {
		    C1:
		    switch (ch) {
		      case C_CSI: BEGIN_STR(C_CSI, VT_CSI); continue;
		      case C_DCS:
		      case C_APC:
		      case C_PM: BEGIN_STR(ch, VT_STR); continue;
		      case C_OSC: BEGIN_STR(ch, VT_STRX); continue;

		      default:
			vt52x_C1(S, ch);
			continue;
		    }
		    }

		    emul_add_char(S, ch);
		    continue;

		  case VT_ESC:
		    nonground_C0();
		    switch (ch) {
		      case 0x20 ... 0x2f:
			if (S->intr && S->intr != ch) {
				S->state = VT_ESC_IGNORE;
				continue;
			}
			S->intr = ch;
			continue;
		      case 0x30 ... 0x7e:
		      case 0x80 ... 0xff:
			if (!S->intr && S->do_C1_lo)
			if (ch >= 0x40 && ch <= 0x5f) {
				ch = (ch&0x1f) | 0x80;
				S->state = VT_GROUND;
				goto C1;
			}
			S->state = VT_GROUND;
			S->fin = ch;
			vt52x_esc(S);
			continue;
		      default:
			S->state = VT_GROUND;
			continue;
		    }
		  case VT_ESC_IGNORE:
		    nonground_C0();
		    switch (ch) {
		      case 0x20 ... 0x2f:
			continue;
		      case 0x30 ... 0x7e:
		      case 0x80 ... 0xff:
		      default:
			S->state = VT_GROUND;
			continue;
		    }

		  case VT_CSI: state_parms(VT_CSI);
		  case VT_CSI_FIN:
		    nonground_C0();
		    switch (ch) {
		      case 0x40 ... 0x7e:
		      case 0x80 ... 0xff: VT_CSI_fin:
			S->state = VT_GROUND;
			S->fin = ch;
			vt52x_csi(S);
			continue;
		      default:
			S->state = VT_CSI_IGNORE;
			continue;
		    }
		  case VT_CSI_IGNORE:
		    nonground_C0();
		    switch (ch) {
		      case 0x20 ... 0x2f:
		      case 0x30 ... 0x3f:
			continue;
		      case 0x40 ... 0x7e:
		      case 0x80 ... 0xff:
		      default:
			S->state = VT_GROUND;
			continue;
		    }

		  case VT_STR: state_parms(VT_STR);
		  case VT_STR_FIN:
		    nonground_C0();
		    switch (ch) {
		      case 0x40 ... 0x7e:
		      case 0x80 ... 0xff: VT_STR_fin:
			S->fin = ch;
			S->state = VT_STR_DATA;
			continue;
		      default:
			S->state = VT_STR_IGNORE;
			continue;
		    }
		  case VT_STR_DATA:
		    data_C0();
		    switch (ch) {
		      case C_NUL ... (C_CAN-1):
		      case (C_CAN+1) ... (C_SUB-1):
		      case (C_ESC+1) ... (C_ST-1):
		      case (C_ST+1) ... 0xff:
			if (S->strs < VTPARSE_MAX_STR)
				S->str[S->strs++] = ch;
			continue;
		      case C_ST:
			S->state = VT_GROUND;
			switch (S->strt) {
			  case C_DCS: vt52x_dcs(S); break;
			  case C_APC: vt52x_apc(S); break;
			}
			continue;
		      case C_ESC:
			switch (S->strt) {
			  case C_DCS: vt52x_dcs(S); break;
			  case C_APC: vt52x_apc(S); break;
			}
			BEGIN_ESC();
			continue;
		    }
		  case VT_STR_IGNORE:
		    data_C0();
		    switch (ch) {
		      case C_ST:
			S->state = VT_GROUND;
			continue;
		      default:
			continue;
		    }

		  case VT_STRX:
		    data_C0();
		    switch (ch) {
		      case C_NUL ... (C_BEL-1):
		      case (C_BEL+1) ... (C_CAN-1):
		      case (C_CAN+1) ... (C_SUB-1):
		      case (C_ESC+1) ... (C_ST-1):
		      case (C_ST+1) ... 0xff:
			if (S->strs < VTPARSE_MAX_STR)
				S->str[S->strs++] = ch;
			continue;
		      case C_ST:
		      case C_BEL:
			S->state = VT_GROUND;
			vt52x_osc(S);
			continue;
		      case C_ESC:
			vt52x_osc(S);
			BEGIN_ESC();
			continue;
		    }
		}
	}
}

/* *** ESCAPES *** */

static void vt52x_C0(TemuEmul *S, guchar c0)
{
	switch (c0) {
	  case C_ENQ:
		NOTIMPL("ENQ", "Enquiry", "definitely");
		break;
	  case C_BEL:
		NOTIMPL("BEL", "Audible bell", "definitely");
		break;
	  case C_BS:
		emul_BS(S);
		break;
	  case C_HT:	/* HT: Horizontal Tab */
		emul_tab_next(S);
		break;
	  case C_VT:
	  case C_FF:
	  case C_LF:	emul_LF(S); break;
	  case C_CR:	emul_CR(S); break;
	  case C_SO:
		NOTIMPL("SO", "Shift-Out", "definitely");
		break;
	  case C_SI:
		NOTIMPL("SI", "Shift-In", "definitely");
		break;
	  case C_DC1:
		NOTIMPL("DC1", "aka XON", "maybe");
		break;
	  case C_DC3:
		NOTIMPL("DC3", "aka XOFF", "maybe");
		break;
	  /* C_CAN/C_SUB/C_ESC/C_DEL handled elsewhere */
	}
}

static void vt52x_C1(TemuEmul *S, guchar c1)
{
	switch (c1) {
	  case C_IND:	emul_IND(S); break;
	  case C_NEL:	emul_CR(S); emul_IND(S); break;
	  case C_HTS:	/* HTS: Horizontal Tab Set */
		emul_tab_set(S, S->cursor_x);
		break;
	  case C_RI:	emul_RI(S); break;
	  case C_SS2:
		NOTIMPL("SS2", "Single Shift G2", "definitely");
		break;
	  case C_SS3:
		NOTIMPL("SS3", "Single Shift G3", "definitely");
		break;
	  case C_SPA:
		NOTIMPL("SPA", "Start Guarded Area", "maybe");
		break;
	  case C_EPA:
		NOTIMPL("EPA", "End Guarded Area", "maybe");
		break;
	  case C_SOS:	/* ignored */ break;
	  case C_DECID:
		NOTIMPL("DECID", "Send Identification", "definitely");
		break;
	  case C_ST:	/* ignored */ break;
	  /* C_DCS/C_CSI/C_OSC/C_PM/C_APC handled elsewhere */
	}
}

static void vt52x_esc(TemuEmul *S)
{
	switch (E(S->strt,S->pre,S->intr,S->fin)) {
	  case E(0,0,'(','1'):
		NOTIMPL("DDD3", "ascii -> G0", "mmaybe");
		break;
	  case E(0,0,')','1'):	/* NOTIMPL, DDD1: */
		NOTIMPL("DDD1", "set DECRLM/DECHEBM/DECHEM", "mmaybe");
		break;
	  case E(0,0,'#','3'):
		NOTIMPL("DECDHL", "Double-Width, Double-Height line (Top Half)", "probably");
		break;
	  case E(0,0,'#','4'):
		NOTIMPL("DECDHL", "Double-Width, Double-Height line (Bottom Half)", "probably");
		break;
	  case E(0,0,'#','5'):
		NOTIMPL("DECSWL", "Single-Width, Single-Height line", "probably");
		break;
	  case E(0,0,'#','6'):
		NOTIMPL("DECDWL", "Double-Width, Single-Height line", "probably");
		break;
	  case E(0,0,0,'6'):
		IMPL("DECBI", "Back Index", "fully");
		emul_DECBI(S);
		break;
	  case E(0,0,0,'7'):	/* DECSC: Save Cursor */
		emul_cursor_save(S);
		break;
	  case E(0,0,0,'8'):	/* DECRC: Restore Cursor */
		emul_cursor_restore(S);
		break;
	  case E(0,0,0,'9'):
		IMPL("DECFI", "Forward Index", "fully");
		emul_DECFI(S);
		break;
	  case E(0,0,'#','8'):	/* DECALN: Screen Alignment Pattern */
		/* Actually, the VT520/525 documentation says
		   this displays greyscale and color bars */
		/* But, what the hey. */
		{
			temu_cell_t cell;

			S->scroll_top = 0; S->scroll_height = HEIGHT;
			emul_move_cursor(S, 0, 0);

			cell = ECELL;
			cell.glyph = L'E';
			temu_screen_fill_rect(T, 0, 0, WIDTH, HEIGHT, &cell);
		}
		break;
	  case E(0,0,0,'='):
		NOTIMPL("DECKPAM", "Keypad Application Modes", "definitely");
		break;
	  case E(0,0,0,'>'):
		NOTIMPL("DECKPNM", "Keypad Numeric Modes", "definitely");
		break;
	  case E(0,0,')','B'):
		NOTIMPL("DDD2", "reset DECRLM, ascii -> G1", "probably not");
		break;
	  case E(0,0,' ','F'):	/* S7C1T: Send C1 Control Character to the Host */
		S->send_C1_lo = TRUE;
		break;
	  case E(0,0,' ','G'):	/* S8C1T: Send C1 Control Character to the Host */
		S->send_C1_lo = FALSE;
		break;
	  case E(0,0,' ','L'):
		NOTIMPL("ANSI Conformance Level", "Level 1 (VT100, 7-bit controls)", "probably");
		break;
	  case E(0,0,' ','M'):
		NOTIMPL("ANSI Conformance Level", "Level 2 (VT200)", "probably");
		break;
	  case E(0,0,' ','N'):
		NOTIMPL("ANSI Conformance Level", "Level 3 (VT300)", "probably");
		break;
	  case E(0,0,0,'c'):	/* RIS: Reset to Initial State */
		emul_reset(S);
		break;
	  case E(0,0,0,'n'):
		NOTIMPL("LS2", "Locking Shift 2", "definitely");
		break;
	  case E(0,0,0,'o'):
		NOTIMPL("LS3", "Locking Shift 3", "definitely");
		break;
	  case E(0,0,0,'|'):
		NOTIMPL("LS3R", "Locking Shift 3, Right", "definitely");
		break;
	  case E(0,0,0,'}'):
		NOTIMPL("LS2R", "Locking Shift 2, Right", "definitely");
		break;
	  case E(0,0,0,'~'):
		NOTIMPL("LS1R", "Locking Shift 1, Right", "definitely");
		break;

	  default:
		NOTIMPL_UNKNOWN("ESC %08x", E(S->strt,S->pre,S->intr,S->fin));
		break;

	  /* NOTIMPL, SCS: Select Character Set -- LOTS of codes */
	}
}

static void vt52x_csi(TemuEmul *S)
{
	switch (E(S->strt,S->pre,S->intr,S->fin)) {
	  case E(C_CSI,0,0,'@'):
		IMPL("ICH", "Insert Character", "fully");
		PARM_DEF(0, 1);
		emul_ICH(S, P(0));
		break;
	  case E(C_CSI,0,' ','@'):
		IMPL("SL", "Scroll Left (not vt52x)", "fully");
		PARM_DEF(0, 1);
		temu_screen_move_rect(T, P(0), S->scroll_top, WIDTH, S->scroll_height, -P(0), 0, &ECELL);
		emul_cursor_cleared(S);
		break;
	  case E(C_CSI,0,0,'A'):	/* CUU: Cursor Up */
		PARM_DEF(0, 1);
		emul_move_cursor(S, S->cursor_x, S->cursor_y - P(0));
		break;
	  case E(C_CSI,0,' ','A'):
		IMPL("SR", "Scroll Right (not vt52x)", "fully");
		PARM_DEF(0, 1);
		emul_cursor_clear(S);
		temu_screen_move_rect(T, 0, S->scroll_top, WIDTH, S->scroll_height, P(0), 0, &ECELL);
		break;
	  case E(C_CSI,0,0,'B'):	/* CUD: Cursor Down */
		PARM_DEF(0, 1);
		emul_move_cursor(S, S->cursor_x, S->cursor_y + P(0));
		break;
	  case E(C_CSI,0,0,'C'):	/* CUF: Cursor Forward */
	  case E(C_CSI,0,0,'a'):	/* HPA: Horizontal Position Relative */
		PARM_DEF(0, 1);
		emul_move_cursor(S, S->cursor_x + P(0), S->cursor_y);
		break;
	  case E(C_CSI,0,0,'D'):	/* CUB: Cursor Backward */
		PARM_DEF(0, 1);
		emul_move_cursor(S, S->cursor_x - P(0), S->cursor_y);
		break;
	  case E(C_CSI,0,0,'E'):	/* CNL: Cursor Next Line */
		PARM_DEF(0, 1);
		emul_move_cursor(S, 0, S->cursor_y + P(0));
		break;
	  case E(C_CSI,0,0,'F'):	/* CPL: Cursor Previous Line */
		PARM_DEF(0, 1);
		emul_move_cursor(S, 0, S->cursor_y - P(0));
		break;
	  case E(C_CSI,0,0,'G'):	/* CHA: Cursor Horizontal Absolute */
	  case E(C_CSI,0,0,'`'):	/* HPA: Horizontal Position Absolute */
		PARM_DEF(0, 1);
		emul_move_cursor(S, CURSOR_X_TRANS(P(0) - 1), S->cursor_y);
		break;
	  case E(C_CSI,0,0,'H'):	/* CUP: Cursor Position */
	  case E(C_CSI,0,0,'f'):	/* HVP: Horizontal and Vertical Position */
		PARM_DEF(0, 1); PARM_DEF(1, 1);
		emul_move_cursor(S, CURSOR_X_TRANS(P(1) - 1), CURSOR_Y_TRANS(P(0) - 1));
		break;
	  case E(C_CSI,0,0,'I'):	/* CHT: Cursor Horizontal Forward Tabulation */
		PARM_DEF(0, 1);
		while (P(0)--) emul_tab_next(S);
		break;
	  case E(C_CSI,0,0,'J'):	/* ED: Erase in Display */
		PARM_DEF(0, 0);
		emul_ED(S, P(0));
		break;
	  case E(C_CSI,'?',0,'J'):
		NOTIMPL("DECSED", "Selective Erase in Display", "probably");
		break;
	  case E(C_CSI,0,0,'K'):	/* EL: Erase in Line */
		PARM_DEF(0, 0);
		emul_EL(S, P(0));
		break;
	  case E(C_CSI,'?',0,'K'):
		NOTIMPL("DECSEL", "Selective Erase in Line", "probably");
		break;
	  case E(C_CSI,0,0,'L'):	/* IL: Insert Line */
		PARM_DEF(0, 1);
		emul_cursor_clear(S);
		temu_screen_do_scroll(T, -P(0), S->cursor_y, (S->scroll_top + S->scroll_height) - S->cursor_y, &ECELL);
		break;
	  case E(C_CSI,0,0,'M'):	/* DL: Delete Line */
		PARM_DEF(0, 1);
		temu_screen_do_scroll(T, P(0), S->cursor_y, (S->scroll_top + S->scroll_height) - S->cursor_y, &ECELL);
		emul_cursor_cleared(S);
		break;
	  case E(C_CSI,0,0,'P'):
		IMPL("DCH", "Delete Character", "fully");
		PARM_DEF(0, 1);
		temu_screen_move_rect(T, S->cursor_x + P(0), S->cursor_y, WIDTH, 1, -P(0), 0, &ECELL);
		emul_cursor_cleared(S);
		break;
	  case E(C_CSI,0,' ','P'):
		NOTIMPL("PPA", "Page Position Absolute", "maybe");
		break;
	  case E(C_CSI,0,' ','Q'):
		NOTIMPL("PPR", "Page Position Relative", "maybe");
		break;
	  case E(C_CSI,0,' ','R'):
		NOTIMPL("PPB", "Page Position Backwards", "maybe");
		break;
	  case E(C_CSI,0,0,'S'):	/* SU: Pan Down */
		PARM_DEF(0, 1);
		emul_scroll(S, P(0));
		break;
	  case E(C_CSI,0,0,'T'):	/* SD: Pan Up */
		PARM_DEF(0, 1);
		emul_scroll(S, -P(0));
		break;
	  case E(C_CSI,0,0,'U'):
		NOTIMPL("NP", "Next Page", "maybe");
		break;
	  case E(C_CSI,0,0,'V'):
		NOTIMPL("PP", "Preceding Page", "maybe");
		break;
	  case E(C_CSI,'?',0,'W'):
		switch (P(0)) {
		  case 5:	/* DECST8C: Set Tab at Every 8 Columns */
			emul_tab_reset(S, FALSE);
			break;
		}
		break;
	  case E(C_CSI,0,0,'X'):	/* ECH: Erase Character */
		PARM_DEF(0, 1);
		temu_screen_fill_rect(T, S->cursor_x, S->cursor_y, P(0), 1, &ECELL);
		emul_cursor_cleared(S);
		break;
	  case E(C_CSI,0,0,'Z'):	/* CBT: Cursor Backward Tabulation */
		PARM_DEF(0, 1);
		while (P(0)--) emul_tab_prev(S);
		break;
	  case E(C_CSI,0,0,'b'):
		NOTIMPL("REP", "Repeat character (not VT52x)", "definitely");
		break;
	  case E(C_CSI,0,0,'c'):
		PARM_DEF(0, 0);
		switch (P(0)) {
		  case 0:	/* DA1: Primary Device Attributes */
			emul_DA1(S);
			break;
		  default:
			NOTIMPL_UNKNOWN("%08x %d", E(S->strt,S->pre,S->intr,S->fin), P(0));
			break;
		}
		break;
	  case E(C_CSI,0,'>','c'):
		PARM_DEF(0, 0);
		switch (P(0)) {
		  case 0: NOTIMPL("DA2", "Secondary Device Attributes", "probably"); break;
		  default: NOTIMPL_UNKNOWN("%08x %d", E(S->strt,S->pre,S->intr,S->fin), P(0)); break;
		}
		break;
	  case E(C_CSI,0,'=','c'):
		PARM_DEF(0, 0);
		switch (P(0)) {
		  case 0: NOTIMPL("DA3", "Tertiary Device Attributes", "probably"); break;
		  default: NOTIMPL_UNKNOWN("%08x %d", E(S->strt,S->pre,S->intr,S->fin), P(0)); break;
		}
		break;
	  case E(C_CSI,0,0,'d'):	/* VPA: Vertical Line Position Absolute */
		PARM_DEF(0, 1);
		emul_move_cursor(S, S->cursor_x, CURSOR_Y_TRANS(P(0) - 1));
		break;
	  case E(C_CSI,0,0,'e'):	/* VPR: Vertical Position Relative */
		PARM_DEF(0, 1);
		emul_move_cursor(S, 0, S->cursor_y + P(0));
		break;
	  case E(C_CSI,0,0,'g'):	/* TBC: Tab Clear */
		switch (P(0)) {
		  case 0:	emul_tab_clear(S, S->cursor_x); break;
		  case 3:	emul_tab_reset(S, TRUE); break;
		  default:
			NOTIMPL_UNKNOWN("TBC: Tab Clear: %d", P(0));
			break;
		}
		break;
	  case E(C_CSI,0,0,'h'):	/* SM: Set Mode (ANSI) */
		emul_SM_ansi(S, TRUE);
		break;
	  case E(C_CSI,'?',0,'h'):	/* SM: Set Mode (DEC) */
		emul_SM_dec(S, TRUE);
		break;
	  case E(C_CSI,0,0,'i'):
		NOTIMPL("MC", "Media Copy (ANSI)", "not planned");
		break;
	  case E(C_CSI,'?',0,'i'):
		NOTIMPL("MC", "Media Copy (DEC)", "not planned");
		break;
	  case E(C_CSI,0,0,'l'):	/* RM: Reset Mode (ANSI) */
		emul_SM_ansi(S, FALSE);
		break;
	  case E(C_CSI,'?',0,'l'):	/* RM: Reset Mode (DEC) */
		emul_SM_dec(S, FALSE);
		break;
	  case E(C_CSI,0,0,'m'):	/* SGR: Select Graphic Rendition */
		emul_SGR(S);
		break;
	  case E(C_CSI,0,0,'n'):	/* DSR: Device Status Reports (ANSI) */
		emul_DSR_ansi(S);
		break;
	  case E(C_CSI,'?',0,'n'):
		/*
		P(0) = 6, DECXCPR: Extended Cursor Position Report
		P(0) = 63, DECCKSR: Memory Checksum Report
		*/
		NOTIMPL("DSR", "Device Status Reports (DEC)", "definitely");
		break;
	  case E(C_CSI,0,0,'p'):
		NOTIMPL("DECSSL", "Select Set-Up Language", "never");
		break;
	  case E(C_CSI,0,' ','p'):
		NOTIMPL("DECSSCLS", "Set Scroll Speed", "not planned");
		break;
	  case E(C_CSI,0,'!','p'):	/* DECSTR: Soft Terminal Reset */
		emul_reset_soft(S);
		break;
	  case E(C_CSI,0,'"','p'):	/* DECSCL: Select Conformance Level */
		emul_DECSCL(S);
		break;
	  case E(C_CSI,0,'$','p'):
		NOTIMPL("DECRQM", "Request Mode (ANSI)", "definitely");
		break;
	  case E(C_CSI,'?','$','p'):
		NOTIMPL("DECRQM", "Request Mode (DEC)", "definitely");
		break;
	  case E(C_CSI,0,')','p'):
		NOTIMPL("DECSDPT", "Select Digital Printed Data Type", "never");
		break;
	  case E(C_CSI,0,'*','p'):
		NOTIMPL("DECSPPCS", "Select ProPrinter Character Set", "never");
		break;
	  case E(C_CSI,0,'+','p'):
		NOTIMPL("DECSR", "Secure Reset", "mmaybe");
		break;
	  case E(C_CSI,0,',','p'):
		NOTIMPL("DECLTOD", "Load Time of Day", "never");
		break;
	  case E(C_CSI,0,'-','p'):
		NOTIMPL("DECARR", "Select Auto Repeat Rate", "maybe option");
		break;
	  case E(C_CSI,0,0,'q'):
		NOTIMPL("DECLL", "Load LEDs", "probably");
		break;
	  case E(C_CSI,0,' ','q'):
		NOTIMPL("DECSCUSR", "Set Cursor Style", "definitely");
		break;
	  case E(C_CSI,0,'"','q'):
		NOTIMPL("DECSCA", "Select Character Protection Attribute", "probably");
		break;
	  case E(C_CSI,0,'$','q'):
		NOTIMPL("DECSDDT", "Select Disconnect Delay Time", "never");
		break;
	  case E(C_CSI,0,'+','q'):
		NOTIMPL("DECELF", "Enable Local Functions", "never");
		break;
	  case E(C_CSI,0,',','q'):
		NOTIMPL("DECTID", "Select Terminal ID", "mmaybe");
		break;
	  case E(C_CSI,0,'-','q'):
		NOTIMPL("DECCRTST", "CRT Saver Timing", "never");
		break;
	  case E(C_CSI,0,0,'r'):	/* DECSTBM: Set Top and Bottom Margins */
		PARM_DEF(0, 0); PARM_DEF(1, 0);

		/* FIXME: Sanity check */
		if (P(0))	S->scroll_top = P(0) - 1;
		else		S->scroll_top = 0;
		if (P(1))	S->scroll_height = (P(1) - 1) - S->scroll_top + 1;
		else		S->scroll_height = HEIGHT - S->scroll_top;

		S->scroll_height_full = ((S->scroll_top+S->scroll_height) == HEIGHT);

		emul_move_cursor(S, S->cursor_x, S->cursor_y);
		break;
	  case E(C_CSI,'?',0,'r'):
		NOTIMPL("DECPCTERM", "Enter/Exit PCTerm or Scancode Mode", "probably not");
		break;
	  case E(C_CSI,0,' ','r'):
		NOTIMPL("DECSKCV", "Set Key Click Volume", "mmaybe");
		break;
	  case E(C_CSI,0,'$','r'):
		NOTIMPL("DECCARA", "Change Attributes in Rectangular Area", "definitely");
		break;
	  case E(C_CSI,0,'*','r'):
		NOTIMPL("DECSCS", "Select Communication Speed", "never");
		break;
	  case E(C_CSI,0,'+','r'):
		NOTIMPL("DECSMKR", "Select Modifier Key Reporting", "definitely");
		break;
	  case E(C_CSI,0,'-','r'):
		NOTIMPL("DECSEST", "Energy Saver Timing", "never");
		break;
	  case E(C_CSI,0,0,'s'):	
		/* Ugh.  xterm supports the ANSI.SYS 'save cursor' for this */
		/* The DECSLRM sequence is only supported in vertical split mode */
		if (1) {
			emul_cursor_save(S);
		} else {
			NOTIMPL("DECSLRM", "Set Left and Right Margins", "maybe");
		}
		break;
	  case E(C_CSI,0,'$','s'):
		NOTIMPL("DECSPRTT", "Select Printer Type", "never");
		break;
	  case E(C_CSI,0,'*','s'):
		NOTIMPL("DECSFC", "Select Flow Control", "not planned");
		break;
	  case E(C_CSI,0,0,'t'):
		NOTIMPL("DECSLPP", "Set Lines Per Page", "maybe");
		break;
	  case E(C_CSI,0,' ','t'):
		NOTIMPL("DECSWBV", "Set Warning Bell Volume", "mmaybe");
		break;
	  case E(C_CSI,0,'$','t'):
		NOTIMPL("DECRARA", "Reverse Attributes in Rectangular Area", "definitely");
		break;
	  case E(C_CSI,0,0,'u'):
		emul_cursor_restore(S);
		break;
	  case E(C_CSI,0,' ','u'):
		NOTIMPL("DECSMBV", "Set Margin Bell Volume", "mmaybe");
		break;
	  case E(C_CSI,0,'"','u'):
		NOTIMPL("DECSTRL", "Set Transmit Rate Limit", "never");
		break;
	  case E(C_CSI,0,'$','u'):
		PARM_DEF(0, 0);
		switch (P(0)) {
		  case 1: NOTIMPL("DECRQTSR", "Request Terminal State Report", "probably not"); break;
		  case 2: NOTIMPL("DECCTR", "Color Table Request", "probably not"); break;
		  default: NOTIMPL_UNKNOWN("%08x %d", E(S->strt,S->pre,S->intr,S->fin), P(0));
		}
		break;
	  case E(C_CSI,0,'&','u'):
		NOTIMPL("DECRQUPSS", "Request User-Preferred Supplemental Set", "maybe");
		break;
	  case E(C_CSI,0,'*','u'):
		NOTIMPL("DECSCP", "Select Communication Port", "never");
		break;
	  case E(C_CSI,0,',','u'):
		NOTIMPL("DECRQKT", "Request Key Type", "maybe");
		break;
	  case E(C_CSI,0,' ','v'):
		NOTIMPL("DECSLCK", "Set Lock Key Style", "mmaybe");
		break;
	  case E(C_CSI,0,'$','v'):
		IMPL("DECCRA", "Copy Rectangular Area", "no pages");
		{
			gint x, y, w, h, dx, dy;

			PARM_DEF(4, 1);	/* TODO: Pages */
			PARM_DEF(5, 1);
			PARM_DEF(6, 1);
			PARM_DEF(7, 1); /* TODO: Pages */

			emul_rect_coords(S, 0, &x, &y, &w, &h);
			dx = P(6) - P(1);
			dy = P(5) - P(0);

			temu_screen_move_rect(T, x, y, w, h, dx, dy, NULL);
		}
		break;
	  case E(C_CSI,0,'$','w'):
		NOTIMPL("DECRQPSR", "Request Presentation State Report", "maybe");
		break;
	  case E(C_CSI,0,'+','w'):
		NOTIMPL("DECSPP", "Set Port Parameters", "never");
		break;
	  case E(C_CSI,0,',','w'):
		NOTIMPL("DECRQKD", "Request Key Definition", "maybe");
		break;
	  case E(C_CSI,0,'$','x'):	/* DECFRA: Fill Rectangular Area */
		{
			gint x, y, w, h;
			temu_cell_t cell;
			cell.glyph = P(0);
			if ((cell.glyph >= C0_FIRST && cell.glyph < C0_PAST)
			 || (cell.glyph >= C1_FIRST && cell.glyph < C1_PAST))
				break;
			cell.attr = ATTR;

			emul_rect_coords(S, 1, &x, &y, &w, &h);
			temu_screen_fill_rect(T, x, y, w, h, &cell);
			emul_cursor_cleared(S);	/* Maybe.. */
		}
		break;
	  case E(C_CSI,0,'&','x'):
		NOTIMPL("DECES", "Enable Session", "probably not");
		break;
	  case E(C_CSI,0,'*','x'):
		NOTIMPL("DECSACE", "Select Attribute Change Extent", "probably");
		break;
	  case E(C_CSI,0,'+','x'):
		NOTIMPL("DECRQPKFM", "Request Program Key Free Memory", "probably not, stub");
		break;
	  case E(C_CSI,0,',','x'):
		NOTIMPL("DECSPMA", "Session Page Memory Allocation", "probably not");
		break;
	  case E(C_CSI,0,0,'y'):
		NOTIMPL("DECTST", "Invoke Confidence Test", "mmaybe partial");
		break;
	  case E(C_CSI,0,'*','y'):
		NOTIMPL("DECRQCRA", "Request Checksum of Rectangular Area", "probably not");
		break;
	  case E(C_CSI,0,'+','y'):
		NOTIMPL("DECPKFMR", "Program Key Free Memory Report", "probably not, stub");
		break;
	  case E(C_CSI,0,',','y'):
		NOTIMPL("DECUS", "Update Session", "probably not");
		break;
	  case E(C_CSI,0,'$','z'):	/* DECERA: Erase Rectangular Area */
		{
			gint x, y, w, h;

			emul_rect_coords(S, 0, &x, &y, &w, &h);
			temu_screen_fill_rect(T, x, y, w, h, &ECELL);
			emul_cursor_cleared(S);	/* Maybe.. */
		}
		break;
	  case E(C_CSI,0,'*','z'):
		NOTIMPL("DECINVM", "Invoke Macro", "probably not");
		break;
	  case E(C_CSI,0,'+','z'):
		NOTIMPL("DECPKA", "Program Key Action", "probably not");
		break;
	  case E(C_CSI,0,',','z'):
		NOTIMPL("DECDLDA", "Down Line Load Allocation", "never");
		break;
	  case E(C_CSI,0,'$','{'):
		NOTIMPL("DECSERA", "Selective Erase Rectangular Area", "definitely");
		break;
	  case E(C_CSI,0,',','{'):
		NOTIMPL("DECSZS", "Select Zero Symbol", "mmaybe");
		break;
	  case E(C_CSI,0,'$','|'):	/* DECSCPP: Select Columns Per Page */
		/* Use something else if you want more control. */
		if (P(0) < 40)		P(0) = 40;
		else if (P(0) > 132)	P(0) = 132;
		emul_resize(S, P(0), HEIGHT);
		emul_move_cursor(S, S->cursor_x, S->cursor_y);
		break;
	  case E(C_CSI,0,'*','|'):	/* DECSNLS: Set Number of Lines Per Screen */
		/* Use something else if you want more control. */
		if (P(0) < 24)		P(0) = 24;
		else if (P(0) > 53)	P(0) = 53;
		emul_resize(S, WIDTH, P(0));
		if ((S->scroll_top+S->scroll_height) > HEIGHT) {
			S->scroll_height = HEIGHT - S->scroll_top;
			S->scroll_height_full = TRUE;
		}
		emul_move_cursor(S, S->cursor_x, S->cursor_y);
		break;
	  case E(C_CSI,0,',','|'):
		PARM_DEF(0, 0);
		NOTIMPL("DECAC", "Assign Color", "probably");
		/* P(0) = 1, Normal Text */
		/* P(0) = 2, Window Frame */
		break;
	  case E(C_CSI,0,' ','}'):
		NOTIMPL("DECKBD", "Keyboard Language Selection", "probably not");
		break;
	  case E(C_CSI,0,'$','}'):
		NOTIMPL("DECSASD", "Set Active Status Display", "maybe");
		break;
	  case E(C_CSI,0,'\'','}'):
		IMPL("DECIC", "Insert Column", "fully");
		PARM_DEF(0, 1);
		emul_cursor_clear(S);
		temu_screen_move_rect(T, S->cursor_x, S->scroll_top, WIDTH, S->scroll_height, P(0), 0, &ECELL);
		break;
	  case E(C_CSI,0,')','}'):
		NOTIMPL("DECSTGLT", "Select Color Look-Up Table", "probably not");
		break;
	  case E(C_CSI,0,'*','}'):
		NOTIMPL("DECLFKC", "Local Function Key Control", "never");
		break;
	  case E(C_CSI,0,',','}'):
		NOTIMPL("DECATC", "Alternate Text Color", "probably");
		break;
	  case E(C_CSI,0,' ','~'):
		NOTIMPL("DECTME", "Terminal Mode Emulation", "mmaybe partial");
		break;
	  case E(C_CSI,0,'$','~'):
		NOTIMPL("DECSSDT", "Select Status Display (Line) Type", "maybe");
		break;
	  case E(C_CSI,0,'\'','~'):
		IMPL("DECDC", "Delete Column", "fully");
		PARM_DEF(0, 1);
		temu_screen_move_rect(T, S->cursor_x + P(0), S->scroll_top, WIDTH, S->scroll_height, -P(0), 0, &ECELL);
		emul_cursor_cleared(S);
		break;
	  case E(C_CSI,0,',','~'):
		NOTIMPL("DECPS", "Play Sound", "probably not");
		break;
	  default:
		NOTIMPL_UNKNOWN("CSI %08x", E(S->strt,S->pre,S->intr,S->fin));
		break;
	}
}

static void vt52x_dcs(TemuEmul *S)
{
	switch (E(S->strt,S->pre,S->intr,S->fin)) {
	  case E(C_DCS,0,'$','p'):
		NOTIMPL("DECRSTS", "Restore Terminal State", "probably not");
		/*
		P(0) = 1, Restore Terminal State
		P(0) = 2, Restore Terminal Color Table State
		*/
		break;
	  case E(C_DCS,0,'$','q'):	/* DECRQSS: Request Selection or Setting */
		emul_DECRQSS(S);
		break;
	  case E(C_DCS,0,0,'r'):
		NOTIMPL("DECLBAN", "Load Banner Message", "never");
		break;
	  case E(C_DCS,0,'$','t'):
		NOTIMPL("DECRSPS", "Restore Presentation State", "maybe");
		break;
	  case E(C_DCS,0,'!','u'):
		NOTIMPL("DECAUPSS", "Assign User-Preferred Supplemental Sets", "maybe");
		break;
	  case E(C_DCS,0,0,'v'):
		NOTIMPL("DECLANS", "Load Answerback Message", "mmaybe");
		break;
	  case E(C_DCS,0,'"','x'):
		NOTIMPL("DECPFK", "Program Function Key", "probably not");
		break;
	  case E(C_DCS,0,'"','y'):
		NOTIMPL("DECPAK", "Program Alphanumeric Key", "probably not");
		break;
	  case E(C_DCS,0,'!','z'):
		NOTIMPL("DECDMAC", "Define Macro", "probably not");
		break;
	  case E(C_DCS,0,'"','z'):
		NOTIMPL("DECCKD", "Copy Key Default", "probably not");
		break;
	  case E(C_DCS,0,0,'{'):
		NOTIMPL("DECDLD", "Dynamiclly Redefinable Character Sets Extension", "probably not");
		break;
	  case E(C_DCS,0,'!','{'):
		NOTIMPL("DECSTUI", "Setting Terminal Unit ID", "probably not");
		break;
	  case E(C_DCS,0,0,'|'):
		NOTIMPL("DECUDK", "User Defined Keys", "probably not");
		break;
	  default:
		NOTIMPL_UNKNOWN("DCS %08x", E(S->strt,S->pre,S->intr,S->fin));
		break;
	}
}

static void vt52x_apc(TemuEmul *S)
{
	switch (E(S->strt,S->pre,S->intr,S->fin)) {
	  default:
		NOTIMPL_UNKNOWN("APC %08x (%d characters of data)", E(S->strt,S->pre,S->intr,S->fin), S->strs);
		break;
	}
}

static void vt52x_osc(TemuEmul *S)
{
	gint i;
	gint mode;

	mode = 0;
	for (i = 0; i < S->strs; i++) {
		if (S->str[i] >= '0' && S->str[i] <= '9') {
			mode *= 10;
			mode += S->str[i] - '0';
		} else if (S->str[i] == ';') {
			break;
		} else {
			if (S->fin && S->fin != S->str[i])
				goto ignore;
			S->fin = S->str[i];
		}
	}

	switch (mode) {
	  case 0: if (S->fin) goto ignore;
		NOTIMPL("(xterm)", "Change Icon Name and Window Title", "probably");
		break;
	  case 1: if (S->fin) goto ignore;
		NOTIMPL("(xterm)", "Change Icon Name", "probably");
		break;
	  case 2:
		switch (S->fin) {
		  case 0:
			NOTIMPL("(xterm)", "Change Window Title", "probably");
			break;
		  case 'L':
			NOTIMPL("DECSIN", "Set Icon Name", "maybe");
			break;
		  default:
			goto ignore;
		}
		break;
	  case 3: if (S->fin) goto ignore;
		NOTIMPL("(xterm)", "Set X property on top-level window", "mmaybe");
		break;
	  case 4: if (S->fin) goto ignore;
		NOTIMPL("(xterm)", "Change Color", "mmaybe");
		break;
	  case 10 ... 17:
		NOTIMPL("(xterm)", "Dynamic colors", "???");
		break;
	  case 21:
		NOTIMPL("DECSWT", "Set Window Title", "maybe");
		break;
	  case 46:
		NOTIMPL("(xterm)", "Log file", "probably not");
		break;
	  default: ignore:
		NOTIMPL_UNKNOWN("OSC: %d characters of data", S->strs);
		break;
	}
}

static void emul_SM_ansi(TemuEmul *S, gboolean set)
{
	gint i;

	for (i = 0; i <= S->parms; i++) {
		switch (P(i)) {
		  case 2:	/* IGNORED (Option?), KAM: Keyboard Action Mode */
			S->o_KAM = set; break;
		  case 3:
			NOTIMPL("CRM", "Show Control Character Mode (set == show)", "probably");
			S->o_CRM = set; break;
		  case 4:	/* IRM: Insert/Replace Mode */
			S->o_IRM = set; break;
		  case 12:	/* SRM: Local Echo: Send/Recieve Mode (Set local echo off) */
			S->o_SRM = set; break;
		  case 20:	/* LNM: Line Feed/New Line Mode (set == LF == NEL) */
			S->o_LNM = set; break;
		  case 81:
			NOTIMPL("DECKPM", "Key Position Mode (send key position reports)", "probably");
			S->o_DECKPM = set; break;
		  default:
			NOTIMPL_UNKNOWN("SM: Set Mode(ANSI): %d", P(i));
			break;
		}
	}
}

static void emul_SM_dec(TemuEmul *S, gboolean set)
{
	gint i;

	for (i = 0; i <= S->parms; i++) {
		switch (P(i)) {
		  case 1:
			NOTIMPL("DECCKM", "Cursor Keys Mode", "definitely");
			break;
		  case 2:
			NOTIMPL("DECANM", "ANSI Mode (VT52)", "probably");
			break;
		  case 3:	/* DECCOLM: Selecting 80 or 132 Columns per Page (Set: 132, Reset: 80) */
			/* Don't use this. */
			if (set)	emul_resize(S, 132, HEIGHT);
			else		emul_resize(S, 80, HEIGHT);
			S->scroll_top = 0; S->scroll_height = HEIGHT;
			S->scroll_height_full = TRUE;
			emul_move_cursor(S, 0, 0);
			if (!S->o_DECNCSM)
				emul_ED(S, 2);
			break;
		  case 4:
			NOTIMPL("DECSCLM", "Scrolling Mode", "probably not");
			break;
		  case 5: {
			IMPL("DECSCNM", "Screen Mode: Light or Dark Screen", "fully");
			SET_SCREEN_ATTR(T, SCREEN_NEGATIVE, set);
			break;
		  }
		  case 6:	/* DECOM: Origin Mode */
			S->o_DECOM = set;
			if (set) emul_move_cursor(S, S->cursor_x, S->cursor_y);
			break;
		  case 7:	/* DECAWM: Autowrap Mode */
			S->o_DECAWM = set;
			break;
		  case 8:
			NOTIMPL("DECARM", "Auto Repeat Mode", "mmaybe");
			break;
		  case 18:
			NOTIMPL("DECPFF", "Print Form Feed Mode", "never");
			break;
		  case 19:
			NOTIMPL("DECPEX", "Print Extent Mode", "never");
			break;
		  case 25:
			NOTIMPL("DECTCEM", "Text Cursor Mode Enable", "definitely");
			break;
		  case 34:
			NOTIMPL("DECRLM", "Right-to-Left Mode", "maybe");
			break;
		  case 35:
			NOTIMPL("DECHEBM", "Hebrew/North-American Keyboard Mapping Mode", "probably not");
			break;
		  case 36:
			NOTIMPL("DECHEM", "Hebrew Encoding Mode", "probably not");
			break;
		  case 42:
			NOTIMPL("DECNRCM", "National Replacement Character Set Mode", "maybe");
			break;
		  case 57:
			NOTIMPL("DECNAKB", "Greek/North-American Keyboard Mapping Mode", "probably not");
			break;
		  case 58:
			NOTIMPL("DECIPEM", "Enter/Return from IBM ProPrinter Emulation Mode", "never");
			break;
		  case 60:
			NOTIMPL("DECHCMM", "Horizontal Cursor-Coupling Mode", "probably not");
			break;
		  case 61:
			NOTIMPL("DECVCMM", "Vertical Cursor-Coupling Mode", "probably not");
			break;
		  case 64:
			NOTIMPL("DECPCCM", "Page Cursor-Coupling Mode", "probably not");
			break;
		  case 66:
			NOTIMPL("DECNKM", "Numeric Keypad Mode", "definitely");
			break;
		  case 67:
			NOTIMPL("DECBKM", "Backarrow Key Mode", "maybe");
			break;
		  case 68:
			NOTIMPL("DECKBUM", "Typewriter or Data Processing Keys (Set: Data Processing, Reset: Typewriter)", "probably not");
			break;
		  case 69:
			NOTIMPL("DECLRMM", "Left Right Margin Mode", "maybe");
			break;
		  case 73:
			NOTIMPL("DECXRLM", "Transmit Rate Limiting", "never");
			break;
		  case 95:	/* DECNCSM: No Clearing Screen On Column Change Mode */
			S->o_DECNCSM = set;
			break;
		  case 96:
			NOTIMPL("DECRCLM", "Right-to-Left Copy Mode", "maybe");
			break;
		  case 97:
			NOTIMPL("DECCRTSM", "Set/Reset CRT Save Mode", "never");
			break;
		  case 98:
			NOTIMPL("DECARSM", "Set/Reset Auto Resize Mode", "probably not");
			break;
		  case 99:
			NOTIMPL("DECMDM", "Modem Control Mode", "never");
			break;
		  case 100:
			NOTIMPL("DECAAM", "Set/Reset Auto Answer Back Mode", "mmaybe");
			break;
		  case 101:
			NOTIMPL("DECCANSM", "Conceal Answerback Message Mode", "never");
			break;
		  case 102:
			NOTIMPL("DECNULM", "Null Mode", "never");
			break;
		  case 103:
			NOTIMPL("DECHDPXM", "Set/Reset Half-Duplex Mode", "never");
			break;
		  case 104:
			NOTIMPL("DECESKM", "Enable Secondary Keyboard Language Mode", "probably not");
			break;
		  case 106:
			NOTIMPL("DECOSCNM", "Set/Reset Overscan Mode", "never");
			break;
		  case 108:
			NOTIMPL("DECNUMLK", "Num Lock Mode", "probably");
			break;
		  case 109:
			NOTIMPL("DECCAPSLK", "Caps Lock Mode", "probably");
			break;
		  case 110:
			NOTIMPL("DECKLHIM", "Keyboard LED's Host Indicator Mode", "probably");
			break;
		  case 111:
			NOTIMPL("DECFWM", "Set/Reset Framed Windows Mode", "probably not");
			break;
		  case 112:
			NOTIMPL("DECRPL", "Review Previous Line Mode", "never");
			break;
		  case 113:
			NOTIMPL("DECHWUM", "Host Wake-Up Mode (CRT and Energy Saver)", "never");
			break;
		  case 114:
			NOTIMPL("DECATCUM", "Set/Reset Alternate Text Color Underline Mode", "definitely");
			break;
		  case 115:
			NOTIMPL("DECATCBM", "Set/Reset Alternate Text Color Blink Mode", "definitely");
			break;
		  case 116:
			NOTIMPL("DECBBSM", "Bold and Blink Style Mode", "definitely");
			break;
		  case 117:
			NOTIMPL("DECECM", "Erase Color Mode (erase to screen bg)", "definitely");
			break;
		  default:
			NOTIMPL_UNKNOWN("SM: Set Mode (DEC): %d", P(i));
			break;
		}
	}
}

/* *** INTERNAL *** */

static void emul_init(TemuEmul *S)
{
	S->out = NULL;
	S->outs = 0;
	S->outmax = 0;

	S->tabstop = NULL;
	S->tabstops = 0;
	S->tabclear = FALSE;

	S->do_C1 = FALSE;
	S->do_C1_lo = TRUE;
	S->send_C1_lo = TRUE;

	S->allow_resize	= TRUE;

	S->conformance = 5;

	emul_reset(S);
}

static void emul_reset_soft(TemuEmul *S)
{
	ECELL.glyph = L' ';
	ATTR_NORMAL();
	ECELL.attr = GET_ATTR_BASE(ATTR, BASE);
	S->saved_attr = ECELL.attr;

	S->charset = CHARSET_UTF8;

	S->scroll_top = 0;
	S->scroll_height = HEIGHT;
	S->scroll_height_full = TRUE;

	S->saved_cursor_x = 0;
	S->saved_cursor_y = 0;

	if (S->tabstop) g_free(S->tabstop);
	S->tabstop = NULL;
	S->tabclear = FALSE;
	S->tabstops = 0;

	S->o_KAM	= FALSE;
	S->o_CRM	= FALSE;
	S->o_IRM	= FALSE;
	S->o_SRM	= TRUE;
	S->o_LNM	= FALSE;
	S->o_DECKPM	= FALSE;

	S->o_DECOM	= FALSE;
	S->o_DECAWM	= TRUE;		/* Real VT520/525's default this to OFF. */

	S->o_DECNCSM	= TRUE;		/* No idea what VT520/525's default to. */
}

static void emul_reset(TemuEmul *S)
{
	S->state = VT_GROUND;
	S->chars = 0;
	S->strs = 0;

	g_free(S->out);
	S->out = NULL;
	S->outs = 0;
	S->outmax = 0;

	emul_reset_soft(S);

	emul_ED(S, 2);
	emul_move_cursor(S, 0, 0);
}

static void emul_resize(TemuEmul *S, gint width, gint height)
{
	if (!S->allow_resize)
		return;

	temu_screen_set_size(T, width, height, -1);
}

static void emul_cursor_cleared(TemuEmul *S)
{
	S->cursor_redraw = TRUE;
}

static void emul_cursor_clear(TemuEmul *S)
{
	temu_cell_t cell;
	gint x;

	if (S->cursor_redraw)
		return;

	S->cursor_redraw = TRUE;

	if (S->cursor_x >= WIDTH)
		x = WIDTH - 1;
	else
		x = S->cursor_x;

	cell = *(temu_screen_get_cell(T, x, S->cursor_y));
	SET_ATTR(cell.attr, CURSOR, 0);
	temu_screen_set_cell(T, x, S->cursor_y, &cell);
}

static void emul_cursor_draw(TemuEmul *S)
{
	temu_cell_t cell;
	gint x;

	if (S->cursor_x >= WIDTH)
		x = WIDTH - 1;
	else
		x = S->cursor_x;

	cell = *(temu_screen_get_cell(T, x, S->cursor_y));
	SET_ATTR(cell.attr, CURSOR, 1);
	temu_screen_set_cell(T, x, S->cursor_y, &cell);
}

static void emul_cursor_save(TemuEmul *S)
{
	S->saved_cursor_x = S->cursor_x;
	S->saved_cursor_y = S->cursor_y;
	S->saved_attr = ATTR;
}

static void emul_cursor_restore(TemuEmul *S)
{
	emul_cursor_clear(S);
	S->cursor_x = S->saved_cursor_x;
	S->cursor_y = S->saved_cursor_y;
	ATTR = S->saved_attr;
}

static void emul_tab_realloc(TemuEmul *S, gint col)
{
	if ((S->tabstops * 8) <= col) {
		gint newstops = (col / 8) + 1;
		S->tabstop = g_realloc(S->tabstop, sizeof(*S->tabstop) * newstops);
		if (S->tabclear) {
			memset(&S->tabstop[S->tabstops], 0x00, newstops - S->tabstops);
		} else {
			memset(&S->tabstop[S->tabstops], 0x01, newstops - S->tabstops);
		}
		S->tabstops = newstops;
	}
}

static void emul_tab_reset(TemuEmul *S, gboolean clear)
{
	S->tabstops = 0;
	g_free(S->tabstop);
	S->tabstop = NULL;
	S->tabclear = clear;
}

static void emul_tab_set(TemuEmul *S, gint col)
{
	emul_tab_realloc(S, col);
	S->tabstop[col / 8] |= (1 << (col % 8));
}

static void emul_tab_clear(TemuEmul *S, gint col)
{
	emul_tab_realloc(S, col);
	S->tabstop[col / 8] &= ~(1 << (col % 8));
}

/* FIXME: What does a vt52x do as far as wrapping?
   what does it do when there are no tabs? */
static void emul_tab_next(TemuEmul *S)
{
	gint stop;
	gint substop;

	emul_cursor_clear(S);

	if (S->cursor_x >= (WIDTH - 1) && S->o_DECAWM) {
		emul_CR(S); emul_IND(S);
	}

	stop = (S->cursor_x + 1) / 8;
	substop = (S->cursor_x + 1) % 8;

	while (stop < S->tabstops) {
		S->cursor_x++;
		if (S->cursor_x >= WIDTH) {
			S->cursor_x = WIDTH - 1;
			return;
		}

		if (S->tabstop[stop] & (1<<substop))
			return;

		substop++;
		if (substop >= 8) { stop++; substop -= 8; }
	}

	if (S->tabclear) {
		S->cursor_x = WIDTH - 1;
	} else {
		S->cursor_x = S->cursor_x + (8 - (S->cursor_x % 8));
		if (S->cursor_x >= WIDTH)
			S->cursor_x = WIDTH - 1;
	}
}

static void emul_tab_prev(TemuEmul *S)
{
	gint stop = (S->cursor_x - 1) / 8;
	gint substop = (S->cursor_x - 1) % 8;

	emul_cursor_clear(S);

	if (stop >= S->tabstops) {
		if (S->tabclear) {
			if (S->tabstops < 1) {
				S->cursor_x = 0;
				return;
			}
			stop = S->tabstops - 1;
			substop = 7;
			S->cursor_x = (stop*8+substop)+1;
		} else {
			S->cursor_x -= (S->cursor_x - 1) % 8 + 1;
			return;
		}
	}

	if (substop < 0) {
		S->cursor_x = 0;
		return;
	}

	while (stop >= 0) {
		S->cursor_x--;
		if (S->cursor_x <= 0) { S->cursor_x = 0; return; }

		if (S->tabstop[stop] & (1<<substop))
			return;

		substop--;
		if (substop < 0) { stop--; substop += 8; }
	}
}

static void emul_move_cursor(TemuEmul *S, gint x, gint y)
{
	emul_cursor_clear(S);

	S->cursor_x = x;
	if (S->cursor_x < 0) S->cursor_x = 0;
	else if (S->cursor_x >= WIDTH) S->cursor_x = WIDTH - 1;

	S->cursor_y = y;
	if (!S->o_DECOM) {
		if (S->cursor_y < 0) S->cursor_y = 0;
		else if (S->cursor_y >= HEIGHT) S->cursor_y = HEIGHT - 1;
	} else {
		if (S->cursor_y < S->scroll_top) {
			S->cursor_y = S->scroll_top;
		} else if (S->cursor_y >= (S->scroll_top + S->scroll_height)) {
			S->cursor_y = (S->scroll_top + S->scroll_height) - 1;
		}
	}
}

static void emul_scroll(TemuEmul *S, gint rows)
{
	emul_cursor_clear(S);
	temu_screen_do_scroll(T, rows, S->scroll_top, S->scroll_height, &ECELL);
}

static void emul_BS(TemuEmul *S)
{
	emul_move_cursor(S, S->cursor_x - 1, S->cursor_y);
}

static void emul_CR(TemuEmul *S)
{
	emul_move_cursor(S, 0, S->cursor_y);
}

static void emul_DA1(TemuEmul *S)
{
	const gint attrs[] = { 1, 21, 22, 0 }, *a;
	gchar buffer[256], *p;

	p = buffer;

	p = emul_stpcpy_c1(S, p, C_CSI);
	p = g_stpcpy(p, "?65");
	for (a = attrs; *a > 0; a++)
		p += g_sprintf(p, ";%d", *a);
	*p++ = 'c';

	emul_add_output(S, buffer, p - buffer);
}

static void emul_DECBI(TemuEmul *S)
{
	emul_cursor_clear(S);
	if (S->cursor_x > 0) {
		S->cursor_x--;
	} else {
		temu_screen_move_rect(T, 0, S->scroll_top, WIDTH, S->scroll_height, 1, 0, &ECELL);
	}
}

static void emul_DECFI(TemuEmul *S)
{
	emul_cursor_clear(S);
	if (S->cursor_x < (WIDTH-1)) {
		S->cursor_x++;
	} else {
		temu_screen_move_rect(T, 0, S->scroll_top, WIDTH, S->scroll_height, -1, 0, &ECELL);
	}
}

static void emul_DECRQSS(TemuEmul *S)
{
	gint i;
	gchar buffer[512], *p;

	p = buffer;
	p = g_stpcpy(p, "1$r");

	S->strt = C_CSI;
	S->pre = S->intr = S->fin = 0;
	for (i = 0; i < S->strs; i++) {
		/* FIXME: How lenient is the vt52x on this? */
		switch (S->str[i]) {
		  case 0x3a: case 0x3c ... 0x3f:
			if (S->pre && S->pre != S->str[i])
				return;
			S->pre = S->str[i];
			break;
		  case 0x20 ... 0x2f:
			if (S->intr && S->intr != S->str[i])
				return;
			S->intr = S->str[i];
			break;
		  case 0x40 ... 0x7e:
		  case 0x80 ... 0xff:
			if (S->fin && S->fin != S->str[i])
				return;
			S->fin = S->str[i];
			break;
		}
	}

	switch (E(S->strt,S->pre,S->intr,S->fin)) {
	  case E(C_CSI,0,',','}'):
		NOTIMPL("DECATC request", "", "probably");
		break;
	  case E(C_CSI,0,',','|'):
		NOTIMPL("DECAC request", "", "probably");
		break;
	  case E(C_CSI,0,'-','q'):
		NOTIMPL("DECCRTST request", "", "never");
		break;
	  case E(C_CSI,0,',','z'):
		NOTIMPL("DECDLDA request", "", "never");
		break;
	  case E(C_CSI,0,'-','r'):
		NOTIMPL("DECSEST request", "", "never");
		break;
	  case E(C_CSI,0,'$','}'):
		NOTIMPL("DECSASD request", "", "maybe");
		break;
	  case E(C_CSI,0,'*','x'):
		NOTIMPL("DECSACE request", "", "probably");
		break;
	  case E(C_CSI,0,'-','p'):
		NOTIMPL("DECARR request", "", "maybe");
		break;
	  case E(C_CSI,0,')','{'):
		NOTIMPL("DECSTGLT request", "", "probably not");
		break;
	  case E(C_CSI,0,'*','u'):
		NOTIMPL("DECSCP request", "", "never");
		break;
	  case E(C_CSI,0,'*','r'):
		NOTIMPL("DECSCS request", "", "never");
		break;
	  case E(C_CSI,0,'(','p'):
		NOTIMPL("DECSDPT request", "", "never");
		break;
	  case E(C_CSI,0,'$','q'):
		NOTIMPL("DECSDDT request", "", "never");
		break;
	  case E(C_CSI,0,'*','s'):
		NOTIMPL("DECSFC request", "", "not planned");
		break;
	  case E(C_CSI,0,'$','s'):
		NOTIMPL("DECSPRTT request", "", "never");
		break;
	  case E(C_CSI,0,'*','p'):
		NOTIMPL("DECPPCS request", "", "never");
		break;
	  case E(C_CSI,0,0,'p'):
		NOTIMPL("DECSSL request", "", "never");
		break;
	  case E(C_CSI,0,',','{'):
		NOTIMPL("DECSZS request", "", "mmaybe");
		break;
	  case E(C_CSI,0,',','x'):
		NOTIMPL("DECSPMA request", "", "probably not");
		break;
	  case E(C_CSI,0,'"','q'):
		NOTIMPL("DECSCA request", "", "probably");
		break;
	  case E(C_CSI,0,'$','|'):
		NOTIMPL("DECSCPP request", "", "definitely");
		break;
	  case E(C_CSI,0,'"','p'):	/* DECSCL request */
		p += g_sprintf(p, "6%d;%d\"p", S->conformance, S->do_C1?2:1);
		break;
	  case E(C_CSI,0,' ','q'):
		NOTIMPL("DECSCUSR request", "", "definitely");
		break;
	  case E(C_CSI,0,0,'m'):	/* DECSGR request */
		*p++ = '0';
		switch (GET_ATTR(ATTR, BOLD)) {
		  case 1: p = g_stpcpy(p, ";1"); break;
		  case 2: p = g_stpcpy(p, ";2"); break;
		}
		switch (GET_ATTR(ATTR, ITALIC)) {
		  case 1: p = g_stpcpy(p, ";3"); break;
		  case 2: p = g_stpcpy(p, ";20"); break;
		}
		switch (GET_ATTR(ATTR, UNDERLINE)) {
		  case 1: p = g_stpcpy(p, ";4"); break;
		  case 2: p = g_stpcpy(p, ";21"); break;
		}
		switch (GET_ATTR(ATTR, BLINK)) {
		  case 1: p = g_stpcpy(p, ";5"); break;
		  case 2: p = g_stpcpy(p, ";6"); break;
		}
		if (GET_ATTR(ATTR, NEGATIVE)) p = g_stpcpy(p, ";7");
		if (GET_ATTR(ATTR, HIDDEN)) p = g_stpcpy(p, ";8");
		if (GET_ATTR(ATTR, OVERSTRIKE)) p = g_stpcpy(p, ";9");
		if (GET_ATTR(ATTR, FONT)) p += g_sprintf(p, ";%d", GET_ATTR(ATTR, FONT) + 10);
		switch (GET_ATTR(ATTR, FG)) {
		  case TEMU_SCREEN_FG_DEFAULT: p = g_stpcpy(p, ";39"); break;
		  default: p += g_sprintf(p, ";%d", (GET_ATTR(ATTR, FG)%8) + 30); break;
		}
		switch (GET_ATTR(ATTR, BG)) {
		  case TEMU_SCREEN_BG_DEFAULT: p = g_stpcpy(p, ";49"); break;
		  default: p += g_sprintf(p, ";%d", (GET_ATTR(ATTR, BG)%8) + 40); break;
		}
		switch (GET_ATTR(ATTR, FRAME)) {
		  case 1: p = g_stpcpy(p, ";51"); break;
		  case 2: p = g_stpcpy(p, ";52"); break;
		}
		if (GET_ATTR(ATTR, OVERLINE)) p = g_stpcpy(p, ";53");
		*p++ = 'm';
		break;
	  case E(C_CSI,0,' ','r'):
		NOTIMPL("DECSKCV request", "", "mmaybe");
		break;
	  case E(C_CSI,0,0,'s'):
		NOTIMPL("DECSLRM request", "", "maybe");
		break;
	  case E(C_CSI,0,0,'t'):
		NOTIMPL("DECSLPP request", "", "maybe");
		break;
	  case E(C_CSI,0,' ','v'):
		NOTIMPL("DECSLCK request", "", "mmaybe");
		break;
	  case E(C_CSI,0,' ','u'):
		NOTIMPL("DECSMBV request", "", "mmaybe");
		break;
	  case E(C_CSI,0,'*','|'):
		NOTIMPL("DECSNLS request", "", "definitely");
		break;
	  case E(C_CSI,0,'+','w'):
		NOTIMPL("DECSPP request", "", "never");
		break;
	  case E(C_CSI,0,' ','p'):
		NOTIMPL("DECSSCLS request", "", "not planned");
		break;
	  case E(C_CSI,0,'$','~'):
		NOTIMPL("DECSSDT request", "", "maybe");
		break;
	  case E(C_CSI,0,0,'r'):
		NOTIMPL("DECSTBM request", "", "definitely");
		break;
	  case E(C_CSI,0,'"','u'):
		NOTIMPL("DECSTRL request", "", "never");
		break;
	  case E(C_CSI,0,' ','t'):
		NOTIMPL("DECSWBV request", "", "mmaybe");
		break;
	  case E(C_CSI,0,' ','~'):
		NOTIMPL("DECTME request", "", "mmaybe partial");
		break;
	  default:
		NOTIMPL_UNKNOWN("DECRQSS: %08x", E(S->strt,S->pre,S->intr,S->fin));
		break;
	}

	if ((p - buffer) > 3) buffer[0] = '0';
	emul_add_output(S, buffer, p - buffer);
}

static void emul_DECSCL(TemuEmul *S)
{
	gint i;
	gboolean eight_bit = TRUE;

	for (i = 0; i <= S->parms; i++) {
		PARM_DEF(i, 0);
		switch (P(i)) {
		  case 61 ... 65:
			S->conformance = P(i) - 60;
			break;
		  case 0: case 2:
			eight_bit = TRUE;
			break;
		  case 1:
			eight_bit = FALSE;
			break;
		}
	}

	if (eight_bit) {
		S->do_C1 = TRUE;
		S->send_C1_lo = FALSE;
	}

	emul_reset(S);
}

static void emul_DSR_ansi(TemuEmul *S)
{
	gchar buffer[128], *p;

	p = buffer;

	switch (P(0)) {
	  case 5:	/* Operating Status Report */
		p = emul_stpcpy_c1(S, p, C_CSI);
		p = g_stpcpy(p, "0n");
		break;
	  case 6:	/* CPR: Cursor Position Report */
		p = emul_stpcpy_c1(S, p, C_CSI);
		p += g_sprintf(p, "%d;%dR", CURSOR_X_RTRANS(S->cursor_y)+1, CURSOR_Y_RTRANS(S->cursor_x)+1);
		break;
	  default:
		NOTIMPL_UNKNOWN("DSR: Device Status Report (ANSI): %d", P(0));
		break;
	}

	emul_add_output(S, buffer, p - buffer);
}

static void emul_ED(TemuEmul *S, gint func)
{
	switch (func) {
	  case 0: /* erase below */
		temu_screen_fill_rect(
			T,
			S->cursor_x, S->cursor_y,
			WIDTH - S->cursor_x, 1,
			&ECELL
		);

		temu_screen_fill_rect(
			T,
			0, S->cursor_y + 1,
			WIDTH, HEIGHT - S->cursor_y - 1,
			&ECELL
		);
		break;
	  case 1: /* erase above */
		temu_screen_fill_rect(
			T,
			0, 0,
			WIDTH, S->cursor_y,
			&ECELL
		);

		temu_screen_fill_rect(
			T,
			0, S->cursor_y,
			S->cursor_x + 1, 1,
			&ECELL
		);
		break;

	  case 2: /* erase all */
		temu_screen_fill_rect(
			T,
			0, 0,
			WIDTH, HEIGHT,
			&ECELL
		);
		break;
	  default:
		NOTIMPL_UNKNOWN("ED: Erase in Display: %d", P(0));
		break;
	}

	emul_cursor_cleared(S);
}

static void emul_EL(TemuEmul *S, gint func)
{
	switch (func) {
	  case 0: /* erase to right */
		temu_screen_fill_rect(
			T,
			S->cursor_x, S->cursor_y,
			WIDTH - S->cursor_x, 1,
			&ECELL
		);
		break;

	  case 1: /* erase to left */
		temu_screen_fill_rect(
			T,
			0, S->cursor_y,
			S->cursor_x + 1, 1,
			&ECELL
		);
		break;

	  case 2: /* erase all */
		temu_screen_fill_rect(
			T,
			0, S->cursor_y,
			WIDTH, 1,
			&ECELL
		);
		break;
	  default:
		NOTIMPL_UNKNOWN("EL: Erase in Line: %d", P(0));
		break;
	}
	
	emul_cursor_cleared(S);
}

static void emul_ICH(TemuEmul *S, gint count)
{
	emul_cursor_clear(S);
	temu_screen_move_rect(T, S->cursor_x, S->cursor_y, WIDTH, 1, count, 0, &ECELL);
}

static void emul_IND(TemuEmul *S)
{
	emul_cursor_clear(S);
	if (S->cursor_y < (S->scroll_top + S->scroll_height - 1)) {
		S->cursor_y++;
	} else {
		emul_scroll(S, 1);
	}
}

static void emul_LF(TemuEmul *S)
{
	if (S->o_LNM)
		emul_CR(S);
	emul_IND(S);
}

static void emul_RI(TemuEmul *S)
{
	emul_cursor_clear(S);
	if (S->cursor_y > S->scroll_top) {
		S->cursor_y--;
	} else {
		emul_scroll(S, -1);
	}
}

static void emul_SGR(TemuEmul *S)
{
	gint i;

	for (i = 0; i <= S->parms; i++) {
		PARM_DEF(i, 0);
		switch (P(i)) {
		  case 0:	ATTR_NORMAL(); break;
		  case 1:	SET_ATTR(ATTR, BOLD, 1); break;
		  case 2:	SET_ATTR(ATTR, BOLD, 2); break;
		  case 3:	SET_ATTR(ATTR, ITALIC, 1); break;
		  case 4:	SET_ATTR(ATTR, UNDERLINE, 1); break;
		  case 5:	SET_ATTR(ATTR, BLINK, 1); break;
		  case 6:	SET_ATTR(ATTR, BLINK, 2); break;
		  case 7:	SET_ATTR(ATTR, NEGATIVE, 1); break;
		  case 8:	SET_ATTR(ATTR, HIDDEN, 1); break;
		  case 9:	SET_ATTR(ATTR, OVERSTRIKE, 1); break;
		  case 10 ... 19: SET_ATTR(ATTR, FONT, P(i) - 10); break;
		  case 20:	SET_ATTR(ATTR, ITALIC, 2); break;
		  case 21:	SET_ATTR(ATTR, UNDERLINE, 2); break;
		  case 22:	SET_ATTR(ATTR, BOLD, 0); break;
		  case 23:	SET_ATTR(ATTR, ITALIC, 0); break;
		  case 24:	SET_ATTR(ATTR, UNDERLINE, 0); break;
		  case 25:	SET_ATTR(ATTR, BLINK, 0); break;
		  case 27:	SET_ATTR(ATTR, NEGATIVE, 0); break;
		  case 28:	SET_ATTR(ATTR, HIDDEN, 0); break;
		  case 29:	SET_ATTR(ATTR, OVERSTRIKE, 0); break;
		  case 30 ... 37: SET_ATTR(ATTR, FG, P(i) - 30); break;
		  case 39:	SET_ATTR(ATTR, FG, TEMU_SCREEN_FG_DEFAULT); break;
		  case 40 ... 47: SET_ATTR(ATTR, BG, P(i) - 40); break;
		  case 49:	SET_ATTR(ATTR, BG, TEMU_SCREEN_BG_DEFAULT); break;
		  case 51:	SET_ATTR(ATTR, FRAME, 1); break;
		  case 52:	SET_ATTR(ATTR, FRAME, 2); break;
		  case 53:	SET_ATTR(ATTR, OVERLINE, 1); break;
		  case 54:	SET_ATTR(ATTR, FRAME, 0); break;
		  case 55:	SET_ATTR(ATTR, OVERLINE, 0); break;
		  default:
			NOTIMPL_UNKNOWN("SGR: Set Graphics Rendition: %d", P(i));
			break;
		}
	}
	
	ECELL.attr = GET_ATTR_BASE(ATTR, BASE);
}

static void emul_add_glyph(TemuEmul *S, gunichar glyph)
{
	temu_cell_t cell;
	gint written;

	cell.glyph = glyph;
	cell.attr = ATTR;
	SET_ATTR(cell.attr, WIDE, g_unichar_iswide(cell.glyph));

	if (S->o_IRM) {
		emul_ICH(S, 1 + GET_ATTR(cell.attr, WIDE));
		SET_LINE_ATTR(T, S->cursor_y, LINE_WRAPPED, 0);
	}

	if (S->o_DECAWM) {
		temu_screen_set_cell_text(T, S->cursor_x, S->cursor_y, &cell, 1, &written);
		if (written <= 0) {
			SET_LINE_ATTR(T, S->cursor_y, LINE_WRAPPED, 1);

			emul_CR(S); emul_IND(S);
			if (S->o_IRM)
				emul_ICH(S, 1 + GET_ATTR(cell.attr, WIDE));
			temu_screen_set_cell_text(T, S->cursor_x, S->cursor_y, &cell, 1, NULL);
		}
	} else {
		if (S->cursor_x >= WIDTH) S->cursor_x = WIDTH - 1;
		temu_screen_set_cell_text(T, S->cursor_x, S->cursor_y, &cell, 1, NULL);
	}

	S->cursor_x += 1 + GET_ATTR(cell.attr, WIDE);

	if (S->cursor_x >= WIDTH)
		SET_LINE_ATTR(T, S->cursor_y, LINE_WRAPPED, 0);

	emul_cursor_cleared(S);
}

static void emul_add_char(TemuEmul *S, guchar ch)
{
	gunichar glyph;

	if (S->charset == CHARSET_UTF8) {
		if (ch == C_DEL)
			return;

		if (S->chars >= 6) {
			/* FIXME: This is actually a big problem. */
			memmove(&S->char_buf, &S->char_buf[1], 5);
			S->char_buf[5] = ch;
		} else {
			S->char_buf[S->chars++] = ch;
		}

		glyph = g_utf8_get_char_validated(S->char_buf, S->chars);
		if (glyph == (gunichar)-1) {
			glyph = G_UNICHAR_UNKNOWN_GLYPH;
			memmove(&S->char_buf, &S->char_buf[1], 5);
			S->chars--;
		} else if (glyph == (gunichar)-2) {
			return;
		} else {
			S->chars = 0;
		}
	} else {
		glyph = G_UNICHAR_UNKNOWN_GLYPH;
	}

	emul_add_glyph(S, glyph);
}
