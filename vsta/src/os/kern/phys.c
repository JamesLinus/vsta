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
#include <sys/fs.h>
#include <sys/mutex.h>
#include <sys/assert.h>

/*
 * Description of an outstanding physio
 */
struct wire {
	struct proc *w_proc;
	struct pset *w_pset;
	struct perpage
		*w_pp;
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
	init_sema(&wired_sema);
	set_sema(&wired_sema, MAX_WIRED);
	init_lock(&wired_lock);
}

/*
 * page_wire()
 *	Wire down a page for a user process
 */
page_wire(void *arg_va, void **arg_pa)
{
	struct pview *pv;
	struct proc *p = curthread->t_proc;
	struct perpage *pp;
	struct pset *ps;
	uint idx;
	struct wire *w;
	int error = 0;

	/*
	 * Allowed?
	 */
	if ((p->p_vas->v_flags & VF_DMA) == 0) {
		return(err(EPERM));
	}

	/*
	 * Queue turn for a wired slot, take first free slot
	 */
	p_sema(&wired_sema, PRILO);
	p_lock(&wired_lock, SPL0);
	for (w = wired; w->w_proc; ++w)
		;
	ASSERT_DEBUG(w < &wired[MAX_WIRED], "page_wire: bad count");
	w->w_proc = p;
	v_lock(&wired_lock, SPL0);

	/*
	 * Look up virtual address
	 */
	pv = find_pview(p->p_vas, arg_va);
	if (!pv) {
		error = err(EINVAL);
		goto out;
	}

	/*
	 * Check appropriate page slot.  Fill in the slot if it's
	 * not valid yet.
	 */
	ps = pv->p_set;
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
	}

	/*
	 * Copy out the PFN value
	 */
	if (copyout(arg_pa, &pp->pp_pfn, sizeof(uint))) {
		unlock_slot(ps, pp);
		error = -1;
	}
	error = w-wired;
	w->w_pset = ps;
	w->w_pp = pp;
out:
	if (error < 0) {
		w->w_proc = 0;
		v_sema(&wired_sema);
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

	if (arg_handle >= MAX_WIRED) {
		return(err(EINVAL));
	}
	w = &wired[arg_handle];
	if (w->w_proc != curthread->t_proc) {
		return(err(EPERM));
	}
	unlock_slot(w->w_pset, w->w_pp);
	w->w_proc = 0;
	v_sema(&wired_sema);
	return(0);
}

/*
 * pages_release()
 *	Called when a VF_DMA vas is torn down
 */
void
pages_release(struct proc *p)
{
	int x;
	struct wire *w = wired;

	for (x = 0; x < MAX_WIRED; ++x, ++w) {
		if (w->w_proc == p) {
			w->w_proc = 0;
			v_sema(&wired_sema);
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
	curthread->t_proc->p_vas->v_flags |= VF_DMA;
	return(0);
}
