CC= gcc
LD= ld
# Add -DSTAND for a more standalone-oriented flavor of shell
CFLAGS= -DSTAND -O -I../../include -I../..
# CFLAGS= -O -I../../include -I../..
LDFLAGS= -L../../libc
LIBS= -lc_s
# LIBS= -lc
CRT0=../../libc/crt0srv.o
# CRT0=../../libc/crt0.o
OBJS= testsh.o dump.o run.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: testsh

testsh: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o testsh $(CRT0) $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f testsh

# Copy to ../../bin if not STAND, but we don't seem to use that much
install: all
	strip testsh
	cp testsh ../../boot
