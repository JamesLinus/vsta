CC	= gcc
LD	= ld
RM	= del
INCS	= -I../../../include
CFLAGS	= -DDEBUG -Wall $(INCS) -O
LDFLAGS	= -L../../../libc -L../../../lib
OBJS	= main.o rw.o stat.o
TOBJS	= libjoystick.o jstest.o
CLIB	= ../../../libc/libc.a
LIBS	= -lusr -lc -lpio
CRT0	= ../../../libc/crt0.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: joystick jstest

joystick: $(OBJS)
	$(LD) $(LDFLAGS) -o joystick $(CRT0) $(OBJS) $(LIBS)

jstest: $(TOBJS)
	$(LD) $(LDFLAGS) -o jstest $(CRT0) $(TOBJS) $(LIBS)

clean:
	$(RM) *.o
	$(RM) joystick
	$(RM) jstest
