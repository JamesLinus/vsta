CC= gcc
LD= ld
INCS= -I../../../include
CFLAGS= -Wall -DDEBUG $(INCS) -O
LDFLAGS= -L../../lib
OBJS= main.o open.o rw.o stat.o
LIBS= -lusr -lc

all: tmpfs
.c.o:
	$(CC) $(CFLAGS) -c $*.c

tmpfs: $(OBJS)
	$(LD) $(LDFLAGS) -o tmpfs ../../lib/crt0.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f tmpfs

install: all
	strip tmpfs
	cp tmpfs ../../../boot
