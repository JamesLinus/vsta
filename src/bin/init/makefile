OBJS=init.o
OUT=init
include ../../makefile.all

init: $(OBJS)
	$(LD) $(LDFLAGS) -o init $(CRT0SRV) $(OBJS) -lusr -lc_s

install: init
	strip init
	cp init $(ROOT)/boot
