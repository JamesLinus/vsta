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
	ulong pc_nopreempt;		/* > 0, preempt held off */
};

/*
 * Bits in pc_flags
 */
#define CPU_UP 0x1	/* CPU is online and taking work */
#define CPU_BOOT 0x2	/* CPU was the boot CPU for the system */
#define CPU_CLOCK 0x4	/* CPU is in clock handling code */
#define CPU_DEBUG 0x8	/* CPU hardware debugging active */
#define CPU_FP 0x10	/* CPU floating point unit present */

#ifdef KERNEL
extern struct percpu cpu;		/* Maps to percpu struct on each CPU */
#define curthread cpu.pc_thread
#define do_preempt cpu.pc_preempt
extern uint ncpu;			/* # CPUs on system */
extern struct percpu *nextcpu;		/* Rotor for preemption scans */

#define NO_PREEMPT() (cpu.pc_nopreempt += 1)
#define PREEMPT_OK() (cpu.pc_nopreempt -= 1)
#endif

#endif /* _PERCPU_H */
