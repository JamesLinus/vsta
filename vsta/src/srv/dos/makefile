CC= gcc
LD= ld
INCS= -I../include -I..
CFLAGS= -DDEBUG $(INCS) -O
LDFLAGS= -L../libc -L../lib
OBJS= main.o fat.o block.o node.o dir.o open.o rw.o stat.o
LIBS= -lusr -lc

all: dos
.c.o:
	$(CC) $(CFLAGS) -c $*.c

dos: $(OBJS)
	$(LD) $(LDFLAGS) -o dos ../libc/crt0srv.o $(OBJS) $(LIBS)

clean:
	del *.o
	del dos
