CC= gcc
LD= ld
INCS= -I../../../include
CFLAGS= -Wall -DCGA -DDEBUG $(INCS) -O
LDFLAGS= -L../../../libc -L../../../lib
OBJS= main.o cons.o stat.o isr.o rw.o
LIBS= -lusr -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

cons: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o cons ../../../libc/crt0srv.o $(OBJS) $(LIBS)

clean:
	del *.o
	del cons
