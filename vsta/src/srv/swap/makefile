CC= gcc
LD= ld
INCS= -I../../include
CFLAGS= -DDEBUG $(INCS) -g
OBJS= main.o rw.o swap.o stat.o
LIBS= -L../../libc -lusr -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

swap: $(OBJS)
	$(LD) -o swap ../../libc/crt0srv.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f swap

install: swap
	strip swap
	cp swap ../../boot
