CC=gcc
AS=as
LD=ld
CPP= cpp
INCS= -I../../include
CFLAGS= $(INCS) -O -DDEBUG
CRT=start.o
target: all

include mach/makefile.inc
include libm/makefile.inc

OBJS= syscall.o malloc.o \
	string.o doprnt.o stdio.o printf.o tty.o ctype.o scanf.o seg.o \
	mount.o open.o stat.o fdl.o hash.o time.o getopt.o \
	shell.o qsort.o float.o signal.o env.o fdmem.o \
	perm.o random.o namer.o dir.o wait.o mktemp.o grp.o pwd.o \
	pipe.o fdnull.o errno.o port.o syslog.o rename.o fnmatch.o \
	bsearch.o strtol.o utime.o getline.o strftime.o mstring.o \
	fdcall.o obsolete.o getpass.o rlimit.o pgrp.o select.o \
	$(MACHOBJS)

SRVOBJS= syscall.o malloc.o srvstr.o srvprnt.o srvopen.o srvfdl.o \
	llist.o hash.o syslog.o printf.o ctype.o srvmisc.o assert.o \
	srvstat.o srvstdio.o mount.o port.o statsup.o srvperm.o \
	permsup.o startsrv.o seg.o files.o namer.o abc.o lock.o \
	srvtime.o getopt.o \
	$(MACHOBJS)

.c.o:
	$(CC) $(CFLAGS) -c $*.c

# Libraries to be build
LIBS=libc.a libc_s.a ld.a libusr.a libtermc.a libregex.a libm.a libsrv.a \
	$(MACHTARGS)

# Databases for shared libraries
DBS=ld.db libc.db
SHLIBIN=ld.tmp libc.tmp
SHLIBS=ld.shl libc.shl

all: $(LIBS) crt0.o crt0srv.o

# Statically linkable version of libc
libc_s.a: $(OBJS) $(CRT)
	rm -f libc_s.a
	ar rsv libc_s.a $(OBJS) $(CRT)


# All shared libraries must be processed at once, as mkshlib assigns
# addresses as it processes each.
mklibs: mkshlib $(SHLIBIN) shlib.o
	./mkshlib $(DBS)
	rm -f mklibs
	touch mklibs

# Utility for creating shared libraries
mkshlib: crt0.o mkshlib.o libc_s.a
	$(LD) -o mkshlib crt0.o mkshlib.o libc_s.a

# All shared libraries are created in both .a (stub) and .shl (loadable
# shared library) formats in the same step, "mklibs".
libc.a: mklibs
ld.a: mklibs

# These are the versions of our shared libraries which are used as
# input to mkshlib.  ld.tmp, which must operate as a dynamically
# mapped standalone program, includes a minimal set of system calls
libc.tmp: start.o $(OBJS)
	rm -f libc.tmp
	$(LD) -r -o libc.tmp start.o $(OBJS)

ld.tmp: ld.o shcalls.o
	$(LD) -r -o ld.tmp ld.o shcalls.o

# Special version for boot servers
startsrv.o: start.c
	$(CC) $(CFLAGS) -DSRV -o startsrv.o -c start.c

USROBJS= llist.o hash.o permsup.o permpr.o statsup.o \
	files.o rmap.o passwd.o ids.o assert.o mem.o \
	sema.o lock.o abc.o startsrv.o selfs.o

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

libsrv.a: $(SRVOBJS)
	rm -f libsrv.a
	ar rsv libsrv.a $(SRVOBJS)

srvstr.o: srv/srvstr.c
	$(CC) $(CFLAGS) -c srv/srvstr.c
srvprnt.o: srv/srvprnt.c
	$(CC) $(CFLAGS) -c srv/srvprnt.c
srvopen.o: open.c
	$(CC) $(CFLAGS) -DSRV -o srvopen.o -c open.c
srvfdl.o: fdl.c
	$(CC) $(CFLAGS) -DSRV -o srvfdl.o -c fdl.c
srvstat.o: stat.c
	$(CC) $(CFLAGS) -DSRV -o srvstat.o -c stat.c
srvstdio.o: stdio.c
	$(CC) $(CFLAGS) -DSRV -o srvstdio.o -c stdio.c
srvperm.o: permpr.c
	$(CC) $(CFLAGS) -DSRV -o srvperm.o -c permpr.c
srvtime.o: time.c
	$(CC) $(CFLAGS) -DSRV -o srvtime.o -c time.c
srvmisc.o: srv/srvmisc.c
	$(CC) $(CFLAGS) -o srvmisc.o -c srv/srvmisc.c

clean:
	rm -f *.o *.tmp *.st

clobber: clean
	rm -f mklibs mkshlib *.a *.shl

install: all mklibs
	cp $(LIBS) crt0.o crt0srv.o ../../lib
	cp $(SHLIBS) ../../lib
