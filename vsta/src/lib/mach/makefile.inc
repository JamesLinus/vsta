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
	$(CPP) mach/io.s /tmp/io.st
	$(AS) -o io.o /tmp/io.st
	rm -f /tmp/io.st
mem.o: mach/mem.s
	$(CPP) mach/mem.s /tmp/mem.st
	$(AS) -o mem.o /tmp/mem.st
	rm -f /tmp/mem.st
misc.o: mach/misc.s
	$(CPP) mach/misc.s /tmp/misc.st
	$(AS) -o misc.o /tmp/misc.st
	rm -f /tmp/misc.st
setjmp.o: mach/setjmp.s
	$(CPP) mach/setjmp.s /tmp/setjmp.st
	$(AS) -o setjmp.o /tmp/setjmp.st
	rm -f /tmp/setjmp.st

# crt0, standard version and one with boot server support
crt0.o: mach/crt0.s
	$(CPP) mach/crt0.s /tmp/crt0.st
	$(AS) -o crt0.o /tmp/crt0.st
	rm -f /tmp/crt0.st
crt0srv.o: mach/crt0.s
	$(CPP) -DSRV mach/crt0.s /tmp/crt0s.st
	$(AS) -o crt0srv.o /tmp/crt0s.st
	rm -f /tmp/crt0s.st
gnufuncs.o: mach/gnufuncs.s
	$(CPP) -DSRV mach/gnufuncs.s /tmp/gnufuncs.st
	$(AS) -o gnufuncs.o /tmp/gnufuncs.st
	rm -f /tmp/gnufuncs.st
exec.o: mach/exec.c
	$(CC) $(CFLAGS) -c mach/exec.c

# Our tricky syscall stub which needs to use CPP
syscalls.o: mach/syscalls.s
	$(CPP) -D__ASM__ $(INCS) mach/syscalls.s /tmp/sysc.st
	as -o syscalls.o /tmp/sysc.st
	rm -f /tmp/sysc.st

# Minimal version for shlib loading
shcalls.o: mach/shcalls.s
	$(CPP) -D__ASM__ $(INCS) mach/shcalls.s /tmp/shc.st
	as -o shcalls.o /tmp/shc.st
	rm -f /tmp/shc.st

# Disk partition interpretation library
libdpart.a: dpart.o
	rm -f libdpart.a
	ar rsv libdpart.a dpart.o
dpart.o: mach/dpart.c
	$(CC) $(CFLAGS) -c mach/dpart.c
