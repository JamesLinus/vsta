CC=gcc
LD=ld
INCS=-I../../../include
CFLAGS=-Wall -DDEBUG $(INCS) -O
LDFLAGS=-L../../lib
OBJS=main.o open.o rw.o stat.o
LIBS=-lsrv

all: namer
.c.o:
	$(CC) $(CFLAGS) -c $*.c

namer: $(OBJS)
	$(LD) $(LDFLAGS) -o namer ../../lib/crt0srv.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f namer

install: all
	strip namer
	cp namer ../../../boot
