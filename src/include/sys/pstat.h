#ifndef _PSTAT_H
#define _PSTAT_H
/*
 * pstat.h
 *	Definitions for pstat() process status query syscall
 */
#include <sys/types.h>
#include <sys/sched.h>
#include <sys/perm.h>
#include <sys/syscall.h>
#include <mach/trap.h>
#include <mach/isr.h>

/*
 * process status struct
 */
struct pstat_proc {
	ulong psp_pid;		/* PID of process */
	char psp_cmd[8];	/* Command name */
	uint psp_nthread;	/* Number of threads under process */
	uint psp_nsleep;	/* Number of threads sleeping */
	uint psp_nrun;		/*  ...runnable */
	uint psp_nonproc;	/*  ...on a CPU */
	ulong psp_usrcpu;	/* Total CPU time used in user mode */
	ulong psp_syscpu;	/*  ...in kernel */
	struct prot psp_prot;	/* Process protection info */
	uchar psp_moves;	/* Does this process move in the list? */
};

/*
 * System type identification string
 */
#define PS_SYSID 16

/*
 * configuration/kernel status struct
 */
struct pstat_kernel {
	ulong psk_memory;	/* Size of system RAM in bytes */
	uint psk_ncpu;		/* Number of CPUs */
	ulong psk_freemem;	/* Bytes of free memory */
	struct time psk_uptime;	/* How long has the system been up? */
	uint psk_runnable;	/* Number of runnable threads */
	uint psk_hz;		/* Clock ticks/second */
};

/*
 * pstat status request types
 */
#define PSTAT_PROC 0
#define PSTAT_PROCLIST 1
#define PSTAT_KERNEL 2

/*
 * pstat()
 *	Get status information out of the kernel
 *
 * You pass it a status type, two arguments, a pointer to a status
 * structure and the sizeof() the struct.  This last allows the struct
 * to grow but still be binary compatible with old executables.
 */
extern int pstat(uint, uint, void *, uint);

#endif /* _PSTAT_H */
