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
#include "../mach/mutex.h"
#include "pset.h"

/*
 * Map generic pset data to COW use
 */
#define p_cow p_data

/*
 * Our pset ops
 */
extern struct portref *swapdev;
extern int pset_writeslot();

static int cow_fillslot(), cow_writeslot(), cow_init();
static void cow_dup(), cow_free(), cow_lastref();
static struct psetops psop_cow = {cow_fillslot, cow_writeslot, cow_init,
	cow_dup, cow_free, cow_lastref};

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
		p_lock_fast(&cow->p_lock, SPL0);
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
static int
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

/*
 * cow_free()
 *	Release a COW page set
 */
static void
cow_free(struct pset *ps)
{
	struct pset *ps2, **psp, *p;

	ASSERT_DEBUG(!valid_pset_slots(ps), "cow_free: still refs");

	/*
	 * Free our reference to the master set on PT_COW
	 */
	ps2 = ps->p_cow;
	p_lock_fast(&ps2->p_lock, SPL0);
	psp = &ps2->p_cowsets;
	for (p = ps2->p_cowsets; p; p = p->p_cowsets) {
		if (p == ps) {
			*psp = p->p_cowsets;
			break;
		}
		psp = &p->p_cowsets;
	}
	ASSERT(p, "cow_free: lost cow");
	v_lock(&ps2->p_lock, SPL0_SAME);

	/*
	 * Remove our reference from him
	 */
	deref_pset(ps2);
}

/*
 * add_cowset()
 *	Add a new pset to the list of COW sets under a master
 */
static void
add_cowset(struct pset *pscow, struct pset *ps)
{
	/*
	 * Attach to the underlying pset
	 */
	ref_pset(pscow);
	p_lock_fast(&pscow->p_lock, SPL0);
	ps->p_cowsets = pscow->p_cowsets;
	pscow->p_cowsets = ps;
	ps->p_cow = pscow;
	v_lock(&pscow->p_lock, SPL0_SAME);
}

/*
 * cow_dup()
 *	Duplicate a COW pset
 */
static void
cow_dup(struct pset *ops, struct pset *ps)
{
	/*
	 * This is a new COW reference into the master pset
	 */
	add_cowset(ops->p_cow, ps);
}

/*
 * alloc_pset_cow()
 *	Allocate a COW pset in terms of another
 */
struct pset *
alloc_pset_cow(struct pset *psold, uint off, uint len)
{
	struct pset *ps;
	uint swapblk;

	ASSERT_DEBUG(psold->p_type != PT_COW, "pset_cow: cow of cow");

	/*
	 * Get swap for our pages.  We get all the room we'll need
	 * if all pages are written.
	 */
	swapblk = alloc_swap(len);
	if (swapblk == 0) {
		return(0);
	}
	ps = alloc_pset(len);
	ps->p_off = off;
	ps->p_swapblk = swapblk;
	ps->p_type = PT_COW;
	ps->p_ops = &psop_cow;
	add_cowset(psold, ps);

	return(ps);
}

/*
 * cow_lastref()
 *	Last ref dropped on slot
 */
static void
cow_lastref(struct pset *ps, struct perpage *pp, uint idx)
{
	struct perpage *pp2;
	struct pset *ps2;

	/*
	 * On last ref of a COW slot, we clear the COW state
	 * and remove the reference of the underlying master copy
	 */
	if (pp->pp_flags & PP_COW) {
		ps2 = ps->p_cow;
		idx += ps->p_off;
		p_lock_fast(&ps2->p_lock, SPL0);
		pp2 = find_pp(ps2, idx);
		lock_slot(ps2, pp2);
		deref_slot(ps2, pp2, idx);
		unlock_slot(ps2, pp2);
	} else {
		free_page(pp->pp_pfn);
	}
	pp->pp_flags &= ~(PP_COW | PP_V);
}
