#ifndef _PROC_H
#define _PROC_H
/*
 * proc.h
 *	Per-process data
 */
#define PROC_DEBUG /* Define for process debugging support */

#include <sys/perm.h>
#include <sys/param.h>
#include <llist.h>
#include <sys/mutex.h>
#include <sys/wait.h>
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

/* Bits in pd_flags */
#define PD_ALWAYS 1		/* Stop at next chance */
#define PD_SYSCALL 2		/*  ...before & after syscalls */
#define PD_EVENT 4		/*  ...when receiving an event */
#define PD_EXIT 8		/*  ...when exiting */
#define PD_BPOINT 16		/*  ...breakpoint reached */
#define PD_EXEC 32		/*  ...exec done (new addr space) */
#define PD_CONNECTING 0x8000	/* Slave has connect in progress */

/*
 * Values for m_op of a debug message.  The sense of the operation
 * is further defined by m_arg and m_arg1.
 */
#define PD_SLAVE 300		/* Slave ready for commands */
#define PD_RUN 301		/* Run */
#define PD_STEP 302		/* Run one instruction, then break */
#define PD_BREAK 303		/* Set/clear breakpoint */
#define PD_RDREG 304		/* Read registers */
#define PD_WRREG 305		/*  ...write */
#define PD_MASK 306		/* Set mask */
#define PD_RDMEM 307		/* Read memory */
#define PD_WRMEM 308		/*  ...write */
#define PD_MEVENT 309		/* Read/write event */

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
	struct perm		/* Permissions granted process */
		p_ids[PROCPERMS];
	sema_t p_sema;		/* Semaphore on proc structure */
	struct prot p_prot;	/* Protections of this process */
	struct thread		/* List of threads in this process */
		*p_threads;
	struct vas *p_vas;	/* Virtual address space of process */
	struct sched *p_runq;	/* Scheduling node for all threads */
	struct port		/* Ports this proc owns */
		*p_ports[PROCPORTS];
	struct portref		/* "files" open by this process */
		*p_open[PROCOPENS];
	struct hash		/* Portrefs attached to our ports */
		*p_prefs;
	ushort p_nopen;		/*  ...# currently open */
	struct proc		/* Linked list of all processes */
		*p_allnext;
	ulong p_sys, p_usr;	/* Cumulative time for all prev threads */
	voidfun p_handler;	/* Handler for events */
	struct pgrp		/* Process group for proc */
		*p_pgrp;
	struct exitgrp		/* Exit groups */
		*p_children,	/*  ...the one our children use */
		*p_parent;	/*  ...the one we're a child of */
	char p_cmd[8];		/* Command name (untrusted) */
	char p_event[ERRLEN];	/* Event which killed us */
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
