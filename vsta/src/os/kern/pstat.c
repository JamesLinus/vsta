/*
 * pstat.c
 *	pstat() system call for looking at kernel status info
 */
#include <sys/proc.h>
#ifdef PSTAT
#include <sys/thread.h>
#include <sys/pstat.h>
#include <sys/percpu.h>
#include <sys/assert.h>
#include <sys/fs.h>
#include "../mach/locore.h"

extern sema_t pid_sema;
extern struct proc *allprocs;
extern uint size_base, size_ext;
extern uint freemem;

extern struct proc *pfind();
extern void uptime();

/*
 * get_pstat_proclist()
 *	Get a list of the requesting user's processes.
 *
 * Returns the number of processes in the list or an error code (-1)
 */
int
get_pstat_proclist(void *pst_info, uint pst_size)
{
	struct perm *perms = curthread->t_proc->p_ids;
	struct proc *p = allprocs;
	pid_t *pid_list = (pid_t *)pst_info;
	int max_ids;
	int x, i = 0;

	/*
	 * How many IDs can we return?
	 */
	max_ids = pst_size / sizeof(pid_t);

	/*
	 * Do this work with the pid_sema held
	 */
	if (p_sema(&pid_sema, PRICATCH)) {
		return(err(EINTR));
	}

	do {
		p_sema(&p->p_sema, PRIHI);

		/*
		 * Check whether we're allowed to see this process
		 */
		x = perm_calc(perms, PROCPERMS, &p->p_prot);
		if (x & P_STAT) {
			copyout(&pid_list[i++], &p->p_pid, sizeof(pid_t));
		}

		/*
		 * Step on to the next process in the list
 		 */
		v_sema(&p->p_sema);
		p = p->p_allnext;
	} while ((p != allprocs) && (i < max_ids));

	v_sema(&pid_sema);
	return(i);
}

/*
 * get_pstat_proc()
 *	Get details of a specific process
 */
int
get_pstat_proc(uint pid, void *pst_info, uint pst_size)
{
	struct pstat_proc psp;
	struct proc *p;
	struct thread *t;
	int x;
	
	if (pst_size > sizeof(struct pstat_proc)) {
		pst_size = sizeof(struct pstat_proc);
	}

	p = pfind((pid_t)pid);
	if (!p) {
		return(err(ESRCH));
	}

	/*
	 * Check whether we're allowed to see into this process
	 */
	x = perm_calc(curthread->t_proc->p_ids, PROCPERMS, &p->p_prot);
	if (!(x & P_STAT)) {
		v_sema(&p->p_sema);
		return(err(EPERM));
	}

	/*
	 * Record process-level goodies
	 */
	psp.psp_pid = p->p_pid;
	bcopy(p->p_cmd, psp.psp_cmd, sizeof(psp.psp_cmd));
	psp.psp_usrcpu = p->p_usr;
	psp.psp_syscpu = p->p_sys;
	psp.psp_prot = p->p_prot;

	/*
	 * Scan threads
	 */
	psp.psp_nthread = psp.psp_nsleep = psp.psp_nrun =
		psp.psp_nonproc = 0;
	for (t = p->p_threads; t; t = t->t_next) {
		/*
		 * Tally the number of threads, accumulate each
		 * thread's CPU usage
		 */
		psp.psp_nthread += 1;
		psp.psp_usrcpu += t->t_usrcpu;
		psp.psp_syscpu += t->t_syscpu;
		/*
		 * Tally # of threads in each state
		 */
		switch (t->t_state) {
		case TS_SLEEP:
			psp.psp_nsleep += 1;
			break;
		case TS_RUN:
			psp.psp_nrun += 1;
			break;
		case TS_ONPROC:
			psp.psp_nonproc += 1;
			break;
		}
	}
	v_sema(&p->p_sema);

	/*
	 * Copy out the process status info
	 */
	return(copyout((struct pstat_proc *)pst_info, &psp, pst_size));
}

/*
 * get_pstat_kernel()
 *	Get system/kernel characteristic details
 *
 * We are passed a size parameter for the return information to allow us
 * to maintain binary compatibility in the future
 */
static int
get_pstat_kernel(void *pst_info, uint pst_size)
{
	struct pstat_kernel psk;
	
	if (pst_size > sizeof(struct pstat_kernel)) {
		pst_size = sizeof(struct pstat_kernel);
	}

	/*
	 * Fill our output structure with useful info
	 */
	psk.psk_memory = size_base + size_ext;
	psk.psk_ncpu = ncpu;
	psk.psk_freemem = freemem * NBPG;
	uptime(&psk.psk_uptime);
	psk.psk_runnable = num_run;

	/*
	 * Give the info back to the user
	 */
	return(copyout((struct pstat_kernel *)pst_info, &psk, pst_size));
}

/*
 * pstat()
 *	System call handler
 */
int
pstat(uint ps_type, uint ps_arg, void *ps_info, uint ps_size)
{
	/*
	 * Decide what information we've been asked for
	 */
	switch(ps_type) {
	case PSTAT_PROC:
		return(get_pstat_proc(ps_arg, ps_info, ps_size));
	case PSTAT_PROCLIST:
		return(get_pstat_proclist(ps_info, ps_size));
	case PSTAT_KERNEL:
		return(get_pstat_kernel(ps_info, ps_size));
	default:
		/*
		 * We don't understand what we've been asked for
		 */
		return(err(EINVAL));
	}
}

#endif /* PSTAT */
