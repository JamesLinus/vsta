COPTS=-DKDB
OBJS=fifo.o main.o rw.o isr.o stat.o control.o
OUT=rs232

include ../../../makefile.all

rs232: $(OBJS)
	$(LD) $(LDFLAGS) -o rs232 $(CRT0) $(OBJS) -lusr -lc

install: rs232
	strip rs232
	cp rs232 $(ROOT)/boot
