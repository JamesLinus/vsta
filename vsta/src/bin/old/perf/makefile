CC= gcc
LD= ld
CFLAGS= -O -I../../include -I../..
LDFLAGS= -L../../libc
LIBS= -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: perf1

perf1: perf1.o
	$(LD) $(LDFLAGS) -o perf1 ../../libc/crt0.o perf1.o -lc

clean:
	rm -f *.o

clobber: clean
	rm -f perf1
