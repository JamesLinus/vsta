#ifndef _SEMA_H
#define _SEMA_H
/*
 * sema.h
 *	User-level semaphores
 *
 * These semaphores are useful for multi-threaded processes, but not
 * useful for inter-process semaphoring, as they use shared memory
 * to provide very high performance when there's no contention.
 */
#include <sys/types.h>
#include <lock.h>

/*
 * The user-level data structure
 */
typedef struct sema {
	lock_t s_locked;	/* Mutex lock on this struct */
	int s_val;		/* Semaphore count */
	port_t s_port,		/* Sempahore server when sleeping */
		s_portmaster;
} sema_t;

/*
 * Routines in -lusr
 */
extern struct sema *alloc_sema(int init_val);
extern int p_sema(struct sema *);
extern void v_sema(struct sema *);

#endif /* _SEMA_H */
