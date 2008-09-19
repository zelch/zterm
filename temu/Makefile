CC=gcc
CFLAGS=-g -Wall -O2 -fPIC `pkg-config gtk+-2.0 pangoxft --cflags`
#CFLAGS=-g -Wall -fPIC `pkg-config gtk+-2.0 pangoxft --cflags`
LDFLAGS=`pkg-config gtk+-2.0 pangoxft --libs` -lutil -g

TEMU_OBJECTS=vt52x.o glyphcache.o screen.o screen-xft.o pty.o terminal.o

all: libtemu.so.0 libtemu.a testgtk temuterm

libtemu.so.0: $(TEMU_OBJECTS)
	$(CC) $(CFLAGS) -shared -o $@ $^ $(LDFLAGS)

libtemu.a: $(TEMU_OBJECTS)
	ar rcs $@ $^

testgtk: testgtk.o libtemu.a
	$(CC) -o $@ $^ $(LDFLAGS)

temuterm: regexp.o temuterm.o libtemu.so.0
	$(CC) -o $@ $^

clean:
	rm -f $(TEMU_OBJECTS) libtemu.a testgtk.o regexp.o temuterm.o testgtk temuterm libtemu.so.0

install: libtemu.a libtemu.so.0
	rm -f /usr/local/lib/libtemu.a /usr/local/lib/libtemu.so.0
	cp libtemu.a libtemu.so.0 /usr/local/lib/

## DEPS ##
# GCC 3.3 sucks, and fails to produce deps with -MM,
# due to the retarded new definition of system includes.
