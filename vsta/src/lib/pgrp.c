/*
 * pgrp.c
 *	Stuff to map POSIX type of process groups onto VSTa
 *
 * The basic difference is that a process group in VSTa is accessed
 * by directing a signal towards a particular PID, and saying "everything
 * in his process group".  POSIX expects a another namespace for process
 * group ID's.  We fake it.
 */
#include <std.h>

/*
 * getpgrp()
 *	Return process group ID of this process
 *
 * Just return our own PID; it'll reach the right people if you
 * signal it as a process group.
 */
pid_t
getpgrp(void)
{
	return(getpid());
}
