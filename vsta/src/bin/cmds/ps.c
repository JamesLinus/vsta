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
 * timestr()
 *	Display time in human-friendly ranges
 *
 * Returned string is in malloc()'ed memory, which we leak.  It shouldn't
 * add up to enough to cause problems in any given ps(1) run (famous last
 * words).
 */
static char *
timestr(uint tm)
{
	char *buf = malloc(16);

	if (tm < 60) {
		sprintf(buf, "%ds", tm);
	} else if (tm < 60*60) {
		sprintf(buf, "%dm", tm / 60);
	} else if (tm < 24*60*60) {
		sprintf(buf, "%dh%dm", tm/(60*60), (tm % (60*60)) / 60);
	} else {
		sprintf(buf, "%dd%dh", tm/(24*60*60),
			(tm % (24*60*60)) / 60*60);
	}
	return(buf);
}

/*
 * printps()
 *	Dump out a process entry
 */
static void
printps(struct pstat_proc *p)
{
	struct prot *prot;
	extern char *cvt_id();

	prot = &p->psp_prot;
	printf("%-6d %-8s %s %4d    %6s/%-6s %-16s\n", p->psp_pid, p->psp_cmd,
		statename(p), p->psp_nthread,
		timestr(p->psp_usrcpu), timestr(p->psp_syscpu),
		cvt_id(prot->prot_id, prot->prot_len));
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
	printf("PID    CMD      STATE  NTHREAD    USR/SYS    ID\n");
	for (p = phead; p; p = p->pl_next) {
		printps(&p->pl_pstat);
	}
	return(0);
}
