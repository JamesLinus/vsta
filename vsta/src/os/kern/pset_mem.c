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
static void mem_dup(), mem_free(), mem_lastref();
static struct psetops psop_mem =
	{mem_fillslot, mem_writeslot, mem_init, mem_dup, mem_free,
	 mem_lastref};

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
	uint x;

	/*
	 * Initialize the basic fields of the pset
	 */
	ps = alloc_pset(npfn);
	ps->p_type = PT_MEM;
	ps->p_ops = &psop_mem;

	/*
	 * For each page slot, put in the physical page which
	 * corresponds to the slot.
	 */
	for (x = 0; x < npfn; ++x) {
		struct perpage *pp;

		pp = find_pp(ps, x);
		pp->pp_pfn = pfn + x;
		pp->pp_flags = PP_V;
	}
	return(ps);
}

/*
 * mem_dup()
 *	Duplicate mem slots by creating a COW view
 */
static void
mem_dup(struct pset *ops, struct pset *ps)
{
	extern struct psetops psop_zfod;

	/*
	 * Map fork() of memory based psets (ala boot processes)
	 * into a ZFOD pset which happens to be filled.  The mem
	 * pset doesn't have swap, so we get it from the mem set.
	 */
	ps->p_type = PT_ZERO;
	ps->p_ops = &psop_zfod;
	ps->p_swapblk = alloc_swap(ps->p_len);
}

/*
 * mem_lastref()
 *	Since just a view of physical memory, do nothing
 */
static void
mem_lastref(void)
{
	/* Nothing */ ;
}
