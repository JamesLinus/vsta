#ifndef _SCHED_H
#define _SCHED_H
/*
 * sched.h
 *	Data structures representing the scheduling tree
 *
 * CPU time is parceled out via a "tree" distribution.  Each
 * node in the tree competes at its level based upon its priority.
 * Once it has "won" some CPU time, it parcels this time down
 * to its members.  Leafs are threads, also possessing a priority.
 * The intent is that the CPU time is evenly distributed based on
 * the relative fractions of each candidate.  Thus, two processes
 * with a priority value of 20 each would get 50/50 CPU time.  A
 * 5:20 ratio would give the first 1/5 of the time, and the other
 * 4/5 of the time.
 */
#include <sys/param.h>
#include <sys/types.h>
#include <llist.h>

/* Number of ticks allowed to run before having to go back into scheduler */
#define RUN_TICKS (6)

/* "cheated" if relinquish with more than this */
#define CHEAT_TICKS (RUN_TICKS / 2)

/* Mask of bits for prio */
#define PRIO_MASK (0x7F)

/* Default numeric priority for a node */
#define PRIO_DEFAULT (50)

/* Values for "priority"; it only differentiates among classes */
#define PRI_IDLE 0
#define PRI_BG 1
#define PRI_TIMESHARE 2
#define PRI_CHEATED 3
#define PRI_RT 4

struct sched {
	struct sched *s_up;	/* Our parent node */
	struct sched		/* For internal node, first node below us */
		*s_hd, *s_tl;	/*  ...for leaf, forward and back pointers */
	union {
		struct thread		/* For leaf, the thread */
			*_s_thread;
		struct sched		/* For nodes, nodes below */
			*_s_down;
	} s_u;
#define s_thread s_u._s_thread
#define s_down s_u._s_down
	uint s_refs;		/* # references to this node */
	uchar s_prio;		/* This node's priority */
	uchar s_leaf;		/* Internal node or leaf? */
	ushort s_nrun;		/* # processes runnable below this node */
};

#ifdef KERNEL
extern struct sched *sched_thread(struct sched *, struct thread *),
	*sched_node(struct sched *);
extern void setrun( /* struct thread * */ ), swtch(void);
extern void free_sched_node(struct sched *);
#endif

#endif /* _SCHED_H */
