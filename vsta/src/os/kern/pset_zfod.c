/*
 * pset_zfod.c
 *	Routines for implementing fill-on-demand (with zero) psets
 */
#include <sys/types.h>
#include <sys/pset.h>
#include <sys/fs.h>
#include <sys/vm.h>
#include <sys/assert.h>

extern struct portref *swapdev;
extern int pset_writeslot(), pset_unref();

static int zfod_fillslot(), zfod_init(), zfod_deinit();
struct psetops psop_zfod =
	{zfod_fillslot, pset_writeslot, zfod_init, zfod_deinit, pset_unref};

/*
 * zfod_init()
 *	Set up pset for zeroed memory
 */
static
zfod_init(struct pset *ps)
{
	return(0);
}

/*
 * zfod_deinit()
 *	Clean up--no action needed
 */
static
zfod_deinit(struct pset *ps)
{
	return(0);
}

/*
 * zfod_fillslot()
 *	Fill pset slot with zeroes
 */
static
zfod_fillslot(struct pset *ps, struct perpage *pp, uint idx)
{
	uint pg;

	ASSERT_DEBUG(!(pp->pp_flags & (PP_V|PP_BAD)),
		"zfod_fillslot: valid");
	pg = alloc_page();
	if (pp->pp_flags & PP_SWAPPED) {
		if (pageio(pg, swapdev, ptob(idx+ps->p_swapblk),
				NBPG, FS_ABSREAD)) {
			free_page(pg);
			return(1);
		}
	} else {
		bzero(ptov(ptob(pg)), NBPG);
	}

	/*
	 * Fill in the new page's value
	 */
	pp->pp_flags |= PP_V;
	pp->pp_flags &= ~(PP_M|PP_R);
	pp->pp_pfn = pg;
	pp->pp_refs = 1;
	return(0);
}
