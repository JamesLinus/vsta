/*
 * buf.c
 *	Buffering for extent-based filesystem
 *
 * To optimize I/O, we do not used a fixed buffer scheme; instead, we create
 * buffers which match the extent which is about to be read in.  A buffer
 * is looked up by the sector which starts the currently allocated extent.
 * When a file's allocation is changed (truncation, deletion, etc.) the
 * buffers must be freed at that point.
 *
 * Each buffer is up to EXTSIZ sectors in size.
 */
#include "vstafs.h"
#include "buf.h"
#include <sys/assert.h>
#include <hash.h>
#include <std.h>
#ifdef DEBUG
#include <stdio.h>
static uint nbuf = 0;		/* Running tally of buffers */
#endif

static uint bufsize;		/* # sectors held in memory currently */
static struct hash *bufpool;	/* Hash daddr_t -> buf */
static struct llist allbufs;	/* Time-ordered list, for aging */

#ifdef DEBUG
/*
 * dump_bufpool()
 *	Show buffer pool
 */
static void
dump_bufpool(void)
{
	struct llist *l;

	for (l = LL_NEXT(&allbufs); l != &allbufs; l = LL_NEXT(l)) {
		struct buf *b;

		b = l->l_data;
		printf(" Start %ld len %d locks %d flags 0x%x\n",
			b->b_start, b->b_nsec, b->b_locks, b->b_flags);
	}
}
#endif

/*
 * free_buf()
 *	Release buffer storage, remove from hash
 */
static void
free_buf(struct buf *b)
{
	ASSERT_DEBUG(b->b_list, "free_buf: null b_list");
	ll_delete(b->b_list);
	hash_delete(bufpool, b->b_start);
	bufsize -= b->b_nsec;
	ASSERT_DEBUG(b->b_data, "free_buf: null b_data");
	free(b->b_data);
#ifdef DEBUG
	bzero(b, sizeof(struct buf));
	ASSERT(nbuf > 0, "free_buf: underflow");
	nbuf -= 1;
#endif
	free(b);
}

/*
 * age_buf()
 *	Find the next available buf header, flush and free it
 *
 * Since this is basically a paging algorithm, it can become arbitrarily
 * complex.  The algorithm here tries to be simple, yet somewhat fair.
 */
static void
age_buf(void)
{
	struct llist *l;
	struct buf *b;

	/*
	 * Pick the oldest buf which isn't locked.
	 */
	for (l = allbufs.l_back; l != &allbufs; l = l->l_back) {
		/*
		 * Only skip if wired
		 */
		b = l->l_data;
		if (b->b_locks) {
			continue;
		}

		/*
		 * Sync out data (sync_buf() checks for DIRTY)
		 */
		sync_buf(b);

		/*
		 * Remove from list, update data structures
		 */
		free_buf(b);
		return;
	}
#ifdef DEBUG
	printf("No buffers aged out of %d in pool:\n", bufsize);
	dump_bufpool();
#endif
	ASSERT(bufsize <= CORESEC, "age_buf: buffers too large");
}

#ifdef DEBUG
/*
 * check_span()
 *	Assert that a buf doesn't contain the given value
 */
static int
check_span(long key, void *data, void *arg)
{
	daddr_t d;
	struct buf *b;

	d = (daddr_t)arg;
	b = data;
	if ((d >= key) && (d < (key + b->b_nsec))) {
		fprintf(stderr, "overlap: %ld resides in %ld len %d\n",
			d, key, b->b_nsec);
		ASSERT(0, "check_span: overlap");
	}
	return(0);
}
#endif /* DEBUG */

/*
 * find_buf()
 *	Given starting sector #, return pointer to buf
 */
struct buf *
find_buf(daddr_t d, uint nsec)
{
	struct buf *b;

	ASSERT_DEBUG(nsec > 0, "find_buf: zero");
	ASSERT_DEBUG(nsec <= EXTSIZ, "find_buf: too big");

	/*
	 * If we can find it, this is easy
	 */
	b = hash_lookup(bufpool, d);
	if (b) {
		return(b);
	}

#ifdef DEBUG
	/*
	 * Make sure there isn't a block which spans it
	 */
	hash_foreach(bufpool, check_span, (void *)d);
#endif

	/*
	 * Get a buf struct
	 */
	b = malloc(sizeof(struct buf));
	if (b == 0) {
		return(0);
	}

	/*
	 * Make room in our buffer cache if needed
	 */
	while ((bufsize+nsec) > CORESEC) {
		age_buf();
	}

	/*
	 * Get the buffer space
	 */
	b->b_data = malloc(stob(nsec));
	if (b->b_data == 0) {
		free(b);
		return(0);
	}

	/*
	 * Add us to pool, and mark us very new
	 */
	b->b_list = ll_insert(&allbufs, b);
	if (b->b_list == 0) {
		free(b->b_data);
		free(b);
		return(0);
	}
	if (hash_insert(bufpool, d, b)) {
		ll_delete(b->b_list);
		free(b->b_data);
		free(b);
		return(0);
	}

	/*
	 * Fill in the rest & return
	 */
	b->b_start = d;
	b->b_nsec = nsec;
	b->b_flags = 0;
	b->b_locks = 0;
	bufsize += nsec;
#ifdef DEBUG
	nbuf += 1;
	ASSERT(nbuf <= CORESEC, "find_buf: too many");
#endif
	return(b);
}

/*
 * resize_buf()
 *	Indicate that the cached region is changing to newsize
 *
 * If "fill" is non-zero, the incremental contents are filled from disk.
 * Otherwise the buffer space is left uninitialized.
 *
 * Returns 0 on success, 1 on error.
 */
int
resize_buf(daddr_t d, uint newsize, int fill)
{
	char *p;
	struct buf *b;

	ASSERT_DEBUG(newsize <= EXTSIZ, "resize_buf: too large");
	ASSERT_DEBUG(newsize > 0, "resize_buf: zero");
	/*
	 * If it isn't currently buffered, we don't care yet
	 */
	if (!(b = hash_lookup(bufpool, d))) {
		return(0);
	}
#ifdef DEBUG
	/* This isn't fool-proof, but should catch most transgressions */
	if (newsize > b->b_nsec) {
		hash_foreach(bufpool, check_span,
			(void *)(b->b_start + newsize - 1));
	}
#endif

	/*
	 * Current activity--move to end of age list
	 */
	ll_movehead(&allbufs, b->b_list);

	/*
	 * Resize to current size is a no-op
	 */
	if (newsize == b->b_nsec) {
		return(0);
	}

	/*
	 * Get the buffer space
	 */
	ASSERT_DEBUG(!(fill && (newsize < b->b_nsec)),
		"resize_buf: fill && shrink");
	p = realloc(b->b_data, stob(newsize));
	if (p == 0) {
		return(1);
	}
	b->b_data = p;

	/*
	 * If needed, fill from disk
	 */
	if (fill) {
		read_secs(b->b_start + b->b_nsec, p + stob(b->b_nsec),
			newsize - b->b_nsec);
	}

	/*
	 * Update buf and return success
	 */
	bufsize = (int)bufsize + ((int)newsize - (int)b->b_nsec);
	b->b_nsec = newsize;
	while (bufsize > CORESEC) {
		age_buf();
	}
	return(0);
}

/*
 * index_buf()
 *	Get a pointer to a run of data under a particular buf entry
 *
 * As a side effect, move us to front of list to make us relatively
 * undesirable for aging.
 */
void *
index_buf(struct buf *b, uint index, uint nsec)
{
	ASSERT((index+nsec) <= b->b_nsec, "index_buf: too far");

	ll_movehead(&allbufs, b->b_list);
	if ((index == 0) && (nsec == 1)) {
		/*
		 * Only looking at 1st sector.  See about reading
		 * only 1st sector, if we don't yet have it.
		 */
		if ((b->b_flags & B_SEC0) == 0) {
			/*
			 * Load the sector, mark it as present
			 */
			read_secs(b->b_start, b->b_data, 1);
			b->b_flags |= B_SEC0;
		}
	} else if ((b->b_flags & B_SECS) == 0) {
		/*
		 * Otherwise if we don't have the whole buffer, get
		 * it now.
		 */
		read_secs(b->b_start, b->b_data, b->b_nsec);
		b->b_flags |= (B_SEC0|B_SECS);
	}
	return((char *)b->b_data + stob(index));
}

/*
 * init_buf()
 *	Initialize the buffering system
 */
void
init_buf(void)
{
	ll_init(&allbufs);
	bufpool = hash_alloc(CORESEC/8);
	bufsize = 0;
	ASSERT(bufpool, "init_buf: bufpool");
}

/*
 * dirty_buf()
 *	Mark the given buffer dirty
 */
void
dirty_buf(struct buf *b)
{
	b->b_flags |= B_DIRTY;
}

/*
 * lock_buf()
 *	Lock down the buf; make sure it won't go away
 */
void
lock_buf(struct buf *b)
{
	b->b_locks += 1;
	ASSERT(b->b_locks > 0, "lock_buf: overflow");
}

/*
 * unlock_buf()
 *	Release previously taken lock
 */
void
unlock_buf(struct buf *b)
{
	ASSERT(b->b_locks > 0, "unlock_buf: underflow");
	b->b_locks -= 1;
}

/*
 * sync_buf()
 *	Sync back buffer if dirty
 *
 * Write back the 1st sector, or the whole buffer, as appropriate
 */
void
sync_buf(struct buf *b)
{
	ASSERT_DEBUG(b->b_flags & (B_SEC0 | B_SECS), "sync_buf: not ref'ed");

	/*
	 * Skip it if not dirty
	 */
	if (!(b->b_flags & B_DIRTY)) {
		return;
	}

	/*
	 * Do the I/O--whole buffer, or just 1st sector if that was
	 * the only sector referenced.
	 */
	if (b->b_flags & B_SECS) {
		write_secs(b->b_start, b->b_data, b->b_nsec);
	} else {
		write_secs(b->b_start, b->b_data, 1);
	}
	b->b_flags &= ~B_DIRTY;
}

/*
 * inval_buf()
 *	Clear out (without sync'ing) some buffer data
 *
 * This routine will handle multiple buffer entries, but "d" must
 * point to an aligned beginning of such an entry.
 */
void
inval_buf(daddr_t d, uint len)
{
	struct buf *b;

	for (;;) {
		b = hash_lookup(bufpool, d);
		if (b) {
			free_buf(b);
		}
		if (len <= EXTSIZ) {
			break;
		}
		d += EXTSIZ;
		len -= EXTSIZ;
	}
}

/*
 * sync()
 *	Write all dirty buffers to disk
 */
void
sync(void)
{
	struct llist *l;

	for (l = LL_NEXT(&allbufs); l != &allbufs; l = LL_NEXT(l)) {
		struct buf *b = l->l_data;

		if (b->b_flags & B_DIRTY) {
			sync_buf(b);
		}
	}
}
