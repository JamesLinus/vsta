/*
 * rlimit.c
 *	Implement get/set rlimit stuff
 *
 * XXX for now, just a stub
 */
#include <resource.h>
#include <sys/fs.h>

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
	bzero(r, sizeof(*r));
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
 * setrlimit()
 *	Control resource consumption of a process
 */
int
setrlimit(int which, const struct rlimit *r)
{
	return(__seterr(ENOTSUP));
}
