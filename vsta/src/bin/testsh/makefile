CC= gcc
LD= ld
# Add -DSTAND for a more standlone-oriented flavor of shell
CFLAGS= -DSTAND -O -I../../include -I../..
LDFLAGS= -L../../libc
LIBS= -lc
OBJS= testsh.o dump.o run.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: testsh

testsh: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o testsh ../../libc/crt0srv.o $(OBJS) -lc

clean:
	del testsh
	del *.o
