CC= gcc
LD= ld
INCS= -I../../../include
CFLAGS= -DDEBUG $(INCS)
LDFLAGS= -L../../lib
OBJS= main.o open.o rw.o stat.o string.o node.o
LIBS= -lusr -lc

all: env
.c.o:
	$(CC) $(CFLAGS) -c $*.c

env: $(OBJS)
	$(LD) $(LDFLAGS) -o env ../../lib/crt0.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f env

install: all
	strip env
	cp env ../../../boot
