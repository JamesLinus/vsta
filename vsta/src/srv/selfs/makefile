CC=gcc
LD=ld
INCS=-I../../../include
CFLAGS=-Wall -g $(INCS)
LDFLAGS= -L../../lib
OBJS=main.o open.o rw.o stat.o
LIBS=-lusr -lc
CRT0=../../lib/crt0.o

all: selfs
.c.o:
	$(CC) $(CFLAGS) -c $*.c

selfs: $(OBJS)
	rm -f selfs
	$(LD) $(LDFLAGS) -o selfs $(CRT0) $(OBJS) $(LIBS)

test: test.o
	rm -f test
	$(LD) $(LDFLAGS) -o test $(CRT0) test.o $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f selfs test

install: all
	strip selfs
	cp selfs ../../../boot
