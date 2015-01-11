/*
 * alloc.c
 *	Routines for managing the block free list
 */
#include "alloc.h"
#include <sys/assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>

/*
 * The head of the free list
 */
static struct freelist *freelist;

/*
 * Blocks which are to be freed once free list coalescing is finished
 */
static daddr_t *free_pend = 0;
static uint free_npend = 0;
ulong lost_blocks = 0L;		/* No room--dropped them instead */

#ifdef XXX
/*
 * dump_freelist()
 *	printf() out the freelist, for debugging
 */
static void
dump_freelist(void)
{
	struct freelist *fr;

	for (fr = freelist; fr; fr = fr->fr_next) {
		struct alloc *a;
		uint x;
		struct free *f = &fr->fr_free;

		a = f->f_free;
		for (x = 0; x < f->f_nfree; ++x, ++a) {
			printf(" %d..%d", a->a_start,
				a->a_start + a->a_len - 1);
		}
	}
	printf("\n");
}
#endif /* DEBUG */

/*
 * init_block()
 *	Initialize the block allocation routines
 *
 * Reads the free list from disk into core.
 */
void
init_block(void)
{
	daddr_t x;
	struct freelist *fr, **fpp;
	ulong largest = 0, nextent = 0, segs = 0;

	fpp = &freelist;
	for (x = FREE_SEC; x; x = fr->fr_free.f_next) {
		/*
		 * Get memory for image of next block
		 */
		fr = malloc(sizeof(struct freelist));
		if (fr == 0) {
			perror("init_alloc");
			exit(1);
		}

		/*
		 * Read sector image in
		 */
		read_sec(x, &fr->fr_free);

		/*
		 * Wire it onto the free chain
		 */
		fr->fr_this = x;
		*fpp = fr;
		fpp = &fr->fr_next;

		/*
		 * Tally it
		 */
		nextent += fr->fr_free.f_nfree;
		{
			struct alloc *a = fr->fr_free.f_free;
			uint x;

			for (x = 0; x < fr->fr_free.f_nfree; ++x,++a) {
				if (a->a_len > largest) {
					largest = a->a_len;
				}
			}
		}
		segs += 1;
	}

	/*
	 * End list with a NULL
	 */
	*fpp = 0;

	/*
	 * Report
	 */
	syslog(LOG_INFO, "%ld free extents,"
		" %ld segments, longest %ld sectors",
		nextent, segs, largest);
}

/*
 * alloc_chunk()
 *	Allocate a contiguous set of blocks from the current block
 */
static daddr_t
alloc_chunk(struct free *f, uint nblk)
{
	ulong x;
	struct alloc *a = f->f_free;
	daddr_t d;

	/*
	 * Scan for a chunk which has enough, first-fit
	 */
	for (x = 0; x < f->f_nfree; ++x, ++a) {
		if (a->a_len >= nblk) {
			break;
		}
	}

	/*
	 * Didn't find, fail now
	 */
	if (x >= f->f_nfree) {
		return(0);
	}

	/*
	 * Remove our space, close the opening if we've consumed it
	 * entirely.  Otherwise just update the entry.
	 */
	d = a->a_start;
	a->a_len -= nblk;
	if (a->a_len == 0) {
		f->f_nfree -= 1;
		bcopy(a+1, a, (f->f_nfree - x)*sizeof(struct alloc));
	} else {
		a->a_start += nblk;
	}

	return(d);
}

/*
 * alloc_block()
 *	Allocate the requested number of contiguous blocks
 */
daddr_t
alloc_block(uint nblk)
{
	struct freelist *fr;
	daddr_t d = 0;

	for (fr = freelist; fr; fr = fr->fr_next) {
		d = alloc_chunk(&fr->fr_free, nblk);
		if (d) {
			break;
		}
	}

	/*
	 * Sync the free list.
	 */
	if (fr) {
		write_sec(fr->fr_this, &fr->fr_free);
	}
	return(d);
}

/*
 * free_chunk()
 *	Free space to a particular freelist entry
 *
 * Returns 0 on success, 1 on failure.
 */
static int
free_chunk(struct free *f, daddr_t d, uint nblk)
{
	uint x;
	struct alloc *a;
	daddr_t dend = d+nblk;

	/*
	 * Scan for its place in the array.  While we scan, we check to
	 * see if can just coalesce it into an existing entry.
	 */
	a = f->f_free;
	for (x = 0; x < f->f_nfree; ++x,++a) {
		/*
		 * This would be so bad, that it's worth checking
		 * always.
		 */
		if (d > a->a_start) {
			ASSERT(d >= (a->a_start + a->a_len),
				"free_chunk: freeing free block");
		}

		/*
		 * Free space abuts against start of this free extent
		 */
		if (a->a_start == dend) {
			a->a_start = d;
			a->a_len += nblk;
			return(0);
		}

		/*
		 * This free extent abuts our freeing range
		 */
		if ((a->a_start + a->a_len) == d) {
			a->a_len += nblk;

			/*
			 * If we span the gap between two entries,
			 * coalesce them here.
			 */
			if (++x < f->f_nfree) {
				a += 1;
				if (a->a_start == dend) {
					(a-1)->a_len += a->a_len;
					f->f_nfree -= 1;
					bcopy(a+1, a, (f->f_nfree - x) *
						 sizeof(struct alloc));
				}
			}
			return(0);
		}
		if (d < a->a_start) {
			break;
		}
	}

	/*
	 * It did not abut, so we must add an entry.  Return failure if
	 * there are no more slots free.
	 */
	if (f->f_nfree >= NALLOC) {
		return(1);
	}

	/*
	 * Move everything following up one, and put this entry into
	 * place.
	 */
	bcopy(a, a+1, (f->f_nfree-x)*sizeof(struct alloc));
	a->a_start = d;
	a->a_len = nblk;
	f->f_nfree += 1;
	return(0);
}

/*
 * move_forward()
 *	Push some elements forward
 *
 * This can get tricky.  If there isn't enough room forward, we recurse
 * to push stuff out of the forward guy's forward direction.  If we
 * reach the end of the list and still haven't made room, we steal a block
 * off the end and make it a new freelist block.
 */
static void
move_forward(struct freelist *from, struct freelist *to, uint cnt)
{
	struct free *fto, *ffrom = &from->fr_free;
	struct alloc *ato, *afrom;

	ASSERT_DEBUG(from->fr_next == to, "move_forward: mismatch");
	ASSERT_DEBUG(cnt < ffrom->f_nfree, "move_forward: too few");

	/*
	 * Create a node if there isn't one yet
	 */
	if (!to) {
		from->fr_next = to = malloc(sizeof(struct freelist));
		ASSERT(to, "move_forward: out of core");
		to->fr_next = 0;
		from->fr_free.f_next = to->fr_this =
			alloc_chunk(ffrom, 1);
		ASSERT_DEBUG(to->fr_this, "move_forward: no block");
		to->fr_free.f_nfree = 0;
		to->fr_free.f_next = 0;
	}

	/*
	 * Our destination exists by now
	 */
	fto = &to->fr_free;
	ASSERT_DEBUG((fto->f_nfree + cnt) <= NALLOC,
		"move_forward: overflow");

	/*
	 * Check for coalesce
	 */
	afrom = &ffrom->f_free[ffrom->f_nfree - 1];
	if (fto->f_nfree > 0) {
		ato = &fto->f_free[0];
		if ((afrom->a_start + afrom->a_len) == ato->a_start) {
			/*
			 * Add space from last "from" element onto first
			 * "to" element.  Update counts.
			 */
			ato->a_start = afrom->a_start;
			cnt -= 1;
			ffrom->f_nfree -= 1;
		}
	}

	/*
	 * If there's not room, move some forward.
	 */
	if ((cnt + fto->f_nfree) > NALLOC) {
		/*
		 * Push the necessary amount forward.  We could push more,
		 * in the expectation that this is a free list rebalance.
		 * But we'll keep the function "pure" for now.
		 */
		move_forward(to, to->fr_next, (fto->f_nfree + cnt) - NALLOC);
	}

	/*
	 * Now we have room.  Move up the entries, and put the required
	 * number into place at the front.
	 */
	bcopy(&fto->f_free[0], &fto->f_free[cnt],
		fto->f_nfree * sizeof(struct alloc));
	bcopy(&ffrom->f_free[ffrom->f_nfree - cnt], &fto->f_free[0],
		cnt * sizeof(struct alloc));
	fto->f_nfree += cnt;
	ffrom->f_nfree -= cnt;
}

/*
 * move_back()
 *	Pull some entries back into the given freelist
 *
 * Assumes all entries in "from" belong after last entry in "to".  Will
 * coalesce if possible.
 */
static void
move_back(struct freelist *to, struct freelist *from, uint cnt)
{
	struct free *fto = &to->fr_free, *ffrom = &from->fr_free;
	struct alloc *ato, *afrom;

	/*
	 * Coalesce if possible
	 */
	afrom = &ffrom->f_free[0];
	if (to->fr_free.f_nfree > 0) {
		ato = &fto->f_free[fto->f_nfree-1];
		if ((ato->a_start + ato->a_len) == afrom->a_start) {
			/*
			 * Move space back into last entry of first freelist.
			 * Advance pointer to next element in "from", and
			 * lower count to reflect consumed entry.
			 */
			ato->a_len += afrom->a_len;
			cnt -= 1;
			ffrom->f_nfree -= 1;
			bcopy(afrom+1, afrom,
				ffrom->f_nfree * sizeof(struct alloc));
		}
		ato += 1;
	} else {
		ato = &fto->f_free[fto->f_nfree];
	}

	/*
	 * Now move back entries the indicated amount
	 */
	ASSERT_DEBUG(ffrom->f_nfree >= cnt, "move_back: underflow");
	bcopy(afrom, ato, cnt * sizeof(struct alloc));
	fto->f_nfree += cnt;
	ffrom->f_nfree -= cnt;
	ASSERT_DEBUG(fto->f_nfree <= NALLOC, "move_back: too far");
	bcopy(afrom+cnt, afrom, ffrom->f_nfree * sizeof(struct alloc));
}

/*
 * write_freelist()
 *	Write out the in-core freelist to disk
 *
 * This is only done after a list rebalance.  To reduce our
 * exposure to crashes, we commit each block twice.  Once with
 * a next pointer of 0, once again with the real next pointer
 * once the next block has successfully been written.  A crash
 * thus at worst steals a big chunk of our free list.  The
 * design of this filesystem tries to make these free list
 * imbalances infrequent, anyway.
 */
static void
write_freelist(void)
{
	struct freelist *fr;
	daddr_t old_next = 0;
	struct freelist *prev_blk = 0;

	for (fr = freelist; fr; fr = fr->fr_next) {
		/*
		 * Write block with a next pointer of 0, then
		 * restore its real value.
		 */
		old_next = fr->fr_free.f_next;
		fr->fr_free.f_next = 0;
		write_sec(fr->fr_this, &fr->fr_free);
		fr->fr_free.f_next = old_next;

		/*
		 * Now update the previous element in the free
		 * list so it points to this in-place and safe
		 * block.
		 */
		if (prev_blk) {
			write_sec(prev_blk->fr_this, &prev_blk->fr_free);
		}
		prev_blk = fr;
	}

	/*
	 * Last block should've had a next pointer of 0 anyway
	 */
	ASSERT_DEBUG(old_next == 0,
		"compress_freelist: last block w. next ptr");
}


/*
 * queue_free()
 *	Queue a free block
 *
 * There's always this fun issue in system design where it takes a
 * resource to free a resource.  We try to control damage here by
 * queueing the block, so we can free it up at a later point.  At
 * worst, we drop a block.
 */
static void
queue_free(daddr_t d)
{
	daddr_t *p;

	p = realloc(free_pend, (free_npend+1) * sizeof(daddr_t));
	if (!p) {
		lost_blocks += 1;
		return;
	}
	free_pend = p;
	free_pend[free_npend++] = d;
}

/*
 * compress_freelist()
 *	Coalesce entries and fiddle layout so all freelist's are half-full
 */
static void
compress_freelist(void)
{
	struct freelist *fr;
	struct free *f;

	for (fr = freelist; fr; fr = fr->fr_next) {
		f = &fr->fr_free;

		/*
		 * If this freelist block has too many, push some forward
		 */
		if (f->f_nfree > NALLOC/2) {
			move_forward(fr, fr->fr_next, f->f_nfree-NALLOC/2);
			continue;
		}

		/*
		 * Otherwise pull some back while we're short of 1/2
		 */
		while ((f->f_nfree < NALLOC/2) && fr->fr_next) {
			uint cnt;
			struct freelist *f2 = fr->fr_next;

			/*
			 * Take either as many as are needed, or as
			 * many as are available.
			 */
			cnt = f2->fr_free.f_nfree;
			if (cnt > (NALLOC/2 - f->f_nfree)) {
				cnt = NALLOC/2 - f->f_nfree;
			}
			move_back(fr, f2, cnt);

			/*
			 * If we have emptied the following entry, remove
			 * it from the list and free it.
			 */
			if (f2->fr_free.f_nfree == 0) {
				fr->fr_next = f2->fr_next;
				queue_free(f2->fr_this);
				free(f2);
			}
		}
	}

	/*
	 * Now that we've fiddled it all in memory, commit to disk
	 */
	write_freelist();
}

/*
 * free_block()
 *	Free the contiguous block range
 */
void
free_block(daddr_t d, uint nblk)
{
	struct freelist *fr, *ff;
	struct free *f = 0;	/* = 0 for -Wall */

	ASSERT_DEBUG(nblk > 0, "free_block: zero len");
retry:
	/*
	 * Scan the free list looking for the right place for this
	 * chunk.  "ff" trails and will be the place we insert the
	 * free chunk.
	 */
	for (ff = fr = freelist; fr; fr = fr->fr_next) {
		/*
		 * This would be an easy place to put it
		 */
		f = &fr->fr_free;
		if (f->f_nfree == 0) {
			ff = fr;
			continue;
		}

		/*
		 * If it must lie here or before, stop
		 */
		if (d < f->f_free[0].a_start) {
			break;
		}

		/*
		 * It lies beyond or within
		 */
		ff = fr;
		if (d < f->f_free[f->f_nfree-1].a_start) {
			break;
		}
	}
	ASSERT(ff, "free_block: no block");

	/*
	 * Now put this block into its place.  Note that contiguous
	 * space which spans freelist sections is not coalesced until
	 * we compress the freelist.  The result of compression is
	 * a generously balanced freelist; forward progress on the retry
	 * is guaranteed.
	 */
	f = &ff->fr_free;
	if (free_chunk(f, d, nblk)) {
		compress_freelist();
		goto retry;
	}
#ifdef DEBUG
	/* Not strictly needed, but useful for debugging */
	write_sec(ff->fr_this, f);
#endif

	/*
	 * If there are pending blocks, iterate with one of them
	 */
	if (free_npend > 0) {
		free_npend -= 1;
		d = free_pend[free_npend];
		nblk = 1;

		/*
		 * When we clean the last of them up, free the storage
		 */
		if (free_npend == 0) {
			free(free_pend);
			free_pend = 0;
		}
		goto retry;
	}
}

/*
 * take_chunk()
 *	Remove blocks from freelist entry, return amount taken
 *
 * Since this is used only for extension operations, the space to be
 * taken always resides at the front of the block.  The block is trimmed
 * or removed if entirely consumed.
 */
static ulong
take_chunk(struct freelist *fr, uint idx, ulong nsec)
{
	struct alloc *a;
	struct free *f = &fr->fr_free;

	/*
	 * Cap at max of wanted or available
	 */
	a = &f->f_free[idx];
	if (nsec > a->a_len) {
		nsec = a->a_len;
	}

	/*
	 * If consumed, close up gap.  Otherwise just update entry.
	 */
	if (nsec == a->a_len) {
		f->f_nfree -= 1;
		bcopy(a+1, a, (f->f_nfree-idx)*sizeof(struct alloc));
	} else {
		a->a_start += nsec;
		a->a_len -= nsec;
	}

	/*
	 * Flush free list slot
	 */
	write_sec(fr->fr_this, f);

	return(nsec);
}

/*
 * take_block()
 *	Try to take some blocks at the given location
 *
 * This is used during file extension to try and grow a file contiguously.
 * Will take up to nsec sectors out of the list, and returns the amount
 * actually taken.  On failure, returns 0.
 */
ulong
take_block(daddr_t d, ulong nsec)
{
	struct freelist *fr;

	for (fr = freelist; fr; fr = fr->fr_next) {
		struct alloc *a;
		ulong x;
		struct free *f = &fr->fr_free;

		/*
		 * Ignore empty blocks
		 */
		if (f->f_nfree == 0) {
			continue;
		}

		/*
		 * If the requested block is before this, then it
		 * does not exist in the free list.
		 */
		a = f->f_free;
		if (d < a->a_start) {
			return(0);
		}

		/*
		 * If it falls after this part of the free list, continue
		 * the loop.
		 */
		a += (f->f_nfree - 1);
		if (d >= (a->a_start + a->a_len)) {
			continue;
		}

		/*
		 * It resides in this block, or not at all
		 */
		a = f->f_free;
		for (x = 0; x < f->f_nfree; ++x, ++a) {
			if (d == a->a_start) {
				ulong l;

				l = take_chunk(fr, x, nsec);
				return(l);
			}
		}

		/*
		 * Didn't find it--return failure
		 */
		break;
	}
	return(0);
}
