CC=gcc
LD=ld
LIBS=-L../../../libc
CFLAGS=-O -I../../../include
OBJS=main.o par_port.o rw.o stat.o
CRT0=../../../libc/crt0.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: par

par: $(OBJS)
	$(LD) -o par $(CRT0) $(OBJS) $(LIBS) -lusr -lc

lpcp: lpcp.o
	$(LD) -o lpcp $(CRT0) lpcp.o $(LIBS) -lc

clean:
	rm -f *.o

clobber: clean
	rm -f par lpcp

install: all
	strip par
	cp par ../../../boot
