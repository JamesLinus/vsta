CC= gcc
LD= ld
CFLAGS= -O -I../../include -I../..
LDFLAGS= -L../../libc -L../../lib
LIBS= -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: test args cat id ls swapd rm cp sleep mkdir pwd kill

pwd: pwd.o
	$(LD) $(LDFLAGS) -o pwd ../../libc/crt0.o pwd.o -lc

kill: kill.o
	$(LD) $(LDFLAGS) -o kill ../../libc/crt0.o kill.o -lc

mkdir: mkdir.o
	$(LD) $(LDFLAGS) -o mkdir ../../libc/crt0.o mkdir.o -lc

sleep: sleep.o
	$(LD) $(LDFLAGS) -o sleep ../../libc/crt0.o sleep.o -lc

cp: cp.o
	$(LD) $(LDFLAGS) -o cp ../../libc/crt0.o cp.o -lc

rm: rm.o
	$(LD) $(LDFLAGS) -o rm ../../libc/crt0.o rm.o -lc

swapd: swapd.o
	$(LD) $(LDFLAGS) -o swapd ../../libc/crt0.o swapd.o -lc

ls: ls.o
	$(LD) $(LDFLAGS) -o ls ../../libc/crt0.o ls.o -lusr -lc

id: id.o
	$(LD) $(LDFLAGS) -o id ../../libc/crt0.o id.o -lc

cat: cat.o
	$(LD) $(LDFLAGS) -o cat ../../libc/crt0.o cat.o -lc

test: test.o
	$(LD) $(LDFLAGS) -o test ../../libc/crt0.o test.o -lc

args: args.o
	$(LD) $(LDFLAGS) -o args ../../libc/crt0.o args.o -lc

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
	del mkdir
	del sleep
	del pwd
	del kill
