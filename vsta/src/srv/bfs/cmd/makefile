CC	= gcc
LD	= ld
INCS	= -I../../../include
CFLAGS	= $(INCS) -g -Wall
LDFLAGS	= -L../../../libc
CRT0	= ../../../libc/crt0.o
OBJS	= mkbfs.o
LIBS	= -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

mkbfs: $(OBJS)
	$(LD) $(LDFLAGS) -o mkbfs $(OBJS) $(CRT0) $(LIBS)

clean:
	del *.o
	del mkbfs
