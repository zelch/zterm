#ifndef TEMU_PTY_h
#define TEMU_PTY_h 1

#include <glib.h>

typedef struct _TemuPty TemuPty;
struct _TemuPty {
	GIOChannel *master;
	guint data_watch, err_watch;
};


TemuPty *temu_pty_new_execve(
	GIOFunc data_func, GIOFunc err_func, gpointer data,
	const char *path, char *const argv[], char *const envp[]
);
void temu_pty_destroy(TemuPty *pty);

int temu_pty_set_size(TemuPty *pty, gint cols, gint rows);

#endif
