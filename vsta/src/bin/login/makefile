CC=gcc
LD=ld
CFLAGS=-O -I../../../include
LDFLAGS=-L../../lib
LIBS=-lusr -lc
OBJS=login.o
CRT0=../../lib/crt0.o

all: login runrc

runrc: runrc.o
	$(LD) $(LDFLAGS) -o runrc $(CRT0) runrc.o $(LIBS)

login: $(OBJS)
	$(LD) $(LDFLAGS) -o login $(CRT0) $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f login runrc

install: all
	strip login runrc
	cp login runrc ../../../bin
