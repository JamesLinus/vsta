/*
 * pset_mem.c
 *	Routines for implementing physical memory page sets
 */
#include <sys/types.h>
#include <sys/pset.h>
#include <sys/assert.h>

static int mem_fillslot(), mem_writeslot(), mem_init(), mem_deinit();
struct psetops psop_mem =
	{mem_fillslot, mem_writeslot, mem_init, mem_deinit};

/*
 * mem_init()
 *	Set up pset for mapping physical memory
 */
static
mem_init(struct pset *ps)
{
	return(0);
}

/*
 * mem_deinit()
 *	Clean up--no action needed
 */
static
mem_deinit(struct pset *ps)
{
	return(0);
}

/*
 * mem_fillslot()
 *	Fill pset--no action
 */
static
mem_fillslot(struct pset *ps, struct perpage *pp, uint idx)
{
	ASSERT(pp->pp_flags & PP_V, "mem_fillslot: not valid");
	return(0);
}

/*
 * mem_writeslot()
 *	Write pset--just clear mod bit here
 */
static
mem_writeslot(struct pset *ps, struct perpage *pp, uint idx, voidfun iodone)
{
	ASSERT_DEBUG(pp->pp_flags & PP_V, "mem_writeslot: invalid");
	pp->pp_flags &= ~(PP_M);
	return(0);
}

/*
 * mem_unref()
 *	Unref slot
 */
mem_unref(struct pset *ps, struct perpage *pp, uint idx)
{
	/* Who cares? */
}
