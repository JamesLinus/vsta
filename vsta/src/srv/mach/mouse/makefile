CC        = gcc
LD        = ld
INCLUDES  = -I../../../include
CFLAGS    = -DDEBUG $(INCLUDES) -O

LDFLAGS   = -L../../../libc -L../../../lib
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
	$(LD) $(LDFLAGS) -e start -o mouse $(CRT0) @objs $(LIBS)

clean:
	del mouse
	del test
	del *.o
