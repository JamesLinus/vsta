COPTS=-g -Wall -DKDB -DDEBUG
OBJS=main.o cons.o stat.o isr.o kbd.o
OUT=cons

include ../../../makefile.all

cons: $(OBJS)
	$(LD) $(LDFLAGS) -o cons $(CRT0SRV) $(OBJS) -lsrv -lusr

install: cons
	strip cons
	cp cons $(ROOT)/boot
