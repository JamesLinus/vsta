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
extern int pset_writeslot();

static int zfod_fillslot(), zfod_init();
static void zfod_dup(), zfod_free();
struct psetops psop_zfod =
	{zfod_fillslot, pset_writeslot, zfod_init, zfod_dup, zfod_free,
	 pset_lastref};

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
	set_core(pg, ps, idx);
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

/*
 * zfod_free()
 *	Release a page set; update pages it references
 */
static void
zfod_free(struct pset *ps)
{
	ASSERT_DEBUG(!valid_pset_slots(ps), "zfod_free: still refs");
}

/*
 * alloc_pset_zfod()
 *	Allocate a generic pset with all invalid pages
 */
struct pset *
alloc_pset_zfod(uint pages)
{
	struct pset *ps;
	uint swapblk;

	/*
	 * Get backing store first
	 */
	if ((swapblk = alloc_swap(pages)) == 0) {
		return(0);
	}

	/*
	 * Allocate pset, set it for our pset type
	 */
	ps = alloc_pset(pages);
	ps->p_type = PT_ZERO;
	ps->p_ops = &psop_zfod;
	ps->p_swapblk = swapblk;

	return(ps);
}

/*
 * zfod_dup()
 *	Create COW view of ZFOD memory
 */
static void
zfod_dup(struct pset *ops, struct pset *ps)
{
	/* Nothing to do; dup_slots() handles it all */
}
