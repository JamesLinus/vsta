CC= gcc
LD= ld
CFLAGS= -O -I../../../include
LDFLAGS= -L../../lib
LIBS= -lc_s

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: perf1

perf1: perf1.o
	$(LD) $(LDFLAGS) -o perf1 ../../lib/crt0.o perf1.o $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f perf1
