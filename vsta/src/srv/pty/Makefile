COPTS=-g -Wall -DDEBUG
OBJS=main.o ioq.o open.o rw.o stat.o
OUT=pty

include ../../makefile.all

pty: $(OBJS)
	rm -f pty
	$(LD) $(LDFLAGS) -o pty $(CRT0) $(OBJS) -lusr -lc

install: all
	strip pty
	cp pty $(ROOT)/boot
