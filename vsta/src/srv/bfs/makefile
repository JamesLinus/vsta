CC	= gcc
LD	= ld
INCS	= -I../../../include
CFLAGS	= -DDEBUG $(INCS) -Wall
LDFLAGS	= -L../../lib
CRT0	= ../../lib/crt0.o
OBJS	= main.o block.o filectrl.o open.o rw.o stat.o
LIBS	= -lusr -lc

all: bfs

.c.o:
	$(CC) $(CFLAGS) -c $*.c

bfs: $(OBJS)
	$(LD) $(LDFLAGS) -o bfs $(CRT0) $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f bfs

install: all
	strip bfs
	cp bfs ../../../boot
