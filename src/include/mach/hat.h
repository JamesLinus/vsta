#ifndef _MACHHAT_H
#define _MACHHAT_H
/*
 * hatvas.h
 *	Hardware-specific information pertaining to a vas
 */
#include <mach/vm.h>
#include <rmap.h>

struct hatvas {
	pte_t *h_vcr3;		/* CR3, in its raw form and as a vaddr */
	ulong h_cr3;
	struct rmap *h_map;	/* Map of vaddr's free */
	ulong h_l1segs;		/* Bit map of which L1 slots used */
};

struct hatpview {
	/* Empty on x86 */
};

struct hatpset {
	/* Empty on x86 */
};

#define H_L1SEGS (32)		/* # parts L1 PTE's broken into */

#endif /* _MACHHAT_H */
