COPTS=-Wall
OBJS=adb.o disasm.o expr.o lex.o sym.o map.o proc.o i386.o
OUT=adb
include ../../makefile.all

adb: $(OBJS)
	$(LD) $(LDFLAGS) -o adb $(CRT0) $(OBJS) -lc

install: adb
	strip adb
	cp adb $(ROOT)/bin
