/*
 * vm_steal.c
 *	Routines for stealing memory when needed
 *
 * There is no such thing as swapping in VSTa.  Just page stealing.
 *
 * The basic mechanism is a two-handed clock algorithm.  This technique
 * provides a smooth mechanism for reaping pages across a wide range
 * of configurations.  However, in a multi-CPU environment it certainly
 * presents some challenges.
 *
 * The per-page information includes an attach list data structure.
 * This is used by the basic pageout algorithm to enumerate the
 * current users of a given page.  The pageout daemon enumerates
 * each page set for a page, updating the central page's notion
 * of its modifed and referenced bits.  For the forward hand, it
 * clears the referenced bit.  For the back hand, it reaps the page
 * if the referenced bit is still clear.
 *
 * Locking is tricky.  The attach list uses the page lock.  Because it
 * is taken before the slot locks in the page sets, page sets which
 * are busy must be skipped.   Especially critical is the TLB shoot-
 * down implied by the deletion of a translation in a multi-threaded
 * process.  The HAT for such an implementation will not be trivial.
 */
#include <sys/types.h>
#include <sys/mutex.h>
#include <mach/param.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/core.h>
#include <sys/fs.h>
#include <sys/qio.h>
#include <sys/vm.h>
#include <sys/malloc.h>

extern struct portref *swapdev;

ulong failed_qios = 0L;		/* Number pushes which failed */

/*
 * The following are in terms of 1/nth memory
 */
#define SPREAD 8		/* Distance between hands */
#define DESFREE 8		/* When less than this, start stealing */
#define MINFREE 16		/* When less than this, synch pushes */
				/* AT MOST: */
#define SMALLSCAN 32		/* Scan this much when free > DESFREE */
#define MEDSCAN 16		/*  ... when free > MINFREE */
#define LARGESCAN 4		/*  ... when free < MINFREE */

extern struct core *core,		/* Per-page info */
	*coreNCORE;
extern uint freemem, totalmem;		/* Free and total pages in system */
					/*  total does not include C_SYS */
sema_t pageout_sema;
static struct qio *pgio;	/* Our asynch I/O descriptor */

/*
 * steal_page()
 *	Remove a page from a pset
 *
 * Called with page and slot locked.  On return page has been
 * removed and is perhaps queued for I/O.  The attach list entry
 * corresponding to this mapping has also been removed.  The
 * slot will either be freed on return or when the queued I/O
 * completes.
 */
static void
steal_page(struct pview *pv, uint idx, struct core *c)
{
	struct pset *ps = pv->p_set;
	struct perpage *pp;
	uint setidx, nref;

	/*
	 * Get per-page information
	 */
	setidx = (idx - btop((ulong)pv->p_vaddr)) + pv->p_off;
	pp = find_pp(ps, setidx);
	ASSERT(pp, "steal_page: page not in set");
	ASSERT(pp->pp_flags & PP_V, "steal_page: page not present");

	/*
	 * Snatch away from its poor users.  Anybody who now tries
	 * to use it will fault in and wait for us to release
	 * the pset.  Bring in the authoritative copy of the ref/mod
	 * info now that our users can't touch it.
	 */
	hat_deletetrans(pv, (char *)pv->p_vaddr + ptob(idx),
		pp->pp_pfn);
	pp->pp_flags |= hat_getbits(pv, idx);

	/*
	 * Remove our reference.
	 */
	deref_slot(ps, pp, idx);
}

/*
 * do_hand()
 *	Do hand algorithm
 *
 * Steal pages as appropriate, do all the fancy locking.
 */
static void
do_hand(struct core *c, int trouble, intfun steal)
{
	struct atl *a, *an, **ap;
	extern void iodone_unlock();

	/*
	 * Lock physical page.  Scan the attach list entries.
	 */
	lock_page(c-core);
	for (a = c->c_atl, ap = &c->c_atl; a; a = an) {
		uint idx;
		struct pset *ps;
		struct perpage *pp;

		an = a->a_next;
		ps = a->a_pview->p_set;
		idx = a->a_idx;

		/*
		 * Lock the set. cp_lock() would be too timid; I suspect
		 * that busy psets might represent most of the memory on
		 * a thrashing system.
		 */
		(void)p_lock(&ps->p_lock, SPL0);
		pp = find_pp(ps, idx);
		if (clock_slot(ps, pp)) {
			v_lock(&ps->p_lock, SPL0);
			break;
		}
		ASSERT_DEBUG(pp->pp_flags & PP_V, "do_hand: atl !v");

		/*
		 * Get and clear HAT copy of information
		 */
		pp->pp_flags |= hat_getbits(a->a_pview, idx);

		/*
		 * If any trouble, see if this page is should be
		 * stolen.
		 */
		if (!(pp->pp_flags & PP_M) &&
				(*steal)(pp->pp_flags, trouble)) {
			/*
			 * Yup.  Take it away and free the atl.
			 * We *should* use free_atl(), but we're
			 * already in the middle of the list, so we
			 * can do it quicker this way.
			 */
			steal_page(a->a_pview, idx, c);
			*ap = an;
			free(a);
		} else if (trouble && (pp->pp_flags & PP_M)) {
			/*
			 * It's dirty, and we're interested in getting
			 * some memory soon.  Start cleaning pages.
			 */
			pp->pp_flags &= ~PP_M;
			(*(ps->p_ops->psop_writeslot))(ps, pp, idx,
				iodone_unlock);
		} else {
			/*
			 * No, leave it alone.  Clear REF bit so we can
			 * estimate its use in the future.
			 */
			pp->pp_flags &= ~(PP_R);
			ap = &a->a_next;
			unlock_slot(ps, pp);
		}
	}
	unlock_page(c-core);
}

/*
 * steal1()
 *	Steal algorithm for hand #1
 */
static int
steal1(int flags, int trouble)
{
	return ((trouble > 1) || !(flags & PP_R));
}

/*
 * steal2()
 *	Steal algorithm for hand #2
 */
static int
steal2(int flags, int trouble)
{
	return (trouble && !(flags & (PP_R|PP_M)));
}

/*
 * pageout()
 *	Endless routine to detect memory shortages and steal pages
 */
void
pageout(void)
{
	struct core *base, *top, *hand1, *hand2;
	int trouble, npg;
	int troub_cnt[3];
#define ADVANCE(hand) {if (++(hand) >= top) hand = base; }

	/*
	 * Skip to first usable page, also record top
	 */
	for (base = core; !(base->c_flags & (C_SYS|C_BAD)); ++base)
		;
	top = coreNCORE;

	/*
	 * Set clock hands
	 */
	hand2 = base;
	hand1 = base + (top-base)/SPREAD;

	/*
	 * Calculate scan counts for each situation
	 */
	troub_cnt[0] = (top-base)/SMALLSCAN;
	troub_cnt[1] = (top-base)/MEDSCAN;
	troub_cnt[2] = (top-base)/LARGESCAN;

	/*
	 * Initialize our semaphore
	 */
	init_sema(&pageout_sema);

	/*
	 * Main loop
	 */
	for (;;) {
		/*
		 * Categorize the current memory situation
		 */
		if (freemem < totalmem/MINFREE) {
			trouble = 2;
		} else if (freemem < totalmem/DESFREE) {
			trouble = 1;
		} else {
			trouble = 0;
		}

		/*
		 * Sleep after the appropriate number of iterations
		 */
		if (npg > troub_cnt[trouble]) {
			npg = 0;
			p_sema(&pageout_sema, PRIHI);
			/*
			 * Clear all v's after wakeup
			 */
			set_sema(&pageout_sema, 0);
		}

		/*
		 * Ensure a slot for asynch page push
		 */
		if (!pgio) {
			pgio = alloc_qio();
		}

		/*
		 * Do the two hand algorithms
		 */
		do_hand(hand1, trouble, steal1);
		ADVANCE(hand1);
		do_hand(hand2, trouble, steal2);
		ADVANCE(hand2);
		npg += 1;
	}
}
