#ifdef KDB
/*
 * trace.c
 *	Routines to do stack backtraces
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <mach/trap.h>
#include <mach/machreg.h>
#include <mach/setjmp.h>
#include "../mach/locore.h"

extern char _etext[];
extern jmp_buf dbg_errjmp;
extern struct proc *allprocs;

/*
 * Shape of stack after procedure entry
 */
struct stkframe {
	int s_ebp;
	int s_eip;
	int s_args[9];
};

/*
 * dbgtfind()
 *	Given tid, hunt down struct thread
 */
static struct thread *
dbgtfind(pid_t tid)
{
	struct proc *p, *pstart;

	p = pstart = allprocs;
	do {
		struct thread *t;

		for (t = p->p_threads; t; t = t->t_next) {
			if (t->t_pid == tid) {
				return(t);
			}
		}
		p = p->p_allnext;
	} while (p != pstart);
	return(0);
}

/*
 * trace()
 *	Given thread ID, provide stack backtrace
 *
 * We use the fact that all stacks have their own address; we can
 * thus directly address each stack frame.  For user mode, prefix
 * thread ID with 'u'; we handle V->P and interrogate the physical
 * version.
 */
void
trace(char *args)
{
	ulong ebp, eip;
	char buf[16];

	/*
	 * If no args, trace this stack
	 */
	if (!args || !args[0]) {
		ebp = (ulong)(&args - 2);
		eip = (ulong)trace;
	} else {
		pid_t tid;
		struct thread *t;

		tid = atoi(args);
		t = dbgtfind(tid);
		if (t == 0) {
			printf("No such thread: %d\n", tid);
			return;
		}
		ebp = t->t_kregs[0].ebp;
		eip = t->t_kregs[0].eip;
	}

 	/*
 	 * Loop, reading pairs of frame pointers and return addresses
 	 */
#define INSTACK(v) (((char *)v >= _etext) && ((ulong)v < 0x40000000))
	while (INSTACK(ebp)) {
		int narg, x;
		char *p, *loc;
		struct stkframe *s;
 		extern char *symloc(), *strchr();

 		/*
 		 * Read next stack frame, output called procedure name
 		 */
		s = (void *)ebp;
		loc = symloc(eip);
		if (p = strchr(loc, '+')) {
			*p = '\0';
		}
 		printf("%s(", loc);

 		/*
 		 * Calculate number of arguments, default to 4.  We
 		 * figure it out by looking at the stack cleanup at
 		 * the return address.  If GNU C has optimized this
 		 * out (for instance, bunched several cleanups together),
 		 * we're out of luck.
 		 */
		if ((s->s_eip < NBPG) || (s->s_eip > (ulong)_etext)) {
			x = 0;
		} else {
			x = *(ulong *)s->s_eip;
		}
 		if ((x & 0xFF) == 0x59) {
 			narg = 1;
		} else if ((x & 0xFFFF) == 0xC483) {
			narg = ((x >> 18) & 0xF);
		} else {
			narg = 4;
		}

 		/*
 		 * Print arguments
 		 */
 		for (x = 0; x < narg; ++x) {
 			if (x > 0) {
 				printf(", ");
			}
 			printf("0x%x", s->s_args[x]);
		}

		/*
		 * Print where called from.  We just assume that there's
		 * a 5-byte long call.  Wrong for function pointers.
		 */
		printf(") called from %s\n", symloc(s->s_eip-5));
 		ebp = s->s_ebp;
 		eip = s->s_eip;
 	}
}

/*
 * strcat()
 *	Concatenate a string
 */
static void
strcat(char *dest, char *src)
{
	char *p;

	for (p = dest; *p; ++p)
		;
	while (*p++ = *src++)
		;
}

/*
 * tname()
 *	Convert trap number into string
 */
static char *
tname(uint t)
{
	static char buf[32];

	switch (t) {
	case T_DIV: strcpy(buf, "DIV"); break;
	case T_DEBUG: strcpy(buf, "DEBUG"); break;
	case T_NMI: strcpy(buf, "NMI"); break;
	case T_BPT: strcpy(buf, "BPT"); break;
	case T_OVFL: strcpy(buf, "OVFL"); break;
	case T_BOUND: strcpy(buf, "BOUND"); break;
	case T_INSTR: strcpy(buf, "INSTR"); break;
	case T_387: strcpy(buf, "387"); break;
	case T_DFAULT: strcpy(buf, "DFAULT"); break;
	case T_INVTSS: strcpy(buf, "INVTSS"); break;
	case T_SEG: strcpy(buf, "SEG"); break;
	case T_STACK: strcpy(buf, "STACK"); break;
	case T_GENPRO: strcpy(buf, "GENPRO"); break;
	case T_PGFLT: strcpy(buf, "PGFLT"); break;
	case T_NPX: strcpy(buf, "NPX"); break;
	case T_CPSOVER: strcpy(buf, "CPSOVER"); break;
	default:
		sprintf(buf, "%d", t);
		break;
	}
	return(buf);
}

/*
 * trapframe()
 *	Print a trap frame
 *
 * Works with either the given address, or defaults to dbg_trap_frame,
 * the latest trap frame on the stack.
 */
void
trapframe(char *p)
{
	struct trapframe *f;
	extern struct trapframe *dbg_trap_frame;

	/*
	 * Get address or default
	 */
	if (!p || !p[0]) {
		if (!dbg_trap_frame) {
			printf("No default trap frame\n");
			longjmp(dbg_errjmp, 1);
		}
		f = dbg_trap_frame;
	} else {
		f = (struct trapframe *)get_num(p);
	}

	/*
	 * Print it
	 */
	printf("Trap type %s err 0x%x eip 0x%x:0x%x\n",
		tname(f->traptype),
		f->errcode, f->ecs, f->eip);
	printf("eax 0x%x ebx 0x%x ecx 0x%x edx 0x%x esi 0x%x edi 0x%x\n",
		f->eax, f->ebx, f->ecx, f->edx, f->esi, f->edi);
	printf("esp 0x%x:0x%x ebp 0x%x eflags 0x%x\n",
		f->ess, f->esp, f->ebp, f->eflags);
}

/*
 * reboot()
 *	Cause an i386 machine reset
 */
void
reboot(void)
{
	for (;;) {
		set_cr3(0);
		bzero(0, NBPG);
	}
}
#endif /* KDB */
