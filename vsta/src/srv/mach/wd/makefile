CC= gcc
LD= ld
INCS= -I../include -I..
CFLAGS= -DDEBUG $(INCS) -g
LDFLAGS= -L../libc -L../lib
OBJS= main.o rw.o wd.o fdisk.o dir.o stat.o
LIBS= ../namer/libnamer.a -lc -lusr

all: wd
.c.o:
	$(CC) $(CFLAGS) -c $*.c

wd: $(OBJS)
	$(LD) $(LDFLAGS) -o wd ../libc/crt0.o $(OBJS) $(LIBS)
