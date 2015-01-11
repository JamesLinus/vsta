COPTS=-DDEBUG
OBJS=main.o ibmrs232.o nec_bus.o ms_bus.o logi_bus.o ps2aux.o \
	mouse.o stat.o
OUT=mouse

include ../../../makefile.all

LIBS=-lusr -lc
TOBJS=test.o

mouse: $(OBJS)
	$(LD) $(LDFLAGS) -o mouse $(CRT0) $(OBJS) $(LIBS)

test: $(TOBJS) 
	$(LD) $(LDFLAGS) -o test $(CRT0) $(TOBJS) $(LIBS)

install: all
	strip mouse
	cp mouse $(ROOT)/boot
