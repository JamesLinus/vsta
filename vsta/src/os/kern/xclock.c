/*
 * xclock.c
 *	Handling of clock ticks and such
 *
 * The assumption in VSTa is that each CPU has a regular clock tick.
 * Each CPU keeps its own count of time; in systems where the CPUs
 * run off a common bus clock there will be no time drift.  For
 * others, additional code to correct drift would have to be added.
 *
 * In order to simplify things we actually only keep track of the time
 * since VSTa was booted - if our system time is changed we simply adjust
 * our boot time and keep the number of seconds of uptime consistent.
 */
#include <sys/percpu.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <mach/machreg.h>
#include <sys/sched.h>
#include <sys/fs.h>
#include <sys/malloc.h>
#include <sys/assert.h>

/*
 * CVT_TIME()
 *	Convert from internal hz/sec format into "struct time"
 */
#define CVT_TIME(tim, t) { \
	(t)->t_sec = (tim)[1]; \
	(t)->t_usec = (tim)[0] * (1000000/HZ); }

/*
 * List of processes waiting for a certain time to pass
 */
struct eventq {
	ulong e_tid;		/* PID of thread */
	struct time e_time;	/* What time to wake */
	struct eventq *e_next;	/* List of sleepers */
	sema_t e_sema;		/* Semaphore to sleep on */
	int e_onlist;		/* Flag that still in eventq list */
};

static struct eventq		/* List of sleepers pending */
	*eventq = 0;
static lock_t time_lock;	/* Mutex on eventq */

extern uint pageout_secs;	/* Interval to call pageout */
static uint pageout_wait;	/* Secs since last call */
static struct time		/* Time that the system booted */
	boot_time = {0, 0};

/*
 * alarm_wakeup()
 *	Wake up all those whose time interval has passed
 */
static void
alarm_wakeup(struct time *t)
{
	long l = t->t_sec, l2 = t->t_usec;
	struct eventq *e, **ep;

	ep = &eventq;
	p_lock(&time_lock, SPLHI);
	for (e = eventq; e; e = e->e_next) {
		/*
		 * Our list is ordered to the second, so we can bail
		 * when we've come this far.
		 */
		if (e->e_time.t_sec > l) {
			break;
		}

		/*
		 * If this is a "hit", wake him and pull him from the list
		 */
		if (((e->e_time.t_sec == l) && (e->e_time.t_usec <= l2)) ||
				(e->e_time.t_sec < l)) {
			/*
			 * Note he can't free this until he takes time_lock,
			 * so we can't race on the use of these fields.
			 */
			e->e_onlist = 0;
			*ep = e->e_next;
			v_sema(&e->e_sema);
		} else {
			/*
			 * Keep looking
			 */
			ep = &e->e_next;
		}
	}
	v_lock(&time_lock, SPLHI);
}

/*
 * hardclock()
 *	Called directly from the hardware interrupt
 *
 * Interrupts are still disabled; this routine enables them when
 * it's ready.
 */
void
hardclock(struct trapframe *f)
{
	struct percpu *c = &cpu;
	struct thread *t;
	struct time tm;

	/*
	 * If we re-entered, just log a tick and get out.  Otherwise
	 * flag us as being in clock handling.  Further interrupts are
	 * now allowed.
	 */
	if (c->pc_flags & CPU_CLOCK) {
		ATOMIC_INCL(&c->pc_ticks);
		return;
	}
	c->pc_flags |= CPU_CLOCK;
	c->pc_time[0] += c->pc_ticks;
	c->pc_ticks = 0;
	sti();

	/*
	 * Bump time for our local CPU.  Lower slot counts up until HZ;
	 * upper bits then are in units of seconds.
	 */
	c->pc_time[0] += 1;
	while (c->pc_time[0] >= HZ) {
		pageout_wait += 1;
		c->pc_time[1] += 1;
		c->pc_time[0] -= HZ;
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
		if (t->t_runticks > 0) {
			t->t_runticks -= 1;
		}
		if (t->t_runticks == 0) {
			if (t->t_oink < T_MAX_OINK) {
				t->t_oink += 1;
			}
			do_preempt = 1;
		}
	}

	/*
	 * Wake anyone waiting to run.  Can't race on pc_time[]
	 * because we hold the CPU_CLOCK bit.
	 */
	if (eventq) {
		CVT_TIME(c->pc_time, &tm);
		alarm_wakeup(&tm);
	}

	/*
	 * If pageout configured, invoke him periodically
	 * XXX generalize
	 */
	if (pageout_secs > 0) {
		if (pageout_wait >= pageout_secs) {
			extern void kick_pageout();

			pageout_wait = 0;
			kick_pageout();
		}
	}

	/*
	 * Clear flag & done
	 */
	c->pc_flags &= ~CPU_CLOCK;
}

/*
 * time_set()
 *	Take time as argument, set system clock
 */
time_set(struct time *arg_time)
{
	struct time t, ct;

	if (!isroot()) {
		return(-1);
	}
	if (copyin(arg_time, &t, sizeof(t))) {
		return(-1);
	}

	/*
	 * Reference the new time to our CPU time and determine when
	 * our boot-time really was
	 */
	cli();
		CVT_TIME(cpu.pc_time, &ct);
	sti();
	
	t.t_sec -= ct.t_sec;
	t.t_usec -= ct.t_usec;
	if (t.t_usec < 0) {
		t.t_sec--;
		t.t_usec += 1000000;
	}

	cli();
		boot_time.t_usec = t.t_usec;
		boot_time.t_sec = t.t_sec;
	sti();
	return(0);
}

/*
 * time_get()
 *	Return time to the second
 */
time_get(struct time *arg_time)
{
	struct time t;

	/*
	 * Get time in desired format, hand to user
	 */
	cli();
		CVT_TIME(cpu.pc_time, &t);
		t.t_sec += boot_time.t_sec;
		t.t_usec += boot_time.t_usec;
	sti();
	if (t.t_usec > 1000000) {
		t.t_sec++;
		t.t_usec -= 1000000;
	}

	if (copyout(arg_time, &t, sizeof(t))) {
		return(-1);
	}
	return(0);
}

/*
 * time_sleep()
 *	Sleep for the indicated amount of time
 */
time_sleep(struct time *arg_time)
{
	struct time t;
	ulong l;
	struct eventq *ev, *e, **ep;

	/*
	 * Get the time the user has in mind
	 */
	if (copyin(arg_time, &t, sizeof(t))) {
		return(-1);
	}
	
	/*
	 * Convert the time to a boot time referenced value
	 */
	cli();
		t.t_sec -= boot_time.t_sec;
		t.t_usec -= boot_time.t_usec;
	sti();
	if (t.t_usec < 0) {
		t.t_sec--;
		t.t_usec += 1000000;
	}

	l = t.t_sec;

	/*
	 * Get an event element, fill it in
	 */
	ev = MALLOC(sizeof(struct eventq), MT_EVENTQ);
	ev->e_time = t;
	ev->e_tid = curthread->t_pid;
	init_sema(&ev->e_sema); set_sema(&ev->e_sema, 0);
	ev->e_onlist = 1;

	/*
	 * Lock the list, find our place, insert.  We only sort in
	 * granularity of the second; this keeps our loop simpler,
	 * and hopefully there won't be too many events scheduled
	 * for the same second.
	 */
	ep = &eventq;
	p_lock(&time_lock, SPLHI);
	for (e = eventq; e; e = e->e_next) {
		if (l <= e->e_time.t_sec) {
			ev->e_next = e;
			*ep = ev;
			break;
		}
		ep = &e->e_next;
	}

	/*
	 * If we fell off the end of the list, place us there
	 */
	if (e == 0) {
		ev->e_next = 0;
		*ep = ev;
	}

	/*
	 * Atomically switch to the semaphore.  We will either return
	 * from an event, or from our time request completing.
	 */
	if (p_sema_v_lock(&ev->e_sema, PRICATCH, &time_lock)) {
		/*
		 * Regain lock, and see if we're still on the list.
		 */
		p_lock(&time_lock, SPLHI);
		if (ev->e_onlist) {
			/*
			 * Hunt ourselves down and remove from the list
			 */
			ep = &eventq;
			for (e = eventq; e; e = e->e_next) {
				if (e == ev) {
					*ep = ev->e_next;
					break;
				}
				ep = &e->e_next;
			}
			ASSERT(e, "alarm_set: lost event");
		}
		v_lock(&time_lock, SPL0);
		FREE(ev, MT_EVENTQ);
		return(err(EINTR));
	}
	ASSERT(ev->e_onlist == 0, "alarm_set: spurious wakeup");
	FREE(ev, MT_EVENTQ);
	return(0);
}

/*
 * uptime()
 *	Return the uptime of the system
 */
void uptime(struct time *t)
{
	cli();
		CVT_TIME(cpu.pc_time, t);
	sti();
}
