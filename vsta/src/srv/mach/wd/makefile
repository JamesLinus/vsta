COPTS=-Wall -DDEBUG
OBJS=main.o rw.o wd.o dir.o stat.o
OUT=wd
LIBS=-ldpart -lsrv

include ../../../makefile.all

wd: $(OBJS)
	rm -f wd
	$(LD) $(LDFLAGS) -o wd $(CRT0SRV) $(OBJS) $(LIBS)

install: all
	strip wd
	cp wd $(ROOT)/boot
