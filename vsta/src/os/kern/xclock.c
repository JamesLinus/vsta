/*
 * clock.c
 *	Handling of clock ticks and such
 *
 * The assumption in VSTa is that each CPU has a regular clock tick.
 * Each CPU keeps its own count of time; in systems where the CPUs
 * run off a common bus clock there will be no time drift.  For
 * others, additional code to correct drift would have to be added.
 */
#include <sys/percpu.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <mach/machreg.h>
#include <sys/sched.h>

extern int do_preempt;

/*
 * hardclock()
 *	Called directly from the hardware interrupt
 *
 * Interrupts are still disabled; this routine enables them when
 * it's ready.
 */
void
hardclock(uint x)
{
	struct trapframe *f = (struct trapframe *)&x;
	struct percpu *c = &cpu;
	struct thread *t;

	/*
	 * If we re-entered, just log a tick and get out.  Otherwise
	 * flag us as being in clock handling.  Further interrupts are
	 * now allowed.
	 */
	if (c->pc_flags & CPU_CLOCK) {
		ATOMIC_INC(&c->pc_ticks);
		return;
	}
	c->pc_flags |= CPU_CLOCK;
	sti();

	/*
	 * Bump time for our local CPU.  Lower bits count up until HZ;
	 * upper bits then are in units of seconds.
	 */
	c->pc_time[0] += c->pc_ticks;
	c->pc_ticks = 0;
	if (++(c->pc_time[0]) >= HZ) {
		c->pc_time[1] += (c->pc_time[0] / HZ);
		c->pc_time[0] =  (c->pc_time[0] % HZ);
	}

	/*
	 * Bill time to current thread
	 */
	if (t = c->pc_thread) {
		if (USERMODE(f)) {
			t->t_usrcpu += 1L;
		} else {
			t->t_syscpu += 1L;
		}

		/*
		 * If current thread's allocated amount of CPU is
		 * complete and there are others waiting to run,
		 * timeslice.
		 */
		if ((t->t_runticks -= 1L) == 0) {
			do_preempt = 1;
		}
	}

	/*
	 * Clear flag & done
	 */
	c->pc_flags &= ~CPU_CLOCK;
}
