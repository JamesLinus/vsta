COPTS=-DDEBUG -Wall
OBJS=main.o alloc.o secio.o node.o rw.o open.o stat.o
OUT=vstafs

include ../../makefile.all

vstafs: $(OBJS)
	rm -f vstafs
	$(LD) $(LDFLAGS) -o vstafs $(CRT0SRV) $(OBJS) -lusr -lsrv

install: vstafs
	strip vstafs
	cp vstafs $(ROOT)/boot
