/*
 * rlimit.c
 *	Implement get/set rlimit stuff
 *
 * XXX for now, just a stub
 */
#include <resource.h>
#include <sys/fs.h>		/* For error names */
#include <sys/pstat.h>
#include <time.h>

int
getpriority(int which, int who)
{
	return(0);
}

/*
 * getrlimit()
 *	Get description of resource consumption limits
 */
int
getrlimit(int which, struct rlimit *r)
{
	bzero(r, sizeof(*r));
	return(0);
}

/*
 * getrusage()
 *	Get description of CPU consumption
 */
int
getrusage(int which, struct rusage *r)
{
	struct pstat_proc ps;

	/*
	 * Most fields are not supported
	 */
	bzero(r, sizeof(*r));

	/*
	 * Pull our user and system CPU times from pstat()
	 */
	if (pstat(PSTAT_PROC, getpid(), &ps, sizeof(ps)) < 0) {
		return(-1);
	}

	/*
	 * Adapt units
	 * TBD: our CPU versus children's.  Probably going to have
	 *  to change kernel accounting, and then the pstat API.
	 */
	r->ru_utime.tv_sec = ps.psp_usrcpu / HZ;
	r->ru_utime.tv_usec = (ps.psp_usrcpu % HZ) * (1000000 / HZ);
	r->ru_stime.tv_sec = ps.psp_syscpu / HZ;
	r->ru_stime.tv_usec = (ps.psp_syscpu % HZ) * (1000000 / HZ);

	return(0);
}

/*
 * setpriority()
 *	Set process [group] CPU priority
 */
int
setpriority(int which, int who, int prio)
{
	return(__seterr(ENOTSUP));
}

/*
 * nice()
 *	Adjust priority relative to current value
 */
int
nice(int incr)
{
	return(__seterr(ENOTSUP));
}

/*
 * setrlimit()
 *	Control resource consumption of a process
 */
int
setrlimit(int which, const struct rlimit *r)
{
	return(__seterr(ENOTSUP));
}

/*
 * clock()
 *	Tell amount of CPU time since program started
 */
clock_t
clock(void)
{
	struct rusage r;

	/*
	 * Extract our CPU usage
	 */
	if (getrusage(RUSAGE_SELF, &r) < 0) {
		return(-1);
	}
	return ((r.ru_utime.tv_sec * CLOCKS_PER_SEC) +
		((r.ru_utime.tv_usec * CLOCKS_PER_SEC) / 1000000));
}
