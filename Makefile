CC=gcc
CFLAGS=-g -Wall -O2 -fPIC `pkg-config gtk+-3.0 vte-2.90 --cflags`
LDFLAGS=`pkg-config gtk+-3.0 vte-2.90 --libs` -lutil -g

all: zterm

zterm: zterm.o
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -f zterm.o zterm
