CC	= gcc
LD	= ld
INCS	= -I../../include
CFLAGS	= -DDEBUG $(INCS) -Wall -g
LDFLAGS	= -L../../libc -L../../lib
CRT0	= ../../libc/crt0srv.o
OBJS	= main.o block.o filectrl.o open.o rw.o stat.o
LIBS	= -lusr -lc

all: bfs

.c.o:
	$(CC) $(CFLAGS) -c $*.c

bfs: $(OBJS)
	$(LD) $(LDFLAGS) -o bfs $(CRT0) $(OBJS) $(LIBS)

clean:
	del *.o
	del bfs
