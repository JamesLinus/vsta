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
#ifdef DEBUG
	int x;

	/*
	 * We only allow read-only views of files
	 */
	for (x = 0; x < ps->p_len; ++x) {
		struct perpage *pp;

		pp = find_pp(ps, x);
		ASSERT((pp->pp_flags & PP_M) == 0, "fod_free: dirty");
	}
	ASSERT(!valid_pset_slots(ps), "fod_free: still refs");
#endif

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

	ASSERT_DEBUG(!(pp->pp_flags & (PP_V|PP_BAD)),
		"fod_fillslot: valid");
	pg = alloc_page();
	set_core(pg, ps, idx);
	if (pageio(pg, DATA(ps)->o_pr, ptob(idx+ps->p_off),
			NBPG, FS_ABSREAD)) {
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

	/*
	 * Allocate pset, set it for our pset type
	 */
	ps = alloc_pset(pages);
	ps->p_type = PT_FILE;
	ps->p_ops = &psop_fod;
	ps->p_data = MALLOC(sizeof(struct open_port), MT_OPENPORT);
	DATA(ps)->o_pr = pr;
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
