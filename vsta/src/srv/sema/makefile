CC=gcc
LD=ld
INCS=-I../../../include
CFLAGS=-Wall $(INCS)
LDFLAGS= -L../../lib
OBJS= main.o open.o rw.o stat.o
LIBS= -lusr -lc

all: sema
.c.o:
	$(CC) $(CFLAGS) -c $*.c

sema: $(OBJS)
	$(LD) $(LDFLAGS) -o sema ../../lib/crt0.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f sema

install: all
	strip sema
	cp sema ../../../boot
