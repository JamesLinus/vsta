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
#include <sys/malloc.h>
#include <alloc.h>
#include "../mach/mutex.h"
#include "pset.h"

extern struct portref *swapdev;

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
		p_lock_fast(&ps->p_lock, SPL0);
	}
	pp->pp_lock |= PP_LOCK;
	v_lock(&ps->p_lock, SPL0);
}

/*
 * clock_slot()
 *	Like lock_slot(), but don't block if slot busy
 */
int
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
	return(0);
}

/*
 * unlock_slot()
 *	Release previously held slot, wake any waiters
 */
void
unlock_slot(struct pset *ps, struct perpage *pp)
{
	int wanted;

	ASSERT_DEBUG(pp->pp_lock & PP_LOCK, "unlock_slot: not locked");
	p_lock_fast(&ps->p_lock, SPL0);
	ps->p_locks--;
	wanted = (pp->pp_lock & PP_WANT);
	pp->pp_lock &= ~(PP_LOCK|PP_WANT);
	if (wanted && blocked_sema(&ps->p_lockwait)) {
		vall_sema(&ps->p_lockwait);
	}
	v_lock(&ps->p_lock, SPL0_SAME);
}

/*
 * pset_writeslot()
 *	Generic code for flushing to a swap page
 *
 * Shared by COW, ZFOD, and FOD pset types.  For async, page slot will
 * be released on I/O completion.  Otherwise page is synchronously written
 * and slot is still held on return.
 */
int
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
	q->q_op = FS_ABSWRITE;
	q->q_pset = ps;
	q->q_pp = pp;
	q->q_off = ptob(idx + ps->p_swapblk);
	q->q_cnt = NBPG;
	q->q_iodone = iodone;
	qio(q);
	return(0);
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
	uint refs;

	ASSERT_DEBUG(ps->p_refs > 0, "deref_pset: 0 ref");

	/*
	 * Lock the page set, reduce its reference count
	 */
	p_lock_fast(&ps->p_lock, SPL0);
	ATOMIC_DECL(&ps->p_refs);
	refs = ps->p_refs;
	v_lock(&ps->p_lock, SPL0_SAME);

	/*
	 * When it reaches 0, ask for it to be freed
	 */
	if (refs == 0) {
		/*
		 * Invoke pset-specific cleanup
		 */
		(*(ps->p_ops->psop_free))(ps);

		/*
		 * Release swap space, if any
		 */
		if (ps->p_swapblk) {
			free_swap(ps->p_swapblk, ps->p_len);
		}

		/*
		 * Release our pset itself
		 */
		FREE(ps->p_perpage, MT_PERPAGE);
		FREE(ps, MT_PSET);
	}
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
 *
 * Caller is responsible for any swap allocations.
 */
struct pset *
alloc_pset(uint pages)
{
	struct pset *ps;

	ps = MALLOC(sizeof(struct pset), MT_PSET);
	bzero(ps, sizeof(struct pset));
	ps->p_len = pages;
	ps->p_perpage = MALLOC(sizeof(struct perpage) * pages, MT_PERPAGE);
	bzero(ps->p_perpage, sizeof(struct perpage) * pages);
	init_lock(&ps->p_lock);
	init_sema(&ps->p_lockwait);
	set_sema(&ps->p_lockwait, 0);
	return(ps);
}

/*
 * copy_page()
 *	Make a copy of a page
 */
static void
copy_page(uint idx, struct perpage *opp, struct perpage *pp,
	struct pset *ops, struct pset *ps)
{
	uint pfn;

	pfn = alloc_page();
	set_core(pfn, ps, idx);
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
		if (pageio(pfn, swapdev, ptob(idx+ops->p_swapblk),
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
			p_lock_fast(&ops->p_lock, SPL0);
			locked = 1;
		}
		pp = find_pp(ops, x);
		pp2 = find_pp(ps, x);
		if (pp->pp_flags & (PP_V|PP_SWAPPED)) {
			lock_slot(ops, pp);
			locked = 0;

			/*
			 * COW views can be filled from the underlying
			 * set on demand.
			 */
			if ((pp->pp_flags & (PP_V|PP_COW)) !=
					(PP_V|PP_COW)) {
				/*
				 * Valid page, need to copy.
				 *
				 * The state can have changed as we may
				 * have slept on the slot lock.  But
				 * there's still something in memory or
				 * on swap to copy--copy_page() handles
				 * both cases.
				 */
				copy_page(x, pp, pp2, ops, ps);
				unlock_slot(ops, pp);
			}
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
	ps = MALLOC(sizeof(struct pset), MT_PSET);
	ps->p_len = ops->p_len;
	ps->p_off = ops->p_off;
	ps->p_type = ops->p_type;
	ps->p_locks = 0;
	ps->p_refs = 0;
	ps->p_flags = ops->p_flags;
	ps->p_ops = ops->p_ops;
	init_lock(&ps->p_lock);
	init_sema(&ps->p_lockwait);
	set_sema(&ps->p_lockwait, 0);

	/*
	 * Let pset-specific code fiddle things
	 */
	(*(ps->p_ops->psop_dup))(ops, ps);

	/*
	 * We need our own perpage storage
	 */
	ps->p_perpage = MALLOC(ops->p_len * sizeof(struct perpage),
		MT_PERPAGE);
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
