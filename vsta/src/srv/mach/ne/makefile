COPTS=-Wall -DDEBUG
OBJS=main.o ne.o rw.o dir.o stat.o netlib.o
OUT=ne

include ../../../makefile.all

ne: $(OBJS)
	rm -f ne
	$(LD) $(LDFLAGS) -o ne $(CRT0) $(OBJS) -lusr -lc

install: ne
	strip ne
	cp ne $(ROOT)/boot
