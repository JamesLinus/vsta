CC=gcc
LD=ld
INCS=-I../../../../include
CFLAGS=-DDEBUG $(INCS) -g
LDFLAGS=-L../../../lib
OBJS=main.o rw.o stat.o dir.o
LIBS=-lusr -lc

all: fd
.c.o:
	$(CC) $(CFLAGS) -c $*.c

fd: $(OBJS)
	$(LD) $(LDFLAGS) -o fd ../../../lib/crt0.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f fd

install: all
	strip fd
	cp fd ../../../../boot
