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

static int do_dbg_enter();

/* #define SYSCALLTRACE /* */

extern int msg_port(), msg_connect(), msg_accept(), msg_send(),
	msg_receive(), msg_reply(), msg_disconnect(), msg_err();
extern int do_exit(), fork(), fork_thread(), enable_io(), enable_isr(),
	mmap(), munmap(), strerror(), notify(), clone();
extern int page_wire(), page_release(), enable_dma(), time_get(),
	time_sleep(), exec(), waits(), perm_ctl(), set_swapdev(),
	run_qio(), set_cmd(), pageout(), getid(), unhash(),
	time_set(), ptrace(), nop(), msg_portname();

struct syscall {
	intfun s_fun;
	int s_narg;
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
	{fork_thread, 1},			/* 10 */
	{enable_io, 2},				/* 11 */
	{enable_isr, 2},			/* 12 */
	{mmap, 6},				/* 13 */
	{munmap, 2},				/* 14 */
	{strerror, 1},				/* 15 */
	{notify, 4},				/* 16 */
	{clone, 1},				/* 17 */
	{page_wire, 2},				/* 18 */
	{page_release, 1},			/* 19 */
	{enable_dma, 0},			/* 20 */
	{time_get, 1},				/* 21 */
	{time_sleep, 1},			/* 22 */
	{do_dbg_enter, 0},			/* 23 */
	{exec, 3},				/* 24 */
	{waits, 2},				/* 25 */
	{perm_ctl, 3},				/* 26 */
	{set_swapdev, 1},			/* 27 */
	{run_qio, 0},				/* 28 */
	{set_cmd, 1},				/* 29 */
	{pageout, 0},				/* 30 */
	{getid, 1},				/* 31 */
	{unhash, 2},				/* 32 */
	{time_set, 1},				/* 33 */
#ifdef PROC_DEBUG
	{ptrace, 2},				/* 34 */
#else
	{nop, 1},
#endif
	{msg_portname, 1},			/* 35 */
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
#if defined(DEBUG) && defined(KDB)
	dbg_enter();
	return(0);
#else
	return(err(EINVAL));
#endif
}

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
syscall(struct trapframe *f)
{
	int callnum, args[MAXARGS];
	struct syscall *s;

	/*
	 * Sanity check system call number
	 */
	callnum = f->eax;
	if ((callnum < 0) || (callnum >= NSYSCALL)) {
#ifdef DEBUG
		printf("Bad syscall # %d\n", callnum);
		dbg_enter();
#endif
		f->eax = err(EINVAL);
		return;
	}
	s = &syscalls[callnum];
	ASSERT_DEBUG(s->s_narg <= MAXARGS, "syscall: too many args");

	/*
	 * See if can get needed number of arguments
	 */
	if (copyin(f->esp + sizeof(ulong), args,
			s->s_narg * sizeof(int))) {
#ifdef DEBUG
		printf("Short syscall args\n");
		dbg_enter();
#endif
		f->eax = err(EFAULT);
		return;
	}

	/*
	 * Default to carry flag clear--no error
	 */
	f->eflags &= ~F_CF;

#ifdef SYSCALLTRACE
	{ int x;
	  printf("%d: syscall %d (", curthread->t_pid, callnum);
	  for (x = 0; x < s->s_narg; ++x) {
	    printf(" 0x%x", args[x]);
	  }
	  printf(" )\n");
	}
#endif

	/*
	 * Interrupted system calls vector here
	 */
	if (setjmp(curthread->t_qsav)) {
		err(EINTR);
		return;
	}

	/*
	 * Call function with needed number of arguments
	 */
	switch (s->s_narg) {
	case 0:
		f->eax = (*(s->s_fun))();
		break;
	case 1:
		f->eax = (*(s->s_fun))(args[0]);
		break;
	case 2:
		f->eax = (*(s->s_fun))(args[0], args[1]);
		break;
	case 3:
		f->eax = (*(s->s_fun))(args[0], args[1], args[2]);
		break;
	case 4:
		f->eax = (*(s->s_fun))(args[0], args[1], args[2],
			args[3]);
		break;
	case 5:
		f->eax = (*(s->s_fun))(args[0], args[1], args[2],
			args[3], args[4]);
		break;
	case 6:
		f->eax = (*(s->s_fun))(args[0], args[1], args[2],
			args[3], args[4], args[5]);
		break;
	default:
		ASSERT(0, "syscall: bad s_narg");
	}
#ifdef SYSCALLTRACE
	printf("%d:  --call %d returns 0x%x",
		curthread->t_pid, callnum, f->eax);
	printf(" / %s\n", (curthread->t_err[0]) ?
		curthread->t_err : "<none>");
	/* dbg_enter(); /* */
#endif
}
