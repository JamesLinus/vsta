CC= gcc
LD= ld
CFLAGS= -O -I../../include -I../..
LDFLAGS= -L../../libc -L../../lib
LIBS= -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: test args cat id ls swapd rm cp sleep

sleep: sleep.o
	$(LD) $(LDFLAGS) -e start -o sleep ../../libc/crt0.o sleep.o -lc

cp: cp.o
	$(LD) $(LDFLAGS) -e start -o cp ../../libc/crt0.o cp.o -lc

rm: rm.o
	$(LD) $(LDFLAGS) -e start -o rm ../../libc/crt0.o rm.o -lc

swapd: swapd.o
	$(LD) $(LDFLAGS) -e start -o swapd ../../libc/crt0.o swapd.o -lc

ls: ls.o
	$(LD) $(LDFLAGS) -e start -o ls ../../libc/crt0.o ls.o -lusr -lc

id: id.o
	$(LD) $(LDFLAGS) -e start -o id ../../libc/crt0.o id.o -lc

cat: cat.o
	$(LD) $(LDFLAGS) -e start -o cat ../../libc/crt0.o cat.o -lc

test: test.o
	$(LD) $(LDFLAGS) -e start -o test ../../libc/crt0.o test.o -lc

args: args.o
	$(LD) $(LDFLAGS) -e start -o args ../../libc/crt0.o args.o -lc

clean:
	del *.o
	del test
	del ls
	del cat
	del args
	del swapd
	del id
	del rm
	del cp
