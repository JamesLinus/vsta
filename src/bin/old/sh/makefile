CC= gcc
LD= ld
CFLAGS= -O -I../../include -I../..
LDFLAGS= -L../../libc
LIBS= -lc
OBJS= sh.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: sh

sh: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o sh ../../libc/crt0.o $(OBJS) -lc
