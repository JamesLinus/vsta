CC=gcc
CFLAGS=-I../../include -I../.. -g
LD=ld
CRT0=../../libc/crt0.o
LIB=-L../../libc -lc

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
	cp $(PROGS) ../../bin
