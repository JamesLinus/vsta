CC=gcc
LD=ld
INCS=-I../../../include
CFLAGS=-Wall -DDEBUG $(INCS) -O
LDFLAGS=-L../../../libc
OBJS=main.o ne.o rw.o dir.o stat.o netlib.o
LIBS=-lusr -lc

all: ne

.c.o:
	$(CC) $(CFLAGS) -c $*.c

ne: $(OBJS)
	$(LD) $(LDFLAGS) -o ne ../../../libc/crt0.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f ne

install: ne
	strip ne
	cp ne ../../../boot/ne
