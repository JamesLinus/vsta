/*
 * vm_swap.c
 *	Routines and data structures for the swap pseudo-device
 *
 * swapdev must initially point to La-La land--we have to fake it until
 * the system comes up far enough to get some swap configured.  A tally
 * is run; the swap blocks are allocated starting at 1.  When a swap
 * manager shows up, the next swap request will have pending swap added
 * to the size.
 *
 * During DEBUG, swap frees are allowed and the space is leaked (lost).
 * During !DEBUG, we panic if we need to free swap before a manager has
 * been started.
 */
#include <sys/types.h>
#include <sys/port.h>
#include <sys/msg.h>
#include <sys/vm.h>
#include <sys/perm.h>
#include <sys/seg.h>
#include <sys/percpu.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/swap.h>
#include <sys/malloc.h>
#include <sys/assert.h>
#include <alloc.h>
#include "msg.h"

extern struct seg *kern_mem();
extern void freesegs();

struct portref *swapdev = 0;
sema_t swap_wait;
lock_t swap_lock;
static ulong swap_pending = 0L;	/* Pages consumed before swapper up */
ulong swap_leaked = 0L;		/* Pages lost before swap manager up */

/*
 * seg_physcopy()
 *	Copy out returned segments to a physical page
 */
static
seg_physcopy(struct sysmsg *sm, uint pfn)
{
	uint x, y, nbyte, cnt;
	char *p;
	struct seg *s;
	struct vas *vas = &curthread->t_proc->p_vas;

	p = (char *)ptov(ptob(pfn));
	nbyte = NBPG;
	for (x = 0; nbyte && (x < sm->sm_nseg); ++x) {
		s = sm->sm_seg[x];

		/*
		 * Calculate amount to copy next
		 */
		if (s->s_len > nbyte) {
			cnt = nbyte;
		} else {
			cnt = s->s_len;
		}

		/*
		 * Attach the memory
		 */
		if (attach_seg(vas, s)) {
			return(1);
		}

		/*
		 * Copy the indicated amount
		 */
		y = copyin((char *)(s->s_pview.p_vaddr)+s->s_off, p, cnt);

		/*
		 * Detach and return error if copyin() failed
		 */
		detach_seg(s);
		if (y) {
			return(1);
		}

		/*
		 * Advance counters
		 */
		p += cnt;
		nbyte -= cnt;
	}
	return(0);
}

/*
 * pageio()
 *	Set up a synchronous page I/O
 *
 * This isn't used strictly for swap, though swapdev is probably
 * the most common destination for pageios.
 */
pageio(uint pfn, struct portref *pr, uint off, uint cnt, int op)
{
	struct sysmsg sm;
	int error = 0;

	ASSERT_DEBUG((op == FS_ABSREAD) || (op == FS_ABSWRITE),
		"pageio: illegal op");
	ASSERT(pr, "pageio: null portref");

	/*
	 * Construct a system message
	 */
	sm.sm_sender = pr;
	sm.sm_op = op;
	sm.sm_nseg = 1;
	sm.sm_arg = cnt;
	sm.sm_arg1 = off;
	sm.sm_seg[0] = kern_mem(ptov(ptob(pfn)), cnt);

	/*
	 * One at a time through the portref
	 */
	p_sema(&pr->p_sema, PRIHI);
	p_lock_fast(&pr->p_lock, SPL0_SAME);

	/*
	 * If port gone, I/O error
	 */
	if (pr->p_port == 0) {
		v_lock(&pr->p_lock, SPL0_SAME);
		error = 1;
		goto out;
	}

	/*
	 * Set up our message transfer state
	 */
	ASSERT_DEBUG(sema_count(&pr->p_iowait) == 0, "pageio: p_iowait");
	pr->p_state = PS_IOWAIT;
	pr->p_msg = &sm;

	/*
	 * Put message on queue
	 */
	queue_msg(pr->p_port, &sm, SPL0);

	/*
	 * Now wait for the I/O to finish or be interrupted
	 */
	p_sema_v_lock(&pr->p_iowait, PRIHI, &pr->p_lock);

	/*
	 * If the server indicates error, set it and leave
	 */
	if (sm.sm_arg == -1) {
		error = 1;
	} else {
		/*
		 * If we got segments back, copy them out and let
		 * them go.
		 */
		if (sm.sm_nseg > 0) {
			error = seg_physcopy(&sm, pfn);
			freesegs(&sm);
		}
	}

	/*
	 * Let server go
	 */
	v_sema(&pr->p_svwait);

out:
	/*
	 * Clean up and return success/failure
	 */
	v_sema(&pr->p_sema);
	return(error);
}

/*
 * set_swapdev()
 *	System call to set swap manager
 *
 * The calling process must have previously opened a portref to
 * the swap manager.  We verify permission and then steal the
 * portref and make it available as "swapdev".
 */
set_swapdev(port_t arg_port)
{
	struct portref *pr;
	extern struct portref *delete_portref();

	/*
	 * Make sure he's "root"
	 */
	if (!isroot()) {
		return(-1);
	}

	/*
	 * See if we already have one
	 */
	if (swapdev) {
		return(err(EBUSY));
	}

	/*
	 * Steal away his port
	 */
	pr = delete_portref(curthread->t_proc, arg_port);
	if (!pr) {
		return(-1);
	}

	/*
	 * It becomes swapdev
	 */
	ASSERT(pr, "set_swapdev: swap daemon died");
	swapdev = pr;
	v_sema(&swapdev->p_sema);
	return(0);
}

/*
 * alloc_swap()
 *	Request a chunk of swap from the swap manager
 */
ulong
alloc_swap(uint pages)
{
	long args[3];

	/*
	 * If there appears to be swap usage pending and the
	 * swap manager appears to be up, account for the
	 * space now.  This code assumes there are no other CPUs
	 * online yet, nor any real-time processes.
	 */
	args[0] = 0;
	if (swap_pending && swapdev) {
		p_lock_fast(&swap_lock, SPL0);
		if (swap_pending) {
			args[0] = swap_pending;
			swap_pending = 0;
		}
		v_lock(&swap_lock, SPL0_SAME);
	}

	/*
	 * We assume that the initial chunk of swap starts at 1--
	 * otherwise our bootup would have no way of assigning
	 * block numbers to the "pending" swap.
	 */
	if (args[0]) {
		ASSERT(alloc_swap(args[0]) == 1,
			"alloc_swap: pend != 1");
	}

	/*
	 * If no swap manager, run a "pending" tally
	 */
	if (!swapdev) {
		p_lock_fast(&swap_lock, SPL0);
		if (!swapdev) {
			args[0] = swap_pending+1;
			swap_pending += pages;
			v_lock(&swap_lock, SPL0_SAME);
			return(args[0]);
		}
		v_lock(&swap_lock, SPL0_SAME);
	}

	/*
	 * Loop until we get swap space
	 */
	for (;;) {
		ASSERT(swapdev, "alloc_swap: manager not ready");
		args[0] = pages; args[1] = 0;
		p_sema(&swapdev->p_sema, PRIHI);
		ASSERT(kernmsg_send(swapdev, SWAP_ALLOC, args) == 0,
			"alloc_swap: failed send");
		v_sema(&swapdev->p_sema);
		if (args[0] > 0) {
			return(args[0]);
		}
		p_sema(&swap_wait, PRIHI);
	}
}

/*
 * free_swap()
 *	Free back a chunk of swap to the swap manager
 */
void
free_swap(ulong block, uint pages)
{
	long args[3];

	/*
	 * This is usually the case of an exec() (discarding
	 * its old stack) during bootup.  We just leak the
	 * space and hope we'll have a swap manager soon.  The
	 * global is just to track how MUCH we leak.
	 */
	if (swapdev == 0) {
		swap_leaked += pages;
		return;
	}
	args[0] = block;
	args[1] = pages;
	p_sema(&swapdev->p_sema, PRIHI);
	ASSERT(kernmsg_send(swapdev, SWAP_FREE, args) == 0,
		"free_swap: send failed");
	v_sema(&swapdev->p_sema);
	if (blocked_sema(&swap_wait)) {
		vall_sema(&swap_wait);
	}
}

/*
 * init_swap()
 *	Initialize swapping code
 *
 * Note, this does not mean we're getting the swap server running.
 * This routine is just to initialize some basic mutexes.
 */
void
init_swap(void)
{
	init_sema(&swap_wait); set_sema(&swap_wait, 0);
	init_lock(&swap_lock);
}
