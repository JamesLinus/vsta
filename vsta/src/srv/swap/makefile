CC= gcc
LD= ld
INCS= -I../../../include
CFLAGS= -DDEBUG $(INCS)
OBJS= main.o rw.o swap.o stat.o
LIBS= -L../../lib -lusr -lc_s

.c.o:
	$(CC) $(CFLAGS) -c $*.c

swap: $(OBJS)
	$(LD) -o swap ../../lib/crt0srv.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f swap

install: swap
	strip swap
	cp swap ../../../boot
