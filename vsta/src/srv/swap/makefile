CC= gcc
INCS= -I..
CFLAGS= -DDEBUG $(INCS) -g
OBJS= main.o rw.o swap.o stat.o
LIBS= ../msgfake/libc.a ../lib/libusr.a

.c.o:
	$(CC) $(CFLAGS) -c $*.c

swap: $(OBJS)
	$(CC) $(CFLAGS) -o swap $(OBJS) $(LIBS)
