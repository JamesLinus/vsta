CC= gcc
LD= ld
INCS= -I../../include
CFLAGS= -DDEBUG $(INCS) -g
OBJS= main.o rw.o swap.o stat.o
LIBS= -L../../libc -L../../lib -lc -lusr

.c.o:
	$(CC) $(CFLAGS) -c $*.c

swap: $(OBJS)
	$(LD) -o swap ../../libc/crt0srv.o $(OBJS) $(LIBS)

clean:
	del *.o
	del swap
