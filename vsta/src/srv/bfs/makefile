CC= gcc
INCS= -I..
CFLAGS= -DDEBUG $(INCS) -g
OBJS= main.o block.o inode.o dir.o open.o blk.o rw.o stat.o
LIBS= ../msgfake/libc.a ../lib/libusr.a

.c.o:
	$(CC) $(CFLAGS) -c $*.c

bfs: $(OBJS)
	$(CC) $(CFLAGS) -o bfs $(OBJS) $(LIBS)
