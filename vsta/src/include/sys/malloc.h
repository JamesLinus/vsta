#ifndef _MALLOC_H
#define _MALLOC_H
/*
 * malloc.h
 *	Function defs for a malloc()/free() kernel interface
 */
#include <sys/types.h>
#include <sys/assert.h>

/*
 * Basic functions
 */
extern void *malloc(uint size);
extern void free(void *);

/*
 * MALLOC/FREE interface.  Maps directly to malloc()/free() unless
 * DEBUG, in which case we tally use of memory types
 */
#ifdef DEBUG
extern void *_malloc(uint, uint), _free(void *, uint);

#define MALLOC(size, type) _malloc(size, type)
#define FREE(ptr, type) _free(ptr, type)
#else
#define MALLOC(size, type) malloc(size)
#define FREE(ptr, type) free(ptr)
#endif

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

#define MALLOCTYPES (23)	/* UPDATE when you add values above */
				/* ALSO check n_allocname[] */

#ifdef MALLOC_INTERNAL
/*
 * per-page information.  We overlay this on the existing "struct core"
 * storage already available per-page.
 */
struct page {
	ushort p_bucket;	/* Bucket # */
	ushort p_out;		/* # elems not free in this page */
};

/*
 * Structure of a chunk of storage while on the free list
 * in a bucket
 */
struct freehead {
	struct freehead
		*f_forw,	/* A doubly-linked list */
		*f_back;
};
#define EMPTY(bucket) ((bucket)->b_mem.f_forw == &(bucket)->b_mem)

/*
 * Our per-storage-size information
 */
struct bucket {
	struct freehead		/* List of chunks of memory */
		b_mem;
	uint b_elems;		/* # chunks available in this bucket */
	uint b_pages;		/* # pages used for this bucket size */
	uint b_size;		/* Size of this kind of chunk */
	lock_t b_lock;		/* Lock for manipulating this bucket */
} buckets[PGSHIFT];
#define MIN_BUCKET 4		/* At least 16 bytes allocated */

#ifdef DEBUG
/*
 * Our per-storage-type tabulation
 */
extern ulong n_alloc[MALLOCTYPES];
#endif

#endif /* MALLOC_INTERNAL */

#endif /* _MALLOC_H */
