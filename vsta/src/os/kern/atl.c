/*
 * atl.c
 *	Routines for maintaining attach lists
 *
 * To implement a two-handed clock paging algorithm, you need to be
 * able to enumerate the translations which are active on a physical
 * page.  This is done using a couple levels of back links.  The
 * core entry points back to the "master" pset's slot using c_pset
 * and c_psidx.  Each slot in the pset holds a linked list of attached
 * views, starting at the p_atl field.  Finally, each COW pset can be
 * enumerated via the p_cowsets field.
 *
 * Mutexing of the p_atl field is provided by the slot lock for the
 * slot in the pset.  Mutexing for the c_pset/c_psidx is done using
 * the lock_page() interface.
 */
#include <sys/types.h>
#include <sys/pset.h>
#include <sys/assert.h>
#include <lib/alloc.h>

/*
 * add_atl()
 *	Add an attach list element for the given view
 *
 * Assumes slot for perpage is already locked.
 */
void
add_atl(struct perpage *pp, struct pview *pv, uint off)
{
	struct atl *a;

	a = malloc(sizeof(struct atl));
	a->a_pview = pv;
	a->a_idx = off;
	a->a_next = pp->pp_atl;
	pp->pp_atl = a;
}

/*
 * delete_atl()
 *	Delete the translation for the given view
 *
 * Return value is 0 if the entry could be found; 1 if not.
 * Page slot is assumed to be held locked.
 */
delete_atl(struct perpage *pp, struct pview *pv, uint off)
{
	struct atl *a, **ap;

	/*
	 * Search for the mapping into our view
	 */
	ap = &pp->pp_atl;
	for (a = pp->pp_atl; a; a = a->a_next) {
		if ((a->a_pview == pv) && (a->a_idx == off)) {
			*ap = a->a_next;
			break;
		}
		ap = &a->a_next;
	}

	/*
	 * If we didn't find it, return failure.
	 */
	if (!a) {
		return(1);
	}

	/*
	 * Otherwise free the memory and return success.
	 */
	free(a);
	return(0);
}
