/*
 * mutex.c
 *	Uniprocessor i386 implementation of mutual exclusion
 *
 * Mutual exclusion's not very hard on a uniprocessor, eh?
 */
#include <sys/mutex.h>
#include <sys/assert.h>
#include <sys/thread.h>
#include <sys/percpu.h>

extern lock_t runq_lock;

/*
 * p_lock()
 *	Take spinlock
 */
spl_t
p_lock(lock_t *l, spl_t s)
{
	int x;

	if (s == SPLHI) {
		x = cli();
	} else {
		x = 1;
	}
	ASSERT_DEBUG(l->l_lock == 0, "p_lock: deadlock");
	l->l_lock = 1;
	ATOMIC_INC(&cpu.pc_locks);
	return(x ? SPL0 : SPLHI);
}

/*
 * cp_lock()
 *	Conditional take of spinlock
 */
spl_t
cp_lock(lock_t *l, spl_t s)
{
	int x;

	if (s == SPLHI) {
		x = cli();
	} else {
		x = 1;
	}
	if (l->l_lock) {
		if ((s == SPLHI) && x) {
			sti();
		}
		return(-1);
	}
	l->l_lock = 1;
	ATOMIC_INC(&cpu.pc_locks);
	return(x ? SPL0 : SPLHI);
}

/*
 * v_lock()
 *	Release spinlock
 */
void
v_lock(lock_t *l, spl_t s)
{
	ASSERT_DEBUG(l->l_lock, "v_lock: not held");
	l->l_lock = 0;
	if (s == SPL0) {
		sti();
	}
	ATOMIC_DEC(&cpu.pc_locks);
}

/*
 * init_lock()
 *	Initialize lock to "not held"
 */
void
init_lock(lock_t *l)
{
	l->l_lock = 0;
}

/*
 * q_sema()
 *	Queue a thread under a semaphore
 *
 * Assumes semaphore is locked.
 */
static void
q_sema(struct sema *s, struct thread *t)
{
	if (!s->s_sleepq) {
		s->s_sleepq = t;
		t->t_hd = t->t_tl = t;
	} else {
		struct thread *t2 = s->s_sleepq->t_tl;

		t->t_hd = t2->t_hd;
		t->t_tl = t2;
		t2->t_tl->t_hd = t;
		t2->t_tl = t;
	}
}

/*
 * dq_sema()
 *	Remove a thread from a semaphore sleep list
 *
 * Assumes semaphore is locked.
 */
void
dq_sema(struct sema *s, struct thread *t)
{
	ASSERT_DEBUG(s->s_count < 0, "dq_sema: bad count");
	s->s_count += 1;
	if (t->t_hd == t) {
		s->s_sleepq = 0;
	} else {
		t->t_hd->t_tl = t->t_tl;
		t->t_tl->t_hd = t->t_hd;
		if (s->s_sleepq == t) {
			s->s_sleepq = t->t_hd;
		}
	}
}

/*
 * p_sema()
 *	Take semaphore, sleep if can't
 */
p_sema(sema_t *s, pri_t p)
{
	struct thread *t;

	ASSERT_DEBUG(cpu.pc_locks == 0, "p_sema: locks held");
	ASSERT_DEBUG(s->s_lock.l_lock == 0, "p_sema: deadlock");

	/*
	 * Counts > 0, just decrement the count and go
	 */
	(void)p_lock(&s->s_lock, SPLHI);
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
	t->t_intr = 0;	/* XXX this can race with notify */
	q_sema(s, t);
	p_lock(&runq_lock, SPLHI);
	v_lock(&s->s_lock, SPLHI);
	swtch();

	/*
	 * We're back.  If we have an event pending, give up on the
	 * semaphore and return the right result.
	 */
	if (t->t_intr) {
		ASSERT_DEBUG(t->t_wchan == 0, "p_sema: intr w. wchan");
		ASSERT_DEBUG(p != PRIHI, "p_sema: intr w. PRIHI");
		if (p != PRICATCH) {
			longjmp(t->t_qsav, 1);
		}
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
		dq_sema(s, t = s->s_sleepq);
		setrun(t);
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
 * blocked_sema()
 *	Tell if anyone's sleeping on the semaphore
 */
blocked_sema(sema_t *s)
{
	return (s->s_count < 0);
}

/*
 * init_sema()
 * 	Initialize semaphore
 *
 * The s_count starts at 1.
 */
void
init_sema(sema_t *s)
{
	s->s_count = 1;
	s->s_sleepq = 0;
	init_lock(&s->s_lock);
}

/*
 * set_sema()
 *	Manually set the value for the semaphore count
 *
 * Use with care; if you strand someone on the queue your system
 * will start to act funny.  If DEBUG is on, it'll probably panic.
 */
void
set_sema(sema_t *s, int cnt)
{
	s->s_count = cnt;
}

/*
 * p_sema_v_lock()
 *	Atomically transfer from a spinlock to a semaphore
 */
p_sema_v_lock(sema_t *s, pri_t p, lock_t *l)
{
	struct thread *t;
	spl_t spl;

	ASSERT_DEBUG(cpu.pc_locks == 1, "p_sema_v: bad lock count");
	ASSERT_DEBUG(s->s_lock.l_lock == 0, "p_sema_v: deadlock");

	/*
	 * Take semaphore lock.  If count is high enough, release
	 * semaphore and lock now.
	 */
	spl = p_lock(&s->s_lock, SPLHI);
	s->s_count -= 1;
	if (s->s_count >= 0) {
		v_lock(&s->s_lock, spl);
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
	q_sema(s, t);
	p_lock(&runq_lock, SPLHI);
	v_lock(&s->s_lock, SPLHI);
	v_lock(l, SPLHI);
	swtch();

	/*
	 * We're back.  If we have an event pending, give up on the
	 * semaphore and return the right result.
	 */
	if (t->t_intr) {
		ASSERT_DEBUG(t->t_wchan == 0, "p_sema: intr w. wchan");
		ASSERT_DEBUG(p != PRIHI, "p_sema: intr w. PRIHI");
		if (p != PRICATCH) {
			longjmp(t->t_qsav, 1);
		}
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
