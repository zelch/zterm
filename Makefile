CC=gcc
CFLAGS=-g -Wall -O2 -fPIC `pkg-config gtk+-3.0 vte-2.91 --cflags`
LDFLAGS=`pkg-config gtk+-3.0 vte-2.91 --libs` `pkg-config --exists libbsd && pkg-config --libs libbsd` -lutil -g
UNAME_S := $(shell uname -s)
CFLAGS += `pkg-config --exists libbsd && echo -D HAVE_LIBBSD`
ifeq (${UNAME_S},Darwin)
	CFLAGS += -D OSX -mmacosx-version-min=11.0
else ifeq (${UNAME_S},Linux)
	CFLAGS += -D LINUX
endif

all: zterm .syntastic_c_config app

zterm: zterm.o
	$(CC) -o $@ $^ $(LDFLAGS)

app: zterm
	rm -rf zterm.app
	mkdir zterm.app
	mkdir zterm.app/Contents
	mkdir zterm.app/Contents/MacOS
	mkdir zterm.app/Contents/Resources
	cp Info.plist zterm.app/Contents/
	cp PkgInfo zterm.app/Contents/
	cp zterm zterm.app/Contents/MacOS/
	./icon_gen Linux_terminal.svg
	cp Linux_terminal.icns zterm.app/Contents/Resources/

clean:
	rm -f zterm.o zterm

.PHONY: .syntastic_c_config
.syntastic_c_config:
	echo "${CFLAGS}" | sed 's/ /\n/g' > .syntastic_c_config
