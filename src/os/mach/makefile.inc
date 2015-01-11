locore.o: ../mach/locore.s
	$(CPP) -D__ASM__ $(INCS) $(DEFS) ../mach/locore.s locore.s
	$(AS) -o locore.o locore.s

init.o: ../mach/init.c
	$(CC) $(CFLAGS) -c ../mach/init.c

mutex.o: ../mach/mutex.c
	$(CC) $(CFLAGS) -c ../mach/mutex.c

hat.o: ../mach/hat.c
	$(CC) $(CFLAGS) -c ../mach/hat.c

dbg.o: ../mach/dbg.c
	$(CC) $(CFLAGS) -c ../mach/dbg.c

isr.o: ../mach/isr.c
	$(CC) $(CFLAGS) -c ../mach/isr.c

trap.o: ../mach/trap.c
	$(CC) $(CFLAGS) -c ../mach/trap.c

machproc.o: ../mach/machproc.c
	$(CC) $(CFLAGS) -c ../mach/machproc.c

syscall.o: ../mach/syscall.c
	$(CC) $(CFLAGS) -c ../mach/syscall.c

dbg_ibm.o: ../mach/dbg_ibm.c
	$(CC) $(CFLAGS) -c ../mach/dbg_ibm.c

dbg_ser.o: ../mach/dbg_ser.c
	$(CC) $(CFLAGS) -c ../mach/dbg_ser.c

disasm.o: ../mach/disasm.c
	$(CC) $(CFLAGS) -c ../mach/disasm.c

dbgmem.o: ../mach/dbgmem.c
	$(CC) $(CFLAGS) -c ../mach/dbgmem.c

dbgtrace.o: ../mach/dbgtrace.c
	$(CC) $(CFLAGS) -c ../mach/dbgtrace.c

dbgio.o: ../mach/dbgio.c
	$(CC) $(CFLAGS) -c ../mach/dbgio.c

vm.o: ../mach/vm.c
	$(CC) $(CFLAGS) -c ../mach/vm.c

MACHOBJS=locore.o init.o mutex.o hat.o dbg.o isr.o trap.o \
	machproc.o syscall.o dbg_ibm.o dbg_ser.o disasm.o \
	dbgmem.o dbgtrace.o dbgio.o vm.o
