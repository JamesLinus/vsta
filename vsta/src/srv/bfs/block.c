/*
 * Filename:	block.c
 * Developed:	Dave Hudson <dave@humbug.demon.co.uk>
 * Originated:	Andy Valencia
 * Last Update: 13th February 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description:	Routines for caching blocks out of our filesystem
 */


#include <fcntl.h>
#include <llist.h>
#include <std.h>
#include <stdio.h>
#include <sys/assert.h>
#include "bfs.h"


extern int blkdev;

char *errstr;			/* String for last error */
static int cached = 0;		/* # blocks currently cached */


/*
 * Structure describing a block in the cache
 */
struct block {
	int b_blkno;			/* Block we describe */
	int b_flags;			/* Flags (see below) */
	void *b_data;			/* Pointer to data */
	struct llist *b_all;		/* Circular list of all blocks */
	struct llist *b_hash;		/* Linked list on hash collision */
	int b_refs;			/* # active references */
};


/*
 * Bits in b_flags
 */
#define B_DIRTY 1		/* New data in buffer */


/*
 * Head of hash chains for each hash value, plus linked list of
 * all blocks outstanding.
 */
#define HASHSIZE 0x20
#define HASHMASK (HASHSIZE-1)


static struct llist hash[HASHSIZE];
static struct llist allblocks;


/*
 * bnew()
 *	Get a new block, associate it with the named block number
 *
 * Since we're only allowed NCACHE blocks in-core, we might have to
 * push a dirty block out.  For a boot filesystem, we just push
 * synchronously.  Other filesystems will doubtless prefer to push
 * dirty blocks asynchronously while still scanning forward for
 * a clean one.
 *
 * On return, the new block has a single reference.
 */
static struct block *
bnew(int blkno)
{
	struct block *b;
	struct llist *l;

	/*
	 * Not above our limit--easy living!
	 */
	if (cached < NCACHE) {
		b = malloc(sizeof(struct block));
		if (b) {
			b->b_data = malloc(BLOCKSIZE);
			if (b->b_data) {
				b->b_blkno = blkno;
				b->b_flags = 0;
				b->b_all = ll_insert(&allblocks, b);
				b->b_hash =
					ll_insert(&hash[blkno & HASHMASK], b);
				b->b_refs = 1;
				return b;
			}
			free(b);
		}
	}
	ASSERT(cached > 0, "bnew: no memory");

	/*
	 * Either above our limit, or we couldn't get any more memory
	 */

	/*
	 * Scan all blocks.  When we find something, move our
	 * placeholder up to one past that point.
	 */
	for (l = allblocks.l_forw; l != &allblocks; l = l->l_forw) {
		/*
		 * Ignore if reference is held
		 */
		b = l->l_data;
		if (b->b_refs > 0)
			continue;

		/*
		 * Clean it if dirty
		 */
		if (b->b_flags & B_DIRTY) {
			lseek(blkdev, b->b_blkno*(long)BLOCKSIZE, 0);
			write(blkdev, b->b_data, BLOCKSIZE);
			b->b_flags &= ~B_DIRTY;
		}

		/*
		 * Update our "all block" header to point one past us
		 */
		ll_movehead(&allblocks, l->l_forw);

		/*
		 * Update the hash chain it resides on, and return it.
		 * Our caller is responsible for putting the right
		 * data into the buffer.
		 */
		ll_delete(b->b_hash);
		b->b_hash = ll_insert(&hash[blkno & HASHMASK], b);
		b->b_refs = 1;
		b->b_blkno = blkno;
		return b;
	}

	/*
	 * Oops.  This really shouldn't be possible in a single-threaded
	 * server, unless we're leaking block references.
	 */
	ASSERT(0, "bnew: all blocks busy");
	return NULL;
}


/*
 * bfind()
 *	Try and find a block in the cache
 *
 * If it's found, it is returned with the b_refs incremented.  On failure,
 * a NULL pointer is returned.
 */
static struct block *
bfind(int blkno)
{
	struct block *b;
	struct llist *l, *lfirst;

	lfirst = &hash[blkno & HASHMASK];
	for (l = lfirst->l_forw; l != lfirst; l = l->l_forw) {
		b = l->l_data;
		if (blkno == b->b_blkno) {
			b->b_refs += 1;
			return b;
		}
	}
	return NULL;
}


/*
 * bdirty()
 *	Flag buffer as dirty--it must be written before reused
 */
void
bdirty(void *bp)
{
	struct block *b = bp;

	ASSERT_DEBUG(b->b_refs > 0, "bdirty: no ref");
	b->b_flags |= B_DIRTY;
}


/*
 * binval()
 *	Throw out a buf header, usually to recover from I/O errors
 */
static void
bjunk(struct block *b)
{
	ll_delete(b->b_hash);
	ll_delete(b->b_all);
	free(b->b_data);
	free(b);
	cached -= 1;
}


/*
 * bget()
 *	Find block in cache or read from disk; return pointer
 *
 * On success, an opaque pointer is returned.  On error,
 * NULL is returned, and the error is recorded in "errstr".
 * The new block has a reference added to it.
 */
void *
bget(int blkno)
{
	struct block *b;
	int x;

	b = bfind(blkno);
	if (!b) {
		b = bnew(blkno);
		if (lseek(blkdev, blkno*(long)BLOCKSIZE, 0) == -1) {
			bjunk(b);
			errstr = strerror();
			return NULL;
		}
		x = read(blkdev, b->b_data, BLOCKSIZE);
		if (x != BLOCKSIZE) {
			bjunk(b);
			errstr = strerror();
			return NULL;
		}
	}
	return b;
}


/*
 * bdata()
 *	Convert opaque pointer into corresponding data
 */
void *
bdata(void *bp)
{
	struct block *b = bp;

	ASSERT_DEBUG(b->b_refs > 0, "bdata: no ref");
	return b->b_data;
}


/*
 * bfree()
 *	Indicate that the current reference is complete
 */
void
bfree(void *bp)
{
	struct block *b = bp;

	ASSERT_DEBUG(b->b_refs > 0, "bfree: free and no ref");
	b->b_refs -= 1;
}


/*
 * binit()
 *	Initialize our buffer hash chains
 */
void
binit(void)
{
	int x;

	for (x = 0; x < HASHSIZE; ++x)
		ll_init(&hash[x]);
	ll_init(&allblocks);
}


/*
 * bsync()
 *	Flush out all dirty buffers
 */
void
bsync(void)
{
	struct llist *l;
	struct block *b;

	for (l = allblocks.l_forw; l != &allblocks; l = l->l_forw) {
		b = l->l_data;
		if (b->b_flags & B_DIRTY) {
			lseek(blkdev, b->b_blkno*(long)BLOCKSIZE, 0);
			write(blkdev, b->b_data, BLOCKSIZE);
			b->b_flags &= ~B_DIRTY;
		}
	}
}
