CC= gcc
LD= ld
INCS= -I../../../include
CFLAGS= -DDEBUG $(INCS) -Wall -O
LDFLAGS= -L../../../libc
LIBS= -lusr -lc
CRT0= ../../../libc/crt0.o

all: kill ps

.c.o:
	$(CC) $(CFLAGS) -c $*.c

kill: kill.o
	$(LD) $(LDFLAGS) -o kill $(CRT0) kill.o $(LIBS)

ps: ps.o
	$(LD) $(LDFLAGS) -o ps $(CRT0) ps.o $(LIBS)

install: all
	strip kill
	cp kill /vsta/bin
	strip ps
	cp ps /vsta/bin

clean:
	rm -f *.o

clobber: clean
	rm -f kill ps
