/*
 * proc.c
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

extern void retuser();

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
dup_stack(struct thread *old, struct thread *new, voidfun f)
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
	 */
	if (f) {
		new->t_uregs->ebp =
		new->t_uregs->esp =
		 (ulong)(new->t_ustack + UMINSTACK) - sizeof(long);
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
 * addresses.
 */
void
resume(void)
{
	extern struct tss *tss;

	/*
	 * Make kernel stack come in on our own stack now.  This
	 * isn't used until we switch out to user mode, at which
	 * time our stack will always be empty.
	 * XXX esp is overkill; only esp0 should ever be used.
	 */
	tss->esp0 = tss->esp = (ulong)
		((char *)(curthread->t_kstack) + KSTACK_SIZE);

	/*
	 * Switch to root page tables for the new process
	 */
	set_cr3(curthread->t_proc->p_vas->v_hat.h_cr3);

	/*
	 * Warp out to the context
	 */
	longjmp(curthread->t_kregs, 1);
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
	 * esp must lie within data segment, so point at last
	 * word at top of stack.
	 */
	u->ebp =
	u->esp = (USTACKADDR+UMINSTACK) - sizeof(ulong);
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
