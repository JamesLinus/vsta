#
# Makefile.inc
#	Included makefile portion for i386isa-specific files
#

# Objects to be contributed to libc.a
MACHOBJS=io.o mem.o misc.o setjmp.o gnufuncs.o exec.o syscalls.o

# Extra targets to be built
MACHTARGS=libdpart.a

# Stuff from machine-dependent directory
io.o: mach/io.s
	$(AS) -o io.o mach/io.s
mem.o: mach/mem.s
	$(AS) -o mem.o mach/mem.s
misc.o: mach/misc.s
	$(AS) -o misc.o mach/misc.s
setjmp.o: mach/setjmp.s
	$(AS) -o setjmp.o mach/setjmp.s

# crt0, standard version and one with boot server support
crt0.o: mach/crt0.s
	$(CPP) mach/crt0.s crt0.st
	$(AS) -o crt0.o crt0.st
	rm -f crt0.st
crt0srv.o: mach/crt0.s
	$(CPP) -DSRV mach/crt0.s crt0s.st
	$(AS) -o crt0srv.o crt0s.st
	rm -f crt0s.st
gnufuncs.o: mach/gnufuncs.s
	$(AS) -o gnufuncs.o mach/gnufuncs.s
exec.o: mach/exec.c
	$(CC) $(CFLAGS) -c mach/exec.c

# Our tricky syscall stub which needs to use CPP
syscalls.o: mach/syscalls.s
	$(CPP) -D__ASM__ $(INCS) mach/syscalls.s sysc.st
	as -o syscalls.o sysc.st
	rm -f sysc.st

# Minimal version for shlib loading
shcalls.o: mach/shcalls.s
	$(CPP) -D__ASM__ $(INCS) mach/shcalls.s shc.st
	as -o shcalls.o shc.st
	rm -f shc.st

# Disk partition interpretation library
libdpart.a: dpart.o
	rm -f libdpart.a
	ar rsv libdpart.a dpart.o
dpart.o: mach/dpart.c
	$(CC) $(CFLAGS) -c mach/dpart.c
