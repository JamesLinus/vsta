/*
 * trap.c
 *	Trap handling for i386 uP
 */
#include <sys/proc.h>
#include <sys/percpu.h>
#include <sys/thread.h>
#include <sys/wait.h>
#include <sys/fs.h>
#include <mach/trap.h>
#include <mach/gdt.h>
#include <mach/tss.h>
#include <mach/vm.h>
#include <mach/icu.h>
#include <mach/isr.h>
#include <mach/pit.h>
#include <sys/assert.h>

extern void selfsig(), check_events(), syscall();
extern int deliver_isr();

extern char *heap;

struct gate *idt;	/* Our IDT for VSTa */
struct segment *gdt;	/*  ...and GDT */
struct tss *tss;	/*  ...and TSS */

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
	Tgenpro(), Tpgflt(), Tnpx(), Tsyscall();
struct trap_tab {
	int t_num;
	voidfun t_vec;
} trap_tab[] = {
	{T_DIV, Tdiv}, {T_DEBUG, Tdebug}, {T_NMI, Tnmi},
	{T_BPT, Tbpt}, {T_OVFL, Tovfl}, {T_BOUND, Tbound},
	{T_INSTR, Tinstr}, {T_387, T387}, {T_DFAULT, Tdfault},
	{T_INVTSS, Tinvtss}, {T_SEG, Tseg}, {T_STACK, Tstack},
	{T_GENPRO, Tgenpro}, {T_PGFLT, Tpgflt}, {T_NPX, Tnpx},
	{T_SYSCALL, Tsyscall},
	{0, 0}
};

/*
 * check_preempt()
 *	If appropriate, preempt current thread
 *
 * This routine will swtch() itself out as needed; just calling
 * it does the job.
 */
void
check_preempt(void)
{
	extern void timeslice();

	/*
	 * If no preemption needed, holding locks, or not running
	 * with a process, don't preempt.
	 */
	if (!do_preempt || !curthread || (cpu.pc_locks > 0)
			|| (cpu.pc_nopreempt > 0)) {
		return;
	}

	/*
	 * Use timeslice() to switch us off
	 */
	timeslice();
}

/*
 * nudge()
 *	Tell CPU to preempt
 *
 * Of course, pretty simple on uP.  Just set flag; it will be seen on
 * way back from kernel mode.
 */
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
static void
page_fault(struct trapframe *f)
{
	ulong l;
	struct vas *vas;
	extern ulong get_cr2();

	ASSERT(curthread, "page_fault: no proc");

	/*
	 * Get fault address.  Drop the high bit because the
	 * user's 0 maps to our 0x80000000, but our vas is set
	 * up in terms of his virtual addresses.
	 */
	l = get_cr2();
	if (l < 0x80000000) {
		ASSERT(f->ecs & 0x3, "trap: kernel fault");

		/*
		 * Naughty, trying to touch the kernel
		 */
		selfsig(EFAULT);
		return;
	}
	l &= ~0x80000000;
#ifdef DEBUG
	dbg_fault_addr = l;
#endif

	/*
	 * Let the portable code try to resolve it
	 */
	vas = curthread->t_proc->p_vas;
	if (vas_fault(vas, l, f->errcode & EC_WRITE)) {
		if (curthread->t_probe) {
#ifdef DEBUG
			printf("cpfail\n"); dbg_enter();
#endif
			ASSERT((f->ecs & 3) == PRIV_KERN,
				"page_fault: probe from user");
			f->eip = (ulong)(curthread->t_probe);
		} else {
			/*
			 * Stack growth.  We try to grow it if it's
			 * a "reasonable" depth below current stack.
			 */
			if ((l < USTACKADDR) &&
					(l > (USTACKADDR-UMINSTACK))) {
				if (alloc_zfod_vaddr(vas, btop(UMAXSTACK),
						USTACKADDR-UMAXSTACK)) {
					return;
				}
			}

			/*
			 * Shoot him
			 */
			selfsig(EFAULT);
		}
	}
}

/*
 * trap()
 *	Central handling for traps
 */
trap(ulong place_holder)
{
	struct trapframe *f = (struct trapframe *)&place_holder;
	int kern_mode;

#ifdef KDB
	dbg_trap_frame = f;
#endif
	/*
	 * If this is first entry (from user mode), mark the place
	 * on the stack.  XXX but it's invariant, is this a waste
	 * of time?
	 */
	kern_mode = ((f->ecs & 0x3) == 0);
	if (!kern_mode) {
		ASSERT_DEBUG(curthread, "trap: user !curthread");
		ASSERT_DEBUG(curthread->t_uregs == 0, "trap: nested user");
		curthread->t_uregs = f;
	} else {
		/*
		 * Make trap type distinct for kernel
		 */
		f->traptype |= T_KERNEL;
	}

	/*
	 * Pick action based on trap
	 */
	switch (f->traptype) {
	case T_PGFLT|T_KERNEL:
	case T_PGFLT:
		page_fault(f);
		break;

	case T_DIV|T_KERNEL:
		ASSERT(0, "trap: kernel divide error");

	case T_DIV:
	case T_OVFL:
	case T_BOUND:
	case T_387:
	case T_NPX:
		selfsig(EMATH);
		break;

#ifdef DEBUG
	case T_DEBUG|T_KERNEL:
	case T_BPT|T_KERNEL:
		printf("Kernel debug trap\n");
		dbg_enter();
		break;
#endif
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

	case T_387|T_KERNEL:
	case T_NPX|T_KERNEL:
		ASSERT(0, "387 used in kernel");

	/* case T_DFAULT|T_KERNEL: XXX stack red zones? */

	case T_DFAULT:
	case T_INVTSS:
	case T_SEG:
	case T_STACK:
	case T_GENPRO:
		selfsig(EFAULT);
		break;

	case T_SYSCALL:
		syscall(f);
		break;

	default:
		printf("Trap frame at 0x%x\n", f);
		ASSERT(0, "trap: bad type");
	}
	ASSERT_DEBUG(cpu.pc_locks == 0, "trap: locks held");

	/*
	 * See if we should handle any events
	 */
	check_events();

	/*
	 * See if we should get off the CPU
	 */
	check_preempt();

	/*
	 * Clear uregs if nesting back to user
	 */
	if (!kern_mode) {
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
	outportb(PIT_CH0, (PIT_TICK / HZ) & 0x00ff);
	outportb(PIT_CH0, ((PIT_TICK / HZ) & 0xff00) >> 8);
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
	struct gate *i;
	struct linmem l;
	extern pte_t *cr3;
	extern void xsyscall(), panic();
	extern char id_stack[];

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
	extern void stray_intr(), stray_ign(), xint32(), xint33();

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
	 * Wire all interrupts to a vector which will push their
	 * interrupt number and call our common C code.
	 */
	p = (char *)xint32;
	intrlen = (char *)xint33 - (char *)xint32;
	for (x = CPUIDT; x < NIDT; ++x) {
		set_idt(&idt[x],
			(voidfun)(p + (x - CPUIDT)*intrlen),
			T_INTR);
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
	 * Load the IDT into hardware
	 */
	l.l_len = (sizeof(struct gate) * NIDT)-1;
	l.l_addr = (ulong)idt;
	lidt(&l.l_len);
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
	e.ev_prevsp = f->esp;
	e.ev_previp = f->eip;
	strcpy(e.ev_event, ev);

	/*
	 * Try and place it on the stack
	 */
	if (copyout(f->esp - sizeof(e), &e, sizeof(e))) {
#ifdef DEBUG
		printf("Stack overflow pid %ld/%ld sp 0x%x\n",
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
 *	Common code for all CPU interrupts
 */
void
interrupt(ulong place_holder)
{
	struct trapframe *f = (struct trapframe *)&place_holder;
	int isr = f->traptype;

	/*
	 * Sanity check and fold into range 0..MAX_IRQ-1.  Enable
	 * further interrupts from the ICU--interrupts are still
	 * masked on-chip.
	 */
	ASSERT(isr >= T_EXTERN, "interrupt: stray low");
	isr -= T_EXTERN;
	ASSERT(isr < MAX_IRQ, "interrupt: stray high");
	EOI();

	/*
	 * Our only hard-wired interrupt handler; the clock
	 */
	if (isr == 0) {
		extern void hardclock();

		hardclock(f);
		goto out;
	}

	/*
	 * Let processes registered for an IRQ get them
	 */
	if (deliver_isr(isr)) {
		goto out;
	}

	/*
	 * Otherwise bomb on stray interrupt
	 */
	ASSERT(0, "interrupt: stray");

	/*
	 * Check for preemption and events if we pushed in from user mode.
	 * When ready for kernel preemption, move check_preempt() to before
	 * the "if" statement.
	 */
out:
	if ((f->ecs & 0x3) == PRIV_USER) {
		struct thread *t = curthread;

		sti();
		if (EVENT(t)) {
			t->t_uregs = f;
			check_events();
			t->t_uregs = 0;
		}
		check_preempt();
	}
}
