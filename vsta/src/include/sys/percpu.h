#ifndef _PERCPU_H
#define _PERCPU_H
/*
 * percpu.h
 *	Data structure which exists per CPU on the system
 */
#include <sys/types.h>

struct percpu {
	struct thread *pc_thread;	/* Thread CPU's running */
	uint pc_locks;			/* # locks held by CPU */
	uint pc_pri;			/* Priority running on CPU */
	uchar pc_flags;			/* See below */
	uchar pc_num;			/* Sequential CPU ID */
	uchar pc_preempt;		/* Flag that preemption needed */
	uchar pc_nopreempt;		/* > 0, preempt held off */
	ulong pc_time[2];		/* HZ and seconds counting */
	ulong pc_ticks;			/* Ticks queued for clock */
	struct percpu *pc_next;		/* Next in list--circular */
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

#define NO_PREEMPT() (cpu.pc_nopreempt++)
#define PREEMPT_OK() (cpu.pc_nopreempt--)
#endif

#endif /* _PERCPU_H */
