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

extern void *malloc();

extern int pset_unref();
static int fod_fillslot(), fod_writeslot(), fod_init(), fod_deinit(),
	fod_unref();
struct psetops psop_fod = {fod_fillslot, fod_writeslot, fod_init,
	fod_deinit, pset_unref};

/*
 * fod_init()
 *	Set up pset for mapping a port
 *
 * This would be really simple, except that we want to keep a pseudo-
 * view around so we can cache pages even when our COW reference breaks
 * away from us.  The hope is that the original view of a data page
 * will be desired multile times as the a.out is run again and again.
 */
static
fod_init(struct pset *ps)
{
	struct pview *pv;

	/*
	 * Get a pview, make it map 1:1 with our pset
	 */
	pv = ps->p_view = malloc(sizeof(struct pview));
	pv->p_set = ps;
	pv->p_vaddr = 0;
	pv->p_len = ps->p_len;
	pv->p_off = 0;
	pv->p_vas = 0;		/* Flags pseudo-view */
	pv->p_next = 0;
	pv->p_prot = PROT_RO;
	return(0);
}

/*
 * fod_deinit()
 *	Tear down the goodies we set up in fod_init()
 */
static
fod_deinit(struct pset *ps)
{
	free(ps->p_view);
#ifdef DEBUG
	ps->p_view = 0;
#endif
	return(0);
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
	if (pageio(pg, ps->p_pr, ptob(idx+ps->p_off), NBPG, FS_ABSREAD)) {
		free_page(pg);
		return(1);
	}

	/*
	 * Fill in the new page's value
	 */
	pp->pp_flags |= PP_V;
	pp->pp_refs = 2;
	pp->pp_flags &= ~(PP_M|PP_R);
	pp->pp_pfn = pg;

	/*
	 * Add our own internal reference to the page
	 */
	add_atl(pg, ps->p_view, ptob(idx));
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
