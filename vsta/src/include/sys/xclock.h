#ifndef _XCLOCK_H
#define _XCLOCK_H
/*
 * xclock.h
 *	Data structures for managing clock events
 */

/*
 * List of processes waiting for a certain time to pass
 */
struct eventq {
	ulong e_tid;		/* PID of thread */
	struct time e_time;	/* What time to wake */
	struct eventq *e_next;	/* List of sleepers */
	sema_t e_sema;		/* Semaphore to sleep on */
	int e_onlist;		/* Flag that still in eventq list */
};

#endif /* _XCLOCK_H */
