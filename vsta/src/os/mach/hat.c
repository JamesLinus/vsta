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
#include <sys/proc.h>
#include <sys/param.h>
#include <sys/pview.h>
#include <sys/vas.h>
#include <mach/pte.h>
#include <sys/pset.h>
#include <sys/thread.h>
#include <sys/percpu.h>
#include <sys/malloc.h>
#include "../mach/locore.h"
#include "../mach/vminline.h"

#define NRMAPSLOT (20)		/* # slots for our vaddr map */

extern void rmap_init();

/*
 * hat_initvas()
 *	Initialize hardware-dependent portion vas
 *
 * May sleep waiting for memory
 */
void
hat_initvas(struct vas *vas)
{
	pte_t *c;
	extern pte_t *cr3;
	extern int freel1pt;

	/*
	 * Get root page table, duplicate the master kernel copy.
	 * This one has all the usual slots fille in for kernel
	 * address space, and all zeroes for user.
	 */
	c = vas->v_hat.h_vcr3 = MALLOC(NBPG, MT_L1PT);
	vas->v_hat.h_cr3 = (ulong)vtop(c);
	bzero(c, NBPG);
	bcopy(cr3, c, freel1pt * sizeof(pte_t));
	vas->v_hat.h_l1segs = 0L;

	/*
	 * Get an address map for doing on-demand virtual address
	 * allocations.
	 */
	vas->v_hat.h_map = MALLOC(NRMAPSLOT * sizeof(struct rmap),
		MT_RMAP);
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
	pte_t *pt, *ptend;
	ulong bit;

	/*
	 * Free any level-two page tables we allocated for
	 * the user.  We use our bitmap in h_l1segs to skip
	 * scanning parts of the L1 pte's which have never been
	 * touched.  We only scan the top 2 GB, since kernel
	 * address occupies the lower 2 GB, and is not involved
	 * with VM address space tear-down.
	 */
	ptend = pt = vas->v_hat.h_vcr3;
	pt += (NPTPG/2);
	ptend += NPTPG;
	bit = (1L << (H_L1SEGS/2));
	while (pt < ptend) {
		/*
		 * If we've touched this part of the address space...
		 */
		if (vas->v_hat.h_l1segs & bit) {
			uint x;

			/*
			 * Walk across each L1 PTE in it and free L2
			 * page tables.
			 */
			for (x = 0; x < NPTPG/H_L1SEGS; ++x,++pt) {
				if (*pt & PT_V) {
					free_page(*pt >> PT_PFNSHIFT);
				}
			}
		} else {
			/*
			 * Skip this part
			 */
			pt += (NPTPG/H_L1SEGS);
		}

		/*
		 * Advance to next part of L1 PTEs
		 */
		bit <<= 1;
	}

	/*
	 * Free root and map
	 */
	FREE(vas->v_hat.h_vcr3, MT_L1PT);
	FREE(vas->v_hat.h_map, MT_RMAP);
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
		pt = (pte_t *)ptov(ptob(pg));
		bzero(pt, NBPG);
		*root = (pg << PT_PFNSHIFT) | PT_V|PT_W|PT_U;
		pv->p_vas->v_hat.h_l1segs |=
			(1L << (L1IDX(va)*H_L1SEGS / NPTPG));
	} else {
		pt = ptov(*root & PT_PFN);
	}

	/*
	 * Get the address
	 */
	pt += L2IDX(va);
	*pt = (pfn << PT_PFNSHIFT) | PT_V | PT_U |
		((prot & PROT_RO) ? 0 : PT_W);
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
	if (pv->p_vas == &curthread->t_proc->p_vas) {
		flush_tlb(va);
	}
}

/*
 * hat_getbits()
 *	Atomically get and clear the ref/mod bits on a translation
 */
uchar
hat_getbits(struct pview *pv, void *vaddr)
{
	pte_t *pt;
	uchar x;

	vaddr = (void *)((ulong)vaddr | 0x80000000);
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
int
hat_attach(struct pview *pv)
{
	uint pg;
	ulong vaddr = (ulong)pv->p_vaddr;

	/*
	 * Don't let them scribble on the kernel's part of the
	 * address space.
	 */
	if (vaddr && (vaddr >= 0x80000000)) {
		return(1);
	}

	/*
	 * We don't allow others to consume our utility space
	 */
	if (vaddr) {
		/*
		 * This bit indicates that this is a duplication of
		 * the address space, so using our vaddrs is OK.  This
		 * also assumes that a hat_fork() will follow to bring the
		 * map up to date.
		 */
		if (pv->p_prot & PROT_FORK) {
			return(0);
		}

		/*
		 * Otherwise leave our stuff alone
		 */
		if ((vaddr >= VMAP_BASE) &&
				(vaddr < (VMAP_BASE+VMAP_SIZE))) {
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

/*
 * hat_fork()
 *	vas is being duplicated, copy over rmap state
 *
 * Must be called after hat_init(), since it assumes the new vas
 * already has its map allocated.
 */
void
hat_fork(struct vas *ovas, struct vas *vas)
{
	bcopy(ovas->v_hat.h_map, vas->v_hat.h_map,
		NRMAPSLOT*sizeof(struct rmap));
}
