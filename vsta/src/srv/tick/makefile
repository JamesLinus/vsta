COPTS=-Wall
OBJS=main.o rw.o stat.o
OUT=tick

include ../../makefile.all

LIBS=-lusr -lc

tick: $(OBJS)
	rm -f tick
	$(LD) $(LDFLAGS) -o tick $(CRT0) $(OBJS) $(LIBS)

tst: tst.o
	rm -f tst
	$(LD) $(LDFLAGS) -o tst $(CRT0) tst.o $(LIBS)

install:
	: # Nothing; this is a demo/test filesystem
