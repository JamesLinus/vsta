/*
 * isr.c
 *	Routines for connecting interrupts to the messaging interface
 */
#include <sys/types.h>
#include <mach/isr.h>
#include <sys/fs.h>
#include <sys/port.h>
#include <sys/mutex.h>
#include <sys/percpu.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <mach/machreg.h>
#include <mach/icu.h>
#include <sys/assert.h>

/*
 * Per-IRQ information on who to call when an IRQ is entered
 */
struct isr_msg {
	struct port *i_port;
	struct sysmsg i_msg;
};
static struct isr_msg handler[MAX_IRQ];
char handlers[MAX_IRQ];

/*
 * Master mask of what interrupts are enabled.  Starts with all
 * interrupts disabled.
 */
ushort intr_mask = 0xFFFF;

/*
 * Mask determining who can do these sort of operations
 */
#define IOPRIV_ISR (1)		/* Can vector ISR's */
#define IOPRIV_IO (2)		/* Can do I/O instructions */
struct prot io_prot = {
	2,
	0,
	{1, 1},
	{0, IOPRIV_ISR | IOPRIV_IO}
};

/*
 * enable_isr()
 *	Connect an ISR to a port
 */
enable_isr(port_t arg_port, int irq)
{
	struct sysmsg *sm;
	struct port *port;
	int error = 0;
	struct proc *p = curthread->t_proc;
	extern struct port *find_port();

	/*
	 * Check for permission
	 */
	if (!issys()) {
		return(-1);
	}

	/*
	 * Validate port, lock it
	 */
	port = find_port(p, arg_port);
	if (!port) {
		return(-1);
	}

	/*
	 * Check IRQ #, make sure the slot's available
	 */
	if ((irq < 0) || (irq >= MAX_IRQ)) {
		error = err(EINVAL);
		goto out;
	}
	if (handlers[irq]) {
		error = err(EBUSY);
		goto out;
	}

	/*
	 * Initialize the message
	 */
	sm = &handler[irq].i_msg;
	sm->m_op = 0;
	sm->m_nseg = sm->m_arg = sm->m_arg1 = 0;
	sm->m_sender = 0;

	/*
	 * Put it in the handler slot
	 */
	handler[irq].i_port = port;

	/*
	 * Flag port as having an IRQ handler
	 */
	port->p_flags |= P_ISR;
	handlers[irq] = 1;

	/*
	 * Enable this interrupt vector
	 */
	intr_mask &= ~(1 << irq);
	SETMASK(intr_mask);
out:
	v_lock(&port->p_lock, SPL0);
	v_sema(&port->p_sema);
	return(error);
}

/*
 * disable_isr()
 *	Disable all ISR reporting to the given port
 */
void
disable_isr(struct port *port)
{
	int x;
	struct isr_msg *i;

	for (x = 0, i = handler; x < MAX_IRQ; ++x,++i) {
		if (i->i_port == port) {
			/*
			 * Shut off this interrupt vector
			 */
			intr_mask |= (1 << x);
			SETMASK(intr_mask);

			/*
			 * Remove handler
			 */
			handlers[x] = 0;
			i->i_port = 0;
		}
	}
}

/*
 * enable_io()
 *	Enable I/O instructions for the current thread
 *
 * For now, just use the IOPL field of the user's eflags.  We *could*
 * use the I/O bitmap, but I hear it has bugs, and I don't feel like
 * chasing them just yet.
 */
enable_io(int arg_low, int arg_high)
{
	struct trapframe *f = curthread->t_uregs;
	struct proc *p = curthread->t_proc;

	ASSERT(f, "enable_io: no uregs");

	/*
	 * Check for permission
	 */
	p_sema(&p->p_sema, PRILO);
	if (!(perm_calc(p->p_ids, PROCPERMS, &io_prot) & IOPRIV_IO)) {
		v_sema(&p->p_sema);
		return(err(EPERM));
	}
	v_sema(&p->p_sema);

	/*
	 * He checks out--turn on all bits in IOPL--this means
	 * level 3 (user mode) can do I/O instructions.
	 */
	f->eflags |= F_IOPL;
	return(0);
}

/*
 * deliver_isr()
 *	See if given ISR has a handler; call it if so
 *
 * Return 1 if a handler was found, 0 otherwise.
 */
deliver_isr(int isr)
{
	struct sysmsg *sm;
	extern void queue_msg();

	/*
	 * Only 16 supported through ISA ICU
	 */
	ASSERT_DEBUG(isr < MAX_IRQ, "deliver_isr: bad isr");

	/*
	 * Anybody registered?
	 */
	if (handlers[isr] == 0) {
		return(0);
	}

	/*
	 * If the m_op field is 0, this message isn't currently
	 * queued.  Set m_op and queue it.
	 */
	sm = &handler[isr].i_msg;
	if (sm->m_op == 0) {
		sm->m_op = M_ISR;
		sm->m_arg = isr;
		sm->m_arg1 = 1;
		queue_msg(handler[isr].i_port, sm);
		return(1);
	}

	/*
	 * Otherwise it's still languishing there.  Bump the
	 * m_arg field to tell them how many times they missed.
	 */
	sm->m_arg1 += 1;
	return(1);
}
