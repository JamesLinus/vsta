CC= gcc
LD= ld
INCS= -I../include -I..
CFLAGS= -DDEBUG $(INCS) -g
LDFLAGS= -L../libc -L../lib
OBJS= main.o fat.o block.o node.o dir.o open.o rw.o stat.o
LIBS= ../namer/libnamer.a -lusr -lc

all: dos
.c.o:
	$(CC) $(CFLAGS) -c $*.c

dos: $(OBJS)
	$(LD) $(LDFLAGS) -o dos ../libc/crt0.o $(OBJS) $(LIBS)
