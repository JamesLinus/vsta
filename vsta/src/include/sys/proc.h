#ifndef _PROC_H
#define _PROC_H
/*
 * proc.h
 *	Per-process data
 */
#define PROC_DEBUG	/* Define for process debugging support */
#define PSTAT		/*  ...for process status query */

#include <sys/perm.h>
#include <sys/param.h>
#include <llist.h>
#include <sys/mutex.h>
#include <sys/wait.h>
#include <sys/vas.h>
#include <mach/machreg.h>

/*
 * Permission bits for current process
 */
#define P_PRIO 1	/* Can set process priority */
#define P_SIG 2		/*  ... deliver events */
#define P_KILL 4	/*  ... terminate */
#define P_STAT 8	/*  ... query status */
#define P_DEBUG 16	/*  ... debug */

/*
 * Description of a process group
 */
struct pgrp {
	uint pg_nmember;	/* # PID's in group */
	uint pg_nelem;		/* # PID's in pg_members[] array */
	ulong *pg_members;	/* Pointer to linear array */
	sema_t pg_sema;		/* Mutex for pgrp */
};
#define PG_GROWTH (20)		/* # slots pg_members[] grows by */

#ifdef PROC_DEBUG
/*
 * Communication area between debugger and slave
 */
struct pdbg {
	port_t pd_port;		/* Debugger serves this port */
	port_name pd_name;	/*  ...portname for port */
	uint pd_flags;		/* See below */
};

/*
 * We have this in its own file so debuggers don't have to hear
 * about all our internal data structures.
 */
#include <sys/ptrace.h>

/*
 * Handy macro for checking if we should drop into debug slave
 * mode.
 */
extern void ptrace_slave(char *, uint);

#define PTRACE_PENDING(p, fl, ev) \
	if ((p)->p_dbg.pd_flags & ((fl) | PD_ALWAYS)) { \
		ptrace_slave(ev, (fl) & (p)->p_dbg.pd_flags); \
	}

#else

/*
 * Just stub this for non-debugging kernel
 */
#define PTRACE_PENDING(p, fl, ev)

#endif /* PROC_DEBUG */

/*
 * The actual per-process state
 */
struct proc {
	pid_t p_pid;		/* Our process ID */
	sema_t p_sema;		/* Semaphore on proc structure */
	struct vas p_vas;	/* Virtual address space of process */
	struct thread		/* List of threads in this process */
		*p_threads;
	uint p_nthread;		/*  ...# of non-ephemeral threads */
	struct sched *p_runq;	/* Scheduling node for all threads */
	struct hash		/* Portrefs attached to our ports */
		*p_prefs;
	ulong p_nopen;		/*  ...# currently open */
	struct proc		/* Linked list of all processes */
		*p_allnext, *p_allprev;
	ulong p_sys, p_usr;	/* Cumulative time for all prev threads */
	voidfun p_handler;	/* Handler for events */
	struct pgrp		/* Process group for proc */
		*p_pgrp;
	struct exitgrp		/* Exit groups */
		*p_children,	/*  ...the one our children use */
		*p_parent;	/*  ...the one we're a child of */
	char p_cmd[8];		/* Command name (untrusted) */
	char p_event[ERRLEN];	/* Event which killed us */
	struct prot p_prot;	/* Protections of this process */
	struct perm		/* Permissions granted process */
		p_ids[PROCPERMS];
	struct port		/* Ports this proc owns */
		*p_ports[PROCPORTS];
	struct portref		/* "files" open by this process */
		*p_open[PROCOPENS];
#ifdef PROC_DEBUG
	struct pdbg p_dbg;	/* Who's debugging us (if anybody) */
	struct dbg_regs		/* Debug register state */
		p_dbgr;		/*  valid if T_DEBUG active */
#endif
};

#ifdef KERNEL
extern pid_t allocpid(void);
extern int alloc_open(struct proc *);
extern void free_open(struct proc *, int);
extern void join_pgrp(struct pgrp *, pid_t),
	leave_pgrp(struct pgrp *, pid_t);
extern struct pgrp *alloc_pgrp(void);

/*
 * notify() syscall and special value for thread ID to signal whole
 * process group instead.
 */
extern notify(pid_t, pid_t, char *, int);
#define NOTIFY_PG ((pid_t)-1)

#endif /* KERNEL */

#endif /* _PROC_H */
