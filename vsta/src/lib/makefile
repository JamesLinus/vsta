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
	bsearch.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: libc.a crt0.o crt0srv.o

libc.a: $(OBJS)
	del libc.a
	ar rsv libc.a @objs

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
	del tmp.s
crt0srv.o: mach/crt0.s
	$(CPP) -DSRV mach/crt0.s tmp.s
	$(AS) -o crt0srv.o tmp.s
	del tmp.s
gnufuncs.o: mach/gnufuncs.s
	$(AS) -o gnufuncs.o mach/gnufuncs.s
exec.o: mach/exec.c
	$(CC) $(CFLAGS) -c mach/exec.c

# Our tricky syscall stub which needs to use CPP
syscalls.o: mach/syscalls.s
	$(CPP) -D__ASM__ $(INCS) mach/syscalls.s tmp.s
	as -o syscalls.o tmp.s
	del tmp.s

# Share this with the kernel
hash.o: ../lib/hash.c
	$(CC) $(CFLAGS) -c ../lib/hash.c

clean:
	del *.o
	del *.a
