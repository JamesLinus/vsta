/*
 * vm.c
 *	Machine specific routines for memory management
 */
#include <sys/param.h>
#include <sys/pset.h>
#include <sys/mman.h>
#include <sys/fs.h>
#include <sys/vm.h>
#include <sys/assert.h>

/*
 * Limit of ISA RAM which can be addressed
 */
#define LIM (16 * 1024 * 1024)	/* The low 24 bits */

/*
 * check_low()
 *	Tell if a given PFN is OK for ISA DMA purposes
 */
static int
check_low(uint pfn)
{
	return(pfn < btop(LIM));
}

/*
 * mach_page_wire()
 *	Hooks for handling page wiring & DMA
 *
 * Returns an error string if the page can't be wired, otherwise NULL.
 */
char *
mach_page_wire(uint flags, struct pview *pv,
	struct perpage *pp, void *va, uint idx)
{
	uint newpfn;

	ASSERT_DEBUG(pp->pp_flags & PP_V, "mach_page_wire: no page");

	/*
	 * Looking for an ISA DMA-able page?
	 */
	if (!(flags & WIRE_16M)) {
		/*
		 * Other mach bits are unknown; ignore
		 */
		return(0);
	}

	/*
	 * No problem.
	 */
	if (pp->pp_pfn < btop(LIM)) {
		return(0);
	}

	/*
	 * Try to grab a low page
	 */
	if (alloc_page_fn(check_low, &newpfn)) {
		return(ENOMEM);
	}

	/*
	 * Duplicate the page contents
	 */
	bcopy(ptov(ptob(pp->pp_pfn)), ptov(ptob(newpfn)), NBPG);

	/*
	 * Remove any old translations
	 */
	pp->pp_flags |= vm_unvirt(pp);
	if (pp->pp_atl) {
		/*
		 * A cache reference is acceptable, as it doesn't
		 * correspond to anybody who has a mapping to the
		 * actual page.
		 */
		ASSERT_DEBUG(pp->pp_refs > 0, "mach_page_wire: atl !ref");
		if (!(pp->pp_atl->a_flags & ATL_CACHE)) {
			return(EBUSY);
		}
	} else {
		/*
		 * Insert a cache reference in case we've removed the
		 * last "real" reference to the page.
		 */
		ASSERT_DEBUG(pp->pp_refs == 0, "mach_page_wire: !atl ref");
		pp->pp_refs = 1;
		add_atl(pp, pv->p_set, idx, ATL_CACHE);
	}

	/*
	 * Free the old page, and use this one
	 */
	free_page(pp->pp_pfn);
	pp->pp_pfn = newpfn;
	return(0);
}
