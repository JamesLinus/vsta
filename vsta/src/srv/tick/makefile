CC=gcc
LD=ld
INCS=-I../../../include
CFLAGS=-g -Wall $(INCS)
LDFLAGS= -L../../lib
OBJS=main.o rw.o stat.o
LIBS=-lusr -lc
CRT0=../../lib/crt0.o

all: tick
.c.o:
	$(CC) $(CFLAGS) -c $*.c

tick: $(OBJS)
	rm -f tick
	$(LD) $(LDFLAGS) -o tick $(CRT0) $(OBJS) $(LIBS)

tst: tst.o
	rm -f tst
	$(LD) $(LDFLAGS) -o tst $(CRT0) tst.o $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f tick tst
