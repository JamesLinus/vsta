/*
 * event.c
 *	Code for delivering events to a thread or process
 *
 * Both threads and processes have IDs.  An event sent to a process
 * results in the event being delivered to each thread under the
 * process.  An event can also be sent to a specific thread.
 *
 * A process has two distinct incoming streams
 * of events--a process-generated one and a system-generated one.
 * The system events take precedence.  For process events, a sender
 * will sleep until the target process has accepted a current event and
 * (if he has a handler registered) returned from the handler.  System
 * events simply overwrite each other; when the process finally gets
 * the event, it only sees the latest event received.
 *
 * t_wchan is used to get a thread interrupted from a sleep.  Because
 * the runq_lock is already held when the semaphore indicated by t_wchan
 * needs to be manipulated, a potential deadlock exists.  The delivery
 * code knows how to back out from this situation and retry.
 *
 * Mutexing is accomplished via the destination process' p_sema and
 * the global runq_lock.  p_sema (the field, not the function) is needed
 * to keep the process around while its threads are manipulated.  runq_lock
 * is used to mutex a consistent thread state while delivering the
 * effects of an event.
 */
#include <sys/percpu.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/fs.h>
#include <lib/hash.h>
#include <lib/alloc.h>
#include <sys/assert.h>

extern sema_t pid_sema;
extern lock_t runq_lock;
extern struct hash *pid_hash;
extern struct proc *pfind();

/*
 * signal_thread()
 *	Send an event to a thread
 *
 * The process mutex is held by the caller.
 */
static
signal_thread(struct thread *t, char *event, int is_sys)
{
	extern void nudge(), lsetrun();

	if (!is_sys) {
		if (p_sema(&t->t_evq, PRICATCH)) {
			return(err(EINTR));
		}
	}

	/*
	 * Take lock, place event in appropriate place
	 */
retry:	p_lock(&runq_lock, SPLHI);
	strcpy((is_sys ? t->t_evsys : t->t_evproc), event);

	/*
	 * As appropriate, kick him awake
	 */
	switch (t->t_state) {
	case TS_SLEEP:		/* Interrupt sleep */
		if (cunsleep(t)) {
			v_lock(&runq_lock, SPL0);
			goto retry;
		}
		lsetrun(t);
		break;

	case TS_ONPROC:		/* Nudge him */
		nudge(t->t_eng);
		break;

	case TS_RUN:		/* Nothing to do for these */
	case TS_DEAD:
		break;

	default:
		ASSERT(0, "signal_thread: unknown state");
	}
	v_lock(&runq_lock, SPL0);
}

/*
 * notifypg()
 *	Send an event to a process group
 */
static
notifypg(struct proc *p, char *event)
{
	ulong *l, *lp;
	uint x, nelem;
	struct pgrp *pg = p->p_pgrp;

	p_sema(&pg->pg_sema, PRIHI);

	/*
	 * Get the list of processes to hit while holding the
	 * group's semaphore.
	 */
	nelem = pg->pg_nmember;
	l = malloc(pg->pg_nmember * sizeof(ulong));
	lp = pg->pg_members;
	for (x = 0; x < nelem; ++x) {
		while (*lp == 0)
			++lp;
		l[x] = *lp++;
		x -= 1;
	}

	/*
	 * Release the semaphores, then go about trying to
	 * signal the processes.
	 */
	v_sema(&pg->pg_sema);
	v_sema(&p->p_sema);
	for (x = 0; x < nelem; ++x) {
		notify2(l[x], 0L, event);
	}
	free(l);
	return(0);
}

/*
 * notify2()
 *	Most of the work for doing an event notification
 *
 * If arg_proc is 0, it means the current process.  If arg_thread is
 * 0, it means all threads under the named proc.
 */
notify2(ulong arg_proc, ulong arg_thread, char *evname)
{
	struct proc *p;
	int x, error = 0;
	struct thread *t;

	if (arg_proc == 0) {
		/*
		 * Our process isn't too hard
		 */
		p = curthread->t_proc;
		p_sema(&p->p_sema, PRILO);
	} else {
		/*
		 * Look up given PID
		 */
		p = pfind(arg_proc);
		if (!p) {
			return(err(ESRCH));
		}
	}

	/*
	 * See if we're allowed to signal him
	 */
	x = perm_calc(curthread->t_proc->p_ids, PROCPERMS, &p->p_prot);
	if (!(x & P_SIG)) {
		error = err(EPERM);
		goto out;
	}

	/*
	 * Magic value for thread means signal process group instead
	 */
	if (arg_thread == NOTIFY_PG) {
		return(notifypg(p, evname));
	}

	/*
	 * If this is a single thread signal, hunt the thread down
	 */
	if (arg_thread != 0) {
		for (t = p->p_threads; t; t = t->t_next) {
			if (t->t_pid == arg_thread) {
				if (signal_thread(t, evname, 0)) {
					error = -1;
					goto out;
				}
			}
		}
		/*
		 * Never found him
		 */
		error = err(ESRCH);
		goto out;
	}

	/*
	 * Otherwise hit each guy in a row
	 */
	for (t = p->p_threads; t; t = t->t_next) {
		if (signal_thread(t, evname, 0)) {
			error = -1;
			goto out;
		}
	}
out:
	v_sema(&p->p_sema);
	return(error);
}

/*
 * notify()
 *	Send a process event somewhere
 *
 * Wrapper to get event string into kernel
 */
notify(ulong arg_proc, ulong arg_thread, char *arg_msg, int arg_msglen)
{
	char evname[EVLEN];

	/*
	 * Get the string event name
	 */
	if (get_ustr(evname, sizeof(evname), arg_msg, arg_msglen)) {
		return(-1);
	}
	return(notify2(arg_proc, arg_thread, evname));
}

/*
 * selfsig()
 *	Send a signal to the current thread
 *
 * Used solely to deliver system exceptions.  Because it only delivers
 * to the current thread, it can assume that the thread is running (and
 * thus exists!)
 */
void
selfsig(char *ev)
{
	spl_t s;

	ASSERT(curthread, "selfsig: no thread");
	s = p_lock(&runq_lock, SPLHI);
	strcpy(curthread->t_evsys, ev);
	v_lock(&runq_lock, s);
}

/*
 * check_events()
 *	If there are events, deliver them
 */
void
check_events(void)
{
	struct thread *t = curthread;
	spl_t s;
	char event[EVLEN];
	extern void sendev();

	/*
	 * No thread or no events--nothing to do
	 */
	if (!t || !EVENT(t)) {
		return;
	}

	/*
	 * Take next events, act on them
	 */
	s = p_lock(&runq_lock, SPLHI);
	if (t->t_evsys[0]) {
		strcpy(event, t->t_evsys);
		t->t_evsys[0] = '\0';
		v_lock(&runq_lock, s);
		sendev(t, event);
		return;
	}
	if (t->t_evproc[0]) {
		strcpy(event, t->t_evproc);
		t->t_evproc[0] = '\0';
		v_lock(&runq_lock, s);
		sendev(t, event);
		v_sema(&t->t_evq);
		return;
	}
	v_lock(&runq_lock, s);
}

/*
 * join_pgrp()
 *	Join a process group
 */
void
join_pgrp(struct pgrp *pg, ulong pid)
{
	void *e;
	ulong *lp;

	p_sema(&pg->pg_sema, PRIHI);

	/*
	 * If there's no more room now for members, grow the list
	 */
	if (pg->pg_nmember >= pg->pg_nelem) {
		e = malloc((pg->pg_nelem + PG_GROWTH) * sizeof(ulong));
		bcopy(pg->pg_members, e, pg->pg_nelem * sizeof(ulong));
		free(pg->pg_members);
		pg->pg_members = e;
		bzero(pg->pg_members + pg->pg_nelem,
			PG_GROWTH*sizeof(ulong));
		pg->pg_nelem += PG_GROWTH;
	}

	/*
	 * Insert in open slot
	 */
	for (lp = pg->pg_members; *lp; ++lp)
		;
	ASSERT_DEBUG(lp < (pg->pg_members + pg->pg_nelem),
		"join_pgrp: no slot");
	*lp = pid;
	pg->pg_nmember += 1;

	v_sema(&pg->pg_sema);
}

/*
 * leave_pgrp()
 *	Leave a process group
 */
void
leave_pgrp(struct pgrp *pg, ulong pid)
{
	ulong *lp;

	p_sema(&pg->pg_sema, PRIHI);

	/*
	 * If we're the last, throw it out
	 */
	if (pg->pg_nmember == 1) {
		free(pg->pg_members);
		free(pg);
		return;
	}

	/*
	 * Otherwise leave the group, zero out our slot
	 */
	pg->pg_nmember -= 1;
	for (lp = pg->pg_members; *lp != pid; ++lp)
		;
	ASSERT_DEBUG(lp < (pg->pg_members + pg->pg_nelem),
		"leave_pgrp: no slot");
	*lp = 0;

	v_sema(&pg->pg_sema);
}

/*
 * alloc_pgrp()
 *	Allocate a new process group
 */
struct pgrp *
alloc_pgrp(void)
{
	struct pgrp *pg;
	uint len;

	pg = malloc(sizeof(struct pgrp));
	pg->pg_nmember = 0;
	len = PG_GROWTH * sizeof(ulong);
	pg->pg_members = malloc(len);
	bzero(pg->pg_members, len);
	pg->pg_nelem = PG_GROWTH;
	init_sema(&pg->pg_sema);
	return(pg);
}
