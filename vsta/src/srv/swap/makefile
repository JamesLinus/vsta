COPTS=-DDEBUG
OBJS=main.o rw.o swap.o stat.o
OUT=swap

include ../../makefile.all

swap: $(OBJS)
	$(LD) $(LDFLAGS) -o swap $(CRT0SRV) $(OBJS) -lusr -lsrv

install: swap
	strip swap
	cp swap $(ROOT)/boot
