/*
 * malloc.c
 *	Routines for allocating/freeing memory
 *
 * This routine is adapted from the power-of-two allocator used
 * in the kernel.  The biggest trick is how to keep the per-virtual-page
 * information when there's no underlying per-page array.  We do it
 * here by allocation in chunks starting at MINPG and doubling to a
 * max of MAXPG.  Each chunk describes its range on a per-page basis.
 *
 * Unlike the kernel allocator, we don't bother trying to pull
 * unused pages out of the allocator.  We assume the VM system will
 * get its physical pages back as needed; we thus save the trouble
 * of maintaining doubly-linked lists.
 */
#include <sys/param.h>
#include <sys/mman.h>
#include <std.h>

/*
 * A poor man's <assert.h> to avoid circular library dependencies
 */
#ifdef DEBUG
#define ASSERT(cond, msg) if (!(cond)) \
	{ write(2, msg, sizeof(msg)-1); abort(); }
#else
#define ASSERT(cond, msg) /* Nothing */
#endif

#define MINPG (16)		/* Smallest storage allocation size */
#define MAXPG (256)		/*  ...largest */
#define MAXCHUNK (16)		/* Max chunks: supports > 12 Mb memory */

/*
 * One of these exists for each chunk of pages
 */
struct chunk {
	void *c_vaddr;		/* Base of mem for chunk */
	int c_len;		/* # pages in chunk */
	ushort *c_perpage;	/* Per-page information (1st page of chunk) */
	struct chunk		/* List of chunks */
		*c_next;
};
static struct chunk *chunks = 0;

/*
 * Our per-storage-size information
 */
#define MIN_BUCKET 4		/* At least 16 bytes allocated */
#define MAX_BUCKET 15		/* Up to 32k chunks */
struct bucket {
	void *b_mem;		/* Next chunk in bucket */
	uint b_elems;		/* # chunks available in this bucket */
	uint b_pages;		/* # pages used for this bucket size */
	uint b_size;		/* Size of this kind of chunk */
} buckets[MAX_BUCKET+1];

#ifdef DEBUG_LEAK
/*
 * Hooks for tracing memory leaks
 */

/*
 * Tap to update the record of who's taken what
 */
#define LEAK(caller, addr, size, alloc) trace_leak(caller, addr, size, alloc)

/*
 * Hack to get caller's address.  This makes some fairly CPU and compiler
 * specific assumptions.  Pass it the first argument to the function,
 * and it'll give the return address.  On an x86.  Under gcc.  We hope.
 */
#define CALLER(var) *(((uint *)&var) - 1)

/*
 * A record arrives on alloc, and is cleared on the corresponding dealloc.
 * Up to LEAK_RECORDS outstanding allocations can be remembered.
 */
#define LEAK_RECORDS 4096
static struct leak_record {
	uint l_caller;
	void *l_addr;
	uint l_size;
} leaks[LEAK_RECORDS];
static uint forgotten;

/*
 * Overall count of pages taken from system
 */
static uint leak_pages;
#define LEAK_MEM(pgs) (leak_pages += (pgs))

/*
 * trace_leak()
 *	Record memory allocation/deallocation transactions
 */
static void
trace_leak(uint caller, void *addr, uint size, int alloc)
{
	uint x;
	struct leak_record *l = &leaks[0], *free = 0;

	if (alloc) {
		/*
		 * Scan all the records to see if this is a dup.  We
		 * have to do this because alloc interfaces like
		 * realloc() use other alloc interfaces internally,
		 * and we then need to override the "caller" field
		 * to point to the application address which invoked
		 * an allocation interface.
		 */
		for (x = 0; x < LEAK_RECORDS; ++x, ++l) {
			if (!l->l_caller && !free) {
				free = l;
			} else if (l->l_addr == addr) {
				l->l_caller = caller;
				return;
			}
		}

		/*
		 * First we've heard of it.  Record it at the first place we
		 * found.
		 */
		if (free) {
			free->l_caller = caller;
			free->l_addr = addr;
			free->l_size = size;
		} else {
			forgotten += 1;
		}
		return;
	}

	/*
	 * Free.  Find the record (if possible) and clear its entry.
	 */
	for (x = 0; x < LEAK_RECORDS; ++x, ++l) {
		if (l->l_addr == addr) {
			bzero(l, sizeof(*l));
			return;
		}
	}
}

#else

/*
 * If not doing leak debugging, no-op these hooks
 */
#define LEAK(caller, addr, size, alloc)
#define CALLER(var) 0
#define LEAK_MEM(pgs)

#endif /* DEBUG_LEAK */

/*
 * init_malloc()
 *	Set up each bucket
 */
static void
init_malloc(void)
{
	int x;

	for (x = 0; x <= MAX_BUCKET; ++x) {
		buckets[x].b_size = (1 << x);
	}
}

/*
 * free_core()
 *	Free core from an alloc_core()
 *
 * Only used for "large" allocations; bucket allocations are always
 * left in the bucket and never freed to the OS (except on exit!).
 */
static void
free_core(void *mem)
{
	struct chunk *c, **cp;

	/*
	 * Patch this chunk out of the list
	 */
	cp = &chunks;
	mem = (char *)mem - NBPG;
	for (c = chunks; c; c = c->c_next) {
		if (c == mem) {
			*cp = c->c_next;
			break;
		}
		cp = &c->c_next;
	}

	/*
	 * Dump the memory back to the operating system
	 */
	LEAK_MEM(-(c->c_len + 1));
	munmap(mem, ptob(c->c_len + 1));
}

/*
 * alloc_core()
 *	Get more core from the OS, add it to our pool
 */
static void *
alloc_core(uint pgs)
{
	void *newmem;
	struct chunk *c;

	/*
	 * Get new virtual space from the OS
	 */
	newmem = mmap(0, ptob(pgs+1), PROT_READ|PROT_WRITE,
		MAP_ANON, 0, 0);
	if (!newmem) {
		return(0);
	}
	LEAK_MEM(pgs+1);

	/*
	 * First page is for our chunk description
	 */
	c = newmem;
	newmem = (char *)newmem + NBPG;
	c->c_vaddr = newmem;
	c->c_len = pgs;
	c->c_perpage = (ushort *)((char *)c + sizeof(struct chunk));
	c->c_next = chunks;
	chunks = c;
	bzero(c->c_perpage, sizeof(ushort *) * c->c_len);

	return(newmem);
}

/*
 * alloc_pages()
 *	Get some memory from the pool
 */
static void *
alloc_pages(uint pgs)
{
	static char *freemem = 0;
	static int freepgs = 0;
	static int allocsz = MINPG;

retry:
	/*
	 * If we have enough here, let them have it
	 */
	if (freepgs >= pgs) {
		void *p;

		p = freemem;
		freemem += ptob(pgs);
		freepgs -= pgs;
		return(p);
	}

	/*
	 * Sanity
	 */
	ASSERT(allocsz >= pgs, "malloc: alloc_pages: too big");

	/*
	 * Get more core.  Note that there might be some residual
	 * memory in freemem; we lose it.
	 */
 	freemem = alloc_core(allocsz);
	if (freemem == 0) {
		return(0);
	}
	freepgs = allocsz;

	/*
	 * Bump allocation size until we reach max
	 */
	if (allocsz < MAXPG) {
		allocsz <<= 1;
	}
	goto retry;
}

/*
 * find_chunk()
 *	Scan all chunks for the one describing this memory
 */
static struct chunk *
find_chunk(void *mem)
{
	struct chunk *c;

	for (c = chunks; c; c = c->c_next) {
		if ((c->c_vaddr <= mem) &&
			(((char *)c->c_vaddr + ptob(c->c_len)) >
			 (char *)mem)) {
				break;
		}
	}
	ASSERT(c, "malloc: find_chunk: no chunk");
	return(c);
}

/*
 * set_size()
 *	Set size attribute for a given set of pages
 */
static void
set_size(void *mem, int size)
{
	struct chunk *c;
	int idx;

	/*
	 * Find chunk containing address
	 */
	c = find_chunk(mem);

	/*
	 * Get index for perpage slot.  Set size attribute for each slot.
	 */
 	idx = btop((char *)mem - (char *)(c->c_vaddr));
	ASSERT(size < 32768, "set_size: overflow");
	c->c_perpage[idx] = size;
}

/*
 * get_size()
 *	Get size attribute for some memory
 */
static
get_size(void *mem)
{
	struct chunk *c;
	int idx;

	c = find_chunk(mem);
 	idx = btop((char *)mem - (char *)(c->c_vaddr));
	return(c->c_perpage[idx]);
}

/*
 * malloc()
 *	Allocate block of given size
 */
void *
malloc(unsigned int size)
{
	struct bucket *b;
	void *f;
	static int init = 0;

	/*
	 * Sorta lame
	 */
 	if (!init) {
		init_malloc();
		init = 1;
	}

	/*
	 * For pages and larger, allocate memory in units of
	 * pages.
	 */
	if (size > (1 << MAX_BUCKET)) {
		uint pgs;
		void *mem;

		pgs = btorp(size);
		mem = alloc_core(pgs);
		set_size(mem, pgs + MAX_BUCKET);

		LEAK(CALLER(size), mem, size, 1);
		return(mem);
	}

	/*
	 * Otherwise allocate from one of our buckets
	 */

	/*
	 * Cap minimum size
	 */
	if (size < (1 << MIN_BUCKET)) {
		size = (1 << MIN_BUCKET);
		b = &buckets[MIN_BUCKET];
	} else {
		uint mask, bucket;

		/*
		 * Poor man's FFS
		 */
		bucket = MAX_BUCKET;
		mask = 1 << bucket;
		while (!(size & mask)) {
			bucket -= 1;
			mask >>= 1;
		}

		/*
		 * Round up as needed
		 */
		if (size & (mask-1)) {
			bucket += 1;
		}
		b = &buckets[bucket];
	}

	/*
	 * We now know what bucket to use.  Add memory if needed.
	 */
	if (!(b->b_mem)) {
		char *p;
		uint x, pgs;

		/*
		 * Fill in per-page information
		 */
		pgs = btorp(b->b_size);
		p = alloc_pages(pgs);
		set_size(p, b-buckets);

		/*
		 * Parcel as many chunks as will fit out of the memory
		 */
		for (x = 0; x < ptob(pgs); x += b->b_size) {
			/*
			 * Add to list
			 */
			*(void **)(p + x) = b->b_mem;
			b->b_mem = (void *)(p + x);

			/*
			 * Tally elements available under bucket
			 */
			b->b_elems += 1;
		}

		/*
		 * Update count of pages consumed by this bucket
		 */
		b->b_pages += pgs;
	}

	/*
	 * Take a chunk
	 */
	f = b->b_mem;
	b->b_mem = *(void **)f;

	/*
	 * Return memory
	 */
	LEAK(CALLER(size), f, size, 1);
	return(f);
}

/*
 * free()
 *	Free a malloc()'ed memory element
 */
void
free(void *mem)
{
	struct bucket *b;
	ushort sz;

	/*
	 * Ignore NULL
	 */
	if (mem == 0) {
		return;
	}

	/*
	 * Get allocation information for this data element
	 */
	sz = get_size(mem);
	LEAK(CALLER(mem), mem, sz, 0);

	/*
	 * If a whole page or more, free directly
	 */
	if (sz > MAX_BUCKET) {
		free_core(mem);
		return;
	}

	/*
	 * Get bucket
	 */
	b = &buckets[sz];

	/*
	 * Free chunk to bucket
	 */
	*(void **)mem = b->b_mem;
	b->b_mem = mem;
	b->b_elems += 1;
}

/*
 * realloc()
 *	Grow size of existing memory block
 *
 * We only "grow" it if the new size is within the same power of
 * two.  Otherwise we allocate a new block and copy over.
 */
void *
realloc(void *mem, uint newsize)
{
	uint oldsize;
	int copy = 0;
	void *newmem;

	/*
	 * When the old pointer is 0, this is the special case of the
	 * first use of the pointer.  It spares the user code from having
	 * to special-case the first time an element is allocated.
	 */
	if (mem == 0) {
		newmem = malloc(newsize);
		LEAK(CALLER(mem), mem, newsize, 1);
		return(newmem);
	}

	/*
	 * Find out how big it is now
	 */
	oldsize = get_size(mem);
	if (oldsize <= MAX_BUCKET) {
		oldsize = (1 << oldsize);
	} else {
		oldsize = ptob(oldsize - MAX_BUCKET);
	}

	/*
	 * If shrinking or same size no problem.  The recorded size is the
	 * actual size of the allocated block--so growth within the same
	 * power of two (for small) or page count (for large) will succeed
	 * here with great efficiency.
	 *
	 * If the size is "large" (> MAX_BUCKET) and the new size is half
	 * as small or smaller, actually convert to a smaller amount of
	 * memory.  Large buffers are munmap()'ed, which will free core
	 * back to the system.
	 */
 	if (newsize <= oldsize) {
		if ((oldsize > (1 << MAX_BUCKET)) &&
				(newsize <= (oldsize >> 1))) {
			/* Fall into below for realloc */ ;
		} else {
			return(mem);
		}
	}

	/*
	 * Otherwise allocate a new block, copy over the old data, and
	 * free the old block.
	 */
	newmem = malloc(newsize);
	if (newmem == 0) {
		return(0);
	}
	bcopy(mem, newmem, (oldsize < newsize) ? oldsize : newsize);
	free(mem);
	LEAK(CALLER(mem), newmem, newsize, 1);
	return(newmem);
}

/*
 * calloc()
 *	Return cleared space
 */
void *
calloc(unsigned int nelem, unsigned int elemsize)
{
	void *p;
	unsigned int total = nelem*elemsize;

	/*
	 * Get space, bail if can't alloc
	 */
	p = malloc(total);
	if (p == 0) {
		return(0);
	}

	/*
	 * Clear it, return a pointer
	 */
	bzero(p, total);
	LEAK(CALLER(nelem), p, total, 1);
	return(p);
}
