COPTS=-Wall
OBJS=main.o open.o rw.o stat.o
OUT=selfs

include ../../makefile.all

LIBS=-lusr -lc

selfs: $(OBJS)
	rm -f selfs
	$(LD) $(LDFLAGS) -o selfs $(CRT0) $(OBJS) $(LIBS)

tst: tst.o
	rm -f tst
	$(LD) $(LDFLAGS) -o tst $(CRT0) tst.o $(LIBS)

install: all
	strip selfs
	cp selfs $(ROOT)/boot
