COPTS=-DDEBUG -Wall
OBJS=main.o proc.o pstat.o root.o
OUT=proc

include ../../makefile.all

proc: $(OBJS)
	$(LD) $(LDFLAGS) -o proc ../../lib/crt0.o $(OBJS) -lusr -lc

install: all
	strip proc
	cp proc $(ROOT)/boot
