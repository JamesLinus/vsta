CC= gcc
LD= ld
INCS= -I../include -I..
CFLAGS= -DDEBUG $(INCS) -O
LDFLAGS= -L../libc -L../lib
OBJS= main.o open.o rw.o stat.o
LIBS= -lusr -lc

all: namer
.c.o:
	$(CC) $(CFLAGS) -c $*.c

namer: $(OBJS)
	$(LD) $(LDFLAGS) -o namer ../libc/crt0.o $(OBJS) $(LIBS)
