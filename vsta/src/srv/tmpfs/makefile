CC= gcc
LD= ld
INCS= -I../../include
CFLAGS= -DDEBUG $(INCS) -O
LDFLAGS= -L../../libc
OBJS= main.o open.o rw.o stat.o
LIBS= -lusr -lc

all: tmpfs
.c.o:
	$(CC) $(CFLAGS) -c $*.c

tmpfs: $(OBJS)
	$(LD) $(LDFLAGS) -o tmpfs ../../libc/crt0srv.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f tmpfs

install: all
	strip tmpfs
	cp tmpfs ../../boot
