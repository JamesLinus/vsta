CC=gcc
LD=ld
CFLAGS=-I../../include -O
OBJS=adb.o disasm.o expr.o lex.o sym.o map.o proc.o i386.o
LIBS=-L../../libc -lc
CRT0=../../libc/crt0.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

adb: $(OBJS)
	$(LD) -o adb $(CRT0) $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f adb

install:
	strip adb
	cp adb ../../bin
