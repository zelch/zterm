CC=gcc
CFLAGS=-g -Wall -O2 `pkg-config gtk+-2.0 --cflags`
LDFLAGS=`pkg-config gtk+-2.0 --libs` -lutil

OBJECTS=testgtk.o vt52x.o glyphcache.o screen.o screen-xft.o pty.o terminal.o

all: testgtk

testgtk: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJECTS) testgtk

## DEPS ##
# GCC 3.3 sucks, and fails to produce deps with -MM,
# due to the retarded new definition of system includes.
