CC= gcc
LD= ld
CFLAGS= -O -I../../include -I../..
LDFLAGS= -L../../libc
LIBS= -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

PROGS=test args cat id ls swapd rm cp sleep mkdir pwd kill mv stat \
	dumpsect man fstab ps rmdir touch

all: $(PROGS)

touch: touch.o
	$(LD) $(LDFLAGS) -o touch ../../libc/crt0.o touch.o -lc

rmdir: rmdir.o
	$(LD) $(LDFLAGS) -o rmdir ../../libc/crt0.o rmdir.o -lc

ps: ps.o
	$(LD) $(LDFLAGS) -o ps ../../libc/crt0.o ps.o -lc

fstab: fstab.o
	$(LD) $(LDFLAGS) -o fstab ../../libc/crt0.o fstab.o -lc

man: man.o
	$(LD) $(LDFLAGS) -o man ../../libc/crt0.o man.o -lc

stat: stat.o
	$(LD) $(LDFLAGS) -o stat ../../libc/crt0.o stat.o -lc

dump.o: ../testsh/dump.c
	$(CC) $(CFLAGS) -c ../testsh/dump.c

dumpsect: dumpsect.o dump.o
	$(LD) $(LDFLAGS) -o dumpsect ../../libc/crt0.o dumpsect.o dump.o -lc

mv: mv.o
	$(LD) $(LDFLAGS) -o mv ../../libc/crt0.o mv.o -lc

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

test: test.o operator.o
	$(LD) $(LDFLAGS) -o test ../../libc/crt0.o test.o operator.o -lc

args: args.o
	$(LD) $(LDFLAGS) -o args ../../libc/crt0.o args.o -lc

clean:
	rm -f *.o

clobber: clean
	rm -f $(PROGS)

install: all
	strip $(PROGS)
	cp $(PROGS) ../../bin
