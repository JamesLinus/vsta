CC= gcc
LD= ld
INCS= -I../../include
CFLAGS= -DDEBUG $(INCS) -g
LDFLAGS= -L../../libc
OBJS= main.o open.o rw.o stat.o string.o node.o
LIBS= -lc -lusr

all: env
.c.o:
	$(CC) $(CFLAGS) -c $*.c

env: $(OBJS)
	$(LD) $(LDFLAGS) -o env ../../libc/crt0srv.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f env

install: all
	strip env
	cp env ../../boot
