CC= gcc
LD= ld
INCS= -I../../../include
CFLAGS= -DDEBUG $(INCS) -Wall -O
LDFLAGS= -L../../lib
OBJS= main.o proc.o pstat.o root.o
LIBS= -lusr -lc

all: proc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

proc: $(OBJS)
	$(LD) $(LDFLAGS) -o proc ../../lib/crt0.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f proc

install: all
	strip proc
	cp proc ../../../boot
