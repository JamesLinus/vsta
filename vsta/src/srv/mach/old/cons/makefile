CC        = gcc
LD        = ld
INCLUDES  = -I../../../include
CFLAGS    = -O -DDEBUG $(INCLUDES) $(MACHDEF)

LDFLAGS   = -L../../../libc
LIBS      = -lusr -lc

MACHDEF   = -DIBM_CONSOLE

IBM_OBJS  = main.o con_ibm.o stat.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: cons

cons: $(IBM_OBJS)
	$(LD) $(LDFLAGS) -e start -o cons ../../../libc/crt0srv.o $(IBM_OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f cons

install: all
	strip cons
	cp cons ../../../boot
