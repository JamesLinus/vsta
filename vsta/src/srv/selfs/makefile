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

tst: tst.o
	rm -f tst
	$(LD) $(LDFLAGS) -o tst $(CRT0) tst.o $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f selfs tst

install: all
	strip selfs
	cp selfs ../../../boot
