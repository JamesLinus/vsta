CC= gcc
LD= ld
CFLAGS= -I../../include -I../..
LDFLAGS= -L../../libc
LIBS= -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: test args cat id

id: id.o
	$(LD) $(LDFLAGS) -e start -o id ../../libc/crt0.o id.o -lc

cat: cat.o
	$(LD) $(LDFLAGS) -e start -o cat ../../libc/crt0.o cat.o -lc

test: test.o
	$(LD) $(LDFLAGS) -e start -o test ../../libc/crt0.o test.o -lc

args: args.o
	$(LD) $(LDFLAGS) -e start -o args ../../libc/crt0.o args.o -lc

clean:
	rm -f test test.o
