CC= gcc
LD= ld
INCS= -I../include -I..
CFLAGS= -DDEBUG $(INCS) -g
LDFLAGS= -L../libc -L../lib
OBJS= main.o open.o rw.o stat.o
LIBS= -lc -lusr

all: namer libnamer.a
.c.o:
	$(CC) $(CFLAGS) -c $*.c

namer: $(OBJS)
	$(LD) $(LDFLAGS) -o namer ../libc/crt0.o $(OBJS) $(LIBS)

libnamer.a: namer.o
	del libnamer.a
	ar rsv libnamer.a namer.o
