CC=gcc
LD=ld
INCS=-I../../../include
CFLAGS=-Wall $(INCS)
LDFLAGS= -L../../lib
OBJS=main.o rw.o
LIBS=-lusr -lc
CRT0=../../lib/crt0.o

all: tick
.c.o:
	$(CC) $(CFLAGS) -c $*.c

tick: $(OBJS)
	$(LD) $(LDFLAGS) -o tick $(CRT0) $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f tick test
