/*
 * atl.c
 *	Routines for maintaining attach lists
 *
 * To implement a two-handed clock paging algorithm, you need to be
 * able to enumerate the translations which are active on a physical
 * page.  This is done using linked lists of pviews and offsets.
 *
 * Mutexing of the c_atl field is provided by the page lock for the
 * page.
 */
#include <sys/types.h>
#include <sys/core.h>
#include <sys/assert.h>
#include <sys/vm.h>

extern struct core *core;
extern void *malloc();

/*
 * add_atl()
 *	Add an attach list element for the given view
 *
 * Handles locking of page directly
 */
void
add_atl(uint pfn, struct pview *pv, uint off)
{
	struct core *c;
	struct atl *a;

	a = malloc(sizeof(struct atl));
	a->a_pview = pv;
	a->a_idx = off;
	c = &core[pfn];
	lock_page(pfn);
	a->a_next = c->c_atl;
	c->c_atl = a;
	unlock_page(pfn);
}

/*
 * delete_atl()
 *	Delete the translation for the given view
 *
 * Return value is 0 if the entry could be found; 1 if not.
 * Handles locking within this routine.
 */
delete_atl(uint pfn, struct pview *pv, uint off)
{
	struct core *c;
	struct atl *a, **ap;

	c = &core[pfn];
	ap = &c->c_atl;

	/*
	 * Search for the mapping into our view
	 */
	lock_page(pfn);
	for (a = c->c_atl; a; a = a->a_next) {
		if ((a->a_pview == pv) && (a->a_idx == off)) {
			*ap = a->a_next;
			break;
		}
		ASSERT_DEBUG(a->a_pview != pv,
			"delete_atl: multiple mapping?");
		ap = &a->a_next;
	}
	unlock_page(pfn);

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
