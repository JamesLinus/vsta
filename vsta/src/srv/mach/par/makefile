CC=gcc
LD=ld
INCS=-I../../../../include
LIBS=-L../../../lib
CFLAGS=-O
OBJS=main.o par_port.o rw.o stat.o
CRT0=../../../lib/crt0.o

.c.o:
	$(CC) $(INCS) $(CFLAGS) -c $*.c

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
	cp par ../../../../boot
