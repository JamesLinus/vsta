CC= gcc
LD= ld
INCS= -I../../../../include
CFLAGS= -DDEBUG $(INCS) -O
LDFLAGS= -L../../../lib
OBJS= main.o rw.o stat.o dir.o
LIBS= -lusr -lc_s

all: fd
.c.o:
	$(CC) $(CFLAGS) -c $*.c

fd: $(OBJS)
	$(LD) $(LDFLAGS) -o fd ../../../lib/crt0srv.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f fd

install: all
	strip fd
	cp fd ../../../../boot
