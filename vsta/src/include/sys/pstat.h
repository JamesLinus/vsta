#ifndef _PSTAT_H
#define _PSTAT_H
/*
 * pstat.h
 *	Definitions for pstat() process status query syscall
 */
#include <sys/types.h>

/*
 * pstat struct
 */
struct pstat {
	ulong ps_pid;		/* PID of process */
	char ps_cmd[8];		/* Command name */
	uint ps_nthread;	/* # threads under process */
	uint ps_nsleep,		/* # threads sleeping */
		ps_nrun,	/*  ...runnable */
		ps_nonproc;	/*  ...on a CPU */
	ulong ps_usrcpu,	/* Total CPU time used in user mode */
		ps_syscpu;	/*  ...in kernel */
};

/*
 * pstat()
 *	Get process status
 *
 * You pass it a pstat struct, a count of such structs, and the sizeof()
 * the struct.  This last allows the struct to grow but still be binary
 * compatible with old executables.
 */
extern int pstat(struct pstat *, uint, uint);

#endif /* _PSTAT_H */
