CC= gcc
LD= ld
INCS= -I../../../../include
CFLAGS= -Wall -DDEBUG $(INCS) -O
LDFLAGS= -L../../../lib
OBJS= main.o rw.o wd.o dir.o stat.o
LIBS= -lusr -ldpart -lc_s

all: wd

.c.o:
	$(CC) $(CFLAGS) -c $*.c

wd: $(OBJS)
	$(LD) $(LDFLAGS) -o wd ../../../lib/crt0srv.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f wd

install: all
	strip wd
	cp wd ../../../../boot
