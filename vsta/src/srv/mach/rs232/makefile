CC= gcc
LD= ld
INCS= -I../../../include
CFLAGS= -DDEBUG $(INCS) -g
LDFLAGS= -L../../../libc -L../../../lib
OBJS= fifo.o main.o rw.o isr.o stat.o
LIBS= -lusr -lc
CRT0= ../../../libc/crt0.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

rs232: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o rs232 $(CRT0) $(OBJS) $(LIBS)

clean:
	del *.o
	del rs232
