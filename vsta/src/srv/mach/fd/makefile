CC= gcc
LD= ld
INCS= -I../include -I..
CFLAGS= -DDEBUG $(INCS) -g
LDFLAGS= -L../libc -L../lib
OBJS= main.o rw.o stat.o dir.o
LIBS= -lc -lusr

all: fd
.c.o:
	$(CC) $(CFLAGS) -c $*.c

fd: $(OBJS)
	$(LD) $(LDFLAGS) -o fd ../libc/crt0.o $(OBJS) $(LIBS)
