COPTS=-DDEBUG
OBJS=main.o rw.o stat.o dir.o
OUT=fd

include ../../../makefile.all

fd: $(OBJS)
	$(LD) $(LDFLAGS) -o fd $(CRT0) $(OBJS) -lusr -lc

install: all
	strip fd
	cp fd $(ROOT)/boot
