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
 * Each physical page points back to the pset using the page, and
 * the index within that pset.  All further state is kept in the
 * per-page struct "perpage".  This includes reference counting,
 * a record of the page's state (modified, referenced, etc.), and
 * a the head of a linked list of struct "atl"s.  The atl enumerates
 * the mappings which are active, each atl entry listing a pview,
 * an index, and a next pointer.
 *
 * In general, the reference count in the perpage reflects the number
 * of atl's hanging off the perpage.
 *
 * A pset which is a view of a file (via a port) has some unique needs.
 * A copy-on-write (COW) set which references a file may transfer from
 * the file's image to a private copy on write, but shares access to
 * the page until the write occurs (if ever).  COW sets are linked off
 * the master fill-on-demand pset mapping the file.  The perpage slot
 * under the COW set counts as a single reference, even though there is
 * no atl and the COW's perpage has a reference count of 0.
 *
 * This is an exception to the general rule noted two paragraphs back.
 *
 * Copy-on-write (COW) sets are tricky.  The affected slot in the cow pset
 * is locked first; then the psop_fillslot is called on this slot.  Within
 * this cow slot filling, the slot of the master set is locked, and a
 * further psop_fillslot is invoked.
 *
 * XXX move some of this discussion to where it belongs--pset.c maybe?
 * XXX yeah, well, maybe, but at least it's a little bit correct now.
 */
#include <sys/types.h>
#include <mach/param.h>
#include <sys/vas.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/percpu.h>
#include <sys/thread.h>
#include <sys/assert.h>
#include <sys/core.h>
#include "../mach/mutex.h"
#include "pset.h"

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
	struct perpage *pp;
	uint idx;
	int error = 0;
	int wasvalid;

	/*
	 * Easiest--no view matches address
	 */
	if ((pv = find_pview(vas, vaddr)) == 0) {
		return(1);
	}
	ps = pv->p_set;

	/*
	 * Next easiest--trying to write to read-only view
	 */
	if (write && (pv->p_prot & PROT_RO)) {
		v_lock(&ps->p_lock, SPL0_SAME);
		return(1);
	}

	/*
	 * Transfer from pset lock to page slot lock
	 */
	idx = btop(((char *)vaddr - (char *)pv->p_vaddr)) + pv->p_off;
	pp = find_pp(ps, idx);
	lock_slot(ps, pp);

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
		if ((*(ps->p_ops->psop_fillslot))(ps, pp, idx)) {
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
		if (wasvalid) {
			uint pvidx;

			/*
			 * May or may not be there.  If it is, remove
			 * its reference from the per-page struct.
			 */
			pvidx = btop((ulong)vaddr - (ulong)pv->p_vaddr);
			if (delete_atl(pp, pv, pvidx) == 0) {
				deref_slot(ps, pp, idx);
			}
		}
		cow_write(ps, pp, idx);
		ASSERT(pp->pp_flags & PP_V, "vm_fault: lost the page 2");
	}

	/*
	 * With a valid slot, add a hat translation and tabulate
	 * the entry with an atl.
	 */
	add_atl(pp, pv, btop((ulong)vaddr - (ulong)(pv->p_vaddr)));
	hat_addtrans(pv, vaddr, pp->pp_pfn, pv->p_prot |
		((pp->pp_flags & PP_COW) ? PROT_RO : 0));

	/*
	 * Free the various things we hold and return
	 */
out:
	unlock_slot(ps, pp);
	return(error);
}
