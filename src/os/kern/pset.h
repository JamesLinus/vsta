#ifndef _KERN_PSET_H
#define _KERN_PSET_H
/*
 * pset.h
 *	Inlined page set routines
 */
#include <sys/vm.h>

/*
 * find_pp()
 *	Given pset and index, return perpage information
 */
inline extern struct perpage *
find_pp(struct pset *ps, uint idx)
{
	ASSERT_DEBUG(idx < ps->p_len, "find_pp: bad index");
	ASSERT_DEBUG(ps->p_perpage, "find_pp: no perpage");
	return(&ps->p_perpage[idx]);
}

/*
 * ref_pset()
 *	Add a reference to a pset
 */
inline extern void
ref_pset(struct pset *ps)
{
	ATOMIC_INC(&ps->p_refs);
}

/*
 * deref_slot()
 *	Decrement reference count on a page slot
 *
 * This routine assumes that it is being called under a locked slot.  On
 * last reference, we let the pset layer know, then clear PP_V and free
 * the page.
 */
inline extern void
deref_slot(struct pset *ps, struct perpage *pp, uint idx)
{
	ASSERT_DEBUG(pp->pp_refs > 0, "deref_slot: zero");
	if ((pp->pp_refs -= 1) == 0) {
		ASSERT_DEBUG(pp->pp_flags & PP_V, "deref_slot: ref !v");
		(*(ps->p_ops->psop_lastref))(ps, pp, idx);
	}
}

/*
 * ref_slot()
 *	Add a reference to a page slot
 *
 * Assumes caller holds the page slot locked.
 */
inline extern void
ref_slot(struct pset *ps, struct perpage *pp, uint idx)
{
	pp->pp_refs += 1;
	ASSERT_DEBUG(pp->pp_refs > 0, "ref_slot: overflow");
}

#endif /* _KERN_PSET_H */
