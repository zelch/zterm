CC=gcc
CFLAGS := -DDEBUG -g -Wall -Werror -O2 -fPIC $(shell pkg-config gtk4 vte-2.91-gtk4 --cflags)
LDFLAGS := $(shell pkg-config gtk4 vte-2.91-gtk4 --libs) $(shell pkg-config --exists libbsd && pkg-config --libs libbsd) -lutil -g
UNAME_S := $(shell uname -s)
CFLAGS += $(shell pkg-config --exists libbsd && echo -D HAVE_LIBBSD)
# CFLAGS += -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED

ifeq (${UNAME_S},Darwin)
	CFLAGS += -mmacosx-version-min=13.0
	EXTRA = zterm.app
else ifeq (${UNAME_S},Linux)
	CFLAGS += -D LINUX
endif

all: zterm .syntastic_c_config ${EXTRA} tags

zterm: zterm.o menus.o config.o
	$(CC) -o $@ $^ $(LDFLAGS)

tags: *.c
	ctags *.c

Linux_terminal.icns: Linux_terminal.svg
	./icon_gen Linux_terminal.svg

zterm.app: zterm Info.plist PkgInfo Linux_terminal.svg Makefile Linux_terminal.icns
	rm -rf zterm.app
	mkdir -p zterm.app/Contents/MacOS
	mkdir -p zterm.app/Contents/Resources
	cp Info.plist zterm.app/Contents/
	cp PkgInfo zterm.app/Contents/
	cp zterm zterm.app/Contents/MacOS/
	cp Linux_terminal.icns zterm.app/Contents/Resources/

clean:
	rm -rf *.o zterm zterm.app

.syntastic_c_config: *.c *.h
	echo "${CFLAGS}" | sed 's/ /\n/g' > .syntastic_c_config
