CC=gcc
LD=ld
INCS=-I../include -I..
CFLAGS=$(INCS) -g
OBJS=main.o alloc.o secio.o buf.o node.o rw.o open.o
LIBS=-L../libc -L../lib -lusr -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

vstafs: $(OBJS)
	$(LD) -o vstafs ../libc/crt0.o $(OBJS) $(LIBS)
