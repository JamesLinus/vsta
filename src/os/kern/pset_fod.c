/*
 * pset_fod.c
 *	Routines for implementing fill-on-demand (from file) psets
 */
#include <sys/types.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/fs.h>
#include <sys/port.h>
#include <sys/assert.h>
#include <sys/malloc.h>
#include "pset.h"

/*
 * Our pset ops
 */
static int fod_fillslot(), fod_writeslot(), fod_init();
static void fod_dup(), fod_free();
static struct psetops psop_fod = {fod_fillslot, fod_writeslot, fod_init,
	fod_dup, fod_free, pset_lastref};

/*
 * fod_init()
 *	Set up pset for mapping a port
 */
static int
fod_init(struct pset *ps)
{
	return(0);
}

/*
 * fod_free()
 *	Tear down the goodies we set up in fod_init(), free memory
 */
static void
fod_free(struct pset *ps)
{
	struct portref *pr = ps->p_data;

	/*
	 * Free slots
	 */
	pset_free(ps);

	/*
	 * Close file connection
	 */
	(void)shut_client(pr, 0);
}

/*
 * fod_fillslot()
 *	Fill pset slot from a port
 */
static int
fod_fillslot(struct pset *ps, struct perpage *pp, uint idx)
{
	uint pg;

	ASSERT_DEBUG(!(pp->pp_flags & (PP_V|PP_BAD)),
		"fod_fillslot: valid");
	pg = alloc_page();
	set_core(pg, ps, idx);
	if (pageio(pg, ps->p_data, ptob(idx+ps->p_off),
			NBPG, FS_ABSREAD)) {
		free_page(pg);
		return(1);
	}

	/*
	 * Fill in the new page's value, leave one reference for
	 * our caller, and another for our cache atl
	 */
	pp->pp_flags |= PP_V;
	pp->pp_refs = 2;
	pp->pp_flags &= ~(PP_M|PP_R);
	pp->pp_pfn = pg;

	/*
	 * Add the cache reference
	 */
	add_atl(pp, ps, idx, ATL_CACHE);

	return(0);
}

/*
 * fod_writeslot()
 *	Write pset slot out to its underlying port
 *
 * We don't have coherent mapped files, so extremely unclear what
 * this condition would mean.  Panic for now.
 */
static int
fod_writeslot(struct pset *ps, struct perpage *pp, uint idx, voidfun iodone)
{
	ASSERT_DEBUG(pp->pp_flags & PP_V, "fod_writeslot: invalid");
	ASSERT(!(pp->pp_flags & PP_M), "fod_writeslot: dirty file");
	return(0);
}

/*
 * alloc_pset_fod()
 *	Allocate a fill-on-demand pset with all invalid pages
 */
struct pset *
alloc_pset_fod(struct portref *pr, uint pages)
{
	struct pset *ps;
	struct pview *pv;

	/*
	 * Allocate pset, set it for our pset type
	 */
	ps = alloc_pset(pages);
	ps->p_type = PT_FILE;
	ps->p_ops = &psop_fod;
	ps->p_data = pr;

	return(ps);
}

/*
 * fod_dup()
 *	Duplicate port reference on new pset
 */
static void
fod_dup(struct pset *ops, struct pset *ps)
{
	/*
	 * We are a new reference into the file.  Ask
	 * the server for duplication.
	 */
	ps->p_data = dup_port(ops->p_data);
}
