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

/*
 * Number of ticks allowed to run before having to go back into scheduler
 */
#define RUN_TICKS (HZ / 4)

/*
 * "cheated" if relinquish with more than this
 */
#define CHEAT_TICKS ((RUN_TICKS + 1) / 2)

/*
 * Mask of bits for prio
 */
#define PRIO_MASK (0x7F)

/*
 * Default numeric priority for a node
 */
#define PRIO_DEFAULT (50)

/*
 * Values for "priority"; it only differentiates among classes
 */
#define PRI_IDLE 0
#define PRI_BG 1
#define PRI_TIMESHARE 2
#define PRI_CHEATED 3
#define PRI_RT 4

/*
 * Scheduler operations for use with sched_op()
 */
#define SCHEDOP_SETPRIO 0
#define SCHEDOP_GETPRIO 1
#define SCHEDOP_YIELD 2
#define SCHEDOP_EPHEM 3
#define SCHEDOP_PROFILE 4

#ifdef KERNEL

/*
 * Scheduler node structure.
 */
struct sched {
	union {
		struct sched	/* For nodes, nodes below */
			*_s_down;
		struct thread	/* For leaf, the thread */
			*_s_thread;
	} s_u;
#define s_thread s_u._s_thread
#define s_down s_u._s_down
	struct sched *s_up;	/* Our parent node */
	struct sched		/* For internal node, first node below us */
		*s_hd, *s_tl;	/*  ...for leaf, forward and back pointers */
	uint s_prio;		/* This node's priority */
		uint s_nrun;		/* # processes runnable below this node */
	uint s_leaf;		/* Internal node or leaf? */
	uint s_refs;		/* # references to this node */
};

extern struct sched *sched_thread(struct sched *, struct thread *),
	*sched_node(struct sched *);
extern void setrun( /* struct thread * */ ), swtch(void);
extern void free_sched_node(struct sched *);

extern uint num_run;
#endif

extern int sched_op(int, int);

#endif /* _SCHED_H */
