CC= gcc
LD= ld
CFLAGS= -O -I../../include -I../..
LDFLAGS= -L../../libc
LIBS= -lusr -lc
OBJS= login.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

login: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o login ../../libc/crt0.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f login

install: login
	strip login
	cp login ../../bin
