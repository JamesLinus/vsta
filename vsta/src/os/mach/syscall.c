/*
 * syscall.c
 *	Tables and functions for doing system calls
 *
 * Much of this is somewhat portable, but is left here because
 * architectures exist in which sizes are not a uniform 32-bits,
 * and more glue is needed to pick up the arguments from the user
 * and pack them into a proper procedure call.
 */
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/percpu.h>
#include <sys/thread.h>
#include <sys/fs.h>
#include <mach/machreg.h>
#include <mach/gdt.h>
#include <sys/assert.h>
#include <sys/pstat.h>
#include <mach/vm.h>
#include <sys/misc.h>
#include "../mach/locore.h"

static int do_dbg_enter();

extern int msg_port(), msg_connect(), msg_accept(), msg_send(),
	msg_receive(), msg_reply(), msg_disconnect(), msg_err();
extern int do_exit(), fork(), fork_thread(), enable_io(), enable_isr(),
	mmap(), munmap(), strerror(), notify(), clone();
extern int page_wire(), page_release(), enable_dma(), time_get(),
	time_sleep(), exec(), waits(), perm_ctl(), set_swapdev(),
	set_cmd(), pageout(), unhash(),
	time_set(), ptrace(), nop(), msg_portname(), pstat();
extern int notify_handler(), sched_op(), setsid(), mutex_thread();
extern void check_events();

struct syscall {
	intfun s_fun;
	uint s_narg;
} syscalls[] = {
	{msg_port, 2},				/*  0 */
	{msg_connect, 2},			/*  1 */
	{msg_accept, 1},			/*  2 */
	{msg_send, 2},				/*  3 */
	{msg_receive, 2},			/*  4 */
	{msg_reply, 2},				/*  5 */
	{msg_disconnect, 1},			/*  6 */
	{msg_err, 3},				/*  7 */
	{do_exit, 1},				/*  8 */
	{fork, 0},				/*  9 */
	{fork_thread, 2},			/* 10 */
	{enable_io, 2},				/* 11 */
	{enable_isr, 2},			/* 12 */
	{mmap, 6},				/* 13 */
	{munmap, 2},				/* 14 */
	{strerror, 1},				/* 15 */
	{notify, 4},				/* 16 */
	{clone, 1},				/* 17 */
	{page_wire, 3},				/* 18 */
	{page_release, 1},			/* 19 */
	{enable_dma, 0},			/* 20 */
	{time_get, 1},				/* 21 */
	{time_sleep, 1},			/* 22 */
	{do_dbg_enter, 0},			/* 23 */
	{exec, 3},				/* 24 */
	{waits, 2},				/* 25 */
	{perm_ctl, 3},				/* 26 */
	{set_swapdev, 1},			/* 27 */
	{(intfun)run_qio, 0},			/* 28 */
	{set_cmd, 1},				/* 29 */
	{pageout, 0},				/* 30 */
	{(intfun)getid, 1},			/* 31 */
	{unhash, 2},				/* 32 */
	{time_set, 1},				/* 33 */
#ifdef PROC_DEBUG
	{ptrace, 2},				/* 34 */
#else
	{nop, 1},
#endif
	{msg_portname, 1},			/* 35 */
#ifdef PSTAT
	{pstat, 4},				/* 36 */
#else
	{nop, 1},
#endif
	{notify_handler, 1},			/* 37 */
	{sched_op, 2},				/* 38 */
	{setsid, 0},				/* 39 */
	{mutex_thread, 1},			/* 40 */
};
#define NSYSCALL (sizeof(syscalls) / sizeof(struct syscall))
#define MAXARGS (6)

/*
 * do_dbg_enter()
 *	Enter debugger on DEBUG kernel, otherwise EINVAL
 */
static
do_dbg_enter(void)
{
#if defined(KDB)
	dbg_enter();
	return(0);
#else
	return(err(EINVAL));
#endif
}

#ifdef KDB
extern struct trapframe *dbg_trap_frame;
#endif

/*
 * mach_flagerr()
 *	Flag that the current operation resulted in an error
 */
void
mach_flagerr(struct trapframe *f)
{
	f->eflags |= F_CF;
}

/*
 * syscall()
 *	Code to handle a trap for system services
 */
void
syscall(ulong place_holder)
{
	struct trapframe *f = (struct trapframe *)&place_holder;
	uint callnum, narg;
	struct syscall *s;
	struct thread *t = curthread;
	ulong parms[MAXARGS];

#ifdef KDB
	dbg_trap_frame = f;
#endif
	/*
	 * Sanity check system call number
	 */
	t->t_uregs = f;
	callnum = f->eax & 0x7F;
	if (callnum >= NSYSCALL) {
#ifdef DEBUG
		printf("Bad syscall # %d\n", callnum);
		dbg_enter();
#endif
		f->eax = err(EINVAL);
		goto out;
	}
	s = &syscalls[callnum];

	/*
	 * Default to carry flag clear - no interruption, no error
	 */
	f->eflags &= ~F_CF;

	/*
	 * Call function with needed number of arguments and
	 * appropriate method for extracting them.  If 0..3
	 * are needed, they are passed in EBX..EDX.  Otherwise
	 * they're all on the stack.
	 */
	narg = s->s_narg;
	if (!narg) {
		f->eax = (*(s->s_fun))();
	} else if ((f->eax & 0x80) && (narg < 4)) {
		switch (narg) {
		case 1:
			f->eax = (*(s->s_fun))(f->ebx);
			break;
		case 2:
			f->eax = (*(s->s_fun))(f->ebx, f->ecx);
			break;
		case 3:
			f->eax = (*(s->s_fun))(f->ebx, f->ecx, f->edx);
			break;
		}
	} else {
		if (copyin((void *)(f->esp + sizeof(ulong)),
				parms, narg * sizeof(ulong))) {
			f->eax = err(EFAULT);
			goto out;
		}
		switch (narg) {
		case 1:
			f->eax = (*(s->s_fun))(parms[0]);
			break;
		case 2:
			f->eax = (*(s->s_fun))(parms[0], parms[1]);
			break;
		case 3:
			f->eax = (*(s->s_fun))(parms[0], parms[1], parms[2]);
			break;
		case 4:
			f->eax = (*(s->s_fun))(parms[0],
				parms[1], parms[2], parms[3]);
			break;
		case 5:
			f->eax = (*(s->s_fun))(parms[0],
				parms[1], parms[2], parms[3],
				parms[4]);
			break;
		case 6:
			f->eax = (*(s->s_fun))(parms[0],
				parms[1], parms[2], parms[3],
				parms[4], parms[5]);
			break;
		default:
			ASSERT(0, "syscall: bad s_narg");
			break;
		}
	}

	ASSERT_DEBUG(cpu.pc_locks == 0, "syscall: locks held");

	/*
	 * See if we should get off the CPU
	 */
out:	CHECK_PREEMPT();

	/*
	 * See if we should handle any events
	 */
	if (EVENT(t)) {
		check_events();
	}

	/*
	 * Offer to drop into the debugger
	 */
	PTRACE_PENDING(t->t_proc, PD_ALWAYS, 0);

	/*
	 * Clear uregs
	 */
	t->t_uregs = 0;
}
