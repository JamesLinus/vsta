COPTS=-g -Wall -DDEBUG
OBJS=main.o ioq.o open.o rw.o stat.o
OUT=pty
EXTRA_CLEAN=tst.o ptysh.o
EXTRA_CLOBBER=ptysh tst

include ../../makefile.all

pty: $(OBJS)
	rm -f pty
	$(LD) $(LDFLAGS) -o pty $(CRT0) $(OBJS) -lusr -lc

tst: tst.o
	rm -f tst
	$(CC) -o tst tst.o

ptysh: ptysh.o
	rm -f ptysh
	$(CC) -o ptysh ptysh.o

install: all
	strip pty
	cp pty $(ROOT)/boot
