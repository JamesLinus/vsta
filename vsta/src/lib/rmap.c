/*
 * rmap.c
 *	Routines for working with a resource map
 *
 * The first entry in a resource map is interpreted as a header.
 * The r_size field tells how many slots the overall data structure
 * has; r_off tells how many non-empty elements exist.
 *
 * The list is kept in ascending order with all non-empty elements
 * first.
 */
#include <rmap.h>
#include <sys/assert.h>

ulong lost_elems = 0L;	/* Diagnostic for space lost due to fragmentation */

/*
 * rmap_init()
 *	Initialize the named resource map
 *
 * "size" is the total size, including the element we will use
 * as the header.
 */
void
rmap_init(struct rmap *r, uint size)
{
	/*
	 * Record # slots available, which is one less (since we use the
	 * first as our own header.
	 */
	r->r_off = 0;
	r->r_size = size-1;
}

/*
 * collapse()
 *	An entry has been emptied, so make it disappear
 */
static void
collapse(struct rmap *rmap, struct rmap *r)
{
	struct rmap *rlim = &rmap[rmap->r_off];

	rmap->r_off -= 1;
	if (r < rlim) {
		bcopy(r+1, r, sizeof(struct rmap)*(rlim-r));
	}
}

/*
 * rmap_alloc()
 *	Allocate some space from a resource map
 *
 * Returns 0 on failure.  Thus, you can't store index 0 in a resource map
 */
uint
rmap_alloc(struct rmap *rmap, uint size)
{
	struct rmap *r, *rlim;
	uint idx;

	ASSERT_DEBUG(size > 0, "rmap_alloc: zero size");
	/*
	 * Find first slot with a fit, return failure if we run
	 * off the end of the list without finding a fit.
	 */
	rlim = &rmap[rmap->r_off];
	for (r = &rmap[1]; r <= rlim; ++r) {
		if (r->r_size >= size)
			break;
	}
	if (r > rlim) {
		return(0);
	}

	/*
	 * Trim the resource element if it still has some left,
	 * otherwise delete from the list.
	 */
	idx = r->r_off;
	if (r->r_size > size) {
		r->r_off += size;
		r->r_size -= size;
	} else {
		collapse(rmap, r);
	}
	return(idx);
}

/*
 * makespace()
 *	Insert room for a new slot at the given position
 *
 * Returns 1 if there isn't room to insert the element, 0 on success.
 */
static int
makespace(struct rmap *rmap, struct rmap *r)
{
	struct rmap *rlim = &rmap[rmap->r_off];

	/*
	 * If no room to insert slot, return failure
	 */
	if (rmap->r_size == rmap->r_off) {
		return(1);
	}
	rmap->r_off += 1;

	/*
	 * If inserting in middle, slide up entries
	 */
	if (r <= rlim) {
		bcopy(r, r+1, sizeof(struct rmap) * ((rlim-r)+1));
		return(0);
	}

	/*
	 * Otherwise it's added at the end
	 */
	ASSERT_DEBUG(r == rlim+1, "rmap makespace: invalid insert");
	return(0);
}

/*
 * rmap_free()
 *	Free some space back into the resource map
 *
 * The list is kept ordered, with the first free element flagged
 * with a size of 0.
 */
void
rmap_free(struct rmap *rmap, uint off, uint size)
{
	struct rmap *r, *rlim;

	ASSERT_DEBUG(off, "rmap_free: zero off");
	ASSERT_DEBUG(size > 0, "rmap_free: zero size");
	/*
	 * Scan forward until we find the place we should be
	 * inserted.
	 */
	rlim = &rmap[rmap->r_off];
	for (r = &rmap[1]; r <= rlim; ++r) {
		/*
		 * If the new free space abuts this entry, tack it
		 * on and return.
		 */
		if ((r->r_off + r->r_size) == off) {
			r->r_size += size;
			/*
			 * If this entry now abuts the next, coalesce
			 */
			if ((r < rlim) && ((r->r_off+r->r_size) ==
					(r[1].r_off))) {
				r->r_size += r[1].r_size;
				rmap->r_off -= 1;
				++r;
				if (r < rlim) {
					bcopy(r+1, r,
					 sizeof(struct rmap)*(rlim-r));
				}
			}
			return;
		}

		/*
		 * If this space abuts the entry, pad it onto
		 * the beginning.
		 */
		if ((off + size) == r->r_off) {
			r->r_size += size;
			r->r_off = off;
			return;
		}

		if (off < r->r_off)
			break;
	}

	/*
	 * Need to add a new element.  See if it'll fit.
	 */
	if (makespace(rmap, r)) {
		/*
		 * Nope.  Tabulate and lose the space.
		 */
		lost_elems += size;
		return;
	}

	/*
	 * Record it
	 */
	r->r_off = off;
	r->r_size = size;
}

/*
 * rmap_grab()
 *	Grab the requested range, or return failure if any of it is not free
 *
 * Returns 0 on success, 1 on failure.
 */
int
rmap_grab(struct rmap *rmap, uint off, uint size)
{
	struct rmap *r, *rlim;
	uint x, top, rtop;

	rlim = &rmap[rmap->r_off];
	for (r = &rmap[1]; r <= rlim; ++r) {
		/*
		 * If we've advanced beyond the requested offset,
		 * we can never match, so end the loop.
		 */
		if (r->r_off > off) {
			break;
		}

		/*
		 * See if this is the range which will hold our request
		 */
		top = r->r_off + r->r_size;
		if (!((r->r_off <= off) && (top > off))) {
			continue;
		}

		/*
		 * Since this run encompasses the requested range, we
		 * can either grab all of it, or none of it.
		 */

		/*
		 * The top of our request extends beyond the block; fail.
		 */
		rtop = off + size;
		if (rtop > top) {
			return(1);
		}

		/*
		 * If the requested start matches our range's start, we
		 * can simply shave it off the front.
		 */
		if (off == r->r_off) {
			r->r_off += size;
			r->r_size -= size;
			if (r->r_size == 0) {
				collapse(rmap, r);
			}
			return(0);
		}

		/*
		 * Similarly, if the end matches, we can shave the end
		 */
		if (rtop == top) {
			r->r_size -= size;

			/*
			 * We know r_size > 0, since otherwise we would've
			 * matched the previous case otherwise.
			 */
			ASSERT_DEBUG(r->r_size > 0, "phase error");

			return(0);
		}

		/*
		 * Otherwise we must split the range
		 */
		if (makespace(rmap, r)) {
			uint osize;

			/*
			 * No room for further fragmentation, so chop off
			 * to the tail.
			 */
			osize = r->r_size;
			r->r_size = top - rtop;
			lost_elems += (osize - r->r_size) - size;
			r->r_off = rtop;
			return(0);
		}

		/*
		 * The current slot is everything below us
		 */
		r->r_size = off - r->r_off;

		/*
		 * The new slot is everything above
		 */
		++r;
		r->r_off = rtop;
		r->r_size = top - rtop;

		/*
		 * OK
		 */
		return(0);
	}

	/*
	 * Nope, not in our range of free entries
	 */
	return(1);
}

#ifdef DEBUG
/*
 * rmap_dump()
 *	Pretty-print a resource map
 */
void
rmap_dump(struct rmap *rmap)
{
	struct rmap *r, *rlim;

	printf("rmap@0x%x has %d of %d elements, lost %ld\n",
		rmap, rmap->r_off, rmap->r_size, lost_elems);
	rlim = &rmap[rmap->r_off];
	for (r = &rmap[1]; r <= rlim; ++r) {
		printf(" [%d..%d]", r->r_off, (r->r_off+r->r_size)-1);
	}
	printf("\n");
}
#endif /* DEBUG */
