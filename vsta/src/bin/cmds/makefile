CC= gcc
LD= ld
CFLAGS= -O -I../../../include
LDFLAGS= -L../../lib
LIBS= -lc
CRT0=../../lib/crt0.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

PROGS=test args cat id ls swapd rm cp sleep mkdir pwd kill mv stat \
	dumpsect man fstab rmdir touch strings xargs echo du \
	tput basename uname fmt purge wc printf ps

all: $(PROGS)

uname: uname.o
	$(LD) $(LDFLAGS) -o uname $(CRT0) uname.o -lc

basename: basename.o
	$(LD) $(LDFLAGS) -o basename $(CRT0) basename.o -lc

tput: tput.o
	$(LD) $(LDFLAGS) -o tput $(CRT0) tput.o -ltermcap -lc

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
	$(LD) $(LDFLAGS) -o id $(CRT0) id.o -lusr -lc

cat: cat.o
	$(LD) $(LDFLAGS) -o cat $(CRT0) cat.o -lc

test: test.o operator.o
	$(LD) $(LDFLAGS) -o test $(CRT0) test.o operator.o -lc

args: args.o
	$(LD) $(LDFLAGS) -o args $(CRT0) args.o -lc

echo: echo.o
	$(LD) $(LDFLAGS) -o echo $(CRT0) echo.o -lc

xargs: xargs.o
	$(LD) $(LDFLAGS) -o xargs $(CRT0) xargs.o -lc

strings: strings.o
	$(LD) $(LDFLAGS) -o strings $(CRT0) strings.o -lc

du: du.o
	$(LD) $(LDFLAGS) -o du $(CRT0) du.o -lc

chmod: chmod.o
	$(LD) $(LDFLAGS) -o chmod $(CRT0) chmod.o -lc

fmt: fmt.o
	$(LD) $(LDFLAGS) -o fmt $(CRT0) fmt.o -lc

purge: purge.o
	$(LD) $(LDFLAGS) -o purge $(CRT0) purge.o -lc

wc: wc.o
	$(LD) $(LDFLAGS) -o wc $(CRT0) wc.o -lc

printf: printf.o
	$(LD) $(LDFLAGS) -o printf $(CRT0) printf.o -lc

ps: ps.o
	$(LD) $(LDFLAGS) -o ps $(CRT0) ps.o -lusr -lc

clean:
	rm -f *.o

clobber: clean
	rm -f $(PROGS)

install: all
	strip $(PROGS)
	cp $(PROGS) ../../../bin
