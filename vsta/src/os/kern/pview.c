/*
 * pview.c
 *	Routines for creating/deleting pviews
 */
#include <sys/vas.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/malloc.h>
#include <sys/assert.h>
#include <std.h>
#include "../mach/mutex.h"
#include "pset.h"

/*
 * alloc_pview()
 *	Create a pview in terms of the given pset
 */
struct pview *
alloc_pview(struct pset *ps)
{
	struct pview *pv;

	pv = MALLOC(sizeof(struct pview), MT_PVIEW);
	bzero(pv, sizeof(struct pview));
	pv->p_set = ps;
	ref_pset(ps);
	pv->p_len = ps->p_len;
	return(pv);
}

/*
 * free_pview()
 *	Delete view, remove reference to pset
 */
void
free_pview(struct pview *pv)
{
	deref_pset(pv->p_set);
	FREE(pv, MT_PVIEW);
}

/*
 * dup_view()
 *	Duplicate an existing view
 */
struct pview *
dup_pview(struct pview *opv)
{
	struct pview *pv;

	pv = MALLOC(sizeof(struct pview), MT_PVIEW);
	bcopy(opv, pv, sizeof(*pv));
	ref_pset(pv->p_set);
	pv->p_next = 0;
	pv->p_vas = 0;
	return(pv);
}

/*
 * copy_view()
 *	Make a copy of the underlying set, create a view to it
 */
struct pview *
copy_pview(struct pview *opv)
{
	struct pset *ps;
	struct pview *pv;

	pv = dup_pview(opv);
	ps = copy_pset(opv->p_set, pv->p_off, pv->p_len);
	deref_pset(opv->p_set);
	pv->p_set = ps;
	ref_pset(ps);
	return(pv);
}

/*
 * remove_pview()
 *	Given vaddr, remove corresponding pview
 *
 * Frees pview after deref'ing the associated pset
 */
void
remove_pview(struct vas *vas, void *vaddr)
{
	struct pview *pv;
	extern struct pview *detach_pview();

	pv = detach_pview(vas, vaddr);
	deref_pset(pv->p_set);
	FREE(pv, MT_PVIEW);
}

/*
 * attach_valid_slots()
 *	Walk through pview, attach translations for all valid slots
 */
void
attach_valid_slots(struct pview *pv)
{
	uint x;
	struct pset *ps = pv->p_set;
	uint idx = pv->p_off;

	for (x = 0; x < pv->p_len; ++x,++idx) {
		struct perpage *pp;

		pp = find_pp(ps, idx);
		if (pp->pp_flags & PP_V) {
			add_atl(pp, pv, x);
			hat_addtrans(pv, (char *)pv->p_vaddr + ptob(x),
				pp->pp_pfn, pv->p_prot |
				((pp->pp_flags & PP_COW) ? PROT_RO : 0));
			ref_slot(ps, pp, idx);
		}
	}
}
