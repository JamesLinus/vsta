#ifndef _WAIT_H
#define _WAIT_H
/*
 * wait.h
 *	Process exit coordination
 */
#include <sys/types.h>

/*
 * Status a child leaves behind on exit()
 */
struct exitst {
	ulong e_pid;		/* PID of exit() */
	int e_code;		/* Argument to exit() */
	ulong e_usr, e_sys;	/* CPU time in user and sys */
	struct exitst *e_next;	/* Next in list */
};

#ifdef KERNEL
#include <sys/mutex.h>

/*
 * An exit group.  All children of the same parent belong to the
 * same exit group.  When children exit, they will leave an exit
 * status message linked here if the parent is still alive.
 */
struct exitgrp {
	struct proc *e_parent;	/* Pointer to parent of group */
	sema_t e_sema;		/* Sema bumped on each exit */
	struct exitst *e_stat;	/* Status of each exit() */
	ulong e_refs;		/* # references (parent + children) */
	lock_t e_lock;		/* Mutex for fiddling */
};

/*
 * Functions for fiddling exit groups
 */
extern struct exitgrp *alloc_exitgrp(struct proc *);
extern void deref_exitgrp(struct exitgrp *);
extern void noparent_exitgrp(struct exitgrp *);
extern void post_exitgrp(struct exitgrp *, struct proc *, int);
extern struct exitst *wait_exitgrp(struct exitgrp *);

#endif /* KERNEL */

#endif /* _WAIT_H */
