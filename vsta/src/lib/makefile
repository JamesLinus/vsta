CC= gcc
AS= as
CPP= cpp
INCS= -I../include
CFLAGS= $(INCS) -O -DDEBUG
OBJS= start.o startsrv.o syscalls.o syscall.o \
	mem.o io.o setjmp.o malloc.o \
	string.o doprnt.o stdio.o printf.o tty.o ctype.o scanf.o seg.o \
	mount.o open.o stat.o fdl.o hash.o time.o exec.o getopt.o \
	shell.o gnufuncs.o qsort.o float.o signal.o env.o fdmem.o \
	perm.o random.o namer.o dir.o wait.o mktemp.o grp.o pwd.o \
	pipe.o fdnull.o errno.o port.o syslog.o rename.o strsep.o \
	bsearch.o strtol.o strtoul.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

LIBS=libc.a libusr.a libtermc.a libregex.a libpio.a libdpart.a
all: $(LIBS) crt0.o crt0srv.o

libc.a: $(OBJS)
	rm -f libc.a
	ar rsv libc.a $(OBJS)

# Special version for boot servers
startsrv.o: start.c
	$(CC) $(CFLAGS) -DSRV -o startsrv.o -c start.c

# Stuff from machine-dependent directory
io.o: mach/io.s
	$(AS) -o io.o mach/io.s
mem.o: mach/mem.s
	$(AS) -o mem.o mach/mem.s
setjmp.o: mach/setjmp.s
	$(AS) -o setjmp.o mach/setjmp.s
# crt0, standard version and one with boot server support
crt0.o: mach/crt0.s
	$(CPP) mach/crt0.s tmp.s
	$(AS) -o crt0.o tmp.s
	rm -f tmp.s
crt0srv.o: mach/crt0.s
	$(CPP) -DSRV mach/crt0.s tmp.s
	$(AS) -o crt0srv.o tmp.s
	rm -f tmp.s
gnufuncs.o: mach/gnufuncs.s
	$(AS) -o gnufuncs.o mach/gnufuncs.s
exec.o: mach/exec.c
	$(CC) $(CFLAGS) -c mach/exec.c

# Our tricky syscall stub which needs to use CPP
syscalls.o: mach/syscalls.s
	$(CPP) -D__ASM__ $(INCS) mach/syscalls.s tmp.s
	as -o syscalls.o tmp.s
	rm -f tmp.s

USROBJS= llist.o hash.o permsup.o permpr.o statsup.o \
	files.o rmap.o passwd.o ids.o assert.o

libusr.a: $(USROBJS)
	rm -f libusr.a
	ar rsv libusr.a $(USROBJS)

libtermc.a: termcap.o tgoto.o tputs.o
	rm -f libtermc.a
	ar rsv libtermc.a termcap.o tgoto.o tputs.o
termcap.o: term/termcap.c
	$(CC) $(CFLAGS) -c term/termcap.c
tgoto.o: term/tgoto.c
	$(CC) $(CFLAGS) -c term/tgoto.c
tputs.o: term/tputs.c
	$(CC) $(CFLAGS) -c term/tputs.c

libregex.a: regexp.o regsub.o regerror.o
	rm -f libregex.a
	ar rsv libregex.a regexp.o regsub.o regerror.o

regexp.o: regexp/regexp.c
	$(CC) $(CFLAGS) -c regexp/regexp.c
regsub.o: regexp/regsub.c
	$(CC) $(CFLAGS) -c regexp/regsub.c
regerror.o: regexp/regerror.c
	$(CC) $(CFLAGS) -c regexp/regerror.c

libpio.a: pio.o
	rm -f libpio.a
	ar rsv libpio.a pio.o

libdpart.a: dpart.o
	rm -f libdpart.a
	ar rsv libdpart.a dpart.o
dpart.o: mach/dpart.c
	$(CC) $(CFLAGS) -c mach/dpart.c

clean:
	rm -f *.o

clobber: clean
	rm -f *.a

install: all
	cp $(LIBS) crt0.o crt0srv.o ../lib

