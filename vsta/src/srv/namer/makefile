COPTS=-Wall -DDEBUG
OBJS=main.o open.o rw.o stat.o
OUT=namer

include ../../makefile.all

namer: $(OBJS)
	$(LD) $(LDFLAGS) -o namer $(CRT0SRV) $(OBJS) -lsrv

install: all
	strip namer
	cp namer $(ROOT)/boot
