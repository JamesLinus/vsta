CC=gcc
LD=ld
INCS=-I../../../include
CFLAGS=-Wall -DDEBUG $(INCS) -O
LDFLAGS=-L../../lib
OBJS=main.o fat.o node.o dir.o open.o rw.o stat.o fat16.o fat12.o fat32.o
LIBS=-lsrv

all: dos
.c.o:
	$(CC) $(CFLAGS) -c $*.c

dos: $(OBJS)
	rm -f dos
	$(LD) $(LDFLAGS) -o dos ../../lib/crt0srv.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f dos

install: all
	strip dos
	cp dos ../../../boot
