/*
 * machproc.c
 *	Machine-dependent parts of process handling
 */
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/vas.h>
#include <sys/boot.h>
#include <sys/param.h>
#include <sys/percpu.h>
#include <mach/vm.h>
#include <mach/tss.h>
#include <mach/gdt.h>
#include <sys/assert.h>
#include "../mach/locore.h"

extern void retuser(), reload_dr(struct dbg_regs *);

/*
 * dup_stack()
 *	Duplicate stack during thread fork
 *
 * "f" is provided to give a starting point for the new thread.
 * Unlike a true fork(), a thread fork gets its own stack within
 * the same virtual address space, and therefore can't run with a
 * copy of the existing stack image.  So we just provide a PC value
 * to start him at, and he runs with a clean stack.
 */
void
dup_stack(struct thread *old, struct thread *new, voidfun f, ulong arg)
{
	ASSERT_DEBUG(old->t_uregs, "dup_stack: no old");

	/*
	 * Calculate location of kernel registers on new stack.
	 * Copy over old to new.
	 */
	new->t_uregs = (struct trapframe *)(
		(new->t_kstack + KSTACK_SIZE) -
		sizeof(struct trapframe));
	bcopy(old->t_uregs, new->t_uregs, sizeof(struct trapframe));

	/*
	 * A thread fork moves to a new, empty stack.  A process
	 * fork has a copy of the stack at the same virtual address,
	 * so the stack location doesn't have to be updated.
	 * Copy the argument in to its place as the argument to
	 * the thread's starting function "call".  This takes advantage
	 * of the fact that thread forks share address space.
	 */
	if (f) {
		ulong sp;

		new->t_uregs->ebp =
		sp = new->t_uregs->esp =
		 (ulong)(new->t_ustack + UMINSTACK) - (2 * sizeof(ulong));
		(void)copyout((uchar *)sp + sizeof(ulong), &arg, sizeof(arg));
		new->t_uregs->eip = (ulong)f;
	}

	/*
	 * New entity returns with 0 value; ESP is one lower so that
	 * the resume() path has a place to write its return address.
	 * This simulates the normal context switch mechanism of
	 * setjmp/longjmp.
	 */
	new->t_kregs->eip = (ulong)retuser;
	new->t_kregs->ebp = (ulong)(new->t_uregs);
	new->t_kregs->esp = (new->t_kregs->ebp) - sizeof(ulong);
	new->t_uregs->eax = 0;

	/*
	 * Now that we're done with setup, flag that he's not in
	 * kernel mode.  New processes vector pretty directly into
	 * user mode.
	 */
	new->t_uregs = 0;
}

/*
 * resume()
 *	Jump via the saved stack frame
 *
 * This works because in VSTa all stacks are at unique virtual
 * addresses.  Interrupts are disabled on entry, and remain so
 * until the longjmp() out to start running in the new context.
 */
void
resume(void)
{
	struct thread *t = curthread;
	struct proc *p = t->t_proc;
	extern struct tss *tss;

#ifdef PROC_DEBUG
	/*
	 * If we've just switched from a debugged process, or we're
	 * switching into a debugged process, fiddle the debug
	 * registers.
	 */
	if ((cpu.pc_flags & CPU_DEBUG) || p->p_dbgr.dr7) {
		/*
		 * dr7 in a non-debugging process will be 0, so this
		 * covers both cases.
		 */
		reload_dr(&p->p_dbgr);
		if (p->p_dbgr.dr7) {
			cpu.pc_flags |= CPU_DEBUG;
		} else {
			cpu.pc_flags &= ~CPU_DEBUG;
		}
	}
#endif

	/*
	 * Make kernel stack come in on our own stack now.  This
	 * isn't used until we switch out to user mode, at which
	 * time our stack will always be empty.
	 * XXX esp is overkill; only esp0 should ever be used.
	 */
	tss->esp0 = tss->esp = (ulong)
		((char *)(t->t_kstack) + KSTACK_SIZE);

	/*
	 * Switch to root page tables for the new process
	 */
	ASSERT_DEBUG(p->p_vas.v_hat.h_cr3 != 0, "resume: cr3 NULL");
	set_cr3(p->p_vas.v_hat.h_cr3);

	/*
	 * Warp out to the context
	 */
	longjmp(t->t_kregs, 1);
}

/*
 * boot_regs()
 *	Set up registers for a boot task
 */
void
boot_regs(struct thread *t, struct boot_task *b)
{
	struct trapframe *u;

	/*
	 * They need to be cleared in t_uregs so it won't look
	 * like a re-entered user mode on our first trap.
	 */
	t->t_uregs = 0;
	u = (struct trapframe *)
		((t->t_kstack + KSTACK_SIZE) - sizeof(struct trapframe));

	/*
	 * Set up user frame to start executing at the boot
	 * task's EIP value.
	 */
	bzero(u, sizeof(struct trapframe));
	u->ecs = GDT_UTEXT|PRIV_USER;
	u->eip = b->b_pc;
	u->esds = ((GDT_UDATA|PRIV_USER) << 16) | GDT_UDATA|PRIV_USER;
	u->ess = GDT_UDATA|PRIV_USER;

	/*
	 * Leave a 0 on the stack to indicate to crt0 that there
	 * are no args.  Leave an extra word so that even once
	 * it has been popped the esp register will always remain
	 * within the stack region.
	 */
	u->ebp =
	u->esp = (USTACKADDR+UMAXSTACK) - 2*sizeof(ulong);
	u->eflags = F_IF;

	/*
	 * Set kernel frame to point to the trapframe from
	 * which we'll return.  A jmp_buf uses one word below
	 * the stack frame we build, where the EIP is placed
	 * for the return.
	 */
	bzero(t->t_kregs, sizeof(t->t_kregs));
	t->t_kregs->eip = (ulong)retuser;
	t->t_kregs->ebp = (ulong)u;
	t->t_kregs->esp = (t->t_kregs->ebp) - sizeof(ulong);
}

/*
 * set_execarg()
 *	Pass an argument back to a newly-exec()'ed process
 *
 * For i386, we push it on the stack.
 */
void
set_execarg(struct thread *t, void *arg)
{
	struct trapframe *u = t->t_uregs;

	ASSERT_DEBUG(u, "set_execarg: no user frame");
	u->esp -= sizeof(void *);
	(void)copyout((void *)u->esp, &arg, sizeof(arg));
}

/*
 * reset_uregs()
 *	Reset the user's registers during an exec()
 */
void
reset_uregs(struct thread *t, ulong entry_addr)
{
	struct trapframe *u = t->t_uregs;

	u->ebp =
	u->esp = (USTACKADDR+UMAXSTACK) - sizeof(ulong);
	u->eflags = F_IF;
	u->eip = entry_addr;
}

#ifdef PROC_DEBUG
/*
 * single_step()
 *	Control state of single-stepping of current process (user mode)
 */
void
single_step(int start)
{
	struct trapframe *u = curthread->t_uregs;

	if (start) {
		u->eflags |= F_TF;
	} else {
		u->eflags &= ~F_TF;
	}
}

/*
 * set_break()
 *	Set/clear a code breakpoint at given address (user mode)
 *
 * Returns 0 for success, 1 for failure
 */
int
set_break(ulong addr, int set)
{
	uint x;
	struct dbg_regs *d = &curthread->t_proc->p_dbgr;

	/*
	 * Sanity
	 */
	if (addr == 0) {
		return(1);
	}

	/*
	 * Convert to user-linear address, get current status
	 */
	addr |= 0x80000000;

	/*
	 * Clear
	 */
	if (!set) {
		/*
		 * Scan for the register which matches the
		 * named linear address
		 */
		for (x = 0; x < 4; ++x) {
			if (d->dr[x] == addr) {
				break;
			}
		}

		/*
		 * If didn't find, error out
		 */
		if (x >= 4) {
			return(1);
		}

		/*
		 * Clear it, and re-load our debug registers
		 */
		d->dr7 &= ~(3 << (x*2));
		d->dr[x] = 0;
		cli();
		reload_dr(d);
		if (d->dr7 == 0) {
			cpu.pc_flags &= ~CPU_DEBUG;
		}
		sti();
		return(0);
	}

	/*
	 * Set--find open slot
	 */
	for (x = 0; x < 4; ++x) {
		if (d->dr[x] == 0) {
			break;
		}
	}

	/*
	 * Bomb if they're all filled up
	 */
	if (x >= 4) {
		return(1);
	}

	/*
	 * Set up, and put into hardware
	 */
	d->dr7 |= (0x2 << (x*2));
	d->dr[x] = addr;
	cli();
	reload_dr(d);
	cpu.pc_flags |= CPU_DEBUG;
	sti();
	return(0);
}

/*
 * getreg()
 *	Get value of register, based on index
 */
long
getreg(long index)
{
	if ((index < 0) || (index >= NREG)) {
		return(-1);
	}
	return(*((long *)(curthread->t_uregs) + index));
}

/*
 * setreg()
 *	Set value of register, based on index
 *
 * Be appropriately paranoid, especially with things like the flags.
 * Return 1 on error, 0 on success.
 */
int
setreg(long index, long value)
{
	struct trapframe *tf;
	ulong *lp;

	if ((index < 0) || (index >= NREG)) {
		return(-1);
	}

	/*
	 * For convenience, point to our register set
	 */
	tf = curthread->t_uregs;
	lp = (ulong *)((long *)tf + index);

	/*
	 * Flags--he can fiddle some, but protect the sensitive ones
	 */
	if (lp == &tf->eflags) {
		static const int priv =
			(F_TF|F_IF|F_IOPL|F_NT|F_RF|F_VM);

		/*
		 * Do not let him touch privved bits in either
		 * direction.  Others he may set at will.
		 */
		tf->eflags = (tf->eflags & priv) | (value & ~priv);
	}

	/*
	 * He can rewrite all the regular registers, but nothing else.
	 */
	if ((lp != &tf->eax) && (lp != &tf->ebx) && (lp != &tf->ecx) &&
		(lp != &tf->edx) && (lp != &tf->edi) && (lp != &tf->esi) &&
		(lp != &tf->esp) && (lp != &tf->ebp)) {
	    return(1);
	}
	*lp = value;
	return(0);
}

#endif /* PROC_DEBUG */
