CC	= gcc
LD	= ld
INCS	= -I../../../include
CFLAGS	= -DDEBUG -Wall $(INCS) -O
LDFLAGS	= -L../../../libc
OBJS	= main.o rw.o stat.o
TOBJS	= libjoystick.o jstest.o
CLIB	= ../../../libc/libc.a
LIBS	= -lusr -lc -lpio
CRT0	= ../../../libc/crt0.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

PROGS=joystick jstest
all: $(PROGS)

joystick: $(OBJS)
	$(LD) $(LDFLAGS) -o joystick $(CRT0) $(OBJS) $(LIBS)

jstest: $(TOBJS)
	$(LD) $(LDFLAGS) -o jstest $(CRT0) $(TOBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f $(PROGS)

install: all
	strip joystick
	cp joystick ../../../boot
