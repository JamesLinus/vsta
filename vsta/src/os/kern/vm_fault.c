/*
 * vm_fault.c
 *	Routines for resolving VM-type faults
 *
 * Basic strategy behind handling of VM faults:
 * A fault initally happens in an address space (vas).  The appropriate
 * pview is found, and the affected slot within the pview's pset is
 * locked.  The pset's slot is then filled via the psop_fillslot function.
 * On success, the resulting physical page number is mapped via the
 * hat layer, and the fault returns success.
 *
 * The obvious errors (no pview for vaddr, slot couldn't be filled, etc.)
 * are handled.
 *
 * Each physical page enumerates the mappings which exist for it using
 * the attach list (atl) which hangs off of the per-physical-page
 * structure (core).  psop_fillslot operates in terms of the pset, and
 * thus has no concept of these mappings.  Thus, the calling fault
 * handler must add the atl around the time it sets up the hat translation.
 * There is no check for an existing atl entry; the HAT must ensure that
 * translations are not lost until deleted.
 *
 * A pset which is a view of a file (via a port) has some unique needs.
 * A copy-on-write (COW) set which references a file may transfer from
 * the file's image to a private copy on write.  This would usually leave
 * the file's slot with zero references--which would free the page.  To
 * cache file contents in this event, a file's pset can maintain its own
 * view.  On psop_fillslot it will add a reference using its own view,
 * so the page may survive intact across multiple runs of an a.out.  Since
 * it is a view, and has an atl, it can still be paged out using the usual
 * paging mechanisms.
 *
 * Copy-on-write (COW) sets are tricky.  The affected slot in the cow pset
 * is locked first; then the psop_fillslot is called on this slot.  Within
 * this cow slot filling, the slot of the master set is locked, and a
 * further psop_fillslot is invoked.
 *
 * XXX move some of this discussion to where it belongs--pset.c maybe?
 */
#include <sys/types.h>
#include <mach/param.h>
#include <sys/vas.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/mutex.h>
#include <sys/percpu.h>
#include <sys/thread.h>
#include <sys/assert.h>
#include <sys/core.h>

extern struct perpage *find_pp();

/*
 * vas_fault()
 *	Process a fault within the given address space
 *
 * Returns 0 if the fault could be resolved, 1 if process needs to
 * receive an event.  The HAT layer is expected to reliably hold
 * a translation added via hat_addtrans() until hat_deletetrans().
 * A lost translation would cause the atl to hold multiple entries.
 */
vas_fault(void *vas, void *vaddr, int write)
{
	struct pview *pv;
	struct pset *ps;
	int holding_ps = 0;
	struct perpage *pp;
	int holding_pp = 0;
	uint idx;
	int error = 0;
	int wasvalid;

	/*
	 * Easiest--no view matches address
	 */
	if ((pv = find_pview(vas, vaddr)) == 0) {
		error = 1;
		goto out;
	}
	ps = pv->p_set;
	holding_ps = 1;

	/*
	 * Next easiest--trying to write to read-only view
	 */
	if (write && (pv->p_prot & PROT_RO)) {
		error = 1;
		goto out;
	}

	/*
	 * User accessing kernel-only view?
	 */
	if (!(curthread->t_flags & T_KERN) && (pv->p_prot & PROT_KERN)) {
		error = 1;
		goto out;
	}

	/*
	 * Transfer from pset lock to page slot lock
	 */
	idx = btop(((char *)vaddr - (char *)pv->p_vaddr)) + pv->p_off;
	pp = find_pp(ps, idx);
	lock_slot(ps, pp); holding_ps = 0; holding_pp = 1;

	/*
	 * If the slot is bad, can't fill
	 */
	if (pp->pp_flags & PP_BAD) {
		error = 1;
		goto out;
	}

	/*
	 * If slot is invalid, request it be filled.  Otherwise just
	 * add a reference.
	 */
	if (!(pp->pp_flags & PP_V)) {
		wasvalid = 0;
		if ((*(ps->p_ops->psop_fillslot))(ps, pp, idx) < 0) {
			error = 1;
			goto out;
		}
		ASSERT(pp->pp_flags & PP_V, "vm_fault: lost the page");
	} else {
		wasvalid = 1;
		ref_slot(ps, pp, idx);
	}

	/*
	 * Break COW association when we write it
	 */
	if ((pp->pp_flags & PP_COW) && write) {
		extern void cow_write();

		if (wasvalid) {
			/*
			 * May or may not be there
			 */
			(void)delete_atl(pp->pp_pfn, pv, btop((ulong)vaddr));
		}
		cow_write(ps, pp, idx);
		ASSERT(pp->pp_flags & PP_V, "vm_fault: lost the page 2");
	}

	/*
	 * With a valid slot, add a hat translation and tabulate
	 * the entry with an atl.
	 */
	add_atl(pp->pp_pfn, pv,
		btop((ulong)vaddr - (ulong)(pv->p_vaddr)));
	hat_addtrans(pv, vaddr, pp->pp_pfn, pv->p_prot |
		((pp->pp_flags & PP_COW) ? PROT_RO : 0));

	/*
	 * Free the various things we hold and return
	 */
out:
	if (holding_ps) {
		v_lock(&ps->p_lock, SPL0);
	}
	if (holding_pp) {
		unlock_slot(ps, pp);
	}
	return(error);
}
