CC= gcc
LD= ld
INCS= -I../include -I..
CFLAGS= -DDEBUG $(INCS) -g
LDFLAGS= -L../libc -L../lib
OBJS= main.o open.o rw.o stat.o string.o node.o
LIBS= -lc -lusr

all: env
.c.o:
	$(CC) $(CFLAGS) -c $*.c

env: $(OBJS)
	$(LD) $(LDFLAGS) -o env ../libc/crt0srv.o $(OBJS) $(LIBS)

clean:
	del *.o
	del env
