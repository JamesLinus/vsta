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
 * Map generic pset data to FOD use.  We have a struct containing
 * the open port, as well as a pview used to leave cache references
 * to pset slots
 */
#define DATA(ps) ((struct open_port *)(ps->p_data))
struct open_port {
	struct portref *o_pr;
	struct pview o_pview;
};

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
	uint x;
	struct open_port *o = DATA(ps);

	/*
	 * Clean up pset.  There can still be cache pview references
	 * to slots.
	 */
	for (x = 0; x < ps->p_len; ++x) {
		struct perpage *pp;

		/*
		 * Get per-page information.  Make sure nobody's
		 * dirtied the page
		 */
		pp = find_pp(ps, x);
		ASSERT_DEBUG((pp->pp_flags & PP_M) == 0, "fod_free: dirty");

		/*
		 * Clean up residual valid pages
		 */
		if (pp->pp_flags & PP_V) {
			struct atl *a = pp->pp_atl;

			/*
			 * Sanity.  Lots of sanity.
			 */
			ASSERT_DEBUG(a, "fod_free: v !atl");
			ASSERT_DEBUG(a->a_pview == &o->o_pview,
				"fod_free: non-cache pview");
			ASSERT_DEBUG(a->a_next == 0,
				"fod_free: other refs");
			ASSERT_DEBUG(pp->pp_refs == 1,
				"fod_free: refs/atl mismatch");

			/*
			 * Drop reference to slot, which will free it
			 */
			deref_slot(ps, pp, a->a_idx);
		} else {
			ASSERT_DEBUG(pp->pp_atl == 0, "fod_free: !v atl");
		}
	}

	/*
	 * Close file connection
	 */
	(void)shut_client(DATA(ps)->o_pr);

	/*
	 * Free our dynamic memory
	 */
	FREE(DATA(ps), MT_OPENPORT);
}

/*
 * fod_fillslot()
 *	Fill pset slot from a port
 */
static int
fod_fillslot(struct pset *ps, struct perpage *pp, uint idx)
{
	uint pg;
	struct open_port *o = DATA(ps);

	ASSERT_DEBUG(!(pp->pp_flags & (PP_V|PP_BAD)),
		"fod_fillslot: valid");
	pg = alloc_page();
	set_core(pg, ps, idx);
	if (pageio(pg, o->o_pr, ptob(idx+ps->p_off),
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
	add_atl(pp, &o->o_pview, idx);

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
	ps->p_data = MALLOC(sizeof(struct open_port), MT_OPENPORT);
	DATA(ps)->o_pr = pr;

	/*
	 * Set up the cache view
	 */
	pv = &DATA(ps)->o_pview;
	bzero(pv, sizeof(struct pview));
	pv->p_set = ps;
	pv->p_len = pages;

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
	DATA(ps)->o_pr = dup_port(DATA(ops)->o_pr);
}
