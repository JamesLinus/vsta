CC= gcc
LD= ld
# Add -DSTAND for a more standalone-oriented flavor of shell
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
	rm -f *.o

clobber: clean
	rm -f testsh

# Copy to ../../bin if not STAND, but we don't seem to use that much
install: all
	strip testsh
	cp testsh ../../boot
