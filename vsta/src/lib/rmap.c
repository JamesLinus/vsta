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
#include <lib/rmap.h>
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
	r->r_off = 0;
	r->r_size = size;
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
	if (r > rlim)
		return(0);

	/*
	 * Trim the resource element if it still has some left,
	 * otherwise delete from the list.
	 */
	idx = r->r_off;
	if (r->r_size > size) {
		r->r_off += size;
		r->r_size -= size;
	} else {
		rmap->r_off -= 1;
		if (r < rlim) {
			bcopy(r+1, r, sizeof(struct rmap)*(rlim-r));
		}
	}
	return(idx);
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
	if (rmap->r_size == rmap->r_off) {
		/*
		 * Nope.  Tabulate and lose the space.
		 */
		lost_elems += size;
		return;
	}
	rmap->r_off += 1;

	/*
	 * If we found the place to insert, do so
	 */
	if (r <= rlim) {
		bcopy(r, r+1, sizeof(struct rmap) * ((rlim-r)+1));
		r->r_off = off;
		r->r_size = size;
		return;
	}

	/*
	 * Otherwise add it at the end
	 */
	++rlim;
	rlim->r_off = off;
	rlim->r_size = size;
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
