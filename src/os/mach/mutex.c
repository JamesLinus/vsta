/*
 * mutex.c
 *	Uniprocessor i386 implementation of mutual exclusion
 *
 * Mutual exclusion's not very hard on a uniprocessor, eh?
 */
#include <sys/assert.h>
#include <sys/thread.h>
#include <sys/percpu.h>
#include <sys/sched.h>
#include "../mach/mutex.h"
#include "../mach/locore.h"

extern lock_t runq_lock;
extern void lsetrun(), swtch();
#ifdef DEBUG
char msg_deadlock[] = "deadlock", msg_notheld[] = "not held";
#endif

/*
 * q_sema()
 *	Queue a thread under a semaphore
 *
 * Assumes semaphore is locked.
 */
inline static void
q_sema(struct sema *s, struct thread *t)
{
	if (!s->s_sleepq) {
		s->s_sleepq = t;
		t->t_hd = t->t_tl = t;
	} else {
		struct thread *t2 = s->s_sleepq;

		t->t_hd = t2;
		t->t_tl = t2->t_tl;
		t->t_tl->t_hd = t;
		t2->t_tl = t;
	}
}

/*
 * dq_sema()
 *	Remove a thread from a semaphore sleep list
 *
 * Assumes semaphore is locked.
 */
inline static void
dq_sema(struct sema *s, struct thread *t)
{
	ASSERT_DEBUG(s->s_count < 0, "dq_sema: bad count");
	s->s_count += 1;
	if (t->t_hd == t) {
		s->s_sleepq = 0;
	} else {
		struct thread *t_hd = t->t_hd;
		struct thread *t_tl = t->t_tl;
		
		t_hd->t_tl = t_tl;
		t_tl->t_hd = t_hd;
		if (s->s_sleepq == t) {
			s->s_sleepq = t_hd;
		}
	}
#ifdef DEBUG
	t->t_hd = t->t_tl = 0;
#endif
}

/*
 * p_sema()
 *	Take semaphore, sleep if can't
 */
int
p_sema(sema_t *s, pri_t p)
{
	struct thread *t;

	ASSERT_DEBUG(cpu.pc_locks == 0, "p_sema: locks held");
	ASSERT_DEBUG(s->s_lock.l_lock == 0, "p_sema: deadlock");

	/*
	 * Counts > 0, just decrement the count and go
	 */
	p_lock_void(&s->s_lock, SPLHI);
	s->s_count -= 1;
	if (s->s_count >= 0) {
		v_lock(&s->s_lock, SPL0);
		return(0);
	}

	/*
	 * We're going to sleep.  Add us at the tail of the
	 * queue, and relinquish the processor.
	 */
	t = curthread;
	t->t_wchan = s;
	t->t_nointr = (p == PRIHI);
	t->t_intr = 0;	/* XXX this can race with notify */
	q_sema(s, t);
	p_lock_void(&runq_lock, SPLHI_SAME);
	v_lock(&s->s_lock, SPLHI_SAME);
	t->t_state = TS_SLEEP;
	ATOMIC_DEC(&num_run);
	swtch();
	sti();

	/*
	 * We're back.  If we have an event pending, give up on the
	 * semaphore and return the right result.
	 */
	t->t_nointr = 0;
	if (t->t_intr) {
		ASSERT_DEBUG(t->t_wchan == 0, "p_sema: intr w. wchan");
		ASSERT_DEBUG(p != PRIHI, "p_sema: intr w. PRIHI");
		return(1);
	}

	/*
	 * We were woken up to be next with the semaphore.  Our waker
	 * did the reference count update, so we just return.
	 */
	return(0);
}

/*
 * cp_sema()
 *	Conditional p_sema()
 */
int
cp_sema(sema_t *s)
{
	spl_t spl;
	int x;

	ASSERT_DEBUG(s->s_lock.l_lock == 0, "p_sema: deadlock");

	spl = p_lock(&s->s_lock, SPLHI);
	if (s->s_count > 0) {
		s->s_count -= 1;
		x = 0;
	} else {
		x = -1;
	}
	v_lock(&s->s_lock, spl);
	return(x);
}

/*
 * v_sema()
 *	Release a semaphore
 */
void
v_sema(sema_t *s)
{
	struct thread *t;
	spl_t spl;

	spl = p_lock(&s->s_lock, SPLHI);
	if (s->s_sleepq) {
		ASSERT_DEBUG(s->s_count < 0, "v_sema: too many sleepers");
		t = s->s_sleepq;
		ASSERT_DEBUG(t->t_wchan == s, "v_sema: mismatch");
		dq_sema(s, t);
		t->t_wchan = 0;
		p_lock_void(&runq_lock, SPLHI_SAME);
		lsetrun(t);
		v_lock(&runq_lock, SPLHI_SAME);
	} else {
		/* dq_sema() does it otherwise */
		s->s_count += 1;
	}
	v_lock(&s->s_lock, spl);
}

/*
 * vall_sema()
 *	Kick everyone loose who's sleeping on the semaphore
 */
void
vall_sema(sema_t *s)
{
	while (s->s_count < 0) {
		/* XXX races on MP; have to expand v_sema here */
		v_sema(s);
	}
}

/*
 * p_sema_v_lock()
 *	Atomically transfer from a spinlock to a semaphore
 */
int
p_sema_v_lock(sema_t *s, pri_t p, lock_t *l)
{
	struct thread *t;

	ASSERT_DEBUG(cpu.pc_locks == 1, "p_sema_v: bad lock count");
	ASSERT_DEBUG(s->s_lock.l_lock == 0, "p_sema_v: deadlock");

	/*
	 * Take semaphore lock.  If count is high enough, release
	 * semaphore and lock now.
	 */
	p_lock_void(&s->s_lock, SPLHI);
	s->s_count -= 1;
	if (s->s_count >= 0) {
		v_lock(&s->s_lock, SPLHI_SAME);
		v_lock(l, SPL0);
		return(0);
	}

	/*
	 * We're going to sleep.  Add us at the tail of the
	 * queue, and relinquish the processor.
	 */
	t = curthread;
	t->t_wchan = s;
	t->t_intr = 0;
	t->t_nointr = (p == PRIHI);
	q_sema(s, t);
	p_lock_void(&runq_lock, SPLHI_SAME);
	v_lock(&s->s_lock, SPLHI_SAME);
	v_lock(l, SPLHI_SAME);
	t->t_state = TS_SLEEP;
	ATOMIC_DEC(&num_run);
	swtch();
	sti();

	/*
	 * We're back.  If we have an event pending, give up on the
	 * semaphore and return the right result.
	 */
	t->t_nointr = 0;
	if (t->t_intr) {
		ASSERT_DEBUG(t->t_wchan == 0, "p_sema: intr w. wchan");
		ASSERT_DEBUG(p != PRIHI, "p_sema: intr w. PRIHI");
		return(1);
	}

	/*
	 * We were woken up to be next with the semaphore.  Our waker
	 * did the reference count update, so we just return.
	 */
	return(0);
}

/*
 * cunsleep()
 *	Try to remove a process from a sema queue
 *
 * Returns 1 on busy mutex; 0 for success
 */
int
cunsleep(struct thread *t)
{
	spl_t s;
	sema_t *sp;

	ASSERT_DEBUG(t->t_wchan, "cunsleep: zero wchan");

	/*
	 * Try for mutex on semaphore
	 */
	sp = t->t_wchan;
	s = cp_lock(&sp->s_lock, SPLHI);
	if (s == -1) {
		return(1);
	}

	/*
	 * Get thread off queue
	 */
	dq_sema(sp, t);

	/*
	 * Null out wchan to be safe.  Sema count was updated
	 * by dq_sema().  Flag that he didn't get the semaphore.
	 */
	t->t_wchan = 0;
	t->t_intr = 1;

	/*
	 * Success
	 */
	v_lock(&sp->s_lock, s);
	return(0);
}
