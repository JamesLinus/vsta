CC= gcc
LD= ld
CFLAGS= -I../include -I..
LDFLAGS= -L../libc
LIBS= -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: testsh

testsh: testsh.o
	$(LD) $(LDFLAGS) -e start -o testsh ../libc/crt0.o testsh.o -lc
