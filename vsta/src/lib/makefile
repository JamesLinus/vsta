CC= gcc
AS= as
CPP= cpp
INCS= -I../include -I..
CFLAGS= $(INCS) -g -DDEBUG
OBJS= crt0.o start.o syscalls.o syscall.o mem.o io.o setjmp.o malloc.o \
	string.o doprnt.o stdio.o printf.o tty.o ctype.o scanf.o seg.o \
	mount.o open.o stat.o fdl.o hash.o time.o exec.o getopt.o \
	shell.o gnufuncs.o qsort.o float.o signal.o env.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

libc.a: $(OBJS)
	del libc.a
	ar rsv libc.a @objs

# Stuff from machine-dependent directory
io.o: mach/io.s
	$(AS) -o io.o mach/io.s
mem.o: mach/mem.s
	$(AS) -o mem.o mach/mem.s
setjmp.o: mach/setjmp.s
	$(AS) -o setjmp.o mach/setjmp.s
crt0.o: mach/crt0.s
	$(AS) -o crt0.o mach/crt0.s
gnufuncs.o: mach/gnufuncs.s
	$(AS) -o gnufuncs.o mach/gnufuncs.s
exec.o: mach/exec.c
	$(CC) $(CFLAGS) -c mach/exec.c

# Our tricky syscall stub which needs to use CPP
syscalls.o: mach/syscalls.s
	$(CPP) $(INCS) mach/syscalls.s  > tmp.s
	as -o syscalls.o tmp.s
	del tmp.s

# Share this with the kernel
hash.o: ../lib/hash.c
	$(CC) $(CFLAGS) -c ../lib/hash.c
