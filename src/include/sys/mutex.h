#ifndef _MUTEX_H
#define _MUTEX_H
/*
 * mutex.h
 *	Both sleep- and spin-oriented mutual exclusion mechanisms
 *
 * VSTa mutual exclusion comes in two basic flavors: spinlocks (lock_t)
 * and semaphores (sema_t).
 *
 * Spinlocks
 *
 * When a spinlock is taken interrupts may be
 * blocked (to protect resources shared between interrupt handlers and
 * non-interrupt code) or left unblocked (for resources accessed only
 * from non-interrupt code, but accessible from more than one thread/
 * CPU at a time).
 *
 * Spinlocks may nest, although nesting an SPL0 lock while holding an SPLHI
 * one will cause a panic.  All locks must be released before the CPU is
 * relinquished.
 *
 * Semaphores
 *
 * Semaphores are the only mechanism for sleeping.  When sleeping on
 * a semaphore, PRIHI will inhibit events from breaking the semaphore,
 * and PRICATCH will allow events which show up as a non-zero completion
 * of the p_sema operation.  PRILO caused the system call to return
 * with EINTR (p_sema never returns, a longjmp happens instead), but
 * has been discarded in favor of explicit PRICATCH with an EINTR return
 * path.  Not only does it solve some problems with using shared
 * code paths between kernel and user clients; it also spares us having
 * to dump all the machine state into a jmp_buf for each and every
 * system call.
 *
 * You may transition from a lock to a semaphore using p_sema_v_lock.
 * There is no way to transition from one semaphore to another atomically.
 */
#include <sys/types.h>

/*
 * A spinlock
 */
typedef struct lock {
	uchar l_lock;
} lock_t;

/*
 *  A sleeping semaphore
 *
 * The s_count can be viewed as a count of the number of p_sema()'s
 * which would go through before a p_sema() would be blocked.
 */
typedef struct sema {
	lock_t s_lock;		/* For manipulating the semaphore */
	int s_count;		/* Count */
	struct thread		/* List of threads waiting */
		*s_sleepq;
} sema_t;

/*
 * Constants
 */
typedef uint spl_t;
#define SPL0 (0)		/* Spin with interrupts enabled */
#define SPLHI (0x80)		/* Spin with interrupts disabled */
#ifdef DEBUG
#define SPL0_SAME SPL0		/* Spin with interrupts unchanged */
#define SPLHI_SAME SPLHI	/* Spin with interrupts unchanged */
#else
#define SPL0_SAME (0x40)	/* Spin with interrupts unchanged */
#define SPLHI_SAME (0x40)	/* Spin with interrupts unchanged */
#endif
typedef uint pri_t;
/* #define PRILO (0) */		/* Sleep interruptibly (obsolete) */
#define PRICATCH (0x7F)		/* PRILO, but p_sema returns error code */
#define PRIHI (0xFF)		/* Sleep uninterruptibly */

/*
 * Routines
 */
extern int p_sema(sema_t *, pri_t);
extern int cp_sema(sema_t *);
extern void v_sema(sema_t *);
extern void vall_sema(sema_t *);
extern int p_sema_v_lock(sema_t *, pri_t, lock_t *);
#define sema_count(s) ((s)->s_count)

#endif /* _MUTEX_H */
