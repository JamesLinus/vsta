CC= gcc
LD= ld
CFLAGS= -O -I../../include -I../..
LDFLAGS= -L../../libc
LIBS= -lc
OBJS= init.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: init

init: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o init ../../libc/crt0srv.o $(OBJS) -lc

clean:
	rm -f *.o init
