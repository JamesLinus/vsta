/*
 * phys.c
 *	Routines for handling physical aspects of memory
 */
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/percpu.h>
#include <sys/vas.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/core.h>
#include <sys/fs.h>
#include <sys/assert.h>
#include <sys/mman.h>
#include "../mach/mutex.h"
#include "pset.h"

/*
 * Description of an outstanding physio
 */
struct wire {
	struct proc *w_proc;
	uint w_pfn;
};
static struct wire wired[MAX_WIRED];
static sema_t wired_sema;
static lock_t wired_lock;

/*
 * init_wire()
 *	Initialization for wired-memory interfaces
 */
void
init_wire(void)
{
	init_sema(&wired_sema); set_sema(&wired_sema, MAX_WIRED);
	init_lock(&wired_lock);
}

/*
 * unwire_page()
 *	Clear the C_WIRED bit on a core entry
 */
static void
unwire_page(uint pfn)
{
	struct core *c;

	c = core+pfn;
	ASSERT_DEBUG(c < coreNCORE, "unwire_page: bad pfn");
	lock_page(pfn);
	c->c_flags &= ~C_WIRED;
	unlock_page(pfn);
}

/*
 * wire_page()
 *	Set the C_WIRED bit on a core entry
 */
static void
wire_page(uint pfn)
{
	struct core *c;

	c = core+pfn;
	ASSERT_DEBUG(c < coreNCORE, "wire_page: bad pfn");
	lock_page(pfn);
	c->c_flags |= C_WIRED;
	unlock_page(pfn);
}

/*
 * page_wire()
 *	Wire down a page for a user process
 */
page_wire(void *arg_va, void **arg_pa, uint flags)
{
	struct pview *pv;
	struct proc *p = curthread->t_proc;
	struct perpage *pp;
	struct pset *ps;
	uint idx;
	struct wire *w;
	int error = 0;
	void *paddr;

	/*
	 * Allowed?
	 */
	if ((p->p_vas.v_flags & VF_DMA) == 0) {
		return(err(EPERM));
	}

	/*
	 * Queue turn for a wired slot, take first free slot
	 */
	if (p_sema(&wired_sema, PRICATCH)) {
		return(err(EINTR));
	}
	p_lock_void(&wired_lock, SPL0);
	for (w = wired; w->w_proc; ++w)
		;
	ASSERT_DEBUG(w < &wired[MAX_WIRED], "page_wire: bad count");
	w->w_proc = (struct proc *)1;	/* Placeholder */
	v_lock(&wired_lock, SPL0_SAME);

	/*
	 * Look up virtual address
	 */
	pv = find_pview(&p->p_vas, arg_va);
	if (!pv) {
		error = err(EFAULT);
		goto out;
	}

	/*
	 * Check appropriate page slot.  Fill in the slot if it's
	 * not valid yet.
	 */
	ps = pv->p_set;
	if (ps->p_type == PT_MEM) {
		error = err(EINVAL);
		goto out;
	}
	idx = btop((char *)arg_va - (char *)pv->p_vaddr) + pv->p_off;
	pp = find_pp(ps, idx);
	lock_slot(ps, pp);
	if ((pp->pp_flags & PP_V) == 0) {
		if ((*(ps->p_ops->psop_fillslot))(ps, pp, idx)) {
			unlock_slot(ps, pp);
			error = err(EFAULT);
			goto out;
		}
		ASSERT_DEBUG(pp->pp_flags & PP_V, "page_wire: lost page");

		/*
		 * We're not really attaching anything to the slot
		 * yet, so don't hold the ref which the fillslot function
		 * has generated.
		 */
		pp->pp_refs -= 1;
	}

	/*
	 * If special handling is requested, let machine specific level
	 * have a shot.
	 */
	if (flags & WIRE_MACH) {
		char *p;

		p = mach_page_wire(flags, pv, pp, arg_va, idx);
		if (p) {
			unlock_slot(ps, pp);
			error = err(p);
			goto out;
		}
	}

	/*
	 * Copy out the PFN value, converted to a physical address
	 */
	wire_page(w->w_pfn = pp->pp_pfn);
	unlock_slot(ps, pp);
	paddr = (char *)ptob(w->w_pfn) + ((ulong)arg_va & (NBPG-1));
	if (copyout(arg_pa, &paddr, sizeof(paddr))) {
		unwire_page(w->w_pfn);
		error = -1;
		goto out;
	}
	error = w-wired;
out:
	if (error < 0) {
		w->w_proc = 0;
		v_sema(&wired_sema);
	} else {
		w->w_proc = p;
	}
	return(error);
}

/*
 * page_release()
 *	DMA done, release hold on page slot
 */
page_release(uint arg_handle)
{
	struct wire *w;
	int error;
	uint pfn;

	/*
	 * Sanity
	 */
	if (arg_handle >= MAX_WIRED) {
		return(err(EINVAL));
	}

	/*
	 * Lock and see if this is really our slot
	 */
	w = &wired[arg_handle];
	p_lock_void(&wired_lock, SPL0);
	if (w->w_proc == curthread->t_proc) {
		pfn = w->w_pfn;
		w->w_proc = 0;
		v_sema(&wired_sema);
		error = 0;
	} else {
		pfn = 0;
		error = err(EPERM);
	}
	v_lock(&wired_lock, SPL0_SAME);

	/*
	 * If we freed a slot, unwire the page now
	 */
	if (pfn) {
		unwire_page(w->w_pfn);
	}
	return(error);
}

/*
 * pages_release()
 *	Called when a VF_DMA vas is torn down
 */
void
pages_release(struct proc *p)
{
	int x;
	struct wire *w;
	uint pfn = 0;

	/*
	 * Scan across all currently wired pages
	 */
	for (x = 0, w = wired; x < MAX_WIRED; ++x, ++w) {
		/*
		 * If it looks like a match...
		 */
		if (w->w_proc == p) {
			/*
			 * Lock and check again
			 */
			p_lock_void(&wired_lock, SPL0);
			if (w->w_proc == p) {
				/*
				 * Record PFN, clear slot
				 */
				pfn = w->w_pfn;
				w->w_proc = 0;
				v_sema(&wired_sema);
			}
			v_lock(&wired_lock, SPL0_SAME);

			/*
			 * With lock released, lock slot and clear
			 * the WIRED bit.
			 */
			if (pfn) {
				unwire_page(pfn);
				pfn = 0;
			}
		}
	}
}

/*
 * enable_dma()
 *	Flag process as a DMA-enabled virtual address space
 */
enable_dma(void)
{
	if (!issys()) {
		return(-1);
	}
	curthread->t_proc->p_vas.v_flags |= VF_DMA;
	return(0);
}
