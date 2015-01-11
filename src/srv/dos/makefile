COPTS=-Wall -DDEBUG -g
OBJS=main.o fat.o node.o dir.o open.o rw.o stat.o fat16.o fat12.o fat32.o
OUT=dos

include ../../makefile.all

dos: $(OBJS)
	rm -f dos
	$(LD) $(LDFLAGS) -o dos $(CRT0SRV) $(OBJS) -lsrv

install: all
	strip dos
	cp dos $(ROOT)/boot
