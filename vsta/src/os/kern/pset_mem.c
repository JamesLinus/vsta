/*
 * pset_mem.c
 *	Routines for implementing physical memory page sets
 */
#include <sys/types.h>
#include <sys/pset.h>
#include <sys/assert.h>
#include <sys/malloc.h>
#include "pset.h"

static int mem_fillslot(), mem_writeslot(), mem_init();
static void mem_free();
static struct psetops psop_mem =
	{mem_fillslot, mem_writeslot, mem_init, mem_free};

/*
 * mem_init()
 *	Set up pset for mapping physical memory
 */
static int
mem_init(struct pset *ps)
{
	return(0);
}

/*
 * mem_free()
 *	Clean up--no action needed
 */
static void
mem_free(struct pset *ps)
{
	/* Nothing */
}

/*
 * mem_fillslot()
 *	Fill pset--no action
 */
static int
mem_fillslot(struct pset *ps, struct perpage *pp, uint idx)
{
	ASSERT(pp->pp_flags & PP_V, "mem_fillslot: not valid");
	return(0);
}

/*
 * mem_writeslot()
 *	Write pset--just clear mod bit here
 */
static int
mem_writeslot(struct pset *ps, struct perpage *pp, uint idx, voidfun iodone)
{
	ASSERT_DEBUG(pp->pp_flags & PP_V, "mem_writeslot: invalid");
	pp->pp_flags &= ~(PP_M);
	return(0);
}

/*
 * physmem_pset()
 *	Create a pset which holds a view of physical memory
 */
struct pset *
physmem_pset(uint pfn, int npfn)
{
	struct pset *ps;
	struct perpage *pp;
	uint x;

	/*
	 * Initialize the basic fields of the pset
	 */
	ps = MALLOC(sizeof(struct pset), MT_PSET);
	ps->p_perpage = MALLOC(npfn * sizeof(struct perpage), MT_PERPAGE);
	ps->p_len = npfn;
	ps->p_off = 0;
	ps->p_type = PT_MEM;
	ps->p_swapblk = 0;
	ps->p_refs = 0;
	ps->p_cowsets = 0;
	ps->p_ops = &psop_mem;
	init_lock(&ps->p_lock);
	ps->p_locks = 0;
	init_sema(&ps->p_lockwait);

	/*
	 * For each page slot, put in the physical page which
	 * corresponds to the slot.
	 */
	for (x = 0; x < npfn; ++x) {
		pp = find_pp(ps, x);
		pp->pp_pfn = pfn + x;
		pp->pp_flags = PP_V;
		pp->pp_refs = 0;
		pp->pp_lock = 0;
		pp->pp_atl = 0;
	}
	return(ps);
}
