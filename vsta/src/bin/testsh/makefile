CC= gcc
LD= ld
CFLAGS= -g -I../../include -I../..
LDFLAGS= -L../../libc
LIBS= -lc
OBJS= testsh.o dump.o run.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: testsh standsh

testsh: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o testsh ../../libc/crt0.o $(OBJS) -lc

standsh: standsh.o dump.o run.o
	$(LD) $(LDFLAGS) -e start -o standsh ../../libc/crt0.o standsh.o dump.o run.o -lc

standsh.o: testsh.c
	$(CC) -DSTAND $(CFLAGS) -o standsh.o -c testsh.c
