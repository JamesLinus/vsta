COPTS=-DDEBUG
OBJS=main.o open.o rw.o stat.o string.o node.o
OUT=env

include ../../makefile.all

env: $(OBJS)
	$(LD) $(LDFLAGS) -o env $(CRT0) $(OBJS) -lusr -lc

install: all
	strip env
	cp env $(ROOT)/boot
