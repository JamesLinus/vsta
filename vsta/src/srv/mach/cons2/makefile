CC= gcc
LD= ld
INCS= -I../include -I..
CFLAGS= -DCGA -DDEBUG $(INCS) -g
LDFLAGS= -L../libc -L../lib
OBJS= main.o cons.o stat.o
LIBS= -lusr -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

cons: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o cons ../libc/crt0.o $(OBJS) $(LIBS)
