/*
 * hat.c
 *	Hardware Address Translation routines
 *
 * These are the i386-specific routines implementing the HAT layer.
 *
 * Virtual memory is organized into two halves; the lower is
 * dedicated to the kernel, the upper is for users.  Each user
 * process has its own root page table and its own page tables
 * for per-process address space.  All processes share the
 * second-level page tables for kernel address space.
 *
 * A HAT data structure is bolted onto each vas.  It contains
 * the virtual and physical versions of the root page table.
 * The physical address of second-level page tables is only kept
 * in the root page table entries.  The virtual address is
 * generated "on the fly" using the ptov() macro, which simply
 * addresses the P->V mapping area.  This means memory for page
 * tables is only freed on process exit--VSTa doesn't have
 * swapping of anything, including page tables.
 *
 * Some of these techniques would work for multiprocessor i386; I
 * have not added any locking, so it still wouldn't be trivial.
 */
#include <sys/param.h>
#include <sys/pview.h>
#include <sys/vas.h>
#include <sys/vm.h>
#include <mach/pte.h>
#include <sys/pset.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/percpu.h>

#define NRMAPSLOT (20)		/* # slots for our vaddr map */

extern uint alloc_page();
extern void flush_tlb(), *malloc(), rmap_init();

/*
 * hat_initvas()
 *	Initialize hardware-dependent portion vas
 *
 * May sleep waiting for memory
 */
void
hat_initvas(struct vas *vas)
{
	uint paddr;
	pte_t *c;
	extern pte_t *cr3;
	extern int freel1pt;

	/*
	 * Get root page table, duplicate the master kernel copy.
	 * This one has all the usual slots fille in for kernel
	 * address space, and all zeroes for user.
	 */
	paddr = ptob(alloc_page());
	c = vas->v_hat.h_vcr3 = ptov(paddr);
	bzero(c, NBPG);
	bcopy(cr3, c, freel1pt * sizeof(pte_t));
	vas->v_hat.h_cr3 = paddr;

	/*
	 * Get an address map for doing on-demand virtual address
	 * allocations.
	 */
	vas->v_hat.h_map = malloc(NRMAPSLOT*sizeof(struct rmap));
	rmap_init(vas->v_hat.h_map, NRMAPSLOT);
	rmap_free(vas->v_hat.h_map, btop(VMAP_BASE), btop(VMAP_SIZE));
}

/*
 * hat_freevas()
 *	De-initialize vas, free any memory HAT portion is holding
 */
void
hat_freevas(struct vas *vas)
{
	pte_t *pt;
	int x;

	/*
	 * Free any level-two page tables we allocated for
	 * the user.
	 */
	pt = vas->v_hat.h_vcr3;
	pt += (NPTPG/2);
	for (x = NPTPG/2; x < NPTPG; ++x,++pt) {
		if (*pt & PT_V) {
			free_page(*pt >> PT_PFNSHIFT);
		}
	}

	/*
	 * Free root and map
	 */
	free_page(btop(vas->v_hat.h_cr3));
	free(vas->v_hat.h_map);
}

/*
 * hat_addtrans()
 *	Add a translation given a view
 *
 * May sleep waiting for memory
 */
void
hat_addtrans(struct pview *pv, void *va, uint pfn, int prot)
{
	pte_t *root, *pt;

	/*
	 * Virtual address is in upper two gigs
	 */
	va = (void *)((ulong)va | 0x80000000);

	/*
	 * If there isn't a L2PT page yet, allocate one
	 */
	root = (pv->p_vas->v_hat.h_vcr3)+L1IDX(va);
	if (!(*root & PT_V)) {
		uint pg;

		pg = alloc_page();
		pt = ptov(ptob(pg));
		bzero(pt, NBPG);
		*root = (pg << PT_PFNSHIFT) | PT_V|PT_W|PT_U;
	} else {
		pt = ptov(*root & PT_PFN);
	}

	/*
	 * Get the address
	 */
	pt += L2IDX(va);
	*pt = (pfn << PT_PFNSHIFT) | PT_V |
		((prot & PROT_RO) ? 0 : PT_W) |
		((prot & PROT_KERN) ? 0 : PT_U);
}

/*
 * hat_vtop()
 *	Given virtual address and address space, return physical address
 *
 * Returns 0 if a translation does not exist
 */
void *
hat_vtop(struct vas *vas, void *va)
{
	pte_t *root, *pt;

	/*
	 * Virtual address is in upper two gigs
	 */
	va = (void *)((ulong)va | 0x80000000);

	/*
	 * Walk root to second level.  Return 0 if invalid
	 * at either level.
	 */
	root = (vas->v_hat.h_vcr3)+L1IDX(va);
	if (!(*root & PT_V)) {
		return(0);
	}
	pt = ptov(*root & PT_PFN);
	pt += L2IDX(va);
	if (!(*pt & PT_V)) {
		return(0);
	}

	/*
	 * Extract phyical address in place, add in offset in page
	 */
	return (void *)((*pt & PT_PFN) | ((ulong)va & ~PT_PFN));
}

/*
 * hat_deletetrans()
 *	Delete a translation
 *
 * It is harmless to delete a non-existent translation
 */
void
hat_deletetrans(struct pview *pv, void *va, uint pfn)
{
	pte_t *pt, *root;

	/*
	 * Virtual address is in upper two gigs
	 */
	va = (void *)((ulong)va | 0x80000000);

	root = (pv->p_vas->v_hat.h_vcr3)+L1IDX(va);
	if (!(*root & PT_V)) {
		return;
	}
	pt = ptov(*root & PT_PFN);
	pt += L2IDX(va);
	if (!(*pt & PT_V)) {
		return;
	}
	*pt &= ~PT_V;
	if (pv->p_vas == curthread->t_proc->p_vas) {
		flush_tlb();
	}
}

/*
 * hat_getbits()
 *	Atomically get and clear the ref/mod bits on a translation
 */
hat_getbits(struct pview *pv, uint idx)
{
	pte_t *pt;
	void *vaddr;
	int x;

	vaddr = (void *)((ulong)(pv->p_vaddr) + ptob(idx) | 0x80000000);
	pt = pv->p_vas->v_hat.h_vcr3 + L1IDX(vaddr);
	if (!(*pt & PT_V)) {
		return(0);
	}
	pt = ptov(*pt & PT_PFN);
	pt += L2IDX(vaddr);
	x = ((*pt & PT_R) ? PP_R : 0) |
		((*pt & PT_M) ? PP_M : 0);
	*pt &= ~(PT_R|PT_M);
	return(x);
}

/*
 * hat_attach()
 *	Decide where to attach a view in a vas
 *
 * If vaddr is 0, this routine chooses on its own.  If vaddr is non-zero,
 * this routine must either attach at the requested address, or fail.
 * The upper levels must verify that a provided vaddr does not overlap
 * with other pviews; we are only being queried for hardware
 * limitations.
 *
 * Returns 0 on success, 1 on failure.
 */
hat_attach(struct pview *pv, void *vaddr)
{
	uint pg;

	/*
	 * We don't allow others to consume our utility space
	 */
	if (vaddr) {
		if ((vaddr >= (void *)VMAP_BASE) &&
				(vaddr < (void *)(VMAP_BASE+VMAP_SIZE))) {
			return(1);
		}
		return(0);
	}

	/*
	 * Otherwise try to get some space from the map, and put the
	 * new view there.
	 */
	pg = rmap_alloc(pv->p_vas->v_hat.h_map, pv->p_len);
	if (pg == 0) {
		return(1);
	}
	pv->p_vaddr = (void *)ptob(pg);
	return(0);
}

/*
 * hat_detach()
 *	pview is being removed from a vas
 */
void
hat_detach(struct pview *pv)
{
	if ((pv->p_vaddr >= (void *)VMAP_BASE) &&
			(pv->p_vaddr < (void *)(VMAP_BASE+VMAP_SIZE))) {
		rmap_free(pv->p_vas->v_hat.h_map,
			btop(pv->p_vaddr), pv->p_len);
	}
}
