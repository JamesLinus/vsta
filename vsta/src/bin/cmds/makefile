CC= gcc
LD= ld
CFLAGS= -O -I../../../include
LDFLAGS= -L../../lib
LIBS= -lc
CRT0=../../lib/crt0.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

#
# Note: "ps", which used to be here, is now located in srv/proc/cmd
#
PROGS=test args cat id ls swapd rm cp sleep mkdir pwd kill mv stat \
	dumpsect man fstab rmdir touch strings

all: $(PROGS)

touch: touch.o
	$(LD) $(LDFLAGS) -o touch $(CRT0) touch.o -lc

rmdir: rmdir.o
	$(LD) $(LDFLAGS) -o rmdir $(CRT0) rmdir.o -lc

fstab: fstab.o
	$(LD) $(LDFLAGS) -o fstab $(CRT0) fstab.o -lc

man: man.o
	$(LD) $(LDFLAGS) -o man $(CRT0) man.o -lc

stat: stat.o
	$(LD) $(LDFLAGS) -o stat $(CRT0) stat.o -lc

dump.o: ../testsh/dump.c
	$(CC) $(CFLAGS) -c ../testsh/dump.c

dumpsect: dumpsect.o dump.o
	$(LD) $(LDFLAGS) -o dumpsect $(CRT0) dumpsect.o dump.o -lc

mv: mv.o
	$(LD) $(LDFLAGS) -o mv $(CRT0) mv.o -lc

pwd: pwd.o
	$(LD) $(LDFLAGS) -o pwd $(CRT0) pwd.o -lc

kill: kill.o
	$(LD) $(LDFLAGS) -o kill $(CRT0) kill.o -lc

mkdir: mkdir.o
	$(LD) $(LDFLAGS) -o mkdir $(CRT0) mkdir.o -lc

sleep: sleep.o
	$(LD) $(LDFLAGS) -o sleep $(CRT0) sleep.o -lc

cp: cp.o
	$(LD) $(LDFLAGS) -o cp $(CRT0) cp.o -lc

rm: rm.o
	$(LD) $(LDFLAGS) -o rm $(CRT0) rm.o -lc

swapd: swapd.o
	$(LD) $(LDFLAGS) -o swapd $(CRT0) swapd.o -lc

ls: ls.o
	$(LD) $(LDFLAGS) -o ls $(CRT0) ls.o -lusr -lc

id: id.o
	$(LD) $(LDFLAGS) -o id $(CRT0) id.o -lc

cat: cat.o
	$(LD) $(LDFLAGS) -o cat $(CRT0) cat.o -lc

test: test.o operator.o
	$(LD) $(LDFLAGS) -o test $(CRT0) test.o operator.o -lc

args: args.o
	$(LD) $(LDFLAGS) -o args $(CRT0) args.o -lc

strings: strings.o
	$(LD) $(LDFLAGS) -o strings $(CRT0) strings.o -lc

clean:
	rm -f *.o

clobber: clean
	rm -f $(PROGS)

install: all
	strip $(PROGS)
	cp $(PROGS) ../../../bin
