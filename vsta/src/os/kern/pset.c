/*
 * pset.c
 *	Routines for creating/searching/filling page sets
 */
#include <sys/pset.h>
#include <sys/assert.h>
#include <sys/fs.h>
#include <sys/qio.h>
#include <sys/fs.h>
#include <sys/port.h>
#include <sys/vm.h>
#include <lib/alloc.h>

extern struct portref *swapdev;

/*
 * find_pp()
 *	Given pset and index, return perpage information
 */
struct perpage *
find_pp(struct pset *ps, uint idx)
{
	ASSERT_DEBUG(idx < ps->p_len, "find_pp: bad index");
	ASSERT_DEBUG(ps->p_perpage, "find_pp: no perpage");
	return(&ps->p_perpage[idx]);
}

/*
 * free_pset()
 *	Release a page set; update pages it references
 */
void
free_pset(struct pset *ps)
{
	int x;
	struct perpage *pp;

	ASSERT_DEBUG(ps->p_refs == 0, "free_pset: refs > 0");
	ASSERT(ps->p_locks == 0, "free_pset: locks > 0");

#ifdef DEBUG
	/*
	 * Make sure there are no valid slots now that all the
	 * views have been detached.
	 */
	if (ps->p_type != PT_MEM) {
		for (x = 0, pp = ps->p_perpage; x < ps->p_len; ++x, ++pp) {
			ASSERT(!(pp->pp_flags & PP_V),
				"free_pset: valid pages");
		}
	}
#endif

	/*
	 * Free our reference to the master set on PT_COW
	 */
	if (ps->p_type == PT_COW) {
		deref_pset(ps->p_cow);
	}
	free(ps->p_perpage);
	free(ps);
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
	int x;
	extern struct psetops psop_mem;

	/*
	 * Initialize the basic fields of the pset
	 */
	ps = malloc(sizeof(struct pset));
	ps->p_perpage = malloc(npfn * sizeof(struct perpage));
	ps->p_len = npfn;
	ps->p_off = 0;
	ps->p_type = PT_MEM;
	ps->p_swapblk = 0;
	ps->p_refs = 0;
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
		pp->pp_pfn = pfn+x;
		pp->pp_flags = PP_V;
		pp->pp_refs = 0;
		pp->pp_lock = 0;
	}
	return(ps);
}

/*
 * lock_slot()
 *	Lock a particular slot within the pset
 *
 * Caller is assumed to hold lock on pset.  This routine may sleep
 * waiting for the slot to become unlocked.  On return, the slot
 * is locked, and the pset lock is released.
 */
void
lock_slot(struct pset *ps, struct perpage *pp)
{
	/*
	 * If we have the poor luck to collide with a parallel operation,
	 * wait for it to finish.
	 */
	ps->p_locks += 1;
	while (pp->pp_lock & PP_LOCK) {
		ASSERT(ps->p_locks > 1, "lock_slot: stray lock");
		pp->pp_lock |= PP_WANT;
		p_sema_v_lock(&ps->p_lockwait, PRIHI, &ps->p_lock);
		(void)p_lock(&ps->p_lock, SPL0);
	}
	pp->pp_lock |= PP_LOCK;
	v_lock(&ps->p_lock, SPL0);
}

/*
 * clock_slot()
 *	Like lock_slot(), but don't block if slot busy
 */
clock_slot(struct pset *ps, struct perpage *pp)
{
	if (pp->pp_lock & PP_LOCK) {
		/*
		 * Sorry, busy
		 */
		return(1);
	}

	/*
	 * Take the lock
	 */
	ps->p_locks += 1;
	pp->pp_lock |= PP_LOCK;
	v_lock(&ps->p_lock, SPL0);
}

/*
 * unlock_slot()
 *	Release previously held slot, wake any waiters
 */
void
unlock_slot(struct pset *ps, struct perpage *pp)
{
	int wanted;

	(void)p_lock(&ps->p_lock, SPL0);
	ps->p_locks -= 1;
	wanted = (pp->pp_lock & PP_WANT);
	pp->pp_lock &= ~(PP_LOCK|PP_WANT);
	if (wanted && blocked_sema(&ps->p_lockwait)) {
		vall_sema(&ps->p_lockwait);
	}
	v_lock(&ps->p_lock, SPL0);
}

/*
 * pset_writeslot()
 *	Generic code for flushing to a swap page
 *
 * Shared by COW, ZFOD, and FOD pset types.  For async, page slot will
 * be released on I/O completion.  Otherwise page is synchronously written
 * and slot is still held on return.
 */
pset_writeslot(struct pset *ps, struct perpage *pp, uint idx, voidfun iodone)
{
	struct qio *q;

	ASSERT_DEBUG(pp->pp_flags & PP_V, "nof_writeslot: invalid");
	pp->pp_flags &= ~(PP_M);
	pp->pp_flags |= PP_SWAPPED;
	if (!iodone) {
		/*
		 * Simple synchronous page push
		 */
		if (pageio(pp->pp_pfn, swapdev,
				ptob(idx+ps->p_swapblk), NBPG, FS_ABSWRITE)) {
			pp->pp_flags |= PP_BAD;
			return(1);
		}
		return(0);
	}

	/*
	 * Asynch I/O
	 */
	q = alloc_qio();
	q->q_port = swapdev;
	q->q_op = FS_WRITE;
	q->q_pset = ps;
	q->q_pp = pp;
	q->q_off = ptob(idx + ps->p_swapblk);
	q->q_cnt = NBPG;
	q->q_iodone = iodone;
	qio(q);
	return(0);
}

/*
 * ref_pset()
 *	Add a reference to a pset
 */
void
ref_pset(struct pset *ps)
{
	ATOMIC_INC(&ps->p_refs);
}

/*
 * deref_pset()
 *	Reduce reference count on pset
 *
 * Must free the pset when reference count reaches 0
 */
void
deref_pset(struct pset *ps)
{
	/*
	 * Lock the page set, reduce its reference count
	 */
	p_lock(&ps->p_lock, SPL0);
	ATOMIC_DEC(&ps->p_refs);

	/*
	 * When it reaches 0, ask for it to be freed
	 */
	if (ps->p_refs == 0) {
		printf("free pset 0x%x\n", ps);
		dbg_enter();
		free_pset(ps);
	} else {
		/*
		 * Otherwise let it go
		 */
		v_lock(&ps->p_lock, SPL0);
	}
}

/*
 * pset_unref()
 *	Release a reference to a slot
 *
 * Generic code shared by objects which move dirty pages to swap--
 * zfod, nfod, cow.
 */
pset_unref(struct pset *ps, struct perpage *pp, uint idx)
{
	extern void iodone_free();

	/*
	 * Take no further action if more refs
	 */
	if ((pp->pp_refs -= 1) > 0) {
		return;
	}

	/*
	 * Flush slot if dirty
	 */
	if ((ps->p_refs > 0) && (pp->pp_flags & PP_M)) {
		(*(ps->p_ops->psop_writeslot))(ps, pp, idx, iodone_free);
		return;
	}

	/*
	 * Release page, clear slot
	 */
	free_page(pp->pp_pfn);
	pp->pp_flags &= ~(PP_V|PP_M|PP_R);
}

/*
 * iodone_free()
 *	Common function to free a page on I/O completion
 *
 * Also updates the perpage information before unlocking the page slot
 */
void
iodone_free(struct qio *q)
{
	struct perpage *pp;

	pp = q->q_pp;
	pp->pp_flags &= ~(PP_V|PP_M|PP_R);
	free_page(pp->pp_pfn);
	unlock_slot(q->q_pset, pp);
}

/*
 * iodone_unlock()
 *	Simply release slot lock on I/O completion
 */
void
iodone_unlock(struct qio *q)
{
	struct perpage *pp = q->q_pp;

	pp->pp_flags &= ~(PP_R|PP_M);
	unlock_slot(q->q_pset, pp);
}

/*
 * alloc_pset()
 *	Common code for allocation of all types of psets
 */
static struct pset *
alloc_pset(uint pages)
{
	struct pset *ps;

	ps = malloc(sizeof(struct pset));
	ps->p_len = pages;
	ps->p_off = 0;
	ps->p_locks = 0;
	ps->p_refs = 0;
	ps->p_swapblk = 0;	/* Caller has to allocate */
	ps->p_perpage = malloc(sizeof(struct perpage) * pages);
	bzero(ps->p_perpage, sizeof(struct perpage) * pages);
	init_lock(&ps->p_lock);
	init_sema(&ps->p_lockwait); set_sema(&ps->p_lockwait, 0);
	return(ps);
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
	extern struct psetops psop_zfod;
	extern uint alloc_swap();

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
 * copy_page()
 *	Make a copy of a page
 */
static void
copy_page(uint idx, struct perpage *opp, struct perpage *pp, struct pset *ps)
{
	uint pfn;

	pfn = alloc_page();
	if (opp->pp_flags & PP_V) {
		/*
		 * Valid means simple memory->memory copy
		 */
		bcopy(ptov(ptob(opp->pp_pfn)), ptov(ptob(pfn)), NBPG);

	/*
	 * Otherwise read from swap
	 */
	} else {
		ASSERT(opp->pp_flags & PP_SWAPPED, "copy_page: !v !swap");
		if (pageio(pfn, swapdev, ptob(idx+ps->p_swapblk),
				NBPG, FS_ABSWRITE)) {
			/*
			 * The I/O failed.  Mark the slot as bad.  Our
			 * new set is in for a rough ride....
			 */
			pp->pp_flags |= PP_BAD;
			free_page(pfn);
			return;
		}
	}
	pp->pp_flags |= PP_V;
	pp->pp_pfn = pfn;
}

/*
 * dup_slots()
 *	Duplicate the contents of each slot under an old pset
 */
static void
dup_slots(struct pset *ops, struct pset *ps)
{
	uint x;
	struct perpage *pp, *pp2;
	int locked = 0;

	for (x = 0; x < ps->p_len; ++x) {
		if (!locked) {
			p_lock(&ops->p_lock, SPL0);
			locked = 1;
		}
		pp = find_pp(ops, x);
		pp2 = find_pp(ps, x);
		if (pp->pp_flags & (PP_V|PP_SWAPPED)) {
			lock_slot(ops, pp);
			locked = 0;
			/*
			 * The state can have changed as we may
			 * have slept on the slot lock.  But
			 * there's still something in memory or
			 * on swap to copy--copy_page() handles
			 * both cases.
			 */
			copy_page(x, pp, pp2, ps);
			unlock_slot(ops, pp);
		}
	}

	/*
	 * Drop any trailing lock
	 */
	if (locked) {
		v_lock(&ops->p_lock, SPL0);
	}
}

/*
 * copy_pset()
 *	Copy one pset into another
 *
 * Invalid pages can be left as-is.  Valid, unmodified, unswapped pages
 * can become invalid in copy.  Other valid pages must be copied.  Worst
 * is that invalid, swapped pages must be paged back into a new page
 * under the new pset.  Sometimes true copy-on-write sounds pretty good.
 * But it's too complex, IMHO.
 */
struct pset *
copy_pset(struct pset *ops)
{
	struct pset *ps;

	/*
	 * Allocate the new pset, set up its basic fields
	 */
	ps = malloc(sizeof(struct pset));
	ps->p_len = ops->p_len;
	ps->p_off = ops->p_off;
	ps->p_type = ops->p_type;
	ps->p_locks = 0;
	ps->p_refs = 0;
	ps->p_flags = ops->p_flags;
	ps->p_ops = ops->p_ops;
	init_lock(&ps->p_lock);
	init_sema(&ps->p_lockwait); set_sema(&ps->p_lockwait, 0);

	/*
	 * For files, we must get our own portref.  For copy-on-write,
	 * we must add a reference to the master pset.
	 */
	if (ps->p_type == PT_FILE) {
		ps->p_pr = dup_port(ps->p_pr);
	} else if (ps->p_type == PT_COW) {
		ref_pset(ps->p_cow);
	}

	/*
	 * We need our own perpage storage
	 */
	ps->p_perpage = malloc(ops->p_len * sizeof(struct perpage));
	bzero(ps->p_perpage, ps->p_len * sizeof(struct perpage));

	/*
	 * If old copy had swap, get swap for new copy
	 */
	if (ops->p_swapblk != 0) {
		ps->p_swapblk = alloc_swap(ps->p_len);
	}

	/*
	 * Now duplicate the contents
	 */
	dup_slots(ops, ps);
	return(ps);
}
