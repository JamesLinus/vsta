/*
 * vas.c
 *	Routines for changing and searching the virtual address space
 */
#include <sys/vas.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/vm.h>
#include <sys/core.h>
#include <sys/assert.h>
#include <sys/hat.h>
#include <sys/malloc.h>
#include "../mach/mutex.h"
#include "pset.h"

/*
 * CONTAINS()
 *	Tell if a virtual address lies within the pview
 */
#define CONTAINS(pv, va) \
	(((pv)->p_vaddr <= (va)) && \
	 (((char *)((pv)->p_vaddr) + ptob((pv)->p_len)) > \
		(char *)(va)))

/*
 * find_pview()
 *	Find a page view given a vas and a vaddr
 *
 * A NULL is returned on failure to find a pview containing the vaddr.
 * On success, the pview is returned, and the pset under it has a lock.
 */
struct pview *
find_pview(struct vas *vas, void *vaddr)
{
	struct pview *pv;

	p_lock_void(&vas->v_lock, SPL0);
	for (pv = vas->v_views; pv; pv = pv->p_next) {
		if (CONTAINS(pv, vaddr)) {
			p_lock_void(&pv->p_set->p_lock, SPL0_SAME);
			v_lock(&vas->v_lock, SPL0_SAME);
			return(pv);
		}
	}
	v_lock(&vas->v_lock, SPL0_SAME);
	return(0);
}

/*
 * detach_pview()
 *	Remove the pview from a vas containing the given address
 *
 * Also releases references which view holds on pset slots.  pset itself
 * still holds a reference from this view.
 */
struct pview *
detach_pview(struct vas *vas, void *vaddr)
{
	struct pset *ps;
	struct perpage *pp;
	int x;
	struct pview *pv, **pvp;

	/*
	 * Remove our pview from the vas.  It's singly linked, so we
	 * have to search from the front.
	 */
	p_lock_void(&vas->v_lock, SPL0);
	pv = vas->v_views;
	pvp = &vas->v_views;
	for ( ; pv; pv = pv->p_next) {
		if (CONTAINS(pv, vaddr)) {
			*pvp = pv->p_next;
			break;
		}
		pvp = &pv->p_next;
	}
	ASSERT(pv, "detach_pview: lost a pview");
	v_lock(&vas->v_lock, SPL0_SAME);
	ps = pv->p_set;

	/*
	 * Walk each valid slot, and tear down any HAT translation.
	 */
	p_lock_void(&ps->p_lock, SPL0_SAME);
	for (x = 0; x < pv->p_len; ++x) {
		uint pfn, idx;

		idx = pv->p_off + x;
		pp = find_pp(ps, idx);

		/*
		 * Can't be a translation if not a valid page
		 */
		if (!(pp->pp_flags & PP_V)) {
			continue;
		}
		pfn = pp->pp_pfn;

		/*
		 * Lock the slot.  Lock the underlying page.  Delete
		 * translation, and deref our use of the slot.
		 */
		lock_slot(ps, pp);
		if (delete_atl(pp, pv, x) == 0) {
			char *va;

			/*
			 * Valid in our view; delete HAT translation.  Record
			 * ref/mod bits for final time.
			 */
			va = pv->p_vaddr;
			va += ptob(x);
			hat_deletetrans(pv, va, pfn);
			pp->pp_flags |= hat_getbits(pv, va);

			/*
			 * Release our reference to the slot
			 */
			deref_slot(ps, pp, idx);
		}
		unlock_slot(ps, pp);

		/*
		 * Reacquire pset lock
		 */
		p_lock_void(&ps->p_lock, SPL0_SAME);
	}

	/*
	 * Release lock on pset
	 */
	v_lock(&ps->p_lock, SPL0_SAME);

	/*
	 * Let hat hear about detach
	 */
	hat_detach(pv);

	return(pv);
}

/*
 * free_vas()
 *	Dump all pviews under a vas
 *
 * Assumes implicit serialization of an exit() scenario
 */
void
free_vas(struct vas *vas)
{
	while (vas->v_views) {
		remove_pview(vas, vas->v_views->p_vaddr);
	}
	hat_freevas(vas);
}

/*
 * attach_pview()
 *	Attach a pview to the vas.
 *
 * The prot, len, and vaddr fields should already be set up
 */
void *
attach_pview(struct vas *vas, struct pview *pv)
{
	void *vaddr;

	/*
	 * Let HAT choose a virtual address
	 */
	pv->p_vas = vas;
	if (hat_attach(pv, pv->p_vaddr)) {
		pv->p_vas = 0;
		return(0);
	}
	vaddr = pv->p_vaddr;

	/*
	 * Now that we're committed, link it onto the vas
	 */
	p_lock_void(&vas->v_lock, SPL0);
	pv->p_next = vas->v_views;
	vas->v_views = pv;
	v_lock(&vas->v_lock, SPL0_SAME);
	return(vaddr);
}

/*
 * alloc_zfod()
 *	Create a ZFOD pset, add it to the named vas
 */
void *
alloc_zfod(struct vas *vas, uint pages)
{
	struct pset *ps;
	struct pview *pv;
	void *vaddr;

	/*
	 * Get a pset and pview
	 */
	ps = alloc_pset_zfod(pages);
	pv = alloc_pview(ps);

	/* 
	 * Try to attach.  If fails, throw them away.
	 */
	if ((vaddr = attach_pview(vas, pv)) == 0) {
		free_pview(pv);
		/* Frees pset as this was only reference */
		pv = 0;
	}
	return(vaddr);
}

/*
 * fork_vas()
 *	Create new vas for thread given existing one
 *
 * Read-only views are duplicated, as are read/write views of
 * psets which are PF_SHARED.  Others are copied.  This is odious
 * enough for in-core dirty pages, but we must also bring in
 * pages on swap in order to make a copy.
 */
void
fork_vas(struct vas *ovas, struct vas *vas)
{
	char *vaddr = 0;
	struct pview *closest;
	struct pview pv2;

	/*
	 * Set initial fields.  Assume that the new vas has been
	 * bzero()'ed.
	 */
	init_lock(&vas->v_lock);
	hat_initvas(vas);

	/*
	 * Duplicate pviews in order of virtual address.  We do this so
	 * we can release the lock on the old vas, but still make
	 * forward progress.
	 */
	for (;;) {
		struct pview *pv;
		struct pset *ps;

		/*
		 * Scan across vas for pview with virtual address closest
		 * to, but above or equal to, vaddr.
		 */
		closest = 0;
		p_lock_void(&ovas->v_lock, SPL0);
		for (pv = ovas->v_views; pv; pv = pv->p_next) {
			if ((char *)pv->p_vaddr <= vaddr) {
				continue;
			}
			if (!closest || (pv->p_vaddr < closest->p_vaddr)) {
				closest = pv;
			}
		}

		/*
		 * If no more, leave loop
		 */
		if (!closest) {
			v_lock(&ovas->v_lock, SPL0_SAME);
			break;
		}

		/*
		 * If found one, lock it.  Release vas.  Add a reference so
		 * it won't go away while we're fiddling with it.  We must
		 * duplicate the pview, since it might go away once we
		 * release the vas lock.
		 */
		ps = closest->p_set;
		pv2 = *closest;
		vaddr = pv2.p_vaddr;
		ref_pset(ps);
		v_lock(&ovas->v_lock, SPL0_SAME);

		/*
		 * If read-only or shared, dup the view
		 */
		if ((pv2.p_prot & PROT_RO) ||
				(ps->p_flags & PF_SHARED)) {
			pv = dup_pview(&pv2);
		} else {
			pv = copy_pview(&pv2);
		}

		/*
		 * Add to our new vas
		 */
		pv->p_prot |= PROT_FORK;
		(void)attach_pview(vas, pv);
		pv->p_prot &= ~PROT_FORK;

		/*
		 * Connect to all the valid pages under the view
		 */
		attach_valid_slots(pv);

		/*
		 * Release our "safety" reference on the underlying pset
		 */
		deref_pset(ps);
	}

	/*
	 * Let hat handle any final details of duplication
	 */
	hat_fork(ovas, vas);
}

/*
 * alloc_zfod_vaddr()
 *	Create a demand-fill zero view
 *
 * Similar to alloc_zfod(), except that the caller chooses the address.
 */
void *
alloc_zfod_vaddr(struct vas *vas, uint pages, void *vaddr)
{
	struct pset *ps;
	struct pview *pv;

	ps = alloc_pset_zfod(pages);
	pv = alloc_pview(ps);
	pv->p_vaddr = vaddr;
	pv->p_prot = 0;
	if ((vaddr = attach_pview(vas, pv)) == 0) {
		free_pview(pv);
		pv = 0;
	}
	return(vaddr);
}
