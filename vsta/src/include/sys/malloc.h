#ifndef _MALLOC_H
#define _MALLOC_H
/*
 * malloc.h
 *	Function defs for a malloc()/free() kernel interface
 */
#include <sys/types.h>
#include <sys/param.h>
#include "../mach/mutex.h"

/*
 * Basic functions
 */
extern void *malloc(uint size);
extern void free(void *);

/*
 * malloc() types, their values as handed to MALLOC()
 */
#define MT_RMAP (0)		/* struct rmap */
#define MT_EVENT (1)		/* Event lists */
#define MT_EXITGRP (2)		/* Exit group */
#define MT_EXITST (3)		/*  ...status */
#define MT_MSG (4)		/* struct msg */
#define MT_SYSMSG (5)		/* struct sysmsg */
#define MT_PORT (6)		/* struct port */
#define MT_PORTREF (7)		/* struct portref */
#define MT_PVIEW (8)		/* struct pview */
#define MT_PSET (9)		/* struct pset */
#define MT_PROC (10)		/* struct proc */
#define MT_THREAD (11)		/* struct thread */
#define MT_KSTACK (12)		/* A kernel stack */
#define MT_VAS (13)		/* struct vas */
#define MT_PERPAGE (14)		/* struct perpage */
#define MT_QIO (15)		/* struct qio */
#define MT_SCHED (16)		/* struct sched */
#define MT_SEG (17)		/* struct seg */
#define MT_EVENTQ (18)		/* struct eventq */
#define MT_L1PT (19)		/* Root page table */
#define MT_L2PT (20)		/*  ...2nd level */
#define MT_PGRP (21)		/* Process grouping */
#define MT_ATL (22)		/* Attach lists */
#define MT_FPU (23)		/* FPU save state */

#define MALLOCTYPES (24)	/* UPDATE when you add values above */
				/* ALSO check n_allocname[] */

extern void *_malloc(uint, uint), _free(void *, uint);

/*
 * Our per-storage-size information
 */
struct bucket {
	void *b_mem;	/* List of chunks of memory */
	uint b_elems;	/* # chunks available in this bucket */
	uint b_pages;	/* # pages used for this bucket size */
	uint b_size;	/* Size of this kind of chunk */
	lock_t b_lock;	/* Lock for manipulating this bucket */
};
extern struct bucket buckets[];
#define MIN_BUCKET 4	/* At least 16 bytes allocated */

/*
 * BUCKET()
 *	Convert size to a bucket index
 *
 * Works for constants (the compiler flattens the expression out to
 * a bucket index constant) and dynamic values (it's an O(log(n))
 * search).
 */
#define BUCKET(x) \
  ((x <= 1024) ? \
    ((x <= 128) ? \
      ((x <= 32) ?  ((x <= 16) ? 4 : 5) : ((x <= 64) ? 6 : 7)) \
    : \
      ((x <= 256) ? 8 : ((x <= 512) ? 9 : 10))) \
  : \
    ((x <= 8192) ? \
      ((x <= 2048) ? 11 : ((x <= 4096) ? 12 : 13)) \
    : \
      ((x <= 16384) ? 14 : 15)))

/*
 * MALLOC/FREE interface.  For DEBUG, always call the procedure which
 * tallies memory type usage.  Otherwise grab blocks inline.
 */
#ifdef DEBUG

#define MALLOC(size, type) _malloc(size, type);
#define FREE(ptr, type) _free(ptr, type);

#else /* !DEBUG */

inline static void *
MALLOC(uint size, uint type)
{
	struct bucket *b;
	void *v;

	if (size > (NBPG/2)) {
		return(_malloc(size, type));
	}
	b = &buckets[BUCKET(size)];
	p_lock_fast(&b->b_lock, SPL0_SAME);
	v = b->b_mem;
	if (v) {
		b->b_mem = *(void **)v;
	}
	v_lock(&b->b_lock, SPL0_SAME);
	if (v) {
		return(v);
	}
	return(_malloc(size, type));
}

inline static void
FREE(void *ptr, uint type)
{
	struct bucket *b;
	uint size;

	size = PAGE_GETVAL(btop(vtop(ptr)));
	if (size >= PGSHIFT) {
		_free(ptr, type);
		return;
	}
	b = &buckets[size];
	_free(ptr, type);
}

#endif /* !DEBUG */


#ifdef DEBUG
/*
 * Our per-storage-type tabulation
 */
extern ulong n_alloc[MALLOCTYPES];
#endif /* DEBUG */

#endif /* _MALLOC_H */
