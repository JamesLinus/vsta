COPTS=-DDEBUG -Wall
JOBJS=main.o rw.o stat.o pio.o
TOBJS=libjoystick.o jstest.o pio.o
OBJS=$(JOBJS) $(TOBJS)
OUT=joystick

include ../../../makefile.all

LIBS=-lusr -lc

joystick: $(JOBJS)
	$(LD) $(LDFLAGS) -o joystick $(CRT0) $(JOBJS) $(LIBS)

jstest: $(TOBJS)
	$(LD) $(LDFLAGS) -o jstest $(CRT0) $(TOBJS) $(LIBS)

install: all
	strip joystick
	cp joystick $(ROOT)/boot
