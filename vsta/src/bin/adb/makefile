CC=gcc
LD=ld
CFLAGS=-I../../include -g
OBJS=adb.o disasm.o expr.o lex.o sym.o map.o
LIBS=-L../../libc -lc
CRT0=../../libc/crt0.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

adb: $(OBJS)
	$(LD) -o adb $(CRT0) $(OBJS) $(LIBS)
