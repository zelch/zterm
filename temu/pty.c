#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pty.h>
#include <termios.h>

#include <glib.h>
#include <glib/gslist.h>

#include "pty.h"

/* This is taken from gnome-pty-helper, and ever so slightly modified. */
static struct termios*
init_term_with_defaults(struct termios* term)
{
	/*
	 *	openpty assumes POSIX termios so this should be portable.
	 *	Don't change this to a structure init - POSIX doesn't say
	 *	anything about field order.
	 */
	memset(term, 0, sizeof(struct termios));

	term->c_iflag = 0
#ifdef BRKINT
	  | BRKINT
#endif
#ifdef ICRNL
	  | ICRNL
#endif
#ifdef IMAXBEL
	  | IMAXBEL
#endif
#if 0	/* MODIFICATION -- No flow control */
#ifdef IXON
	  | IXON
#endif
#ifdef IXANY
	  | IXANY
#endif
#endif
	  ;
	term->c_oflag = 0
#ifdef OPOST
	  | OPOST
#endif
#ifdef ONLCR
	  | ONLCR
#endif
#ifdef NL0
	  | NL0
#endif
#ifdef CR0
	  | CR0
#endif
#ifdef TAB0
	  | TAB0
#endif
#ifdef BS0
	  | BS0
#endif
#ifdef VT0
	  | VT0
#endif
#ifdef FF0
	  | FF0
#endif
	  ;
	term->c_cflag = 0
#ifdef CREAD
	  | CREAD
#endif
#ifdef CS8
	  | CS8
#endif
#ifdef HUPCL
	  | HUPCL
#endif
	  ;
#ifdef EXTB
	cfsetispeed(term, EXTB);
	cfsetospeed(term, EXTB);
#else
#   ifdef B38400
        cfsetispeed(term, B38400);
        cfsetospeed(term, B38400);
#   else
#       ifdef B9600
        cfsetispeed(term, B9600);
        cfsetospeed(term, B9600);
#       endif
#   endif
#endif /* EXTB */

	term->c_lflag = 0
#ifdef ECHO
	  | ECHO
#endif
#ifdef ICANON
	  | ICANON
#endif
#ifdef ISIG
	  | ISIG
#endif
#ifdef IEXTEN
	  | IEXTEN
#endif
#ifdef ECHOE
	  | ECHOE
#endif
#ifdef ECHOKE
	  | ECHOKE
#endif
#ifdef ECHOK
	  | ECHOK
#endif
#ifdef ECHOCTL
	  | ECHOCTL
#endif
	  ;

#ifdef N_TTY
	/* should really be a check for c_line, but maybe this is good enough */
	term->c_line = N_TTY;
#endif

	/* These two may overlap so set them first */
	/* That setup means, that read() will be blocked until  */
	/* at least 1 symbol will be read.                      */
	term->c_cc[VMIN]  = 1;
	term->c_cc[VTIME] = 0;
	
	/*
	 * Now set the characters. This is of course a religious matter
	 * but we use the defaults, with erase bound to the key gnome-terminal
	 * maps.
	 *
	 * These are the ones set by "stty sane".
	 */
	   
	term->c_cc[VINTR]  = 'C'-64;
	term->c_cc[VQUIT]  = '\\'-64;
	term->c_cc[VERASE] = 127;
	term->c_cc[VKILL]  = 'U'-64;
	term->c_cc[VEOF]   = 'D'-64;
#ifdef VSWTC
	term->c_cc[VSWTC]  = 255;
#endif
	term->c_cc[VSTART] = 'Q'-64;
	term->c_cc[VSTOP]  = 'S'-64;
	term->c_cc[VSUSP]  = 'Z'-64;
	term->c_cc[VEOL]   = 255;
	
	/*
	 *	Extended stuff.
	 */
	 
#ifdef VREPRINT	
	term->c_cc[VREPRINT] = 'R'-64;
#endif
#ifdef VSTATUS
	term->c_cc[VSTATUS]  = 'T'-64;
#endif
#ifdef VDISCARD	
	term->c_cc[VDISCARD] = 'O'-64;
#endif
#ifdef VWERASE
	term->c_cc[VWERASE]  = 'W'-64;
#endif	
#ifdef VLNEXT
	term->c_cc[VLNEXT]   = 'V'-64;
#endif
#ifdef VDSUSP
	term->c_cc[VDSUSP]   = 'Y'-64;
#endif
#ifdef VEOL2	
	term->c_cc[VEOL2]    = 255;
#endif
    return term;
}

static GSList	*pty_fds;

TemuPty *temu_pty_new_execve(
	GIOFunc data_func, GIOFunc err_func, gpointer data,
	const char *path, char *const argv[], char *const envp[]
)
{
	TemuPty *pty;
	pid_t pid;
	int master;
	GIOFlags flags;
	struct termios ti;
	GSList *fds;

	init_term_with_defaults(&ti);

	pid = forkpty(&master, NULL, &ti, NULL);
	if (pid == -1)
		return NULL;

	if (!pid) {
		for (fds = pty_fds; fds; fds = g_slist_next(fds))
			close(GPOINTER_TO_INT(fds->data));

		execve(path, argv, envp);
		exit(127);
	}

	pty = g_new(TemuPty, 1);

	pty_fds = g_slist_prepend(pty_fds, GINT_TO_POINTER(master));

	/* Whoever decided to screw with the data by _DEFAULT_ loses. */
	pty->master = g_io_channel_unix_new(master);
	g_io_channel_set_encoding(pty->master, NULL, NULL);
	g_io_channel_set_buffered(pty->master, FALSE);
	flags = g_io_channel_get_flags(pty->master) | G_IO_FLAG_NONBLOCK;
	g_io_channel_set_flags(pty->master, flags, NULL);

	pty->data_watch = g_io_add_watch_full(pty->master, G_PRIORITY_HIGH, G_IO_IN|G_IO_PRI, data_func, data, NULL);
	pty->err_watch = g_io_add_watch(pty->master, G_IO_ERR|G_IO_HUP, err_func, data);

	return pty;
}

void temu_pty_destroy(TemuPty *pty)
{
	pty_fds = g_slist_remove(pty_fds, GINT_TO_POINTER(g_io_channel_unix_get_fd(pty->master)));
	g_source_remove(pty->data_watch);
	g_source_remove(pty->err_watch);
	g_io_channel_shutdown(pty->master, TRUE, NULL);
	g_io_channel_unref(pty->master);
	g_free(pty);
}

int temu_pty_set_size(TemuPty *pty, gint cols, gint rows)
{
	struct winsize size;

	memset(&size, 0, sizeof(size));
	size.ws_col = cols;
	size.ws_row = rows;

	return ioctl(g_io_channel_unix_get_fd(pty->master), TIOCSWINSZ, &size);
}
