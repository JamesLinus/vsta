CC= gcc
INCS= -I..
CFLAGS= -DDEBUG $(INCS) -g
OBJS= main.o rw.o stat.o dir.o
LIBS= ../msgfake/libc.a ../lib/libusr.a -lpc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

fd: $(OBJS)
	$(CC) $(CFLAGS) -o fd $(OBJS) $(LIBS)
