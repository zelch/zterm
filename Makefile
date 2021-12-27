CC=gcc
CFLAGS=-g -Wall -O2 -fPIC `pkg-config gtk+-3.0 vte-2.91 --cflags`
LDFLAGS=`pkg-config gtk+-3.0 vte-2.91 --libs` `pkg-config --exists libbsd && pkg-config --libs libbsd` -lutil -g
UNAME_S := $(shell uname -s)
ifeq (${UNAME_S},Darwin)
	CFLAGS += -D OSX -mmacosx-version-min=11.0
else ifeq (${UNAME_S},Linux)
	CFLAGS += -D LINUX
endif

all: zterm .syntastic_c_config

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

clean:
	rm -f zterm.o zterm

.PHONY: .syntastic_c_config
.syntastic_c_config:
	echo "${CFLAGS}" | sed 's/ /\n/g' > .syntastic_c_config
