CC=gcc
LD=ld
INCS=-I../../../include
CFLAGS=-Wall $(INCS)
LDFLAGS= -L../../lib
OBJS= main.o open.o rw.o stat.o
LIBS= -lusr -lc
CRT0=../../lib/crt0.o

all: sema
.c.o:
	$(CC) $(CFLAGS) -c $*.c

sema: $(OBJS)
	$(LD) $(LDFLAGS) -o sema $(CRT0) $(OBJS) $(LIBS)

test: test.o
	$(LD) $(LDFLAGS) -o test $(CRT0) test.o $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f sema test

install: all
	strip sema
	cp sema ../../../boot
