CC= gcc
LD= ld
CFLAGS= -g -I../../include -I../..
LDFLAGS= -L../../libc
LIBS= -lc
OBJS= testsh.o dump.o run.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: testsh

testsh: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o testsh ../../libc/crt0.o $(OBJS) -lc
