#ifndef _MACHHAT_H
#define _MACHHAT_H
/*
 * hatvas.h
 *	Hardware-specific information pertaining to a vas
 */
#include <mach/vm.h>
#include <lib/rmap.h>

struct hatvas {
	pte_t *h_vcr3;		/* CR3, in its raw form and as a vaddr */
	ulong h_cr3;
	struct rmap *h_map;	/* Map of vaddr's free */
};

#endif /* _MACHHAT_H */
