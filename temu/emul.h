#ifndef TEMU_EMUL_h
#define TEMU_EMUL_h 1

typedef struct _TemuEmul TemuEmul;

typedef enum {
	CHARSET_NONE,
	CHARSET_GRAPHIC,
	CHARSET_UTF8
} temu_cset_t;

TemuEmul *temu_emul_new(TemuScreen *screen);
void temu_emul_destroy(TemuEmul *emul);

void temu_emul_emulate(TemuEmul *emul, const gchar *text, gint count);

gssize temu_emul_get_responses(TemuEmul *emul, gchar *buffer, gint len);
gboolean temu_emul_translate(TemuEmul *emul, GdkEventKey *key, guchar buffer[16], gint *count);

#endif
