COPTS=-Wall
OBJS=main.o rw.o dir.o stat.o
OUT=nvram

include ../../../makefile.all

nvram: $(OBJS)
	$(LD) $(LDFLAGS) -o nvram $(CRT0) $(OBJS) -lusr -lc

install: nvram
	strip nvram
	cp nvram $(ROOT)/boot
