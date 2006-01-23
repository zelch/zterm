CC=gcc
CFLAGS=-g -Wall -O2 `pkg-config gtk+-2.0 pangoxft --cflags`
LDFLAGS=`pkg-config gtk+-2.0 pangoxft --libs` -lutil -g

TEMU_OBJECTS=vt52x.o glyphcache.o screen.o screen-xft.o pty.o terminal.o

all: testgtk temuterm

libtemu.a: $(TEMU_OBJECTS)
	ar rcs $@ $^

testgtk: testgtk.o libtemu.a
	$(CC) -o $@ $^ $(LDFLAGS)

temuterm: regexp.o temuterm.o libtemu.a
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TEMU_OBJECTS) libtemu.a testgtk.o regexp.o temuterm.o testgtk temuterm

## DEPS ##
# GCC 3.3 sucks, and fails to produce deps with -MM,
# due to the retarded new definition of system includes.
