/*
 * malloc.c
 *	Power-of-two storage allocator
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/assert.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/port.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/qio.h>
#include <sys/sched.h>
#include <sys/xclock.h>
#include "../mach/vminline.h"
#include "../mach/mutex.h"
#include <sys/malloc.h>
#include <sys/core.h>

/*
 * Per-bucket state
 */
struct bucket buckets[PGSHIFT];

#ifdef DEBUG
/*
 * Value to name mapping, to help the kernel debugger print
 * things out nicely
 */
char *n_allocname[MALLOCTYPES] = {
	"MT_RMAP",
	"MT_EVENT",
	"MT_EXITGRP",
	"MT_EXITST",
	"MT_MSG",
	"MT_SYSMSG",
	"MT_PORT",
	"MT_PORTREF",
	"MT_PVIEW",
	"MT_PSET",
	"MT_PROC",
	"MT_THREAD",
	"MT_KSTACK",
	"MT_VAS",
	"MT_PERPAGE",
	"MT_QIO",
	"MT_SCHED",
	"MT_SEG",
	"MT_EVENTQ",
	"MT_L1PT",
	"MT_L2PT",
	"MT_PGRP",
	"MT_ATL",
	"MT_FPU",
};
#endif /* DEBUG */

/*
 * malloc()
 *	Allocate block of given size
 */
void *
malloc(uint size)
{
	struct bucket *b;
	void *f;
	uint bucket;

	/*
	 * For pages and larger, allocate memory in units of
	 * pages. Power-of-two allocations, so > half page
	 * also consumes a whole page.
	 */
	if (size > (NBPG / 2)) {
		uint pgs;
		void *mem;

		pgs = btorp(size);
		mem = alloc_pages(pgs);
		PAGE_SETVAL(btop(vtop(mem)), PGSHIFT + pgs);
		return(mem);
	}

	/*
	 * Otherwise allocate from one of our buckets
	 */
	bucket = BUCKET(size);
	b = &buckets[bucket];

	/*
	 * Lock and see if our bucket is empty or not
	 */
	p_lock_fast(&b->b_lock, SPL0);
	
	/*
	 * We now know what bucket to use.  If it appears to be empty,
	 * grab a page and chop it into appropriately sized pieces
	 */
	if (b->b_mem == 0) {
		uint pg;
		uchar *p;
		int x;
		struct core *c;

		/*
		 * Keep trying until we get a page
		 */
		v_lock(&b->b_lock, SPL0_SAME);
		pg = alloc_page();
		p_lock_fast(&b->b_lock, SPL0_SAME);

		/*
		 * Fill in per-page information, flag page as
		 * consumed for kernel memory.
		 */
		PAGE_SETVAL(pg, bucket);
		PAGE_SETSYS(pg);

		/*
		 * Parcel as many chunks as will fit out of the page
		 */
		p = ptov(ptob(pg));
		for (x = 0; x < NBPG; x += b->b_size) {
			*(void **)(p+x) = b->b_mem;
			b->b_mem = p+x;
		}

		/*
		 * Update count of pages consumed by this bucket
		 */
		b->b_pages += 1;
	}

	/*
	 * Take a chunk
	 */
	f = b->b_mem;
	b->b_mem = *(void **)f;

	/*
	 * Release lock.
	 */
	v_lock(&b->b_lock, SPL0_SAME);

	/*
	 * Return memory
	 */
	return(f);
}

#ifdef DEBUG
/*
 * Tally of use of types
 */
ulong n_alloc[MALLOCTYPES];

/*
 * _malloc()
 *	Allocate with type attribute
 */
void *
_malloc(uint size, uint type)
{
	ASSERT(type < MALLOCTYPES, "_malloc: bad type");
	ATOMIC_INCL(&n_alloc[type]);
	return(malloc(size));
}

/*
 * _free()
 *	Free with type attribute
 */
void
_free(void *ptr, uint type)
{
	ASSERT(type < MALLOCTYPES, "_free: bad type");
	ATOMIC_DECL(&n_alloc[type]);
	free(ptr);
	return;
}
#endif /* DEBUG */

/*
 * free()
 *	Free a malloc()'ed memory element
 */
void
free(void *mem)
{
	uint pg, x;
	void *f;
	struct bucket *b;

	/*
	 * Get page information
	 */
	pg = btop(vtop(mem));
	x = PAGE_GETVAL(pg);

	/*
	 * If a whole page or more, free directly
	 */
	if (x >= PGSHIFT) {
		ASSERT_DEBUG(x != PGSHIFT, "free: npgs == 0");
		free_pages(mem, x - PGSHIFT);
		return;
	}

	/*
	 * Get bucket and lock
	 */
	b = &buckets[x];
	p_lock_fast(&b->b_lock, SPL0);

#ifdef DEBUG
	/*
	 * Slow, but can catch truly horrible bugs.  See if
	 * this memory is being freed when already free.
	 */
	for (f = b->b_mem; f; f = *(void **)f) {
		ASSERT(f != mem, "free: already on list");
	}
#endif

	/*
	 * Free chunk to bucket
	 */
	*(void **)mem = b->b_mem;
	b->b_mem = mem;

	v_lock(&b->b_lock, SPL0_SAME);
}

/*
 * init_malloc()
 *	Initialize malloc() data structures
 */
void
init_malloc(void)
{
	int x;
	struct bucket *b;

	/*
	 * Setup the buckets
	 */
	for (x = 0; x < PGSHIFT; ++x) {
		b = &buckets[x];
		b->b_size = (1 << x);
		init_lock(&b->b_lock);
	}
}
