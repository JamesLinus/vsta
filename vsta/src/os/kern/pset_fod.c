/*
 * pset_fod.c
 *	Routines for implementing fill-on-demand (from file) psets
 */
#include <sys/types.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/qio.h>
#include <sys/fs.h>
#include <sys/assert.h>
#include <alloc.h>
#include "pset.h"

extern int pset_deinit();
static int fod_fillslot(), fod_writeslot(), fod_init(), fod_deinit();
struct psetops psop_fod = {fod_fillslot, fod_writeslot, fod_init,
	fod_deinit};

/*
 * fod_init()
 *	Set up pset for mapping a port
 */
static
fod_init(struct pset *ps)
{
	return(0);
}

/*
 * fod_deinit()
 *	Tear down the goodies we set up in fod_init(), free memory
 */
static
fod_deinit(struct pset *ps)
{
#ifdef DEBUG
	/*
	 * We only allow read-only views of files
	 */
	{
		int x;
		struct perpage *pp;

		for (x = 0; x < ps->p_len; ++x) {
			pp = find_pp(ps, x);
			ASSERT((pp->pp_flags & PP_M) == 0,
				"fod_deinit: dirty");
		}
	}
#endif
	return(pset_deinit(ps));
}

/*
 * fod_fillslot()
 *	Fill pset slot from a port
 */
static
fod_fillslot(struct pset *ps, struct perpage *pp, uint idx)
{
	uint pg;

	ASSERT_DEBUG(!(pp->pp_flags & (PP_V|PP_BAD)),
		"fod_fillslot: valid");
	pg = alloc_page();
	set_core(pg, ps, idx);
	if (pageio(pg, ps->p_pr, ptob(idx+ps->p_off), NBPG, FS_ABSREAD)) {
		free_page(pg);
		return(1);
	}

	/*
	 * Fill in the new page's value
	 */
	pp->pp_flags |= PP_V;
	pp->pp_refs = 1;
	pp->pp_flags &= ~(PP_M|PP_R);
	pp->pp_pfn = pg;
	return(0);
}

/*
 * fod_writeslot()
 *	Write pset slot out to its underlying port
 *
 * We don't have coherent mapped files, so extremely unclear what
 * this condition would mean.  Panic for now.
 */
static
fod_writeslot(struct pset *ps, struct perpage *pp, uint idx, voidfun iodone)
{
	ASSERT_DEBUG(pp->pp_flags & PP_V, "fod_writeslot: invalid");
	ASSERT(!(pp->pp_flags & PP_M), "fod_writeslot: dirty file");
	return(0);
}
