CC=gcc
CFLAGS=-I../../../include
LD=ld
CRT0=../../lib/crt0.o
LIB=-L../../lib -lc

PROGS=setime date
all: $(PROGS)

.c.o:
	$(CC) $(CFLAGS) -c $*.c

setime: setime.o time.o getdate.o
	$(LD) $(CRT0) -o setime setime.o time.o getdate.o $(LIB)

date: date.o time.o
	$(LD) $(CRT0) -o date date.o time.o $(LIB)

clean:
	rm -f *.o

clobber: clean
	rm -f $(PROGS)

install: all
	strip $(PROGS)
	cp $(PROGS) ../../../bin
