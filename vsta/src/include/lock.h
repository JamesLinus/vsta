#ifndef _LOCK_H
#define _LOCK_H
/*
 * lock.h
 *	User-level spinlocks
 */
#include <sys/types.h>

typedef unsigned int lock_t;

/*
 * Inline
 */
inline static void
v_lock(volatile lock_t *lp)
{
	*lp = 0;
}
inline static void
init_lock(lock_t *lp)
{
	*lp = 0;
}

/*
 * Routines in -lusr
 */
extern void p_lock(volatile lock_t *);

#endif /* _LOCK_H */
