CC= gcc
LD= ld
CFLAGS= -O -I../../../include
LDFLAGS= -L../../lib
LIBS= -lusr -lc_s
OBJS= init.o
CRT0= ../../lib/crt0srv.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: init

init: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o init $(CRT0) $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f init

install: all
	strip init
	cp init ../../../boot
