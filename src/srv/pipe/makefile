COPTS=-DDEBUG
OBJS= main.o open.o rw.o stat.o
OUT=pipe

include ../../makefile.all

pipe: $(OBJS)
	$(LD) $(LDFLAGS) -o pipe $(CRT0) $(OBJS) -lusr -lc

install: all
	strip pipe
	cp pipe $(ROOT)/boot
