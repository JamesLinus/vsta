OBJS=main.o
OUT=devnull

include ../../makefile.all

LIBS=-lusr -lc

devnull: $(OBJS)
	$(LD) $(LDFLAGS) -o devnull $(CRT0) $(OBJS) $(LIBS)

install: all
	strip devnull
	cp devnull $(ROOT)/boot
