CC= gcc
LD= ld
INCS= -I../../../../include
CFLAGS= -Wall -DKDB -DDEBUG $(INCS) -O
LDFLAGS= -L../../../lib
OBJS= main.o cons.o stat.o isr.o rw.o
LIBS= -lsrv
CRT0=../../../lib/crt0srv.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

cons: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o cons $(CRT0) $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f cons

install: cons
	strip cons
	cp cons ../../../../boot
