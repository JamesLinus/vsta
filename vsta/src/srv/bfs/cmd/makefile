CC= gcc
INCS= -I../..
CFLAGS= $(INCS) -g
OBJS= mkfs.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

mkfs: $(OBJS)
	$(CC) $(CFLAGS) -o mkfs $(OBJS)
