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
	strcpy(is_sys ? t->t_evsys : t->t_evproc, event);

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
 * notify()
 *	Send a process event somewhere
 *
 * If arg_proc is 0, it means the current process.  If arg_thread is
 * 0, it means all threads under the named proc.
 */
notify(ulong arg_proc, ulong arg_thread, char *arg_msg, int arg_msglen)
{
	struct proc *p;
	char evname[EVLEN];
	int x, error = 0;
	struct thread *t;

	printf("Notify\n"); dbg_enter();
	/*
	 * Get the string event name
	 */
	if (get_ustr(evname, sizeof(evname), arg_msg, arg_msglen)) {
		return(-1);
	}

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
		sendev(t, t->t_evsys);
		return;
	}
	if (t->t_evproc[0]) {
		strcpy(event, t->t_evsys);
		t->t_evproc[0] = '\0';
		v_lock(&runq_lock, s);
		sendev(t, t->t_evproc);
		v_sema(&t->t_evq);
		return;
	}
	v_lock(&runq_lock, s);
}
