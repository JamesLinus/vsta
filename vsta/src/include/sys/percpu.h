#ifndef _PERCPU_H
#define _PERCPU_H
/*
 * percpu.h
 *	Data structure which exists per CPU on the system
 */
#include <sys/types.h>

struct percpu {
	struct thread *pc_thread;	/* Thread CPU's running */
	uchar pc_num;			/* Sequential CPU ID */
	uchar pc_pri;			/* Priority running on CPU */
	ushort pc_locks;		/* # locks held by CPU */
	struct percpu *pc_next;		/* Next in list--circular */
	ulong pc_flags;			/* See below */
	ulong pc_ticks;			/* Ticks queued for clock */
	ulong pc_time[2];		/* HZ and seconds counting */
	int pc_preempt;			/* Flag that preemption needed */
};

/*
 * Bits in pc_flags
 */
#define CPU_UP 1	/* CPU is online and taking work */
#define CPU_BOOT 2	/* CPU was the boot CPU for the system */
#define CPU_CLOCK 4	/* CPU is in clock handling code */

#ifdef KERNEL
extern struct percpu cpu;		/* Maps to percpu struct on each CPU */
#define curthread cpu.pc_thread
#define do_preempt cpu.pc_preempt
extern uint ncpu;			/* # CPUs on system */
extern struct percpu *nextcpu;		/* Rotor for preemption scans */
#endif

#endif /* _PERCPU_H */
