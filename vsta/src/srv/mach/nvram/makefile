CC	= gcc
LD	= ld
RM	= rm -f
INCS	= -I../../../include
CFLAGS	= -Wall $(INCS)
LDFLAGS	= -L../../../libc
OBJS	= main.o rw.o dir.o stat.o
CLIB	= ../../../libc/libc.a
LIBS	= -lusr -lc
CRT0	= ../../../libc/crt0srv.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: nvram

nvram: $(OBJS)
	$(LD) $(LDFLAGS) -o nvram $(CRT0) $(OBJS) $(LIBS)

clean:
	$(RM) *.o

clobber: clean
	$(RM) nvram
