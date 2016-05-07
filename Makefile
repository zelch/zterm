CC=gcc
CFLAGS=-g -Wall -O2 -fPIC `pkg-config gtk+-3.0 vte-2.91 --cflags`
LDFLAGS=`pkg-config gtk+-3.0 vte-2.91 --libs` -lutil -g -lbsd

all: zterm

zterm: zterm.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f zterm.o zterm
