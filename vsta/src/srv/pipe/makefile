CC= gcc
LD= ld
INCS= -I../include -I..
CFLAGS= -DDEBUG $(INCS) -O
LDFLAGS= -L../libc -L../lib
OBJS= main.o open.o rw.o stat.o
LIBS= -lusr -lc

all: pipe
.c.o:
	$(CC) $(CFLAGS) -c $*.c

pipe: $(OBJS)
	$(LD) $(LDFLAGS) -o pipe ../libc/crt0srv.o $(OBJS) $(LIBS)

clean:
	rm -f *.o pipe
