#ifndef _PROC_H
#define _PROC_H
/*
 * proc.h
 *	Per-process data
 */
#include <sys/perm.h>
#include <sys/param.h>
#include <lib/llist.h>
#include <sys/mutex.h>

/*
 * Permission bits for current process
 */
#define P_PRIO 1	/* Can set process priority */
#define P_SIG 2		/*  ... deliver events */
#define P_KILL 4	/*  ... terminate */
#define P_STAT 8	/*  ... query status */
#define P_DEBUG 16	/*  ... debug */

/*
 * The actual per-process state
 */
struct proc {
	ulong p_pid;		/* Our process ID */
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
};

#ifdef KERNEL
extern ulong allocpid(void);
extern int alloc_open(struct proc *);
extern void free_open(struct proc *, int);
#endif

#endif /* _PROC_H */
