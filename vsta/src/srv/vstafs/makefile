CC=gcc
CFLAGS=-I.. -Ivsta -Ivsta/include -g
OBJS=main.o alloc.o secio.o buf.o node.o rw.o

vstafs: $(OBJS)
	$(CC) $(CFLAGS) -o vstafs $(OBJS)
