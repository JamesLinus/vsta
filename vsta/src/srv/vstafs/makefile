CC=gcc
LD=ld
INCS=-I../../include
CFLAGS=-DDEBUG -Wall $(INCS) -g
OBJS=main.o alloc.o secio.o buf.o node.o rw.o open.o stat.o
LIBS=-L../../libc -lusr -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

vstafs: $(OBJS)
	$(LD) -o vstafs ../../libc/crt0.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f vstafs

install: vstafs
	strip vstafs
	cp vstafs ../../boot
