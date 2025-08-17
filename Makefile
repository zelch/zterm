CC=gcc
CFLAGS := -std=gnu23 -g -Wall -Werror -O2 $(shell pkg-config gtk4 vte-2.91-gtk4 libbsd-overlay --cflags)
LDFLAGS := $(shell pkg-config gtk4 vte-2.91-gtk4 libbsd-overlay --libs)
UNAME_S := $(shell uname -s)
# CFLAGS += -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED

ifeq (${UNAME_S},Darwin)
	CFLAGS += -mmacosx-version-min=13.0
	EXTRA = zterm.app
else ifeq (${UNAME_S},Linux)
	LDFLAGS += -lm
endif

BEAR := $(shell which bear)
ifeq (${BEAR},bear not found)
	BEAR :=
else ifeq (${BEAR},)
	BEAR :=
else
	BEAR += --append --
endif

FILES = zterm.o menus.o config.o

all: update_cflags zterm ${EXTRA}

debug : CFLAGS += -DDEBUG
debug : all

zterm: $(FILES)
	$(BEAR) $(CC) -o $@ $^ $(LDFLAGS)

$(FILES): %.o: %.c .cflags
	$(BEAR) $(CC) -c $(CFLAGS) -o $@ $<

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
	rm -rf *.o zterm zterm.app .cflags .syntastic_c_config compile_flags.txt compile_flags.json compile_commands.json tags

.PHONY: update_cflags compile_flags.txt
update_cflags: compile_flags.txt
	@bash ./maybe_update .cflags "$(CFLAGS)"

compile_flags.txt:
	@bash ./maybe_update $@ "$$(echo "${CFLAGS}" | sed 's/ /\n/g')"

# vim: set ts=8 sw=8:
