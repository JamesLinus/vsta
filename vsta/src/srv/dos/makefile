CC= gcc
LD= ld
INCS= -I../../include -I../..
CFLAGS= -Wall -DDEBUG $(INCS) -O
LDFLAGS= -L../../libc
OBJS= main.o fat.o block.o node.o dir.o open.o rw.o stat.o
LIBS= -lusr -lc_s

all: dos
.c.o:
	$(CC) $(CFLAGS) -c $*.c

dos: $(OBJS)
	$(LD) $(LDFLAGS) -o dos ../../libc/crt0srv.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f dos

install: all
	strip dos
	cp dos ../../boot
