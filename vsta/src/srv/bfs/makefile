CC= gcc
LD= ld
INCS= -I../include -I..
CFLAGS= -DDEBUG $(INCS) -g
LDFLAGS= -L../libc -L../lib
OBJS= main.o block.o inode.o dir.o open.o blk.o rw.o stat.o
LIBS= -lusr -lc

all: bfs
.c.o:
	$(CC) $(CFLAGS) -c $*.c

bfs: $(OBJS)
	$(LD) $(LDFLAGS) -o bfs ../libc/crt0srv.o $(OBJS) $(LIBS)
