#ifndef _MACH_VM_H
#define _MACH_VM_H
/*
 * vm.h
 *	Machine-dependent virtual memory support
 */
#include <sys/vm.h>
#include <sys/assert.h>
#include "../mach/locore.h"

/*
 * Precalculated address range for utility mapping area
 */
#define MAPLOW ((void *)(BYTES_L1PT*L1PT_UTIL))
#define MAPHIGH ((void *)(BYTES_L1PT*L1PT_UTIL+BYTES_L1PT))

/*
 * Place where, because we recursively remap our root, all the
 * L2 page tables are visible.
 */
#define L2PTMAP (pte_t *)(L1PT_CR3*BYTES_L1PT)

/*
 * A static L1PT with only root entries.  Nice because it'll work
 * even when you're running on the idle stack.
 */
extern pte_t *cr3;

/*
 * vtop()
 *	Return physical address for kernel virtual one
 *
 * Only works for kernel addresses, since it uses information from
 * a root page table which only uses the L2PTEs for root mappings.
 */
inline extern void *
vtop(void *vaddr)
{
	pte_t *pt;

	/*
	 * Point to base of L1PTEs
	 */
	pt = cr3;

	/*
	 * See if L1 is valid
	 */
	pt += L1IDX(vaddr);
	ASSERT_DEBUG(*pt & PT_V, "vtop: invalid L1");

	/*
	 * If it is, walk down to second level
	 */
	pt = L2PTMAP + ((ulong)vaddr >> PT_PFNSHIFT);
	ASSERT_DEBUG(*pt & PT_V, "vtop: invalid L2");

	/*
	 * Construct physical addr by preserving old offset, but
	 * swapping in new pfn.
	 */
	return (void *)((*pt & PT_PFN) | ((ulong)vaddr & ~PT_PFN));
}

/*
 * kern_addtrans()
 *	Add a kernel translation
 */
inline extern void
kern_addtrans(void *vaddr, uint pfn)
{
	pte_t *pt;

	ASSERT_DEBUG((vaddr >= MAPLOW) && (vaddr < MAPHIGH),
		"kern_addtrans: bad vaddr");
	pt = L2PTMAP + ((ulong)vaddr >> PT_PFNSHIFT);
	*pt = (pfn << PT_PFNSHIFT) | PT_V|PT_W;
}

/*
 * kern_deletetrans()
 *	Delete a kernel translation
 */
inline extern void
kern_deletetrans(void *vaddr, uint pfn)
{
	pte_t *pt;

	ASSERT_DEBUG((vaddr >= MAPLOW) && (vaddr < MAPHIGH),
		"kern_deletetrans: bad vaddr");
	pt = L2PTMAP;
	pt += ((ulong)vaddr >> PT_PFNSHIFT);
	*pt = 0;
	flush_tlb(vaddr);
}

#endif /* _MACH_VM_H */
