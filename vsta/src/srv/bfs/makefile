COPTS=-DDEBUG -Wall
OBJS=main.o block.o filectrl.o open.o rw.o stat.o
OUT=bfs

include ../../makefile.all

bfs: $(OBJS)
	$(LD) $(LDFLAGS) -o bfs $(CRT0) $(OBJS) -lusr -lc

install: all
	strip bfs
	cp bfs $(ROOT)/boot
