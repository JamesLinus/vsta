/*
 * ps.c
 *	Poor man's ps(1)
 */
#include <sys/pstat.h>
#include <std.h>

/*
 * Container for a pstat entry so we can keep them
 * in sorted order.
 */
struct plist {
	struct pstat_proc pl_pstat;
	struct plist *pl_next;
};

/*
 * statename()
 *	Convert process state to (roughly) string name of CPU state
 *
 * Doomed to failure, since one can have multiple threads in multiple
 * states, but works for "classic" single-threaded procs.
 */
static char *
statename(struct pstat_proc *p)
{
	if (p->psp_nonproc > 0) {
		return("ONPROC");
	} else if (p->psp_nrun > 0) {
		return("RUN   ");
	} else {
		return("SLP   ");
	}
}

/*
 * printps()
 *	Dump out a process entry
 */
static void
printps(struct pstat_proc *p)
{
	printf("%6d %-8s %s %7d %d/%d\n", p->psp_pid, p->psp_cmd,
		statename(p), p->psp_nthread,
		p->psp_usrcpu, p->psp_syscpu);
}

main(int argc, char **argv)
{
	struct pstat_proc ps;
	pid_t pids[NPROC*4];
	int x, npid;
	struct plist *phead = 0, *p, **pp, *pnew;

	npid = pstat(PSTAT_PROCLIST, 0, pids, sizeof(pids));
	if (npid < 0) {
		perror(argv[0]);
		exit(1);
	}
	for (x = 0; x < npid; ++x) {
		/*
		 * Get next slot
		 */
		if (pstat(PSTAT_PROC, pids[x], &ps, sizeof(ps)) < 0) {
			continue;
		}

		/*
		 * Add pstat entry in its place
		 */
		pnew = malloc(sizeof(struct plist));
		if (pnew == 0) {
			perror(argv[0]);
			exit(1);
		}
		pnew->pl_pstat = ps;
		pp = &phead;
		for (p = phead; p; p = p->pl_next) {
			if (p->pl_pstat.psp_pid > ps.psp_pid) {
				break;
			}
			pp = &p->pl_next;
		}
		pnew->pl_next = *pp;
		*pp = pnew;
	}

	/*
	 * Print them
	 */
	printf("PID    CMD      STATE  NTHREAD USR/SYS\n");
	for (p = phead; p; p = p->pl_next) {
		printps(&p->pl_pstat);
	}
	return(0);
}
