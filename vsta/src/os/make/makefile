CC=gcc
AS=as
CPP=cpp
LD=ld
STRIP=strip
INCS=-I../../include -I.

target: vsta

include ../mach/makefile.inc

DEFS= -DKERNEL -DKDB -DDEBUG
CFLAGS= $(DEFS) $(INCS) -O2
LIBS= ../../lib/libusr.a
LDFLAGS=-Ttext 0x100020 -Tvsta.lnk


config: config.c
	$(CC) -o config config.c

locore.o: assym.h

assym.h: genassym
	./genassym assym.h

genassym: ../mach/genassym.c
	$(CC) $(INCS) -o genassym ../mach/genassym.c

main.o: ../kern/main.c
	$(CC) $(CFLAGS) -c ../kern/main.c

vm_fault.o: ../kern/vm_fault.c
	$(CC) $(CFLAGS) -c ../kern/vm_fault.c

vm_steal.o: ../kern/vm_steal.c
	$(CC) $(CFLAGS) -c ../kern/vm_steal.c

vm_page.o: ../kern/vm_page.c
	$(CC) $(CFLAGS) -c ../kern/vm_page.c

malloc.o: ../kern/malloc.c
	$(CC) $(CFLAGS) -c ../kern/malloc.c

misc.o: ../kern/misc.c
	$(CC) $(CFLAGS) -c ../kern/misc.c

vas.o: ../kern/vas.c
	$(CC) $(CFLAGS) -c ../kern/vas.c

pset.o: ../kern/pset.c
	$(CC) $(CFLAGS) -c ../kern/pset.c

msg.o: ../kern/msg.c
	$(CC) $(CFLAGS) -c ../kern/msg.c

msgcon.o: ../kern/msgcon.c
	$(CC) $(CFLAGS) -c ../kern/msgcon.c

msgkern.o: ../kern/msgkern.c
	$(CC) $(CFLAGS) -c ../kern/msgkern.c

seg.o: ../kern/seg.c
	$(CC) $(CFLAGS) -c ../kern/seg.c

port.o: ../kern/port.c
	$(CC) $(CFLAGS) -c ../kern/port.c

atl.o: ../kern/atl.c
	$(CC) $(CFLAGS) -c ../kern/atl.c

qio.o: ../kern/qio.c
	$(CC) $(CFLAGS) -c ../kern/qio.c

pset_fod.o: ../kern/pset_fod.c
	$(CC) $(CFLAGS) -c ../kern/pset_fod.c

pset_zfod.o: ../kern/pset_zfod.c
	$(CC) $(CFLAGS) -c ../kern/pset_zfod.c

pset_mem.o: ../kern/pset_mem.c
	$(CC) $(CFLAGS) -c ../kern/pset_mem.c

pset_cow.o: ../kern/pset_cow.c
	$(CC) $(CFLAGS) -c ../kern/pset_cow.c

vm_swap.o: ../kern/vm_swap.c
	$(CC) $(CFLAGS) -c ../kern/vm_swap.c

sched.o: ../kern/sched.c
	$(CC) $(CFLAGS) -c ../kern/sched.c

rand.o: ../kern/rand.c
	$(CC) $(CFLAGS) -c ../kern/rand.c

proc.o: ../kern/proc.c
	$(CC) $(CFLAGS) -c ../kern/proc.c

pview.o: ../kern/pview.c
	$(CC) $(CFLAGS) -c ../kern/pview.c

xclock.o: ../kern/xclock.c
	$(CC) $(CFLAGS) -c ../kern/xclock.c

event.o: ../kern/event.c
	$(CC) $(CFLAGS) -c ../kern/event.c

mmap.o: ../kern/mmap.c
	$(CC) $(CFLAGS) -c ../kern/mmap.c

phys.o: ../kern/phys.c
	$(CC) $(CFLAGS) -c ../kern/phys.c

exec.o: ../kern/exec.c
	$(CC) $(CFLAGS) -c ../kern/exec.c

exitgrp.o: ../kern/exitgrp.c
	$(CC) $(CFLAGS) -c ../kern/exitgrp.c

ptrace.o: ../kern/ptrace.c
	$(CC) $(CFLAGS) -c ../kern/ptrace.c

pstat.o: ../kern/pstat.c
	$(CC) $(CFLAGS) -c ../kern/pstat.c

dbgmain.o: ../dbg/dbgmain.c
	$(CC) $(CFLAGS) -c ../dbg/dbgmain.c

dump.o: ../dbg/dump.c
	$(CC) $(CFLAGS) -c ../dbg/dump.c

expr.o: ../dbg/expr.c
	$(CC) $(CFLAGS) -c ../dbg/expr.c

lex.o: ../dbg/lex.c
	$(CC) $(CFLAGS) -c ../dbg/lex.c

names.o: ../dbg/names.c
	$(CC) $(CFLAGS) -c ../dbg/names.c

dbgproc.o: ../dbg/dbgproc.c
	$(CC) $(CFLAGS) -c ../dbg/dbgproc.c

OBJS=$(MACHOBJS) \
	main.o vm_fault.o vm_steal.o vm_page.o malloc.o misc.o \
	vas.o pset.o msg.o msgcon.o msgkern.o seg.o \
	port.o atl.o qio.o pset_fod.o pset_zfod.o \
	pset_mem.o pset_cow.o vm_swap.o sched.o rand.o \
	proc.o pview.o xclock.o event.o mmap.o phys.o \
	exec.o exitgrp.o ptrace.o pstat.o dbgmain.o \
	dump.o expr.o lex.o names.o dbgproc.o

vsta: nostrip dbsym
	./dbsym vsta
	$(STRIP) vsta

nostrip: $(OBJS)
	$(LD) $(LDFLAGS) -o vsta $(OBJS) $(LIBS)

dbsym: ../dbg/dbsym.c
	$(CC) $(DEFS) -o dbsym ../dbg/dbsym.c

clean:
	rm -f genassym assym.h locore.s *.o dbsym config

clobber: clean
	rm -f vsta

install: vsta
	cp vsta ../../../boot
