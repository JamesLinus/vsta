COPTS=-Wall
OBJS=main.o open.o rw.o stat.o
OUT=sema

include ../../makefile.all

LIBS=-lusr -lc

sema: $(OBJS)
	$(LD) $(LDFLAGS) -o sema $(CRT0) $(OBJS) $(LIBS)

test: test.o
	$(LD) $(LDFLAGS) -o test $(CRT0) test.o $(LIBS)

install: all
	strip sema
	cp sema $(ROOT)/boot
