COPTS=-Wall
OBJS=mkbfs.o
OUT=mkbfs

include ../../../makefile.all

mkbfs: $(OBJS)
	$(LD) $(LDFLAGS) -o mkbfs $(CRT0) $(OBJS) -lc

install: all
	strip mkbfs
	cp mkbfs $(ROOT)/bin/mkfs_bfs
