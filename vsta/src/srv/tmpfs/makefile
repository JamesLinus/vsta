COPTS=-Wall -DDEBUG
OBJS=main.o open.o rw.o stat.o
OUT=tmpfs

include ../../makefile.all

tmpfs: $(OBJS)
	$(LD) $(LDFLAGS) -o tmpfs $(CRT0) $(OBJS) -lusr -lc

install: all
	strip tmpfs
	cp tmpfs $(ROOT)/boot
