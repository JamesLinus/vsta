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
#include <sys/fs.h>
#include <sys/malloc.h>
#include <hash.h>
#include <sys/assert.h>
#include "../mach/mutex.h"

extern lock_t runq_lock;
extern struct hash *pid_hash;
extern struct proc *pfind();

/*
 * signal_thread()
 *	Send an event to a thread
 *
 * The process mutex is held by the caller.
 */
static int
signal_thread(struct thread *t, char *event, int is_sys)
{
	extern void nudge(), lsetrun();

	if (!is_sys) {
		if (cp_sema(&t->t_evq)) {
			return(err(EAGAIN));
		}
	}

	/*
	 * Take lock, place event in appropriate place
	 */
retry:	p_lock_void(&runq_lock, SPLHI);
	strcpy((is_sys ? t->t_evsys : t->t_evproc), event);

	/*
	 * As appropriate, kick him awake
	 */
	switch (t->t_state) {
	case TS_SLEEP:		/* Interrupt sleep */
		if (t->t_nointr == 0) {
			if (cunsleep(t)) {
				v_lock(&runq_lock, SPL0);
				goto retry;
			}
			lsetrun(t);
		}
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
	return(0);
}

/*
 * notifypg()
 *	Send an event to a process group
 */
static int
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
	l = MALLOC(pg->pg_nmember * sizeof(ulong), MT_PGRP);
	lp = pg->pg_members;
	for (x = 0; x < nelem; ++x) {
		while (*lp == 0)
			++lp;
		l[x] = *lp++;
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
	FREE(l, MT_PGRP);
	return(0);
}

/*
 * notify2()
 *	Most of the work for doing an event notification
 *
 * If arg_proc is 0, it means the current process.  If arg_thread is
 * 0, it means all threads under the named proc.
 */
int
notify2(pid_t arg_proc, pid_t arg_thread, char *evname)
{
	struct proc *p;
	int x, error = 0;
	struct thread *t;

	if (arg_proc == 0) {
		/*
		 * Our process isn't too hard
		 */
		p = curthread->t_proc;
		if (p_sema(&p->p_sema, PRICATCH)) {
			return(err(EINTR));
		}
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
int
notify(pid_t arg_proc, pid_t arg_thread, char *arg_msg, int arg_msglen)
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
 *	Handle any events that may be pending.
 *
 * We *know* we have a current thread!
 */
void
check_events(void)
{
	struct thread *t = curthread;
	spl_t s;
	char event[EVLEN];
	extern void sendev();

	ASSERT_DEBUG(t, "check_events: no thread");
	ASSERT_DEBUG(EVENT(t), "check_events: no events");

	/*
	 * Take next events, act on them
	 */
	s = p_lock(&runq_lock, SPLHI);
	if (t->t_evsys[0]) {
		strcpy(event, t->t_evsys);
		t->t_evsys[0] = '\0';
		v_lock(&runq_lock, s);
		PTRACE_PENDING(t->t_proc, PD_EVENT, event);
		if (event[0])
			sendev(t, event);
		return;
	}
	if (t->t_evproc[0]) {
		strcpy(event, t->t_evproc);
		t->t_evproc[0] = '\0';
		v_lock(&runq_lock, s);
		PTRACE_PENDING(t->t_proc, PD_EVENT, event);
		if (event[0])
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
join_pgrp(struct pgrp *pg, pid_t pid)
{
	void *e;
	ulong *lp;

	p_sema(&pg->pg_sema, PRIHI);

	/*
	 * If there's no more room now for members, grow the list
	 */
	if (pg->pg_nmember >= pg->pg_nelem) {
		e = MALLOC((pg->pg_nelem + PG_GROWTH) * sizeof(ulong),
			MT_PGRP);
		bcopy(pg->pg_members, e, pg->pg_nelem * sizeof(ulong));
		FREE(pg->pg_members, MT_PGRP);
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
leave_pgrp(struct pgrp *pg, pid_t pid)
{
	ulong *lp;

	p_sema(&pg->pg_sema, PRIHI);

	/*
	 * If we're the last, throw it out
	 */
	if (pg->pg_nmember == 1) {
		FREE(pg->pg_members, MT_PGRP);
		FREE(pg, MT_PGRP);
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

	pg = MALLOC(sizeof(struct pgrp), MT_PGRP);
	pg->pg_nmember = 0;
	len = PG_GROWTH * sizeof(ulong);
	pg->pg_members = MALLOC(len, MT_PGRP);
	bzero(pg->pg_members, len);
	pg->pg_nelem = PG_GROWTH;
	init_sema(&pg->pg_sema);
	return(pg);
}

/*
 * notify_handler()
 *	Register handler for notify() events
 */
int
notify_handler(voidfun handler)
{
	curthread->t_proc->p_handler = handler;
	return(0);
}
