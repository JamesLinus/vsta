CC        = gcc
LD        = ld
INCLUDES  = -I../../../include
CFLAGS    = -DDEBUG $(INCLUDES) -O

LDFLAGS   = -L../../../libc
LIBS      = -lpio -lusr -lc
CRT0	  = ../../../libc/crt0.o

OBJS      = main.o ibmrs232.o nec_bus.o ms_bus.o logi_bus.o mouse.o stat.o
TOBJS     = test.o libmouse.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: mouse test

test: $(TOBJS) 
	$(LD) $(LDFLAGS) -e start -o test $(CRT0) $(TOBJS) $(LIBS)

mouse: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o mouse $(CRT0) $(OBJS) $(LIBS)

clean:
	rm -f mouse test

clobber: clean
	rm -f *.o

install: all
	strip mouse
	cp mouse ../../../boot
