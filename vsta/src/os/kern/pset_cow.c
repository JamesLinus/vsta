/*
 * pset_cow.c
 *	Routines for implementing copy-on-write psets
 */
#include <sys/types.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/qio.h>
#include <sys/fs.h>
#include <sys/vm.h>
#include <sys/assert.h>

extern void *malloc();
extern struct portref *swapdev;
extern int pset_writeslot(), pset_deinit();

static int cow_fillslot(), cow_writeslot(), cow_init();
struct psetops psop_cow = {cow_fillslot, cow_writeslot, cow_init,
	pset_deinit};

/*
 * cow_init()
 *	Set up pset for a COW view of another pset
 */
static
cow_init(struct pset *ps)
{
	return(0);
}

/*
 * cow_fillslot()
 *	Fill pset slot from the underlying master copy
 */
static
cow_fillslot(struct pset *ps, struct perpage *pp, uint idx)
{
	struct perpage *pp2;
	uint idx2;
	uint pg;

	ASSERT_DEBUG(!(pp->pp_flags & (PP_V|PP_BAD)),
		"cow_fillslot: valid");

	/*
	 * If we're on swap, bring in from there
	 */
	if (pp->pp_flags & PP_SWAPPED) {
		pg = alloc_page();
		set_core(pg, ps, idx);
		if (pageio(pg, swapdev, ps->p_swapblk + idx,
				NBPG, FS_ABSREAD)) {
			free_page(pg);
			return(1);
		}
	} else {
		struct pset *cow = ps->p_cow;

		/*
		 * Lock slot of underlying pset
		 */
		idx2 = ps->p_off + idx;
		p_lock(&cow->p_lock, SPL0);
		pp2 = find_pp(cow, idx2);
		lock_slot(cow, pp2);

		/*
		 * If the memory isn't available, call its
		 * slot filling routine.
		 */
		if (!(pp2->pp_flags & PP_V)) {
			if ((*(cow->p_ops->psop_fillslot))
					(cow, pp2, idx2)) {
				unlock_slot(cow, pp2);
				return(1);
			}
			ASSERT_DEBUG(pp2->pp_flags & PP_V,
				"cow_fillslot: cow fill !v");
		} else {
			pp2->pp_refs += 1;
		}

		/*
		 * Always initially fill with sharing of page.  We'll
		 * break the sharing and copy soon, if needed.
		 */
		pg = pp2->pp_pfn;
		pp->pp_flags |= PP_COW;
		unlock_slot(cow, pp2);
	}

	/*
	 * Fill in the new page's value
	 */
	pp->pp_refs = 1;
	pp->pp_flags |= PP_V;
	pp->pp_flags &= ~(PP_M|PP_R);
	pp->pp_pfn = pg;
	return(0);
}

/*
 * cow_writeslot()
 *	Write pset slot out to swap as needed
 *
 * The caller may sleep even with async set to true, if the routine has
 * to sleep waiting for a qio element.  The routine is called with the
 * slot locked.  On non-async return, the slot is still locked.  For
 * async I/O, the slot is unlocked on I/O completion.
 */
static
cow_writeslot(struct pset *ps, struct perpage *pp, uint idx, voidfun iodone)
{
	return(pset_writeslot(ps, pp, idx, iodone));
}

/*
 * cow_write()
 *	Do the writing-cow action
 *
 * Copies the current contents, frees reference to the master copy,
 * and switches page slot to new page.
 */
void
cow_write(struct pset *ps, struct perpage *pp, uint idx)
{
	struct perpage *pp2;
	uint idx2 = ps->p_off + idx;
	uint pg;

	pp2 = find_pp(ps->p_cow, idx2);
	pg = alloc_page();
	set_core(pg, ps, idx);
	ASSERT(pp2->pp_flags & PP_V, "cow_write: !v");
	bcopy(ptov(ptob(pp2->pp_pfn)), ptov(ptob(pg)), NBPG);
	deref_slot(ps->p_cow, pp2, idx2);
	pp->pp_pfn = pg;
	pp->pp_flags &= ~PP_COW;
}
