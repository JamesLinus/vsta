/*
 * thread.h
 *	Definitions of a stream of execution
 */
#ifndef _THREAD_H
#define _THREAD_H

#include <sys/param.h>
#include <sys/types.h>
#include <mach/setjmp.h>
#include <mach/machreg.h>
#include <sys/mutex.h>
#include <sys/percpu.h>

struct thread {
	sema_t *t_wchan;	/* Semaphore we're asleep on */
	jmp_buf t_kregs;	/* Saved thread kernel state */
	struct proc *t_proc;	/* Our process */
	pid_t t_pid;		/* ID of thread */
	struct trapframe
		*t_uregs;	/* User state on kernel stack */
	char *t_kstack;		/* Base of our kernel stack */
	char *t_ustack;		/*  ...user stack for this thread */
	struct sched *t_runq;	/* Node in scheduling tree */
	uchar t_runticks;	/* # ticks left for this proc to run */
	uchar t_state;		/* State of process (see below) */
	uchar t_intr;		/*  ...flag that we were interrupted */
	uchar t_nointr;		/*  ...flag non-interruptable */
	uchar t_flags;		/* Misc. flags */
	uchar t_oink;		/* # times we ate our whole CPU quanta */
	ushort t_dummy;		/* Dummy structure padding */
	struct thread		/* Run queue list */
		*t_hd, *t_tl,
		*t_next;	/* List of threads under a process */
	volatile intfun
		t_probe;	/* When probing user memory */
	char t_err[ERRLEN];	/* Error from last syscall */
	ulong t_syscpu,		/* Ticks in system and user mode */
		t_usrcpu;
	char t_evsys[EVLEN];	/* Event from system */
	char t_evproc[EVLEN];	/*  ...from user process */
	struct percpu		/* CPU running on for TS_ONPROC */
		*t_eng;
	struct fpu *t_fpu;	/* Saved FPU state, if any */
	sema_t t_mutex;		/* Sema for thread coordination */
};

/*
 * Macros for fiddling fields
 */
#define EVENT(t) ((t)->t_evsys[0] || (t)->t_evproc[0])

/*
 * Bits in t_flags
 */
#define T_RT (0x1)		/* Thread is real-time priority */
#define T_BG (0x2)		/*  ... background priority */
#define T_FPU (0x8)		/* Thread has new state in the FPU */
#define T_EPHEM (0x10)		/* Thread is ephemeral (process exits */
				/*  when all non-ephemeral threads die) */
#define T_PROFILE (0x20)	/* Hand it an event every clock tick */

/*
 * Values for t_state
 */
#define TS_SLEEP (1)		/* Thread sleeping */
#define TS_RUN (2)		/* Thread waiting for CPU */
#define TS_ONPROC (3)		/* Thread running */
#define TS_DEAD (4)		/* Thread dead/dying */

/*
 * Max value of t_oink.  This changes how much memory we will keep
 * of high CPU usage.
 */
#define T_MAX_OINK (32)

#ifdef KERNEL
extern void dup_stack(struct thread *, struct thread *, voidfun, ulong);
extern void check_preempt(void);
extern int signal_thread(struct thread *, char *, int);

/*
 * Quick global check before the full procedure call
 */
#define CHECK_PREEMPT() {if (do_preempt) check_preempt();}
#endif

#endif /* _THREAD_H */
