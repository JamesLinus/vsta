CC=gcc
LD=ld
INCS= -I../../../include
CFLAGS= $(INCS) -g
OBJS= mkfs.o
CRT0=../../../libc/crt0.o
LIBS=-L../../../libc -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

mkfs: $(OBJS)
	$(LD) -o mkfs $(CRT0) $(OBJS) $(LIBS)
