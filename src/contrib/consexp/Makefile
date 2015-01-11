CC=gcc
LD=ld
CFLAGS=-O -Wall -DKDB -DDEBUG
OBJS=main.o cons.o stat.o isr.o kbd.o
CRT0SRV=/vsta/lib/crt0srv.o

consexp: $(OBJS)
	$(LD) -o consexp $(CRT0SRV) $(OBJS) -lsrv -lusr

install: consexp
	strip consexp
	cp consexp $(ROOT)/boot

clean:
	rm -f *.o

clobber: clean
	rm -f consexp
