CC=gcc
AS=as
LD=ld
CPP=cpp
INCS=-nostdinc -I../include
CFLAGS=$(INCS) -O -DDEBUG
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
	event.o err.o \
	$(MACHOBJS)

SRVOBJS= syscall.o malloc.o srvstr.o srvprnt.o srvopen.o srvfdl.o \
	llist.o hash.o syslog.o printf.o ctype.o srvmisc.o assert.o \
	srvstat.o srvstdio.o mount.o port.o statsup.o srvperm.o \
	permsup.o startsrv.o seg.o files.o namer.o abc.o lock.o \
	srvtime.o getopt.o \
	$(MACHOBJS)

# Libraries to be build
LIBS=libc.a libc_s.a ld.a libusr.a libtermcap.a libregexp.a \
	libm.a libsrv.a libregex.a \
	$(MACHTARGS)

# Databases for shared libraries
DBS=ld.db libc.db libm/libm.db regexp/libregexp.db term/libtermcap.db
SHLIBIN=ld.tmp libc.tmp libm.tmp libregexp.tmp libtermcap.tmp
SHLIBS=ld.shl libc.shl libm.shl libregexp.shl libtermcap.shl

all: $(LIBS) crt0.o crt0srv.o mklibs

# Statically linkable version of libc
libc_s.a: $(OBJS) $(CRT)
	rm -f libc_s.a
	ar rs libc_s.a $(OBJS) $(CRT)


# All shared libraries must be processed at once, as mkshlib assigns
# addresses as it processes each.
mklibs: mkshlib $(SHLIBIN)
	./mkshlib -l $(DBS)
	rm -f mklibs
	touch mklibs

# Utility for creating shared libraries
mkshlib: crt0.o mkshlib.o libc_s.a shlib.o shcalls.o
	rm -f mkshlib
	$(LD) -o mkshlib crt0.o mkshlib.o libc_s.a

# All shared libraries are created in both .a (stub) and .shl (loadable
# shared library) formats.  The stubs are needed before .shl, because
# one shared library's .a may be needed to generate another's .shl.
libc.a: mkshlib libc.tmp libc.db
	./mkshlib -s libc.db
ld.a: mkshlib ld.tmp ld.db
	./mkshlib -s ld.db

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
	sema.o lock.o abc.o startsrv.o selfs.o complete.o

libusr.a: $(USROBJS)
	rm -f libusr.a
	ar rs libusr.a $(USROBJS)

TERMOBJS=termcap.o tgoto.o tputs.o
libtermcap.a: mkshlib libtermcap.tmp term/libtermcap.db
	./mkshlib -s term/libtermcap.db
libtermcap.tmp: $(TERMOBJS) libc.a
	$(LD) -r -o libtermcap.tmp $(TERMOBJS) libc.a
libtermcap_s.a: $(TERMOBJS)
	ar -crs libtermcap_s.a $(TERMOBJS)

termcap.o: term/termcap.c
	$(CC) $(CFLAGS) -c term/termcap.c
tgoto.o: term/tgoto.c
	$(CC) $(CFLAGS) -c term/tgoto.c
tputs.o: term/tputs.c
	$(CC) $(CFLAGS) -c term/tputs.c

libregexp.a: mkshlib libregexp.tmp regexp/libregexp.db
	./mkshlib -s regexp/libregexp.db
libregexp.tmp: regexp.o regsub.o regerror.o libc.a
	$(LD) -r -o libregexp.tmp regexp.o regsub.o regerror.o libc.a

regexp.o: regexp/regexp.c
	$(CC) $(CFLAGS) -c regexp/regexp.c
regsub.o: regexp/regsub.c
	$(CC) $(CFLAGS) -c regexp/regsub.c
regerror.o: regexp/regerror.c
	$(CC) $(CFLAGS) -c regexp/regerror.c

REGEX=regcomp.o regerr.o regexec.o regfree.o
libregex.a: $(REGEX)
	rm -f libregex.a
	ar crs libregex.a $(REGEX)

regcomp.o: regex/regcomp.c
	$(CC) -Iregex $(CFLAGS) -c regex/regcomp.c
regerr.o: regex/regerr.c
	$(CC) -Iregex $(CFLAGS) -c regex/regerr.c
regexec.o: regex/regexec.c
	$(CC) -Iregex $(CFLAGS) -c regex/regexec.c
regfree.o: regex/regfree.c
	$(CC) -Iregex $(CFLAGS) -c regex/regfree.c

libsrv.a: $(SRVOBJS)
	rm -f libsrv.a
	ar rs libsrv.a $(SRVOBJS)

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
