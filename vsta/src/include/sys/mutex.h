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
 * PRICATCH will allow events which show up as a non-zero completion
 * of the p_sema operation, and PRILO will cause the system call to return
 * with EINTR (p_sema never returns, a longjmp happens instead).
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
#define SPLHI (0xFF)		/* Spin with interrupts disabled */
typedef uint pri_t;
#define PRILO (0)		/* Sleep interruptibly */
#define PRICATCH (0x7F)		/* PRILO, but p_sema returns error code */
#define PRIHI (0xFF)		/* Sleep uninterruptibly */

/*
 * Routines
 */
spl_t p_lock(lock_t *, spl_t);		/* For locks */
spl_t cp_lock(lock_t *, spl_t);
void v_lock(lock_t *, spl_t);
void init_lock(lock_t *);
int p_sema(sema_t *, pri_t);		/* For semaphores */
int cp_sema(sema_t *);
void v_sema(sema_t *);
void vall_sema(sema_t *);
int blocked_sema(sema_t *);
void init_sema(sema_t *);
void set_sema(sema_t *, int);
int p_sema_v_lock(sema_t *, pri_t, lock_t *);
#define sema_count(s) ((s)->s_count)

/*
 * Atomic increment/decrement.  Sometimes saves you a full lock operation.
 */
extern void ATOMIC_INCW(ushort *);
extern void ATOMIC_DECW(ushort *);
extern void ATOMIC_INCL(ulong *);
extern void ATOMIC_DECL(ulong *);
extern void ATOMIC_INC(uint *);
extern void ATOMIC_DEC(uint *);

#endif /* _MUTEX_H */
