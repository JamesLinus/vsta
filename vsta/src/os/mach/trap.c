/*
 * trap.c
 *	Trap handling for i386 uP
 */
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/wait.h>
#include <sys/fs.h>
#include <sys/malloc.h>
#include <sys/percpu.h>
#include <mach/trap.h>
#include <mach/gdt.h>
#include <mach/tss.h>
#include <mach/vm.h>
#include <mach/icu.h>
#include <mach/isr.h>
#include <mach/pit.h>
#include <sys/assert.h>
#include <sys/pstat.h>
#include <sys/misc.h>
#include "../mach/locore.h"

extern void selfsig(), check_events();
extern int deliver_isr();

extern char *heap;

struct gate *idt;	/* Our IDT for VSTa */
struct segment *gdt;	/*  ...and GDT */
struct tss *tss;	/*  ...and TSS */

ulong latch_ticks = PIT_LATCH;

#ifdef KDB
/* This can be helpful sometimes while debugging */
struct trapframe *dbg_trap_frame;
ulong dbg_fault_addr;
#endif

/*
 * These wire up our IDT to point to the various handlers we
 * need for i386 traps/exceptions
 */
extern void Tdiv(), Tdebug(), Tnmi(), Tbpt(), Tovfl(), Tbound(),
	Tinstr(), T387(), Tdfault(), Tinvtss(), Tseg(), Tstack(),
	Tgenpro(), Tpgflt(), Tnpx(), Tsyscall(), Tcpsover();
struct trap_tab {
	int t_num;
	voidfun t_vec;
} trap_tab[] = {
	{T_DIV, Tdiv}, {T_DEBUG, Tdebug}, {T_NMI, Tnmi},
	{T_BPT, Tbpt}, {T_OVFL, Tovfl}, {T_BOUND, Tbound},
	{T_INSTR, Tinstr}, {T_387, T387}, {T_DFAULT, Tdfault},
	{T_INVTSS, Tinvtss}, {T_SEG, Tseg}, {T_STACK, Tstack},
	{T_GENPRO, Tgenpro}, {T_PGFLT, Tpgflt}, {T_NPX, Tnpx},
	{T_SYSCALL, Tsyscall}, {T_CPSOVER, Tcpsover},
	{0, 0}
};

/*
 * eoi()
 *	Clear an interrupt and allow new ones to arrive
 */
inline static void
eoi(void)
{
	outportb(ICU0, EOI_FLAG);
	outportb(ICU1, EOI_FLAG);
}

/*
 * nudge()
 *	Tell CPU to preempt
 *
 * Of course, pretty simple on uP.  Just set flag; it will be seen on
 * way back from kernel mode.
 */
void
nudge(struct percpu *c)
{
	do_preempt = 1;
}

/*
 * page_fault()
 *	Handle page faults
 *
 * This is the machine-dependent code which calls the portable vas_fault()
 * once it has figured out the faulting address and such.
 */
void
page_fault(ulong place_holder)
{
	struct trapframe *f = (struct trapframe *)&place_holder;
	ulong l;
	struct vas *vas;
	int user_mode = USERMODE(f);
	struct thread *t = curthread;

#ifdef KDB
	dbg_trap_frame = f;
#endif

	/*
	 * Get fault address, then let interrupts back in.  This
	 * minimizes latency on kernel preemption, while still keeping
	 * a preempting task from hosing our CR2 value.
	 */
	l = get_cr2();
	sti();

	/*
	 * User mode page fault
	 */
	if (user_mode) {
		ASSERT_DEBUG(t, "page_fault: user !curthread");
		ASSERT_DEBUG(t->t_uregs == 0,
			"page_fault: nested user");
		t->t_uregs = f;
	}

	/*
	 * Drop the high bit because the
	 * user's 0 maps to our 0x80000000, but our vas is set
	 * up in terms of his virtual addresses.
	 */
	if (l < 0x80000000) {
		ASSERT(user_mode, "trap: kernel fault");

		/*
		 * Naughty, trying to touch the kernel
		 */
		selfsig(EFAULT);
		goto out;
	}
	l &= ~0x80000000;

#ifdef DEBUG
	/*
	 * If we have the kernel debugger available this can
	 * tell us the address that caused the last page fault
	 */
	dbg_fault_addr = l;
#endif

	/*
	 * Let the portable code try to resolve it
	 */
	vas = &t->t_proc->p_vas;
	if (vas_fault(vas, (void *)l, f->errcode & EC_WRITE)) {
		if (t->t_probe) {
			ASSERT(!user_mode, "page_fault: probe from user");
			f->eip = (ulong)(t->t_probe);
		} else {
			/*
			 * Shoot him
			 */
			ASSERT(user_mode,
				"page_fault: kernel ref to user vaddr");
			selfsig(EFAULT);
		}
	}

out:
	ASSERT_DEBUG(cpu.pc_locks == 0, "trap: locks held");

	/*
	 * See if we should get off the CPU
	 */
	CHECK_PREEMPT();

	/*
	 * See if we should handle any events
	 */
	if (EVENT(t)) {
		check_events();
	}

	/*
	 * Clear uregs if nesting back to user
	 */
	if (user_mode) {
		PTRACE_PENDING(t->t_proc, PD_ALWAYS, 0);
		t->t_uregs = 0;
	}
}

/*
 * trap()
 *	Central handling for traps
 */
void
trap(ulong place_holder)
{
	struct trapframe *f = (struct trapframe *)&place_holder;
	int user_mode;

#ifdef KDB
	dbg_trap_frame = f;
#endif

	/*
	 * If this is first entry (from user mode), mark the place
	 * on the stack.  XXX but it's invariant, is this a waste
	 * of time?
	 */
	user_mode = USERMODE(f);
	if (user_mode) {
		ASSERT_DEBUG(curthread, "trap: user !curthread");
		ASSERT_DEBUG(curthread->t_uregs == 0, "trap: nested user");
		curthread->t_uregs = f;

		/*
		 * Handle user mode traps
		 */
		switch (f->traptype) {
		case T_PGFLT:
			ASSERT(0, "trap: page fault handler elsewhere");

		case T_387:
			if (cpu.pc_flags & CPU_FP) {
				ASSERT((curthread->t_flags & T_FPU) == 0,
					"trap: T_387 but 387 enabled");
				if (curthread->t_fpu == 0) {
					curthread->t_fpu =
						MALLOC(sizeof(struct fpu),
						       MT_FPU);
					fpu_enable((struct fpu *)0);
				} else {
					fpu_enable(curthread->t_fpu);
				}
				curthread->t_flags |= T_FPU;
				break;
			}

			/* VVV Otherwise, fall into VVV */

		case T_NPX:
			if (curthread->t_flags & T_FPU) {
				fpu_maskexcep();
			}

			/* VVV continue falling... VVV */

		case T_DIV:
		case T_OVFL:
		case T_BOUND:
			selfsig(EMATH);
			break;

		case T_DEBUG:
		case T_BPT:
#ifdef PROC_DEBUG
			f->eflags |= F_RF;	/* i386 doesn't set it */
			ASSERT_DEBUG(curthread, "trap: user debug !curthread");
			PTRACE_PENDING(curthread->t_proc, PD_BPOINT, 0);
			break;
#endif
		case T_INSTR:
			selfsig(EILL);
			break;

		case T_DFAULT:
		case T_INVTSS:
		case T_SEG:
		case T_STACK:
		case T_GENPRO:
		case T_CPSOVER:
			selfsig(EFAULT);
			break;

		default:
			printf("trap frame at 0x%x\n", f);
			ASSERT(0, "trap: bad user type");
			return;
		}
	} else {
		/*
		 * Handle kernel mode traps
		 */
		switch (f->traptype) {
		case T_PGFLT:
			ASSERT(0, "trap: page fault handler elsewhere");

		case T_DIV:
			ASSERT(0, "trap: kernel divide error");

#ifdef DEBUG
		case T_DEBUG:
		case T_BPT:
			printf("trap: kernel debug\n");
			dbg_enter();
			break;
#endif

		case T_387:
		case T_NPX:
			ASSERT(0, "trap: FP used in kernel");

		/*
		 * We need these explicitly listed as a hint to the
		 * compiler otherwise things don't optimise too well
		 */
		case T_DFAULT:
		case T_INVTSS:
		case T_SEG:
		case T_STACK:
		case T_GENPRO:
		case T_OVFL:
		case T_BOUND:
		case T_CPSOVER:
		default:
			printf("trap frame at 0x%x\n", f);
			ASSERT(0, "trap: bad kernel type");
			return;
		}
	}

	ASSERT_DEBUG(cpu.pc_locks == 0, "trap: locks held");

	/*
	 * See if we should get off the CPU
	 */
	CHECK_PREEMPT();

	/*
	 * See if we should handle any events
	 */
	if (EVENT(curthread)) {
		check_events();
	}

	/*
	 * Clear uregs if nesting back to user
	 */
	if (user_mode) {
		PTRACE_PENDING(curthread->t_proc, PD_ALWAYS, 0);
		curthread->t_uregs = 0;
	}
}

/*
 * init_icu()
 *	Initial the 8259 interrupt controllers
 */
static void
init_icu(void)
{
	/* initialize 8259's */
	outportb(ICU0, 0x11);		/* Reset */
	outportb(ICU0+1, CPUIDT);	/* Vectors served */
	outportb(ICU0+1, 1 << 2);	/* Chain to ICU1 */
	outportb(ICU0+1, 1);		/* 8086 mode */
	outportb(ICU0+1, 0xff);		/* No interrupts for now */
	outportb(ICU0, 2);		/* ISR mode */

	outportb(ICU1, 0x11);		/* Reset */
	outportb(ICU1+1, CPUIDT+8);	/* Vectors served */
	outportb(ICU1+1, 2);		/* We are slave */
	outportb(ICU1+1, 1);		/* 8086 mode */
	outportb(ICU1+1, 0xff);		/* No interrupts for now */
	outportb(ICU1, 2);		/* ISR mode */
}

/*
 * init_pit()
 *	Set the main interval timer to tick at HZ times per second
 */
static void
init_pit(void)
{
	/*
	 * initialise 8254 (or 8253) channel 0.  We set the timer to
	 * generate an interrupt every time we have done enough ticks.
	 * The output format is command, LSByte, MSByte.  Note that the
	 * lowest the value of HZ can be is 19 - otherwise the
	 * calculations get screwed up,
	 */
	outportb(PIT_CTRL, CMD_SQR_WAVE);
	outportb(PIT_CH0, PIT_LATCH & 0x00ff);
	outportb(PIT_CH0, (PIT_LATCH & 0xff00) >> 8);
}

/*
 * setup_gdt()
 *	Switch from boot GDT to our hand-crafted one
 */
static void
setup_gdt(void)
{
	struct tss *t;
	struct segment *g, *s;
	struct linmem l;
	extern pte_t *cr3;

	/*
	 * Allocate our 32-bit task and our GDT.  We will always
	 * run within the same task, just switching CR3's around.  But
	 * we need it because it tabulates stack pointers and such.
	 */
	tss = t = (struct tss *)heap;
	heap += sizeof(struct tss);
	gdt = g = (struct segment *)heap;
	heap += NGDT*sizeof(struct segment);

	/*
	 * Create 32-bit TSS
	 */
	bzero(t, sizeof(struct tss));
	t->cr3 = (ulong)cr3;
	t->eip = (ulong)panic;
	t->cs = GDT_KTEXT;
	t->ss0 = t->ds = t->es = t->ss = GDT_KDATA;
	t->esp = t->esp0 = (ulong)id_stack;

	/*
	 * Null entry--actually, zero whole thing to be safe
	 */
	bzero(g, NGDT*sizeof(struct segment));

	/*
	 * Kernel data--all 32 bits allowed, read-write
	 */
	s = &g[GDTIDX(GDT_KDATA)];
	s->seg_limit0 = 0xFFFF;
	s->seg_base0 = 0;
	s->seg_base1 = 0;
	s->seg_base2 = 0;
	s->seg_type = T_MEMRW;
	s->seg_dpl = PRIV_KERN;
	s->seg_p = 1;
	s->seg_limit1 = 0xF;
	s->seg_32 = 1;
	s->seg_gran = 1;

	/*
	 * Kernel text--low 2 gig, execute and read
	 */
	s = &g[GDTIDX(GDT_KTEXT)];
	*s = g[GDTIDX(GDT_KDATA)];
	s->seg_type = T_MEMXR;
	s->seg_limit1 = 0x7;

	/*
	 * 32-bit boot TSS descriptor
	 */
	s = &g[GDTIDX(GDT_BOOT32)];
	s->seg_limit0 = sizeof(struct tss)-1;
	s->seg_base0 = (ulong)t & 0xFFFF;
	s->seg_base1 = ((ulong)t >> 16) & 0xFF;
	s->seg_type = T_TSS;
	s->seg_dpl = PRIV_KERN;
	s->seg_p = 1;
	s->seg_limit1 = 0;
	s->seg_32 = 0;
	s->seg_gran = 0;
	s->seg_base2 = ((ulong)t >> 24) & 0xFF;

	/*
	 * 32-bit user data.  User addresses are offset 2 GB.
	 */
	s = &g[GDTIDX(GDT_UDATA)];
	s->seg_limit0 = 0xFFFF;
	s->seg_base0 = 0;
	s->seg_base1 = 0;
	s->seg_base2 = 0x80;
	s->seg_type = T_MEMRW;
	s->seg_dpl = PRIV_USER;
	s->seg_p = 1;
	s->seg_limit1 = 0x7;
	s->seg_32 = 1;
	s->seg_gran = 1;

	/*
	 * 32-bit user text
	 */
	s = &g[GDTIDX(GDT_UTEXT)];
	*s = g[GDTIDX(GDT_UDATA)];
	s->seg_type = T_MEMXR;

	/*
	 * Set GDT to our new structure
	 */
	l.l_len = (sizeof(struct segment) * NGDT)-1;
	l.l_addr = (ulong)gdt;
	lgdt(&l.l_len);

	/*
	 * Now that we have a GDT slot for our TSS, we can
	 * load the task register.
	 */
	ltr(GDT_BOOT32);
}

/*
 * set_idt()
 *	Set up an IDT slot
 */
static void
set_idt(struct gate *i, voidfun f, int typ)
{
	i->g_off0 = (ulong)f & 0xFFFF;
	i->g_sel = GDT_KTEXT;
	i->g_stkwds = 0;
	i->g_type = typ;
	i->g_dpl = PRIV_KERN;
	i->g_p = 1;
	i->g_off1 = ((ulong)f >> 16) & 0xFFFF;
}

/*
 * init_trap()
 *	Create an IDT
 */
void
init_trap(void)
{
	int x, intrlen;
	struct trap_tab *t;
	struct linmem l;
	char *p;
	extern void stray_intr(), stray_ign();
	extern void xint32(), xint33();

	/*
	 * Set up GDT first
	 */
	setup_gdt();

	/*
	 * Set ICUs to known state
	 */
	init_icu();

	/*
	 * Set the interval timer to the correct tick speed
	 */
	init_pit();

	/*
	 * Carve out an IDT table for all possible 256 sources
	 */
	idt = (struct gate *)heap;
	heap += (sizeof(struct gate) * NIDT);

	/*
	 * Set all trap entries to initially log stray events
	 */
	bzero(idt, sizeof(struct gate) * NIDT);
	for (x = 0; x < CPUIDT; ++x) {
		set_idt(&idt[x], stray_intr, T_TRAP);
	}

	/*
	 * Wire all hard interrupts to a vector which will push their
	 * interrupt number and call our common C code.
	 */
	p = (char *)xint32;
	intrlen = (char *)xint33 - (char *)xint32;
	for (x = CPUIDT; x < CPUIDT + IDTISA; ++x) {
		set_idt(&idt[x],
			(voidfun)(p + (x - CPUIDT) * intrlen),
			T_INTR);
	}

	/*
	 * Any other interrupts are stray so handle them appropriately
	 */
	for (x = CPUIDT + IDTISA; x < NIDT; ++x) {
		set_idt(&idt[x], (voidfun)stray_intr, T_INTR);
	}

	/*
	 * Map interrupt 7 to a fast ingore.  I get bursts of these
	 * even when IRQ 7 is masked from the PIC.  It only happens
	 * when I enable the slave PIC, so it smells like hardware
	 * weirdness.
	 */
	set_idt(&idt[CPUIDT+7], stray_ign, T_INTR);

	/*
	 * Hook up the traps we understand
	 */
	for (t = trap_tab; t->t_vec; ++t) {
		set_idt(&idt[t->t_num], t->t_vec, T_TRAP);
	}

	/*
	 * Users can make system calls with "int $T_SYSCALL"
	 */
	idt[T_SYSCALL].g_dpl = PRIV_USER;

	/*
	 * Hold off interrupts on a page fault until we can grab
	 * CR2's value (thanks, Intel, next time put it on the stack
	 * along with the rest of the trap context, eh?)
	 */
	idt[T_PGFLT].g_type = T_INTR;

	/*
	 * Load the IDT into hardware
	 */
	l.l_len = (sizeof(struct gate) * NIDT)-1;
	l.l_addr = (ulong)idt;
	lidt(&l.l_len);

	/*
	 * Flush our segment registers to point at current GDT
	 * format.
	 */
	refresh_segregs();
}

/*
 * sendev()
 *	Do machinery of event delivery
 *
 * "thread" is passed, but must be the current process.  I think this
 * is a little more efficient than using curthread.  I'm also thinking
 * that it might be worth it to make this routine work across threads.
 * Maybe.
 */
void
sendev(struct thread *t, char *ev)
{
	struct evframe e;
	struct trapframe *f;
	struct proc *p;
	extern int do_exit();

	ASSERT(t->t_uregs, "sendev: no user frame");
	f = t->t_uregs;

	/*
	 * If no handler or KILL, process dies
	 */
	p = t->t_proc;
	if (!p->p_handler || !strcmp(ev, EKILL)) {
		strcpy(p->p_event, ev);
		do_exit(_W_EV);
	}

	/*
	 * Build event frame
	 */
	e.ev_previp = f->eip;
	strcpy(e.ev_event, ev);

	/*
	 * Try and place it on the stack
	 */
	if (copyout((void *)(f->esp - sizeof(e)), &e, sizeof(e))) {
#ifdef DEBUG
		printf("Stack overflow pid %d/%d sp 0x%x\n",
			p->p_pid, t->t_pid, f->esp);
		dbg_enter();
#endif
		do_exit(1);
	}

	/*
	 * Update user's registers to reflect this nesting
	 */
	f->esp -= sizeof(e);
	f->eip = (ulong)(p->p_handler);
}

/*
 * interrupt()
 *	Hardware interrupt delivery code
 */
void
interrupt(ulong place_holder)
{
	struct trapframe *f = (struct trapframe *)&place_holder;
	int isr = f->traptype;

	/*
	 * Enable further interrupts from the ICU - interrupts are
	 * still masked on-chip.
	 */
	eoi();

	/*
	 * Our only hard-wired interrupt handler; the clock
	 */
	if (isr == 0) {
		extern void hardclock();

		hardclock(f);
		goto out;
	}

#ifdef KDB
	dbg_trap_frame = f;
#endif

	/*
	 * Let processes registered for an IRQ get them
	 */
	if (!deliver_isr(isr)) {
		/*
		 * Otherwise bomb on stray interrupt
		 */
		ASSERT(0, "handle_isr: stray");
	}

	/*
	 * Check for preemption
	 * Check for events if we pushed in from user mode
	 */
out:
	if (USERMODE(f)) {
		struct thread *t = curthread;

		sti();
		if (EVENT(t)) {
			t->t_uregs = f;
			check_events();
			t->t_uregs = 0;
		}
	}
	CHECK_PREEMPT();
}

/*
 * stray_interrupt()
 *	Code for all miscellaneous CPU interrupts that don't have
 *	aynwhere better to be processed
 */
void
stray_interrupt(ulong place_holder)
{
	struct trapframe *f = (struct trapframe *)&place_holder;
	int isr = f->traptype;

#ifdef KDB
	dbg_trap_frame = f;
#endif

	/*
	 * The kernel certainly shouldn't lead us here!
	 */
	ASSERT(USERMODE(f), "interrupt: stray kernel interrupt");

	/*
	 * OK so the user's been messing us about - we'll show him!
	 */
	selfsig(EILL);
}
