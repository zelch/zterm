#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>

#include "terminal.h"

int main(int argc, char *argv[], char *envp[])
{
	GtkWidget *window, *term;
	GdkGeometry geom;
	GdkWindowHints hints;
	GdkColor black = { .red = 0, .green = 0, .blue = 0 };

	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 1);
	gtk_widget_modify_bg(window, GTK_STATE_NORMAL, &black);

	term = temu_terminal_new();
	gtk_container_add(GTK_CONTAINER(window), term);
	gtk_widget_realize(term);
	temu_screen_get_base_geometry_hints(TEMU_SCREEN(term), &geom, &hints);
	gtk_window_set_geometry_hints(GTK_WINDOW(window), GTK_WIDGET(term), &geom, hints);

	gtk_widget_show(term);
	gtk_widget_show(window);


	{
#if 0
		char *argv[] = {
			"/bin/bash",
			"-c",
			"/home/zinx/bin/psmany",
			NULL
		};
#else
		char *argv[] = {
			"/bin/bash",
			"--login",
			NULL
		};
#endif

#if 0
		char *envp[] = {
			"TERM=xterm",
			"COLORTERM=temu",
			NULL
		};
#else
		setenv("TERM", "xterm", 1);
		setenv("COLORTERM", "temu", 1);
#endif

		temu_terminal_execve(TEMU_TERMINAL(term), argv[0], argv, envp);
	}

	gtk_main();

	return 0;
}
