/*
 * pstat.c
 *	pstat() system call for looking at process status
 *
 * The big problem with pstat() is how allow a process to walk
 * along the list of processes when you don't know how long the
 * list even is.  The solution used here is to move the current
 * process to a new list position right after the last element
 * returned.  Thus, our own process' position in the allprocs
 * list indicates who should be dumped next.
 *
 * The current process is returned when we reach the end of the
 * list and wrap back to the beginning.
 */
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/pstat.h>
#include <sys/percpu.h>
#include <sys/assert.h>

#ifdef PSTAT

extern sema_t pid_sema;

/*
 * pstat()
 *	System call handler
 */
int
pstat(struct pstat *psp, uint npst, uint pst_size)
{
	struct proc *p, *thisproc = curthread->t_proc;
	struct thread *t;
	struct pstat ps;
	pid_t startpid = 0;
	uint count = 0, size;

	ASSERT_DEBUG(sizeof(ps.ps_cmd) <= sizeof(p->p_cmd),
		"pstat: p_cmd too small");
	if (pst_size > sizeof(struct pstat)) {
		pst_size = sizeof(struct pstat);
	}

	/*
	 * Fetch next process.  Hold PID semaphore, gather data
	 * on a single process, move us past him, and release.
	 * Iterate until we have the requested amount, or have
	 * wrapped back to the process we started with.
	 */
	do {
		/*
		 * Get next
		 */
		p_sema(&pid_sema, PRILO);
		p = thisproc->p_allnext;

		/*
		 * Record process-level goodies
		 */
		ps.ps_pid = p->p_pid;
		bcopy(p->p_cmd, ps.ps_cmd, sizeof(ps.ps_cmd));
		ps.ps_usrcpu = p->p_usr;
		ps.ps_syscpu = p->p_sys;

		/*
		 * Lock process, and scan threads
		 */
		ps.ps_nthread = ps.ps_nsleep = ps.ps_nrun =
			ps.ps_nonproc = 0;
		p_sema(&p->p_sema, PRIHI);
		for (t = p->p_threads; t; t = t->t_next) {
			/*
			 * Tally # of threads, accumulate each
			 * thread's CPU usage
			 */
			ps.ps_nthread += 1;
			ps.ps_usrcpu += t->t_usrcpu;
			ps.ps_syscpu += t->t_syscpu;

			/*
			 * Tally # of threads in each state
			 */
			switch (t->t_state) {
			case TS_SLEEP:
				ps.ps_nsleep += 1;
				break;
			case TS_RUN:
				ps.ps_nrun += 1;
				break;
			case TS_ONPROC:
				ps.ps_nonproc += 1;
				break;
			}
		}
		v_sema(&p->p_sema);

		/*
		 * Move our own process' position to reflect
		 * who we just scanned.  PF_MOVED is a hint to
		 * other users of pstat() that this process
		 * moves around.
		 */
		thisproc->p_allprev->p_allnext = thisproc->p_allnext;
		thisproc->p_allnext->p_allprev = thisproc->p_allprev;
		thisproc->p_allnext = p->p_allnext;
		thisproc->p_allprev = p;
		thisproc->p_flags |= PF_MOVED;
		p->p_allnext->p_allprev = thisproc;
		p->p_allnext = thisproc;
		v_sema(&pid_sema);

		/*
		 * Record pid
		 */
		if (startpid == 0) {
			startpid = ps.ps_pid;
		}

		/*
		 * Copy out, advance to next
		 */
		if (copyout(psp, &ps, pst_size) < 0) {
			return(-1);
		}
		if (++count >= npst) {
			break;
		}
		psp = (struct pstat *)((char *)psp + pst_size);
	} while (ps.ps_pid != startpid);
	return(count);
}

#endif /* PSTAT */
