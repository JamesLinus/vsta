#ifdef KDB
/*
 * mem.c
 *	Routines for examining memory (or an image thereof)
 */
#include <sys/types.h>
#include <sys/param.h>
#include <mach/setjmp.h>
#include <mach/pte.h>
#include <sys/assert.h>
#include <sys/vm.h>

#define DBG_PAGES (4)
#define VOFF (NBPG-1)
#define M (1024*1024)

extern jmp_buf dbg_errjmp;
static char *dbg_root, *dbg_utl;
extern int upyet;

/*
 * init_debug()
 *	Initialize debugger
 *
 * This must be called after the vmap is set up in the portable
 * layer.  We carve out a number of virtual pages which we then
 * use to map arbitrary physical pages into.
 */
void
init_debug(void)
{
	extern uint alloc_vmap();

	dbg_root = (char *)ptob(alloc_vmap(1));
	ASSERT(dbg_root != 0, "init_debug: no pages");
	dbg_utl = (char *)ptob(alloc_vmap(DBG_PAGES));
	ASSERT(dbg_utl != 0, "init_debug: no utl pages");
}

/*
 * dbg_vtop()
 *	Convert kernel virtual into physical
 */
static ulong
dbg_vtop(addr)
	ulong addr;
{
	pte_t *pt;
	ulong l, pfn;
	extern ulong get_cr3();

	/*
	 * Get level 1 PTE
	 */
	pfn = (get_cr3() & PT_PFN) >> PT_PFNSHIFT;
	kern_addtrans(dbg_root, pfn);
	pt = (pte_t *)dbg_root;
	pt += L1IDX(addr);
	l = *pt;
	kern_deletetrans(dbg_root, pfn);
	if (!(l & PT_V)) {
 		printf("Error: L1PT invalid on 0x%x\n", addr);
 		longjmp(dbg_errjmp, 1);
	}

 	/*
 	 * Get just page number part, add in offset from vaddr,
 	 * get appopriate PTE
 	 */
	pfn = (l & PT_PFN) >> PT_PFNSHIFT;
	kern_addtrans(dbg_root, pfn);
	pt = (pte_t *)dbg_root;
	pt += L2IDX(addr);
	l = *pt;
	kern_deletetrans(dbg_root, pfn);
	if (!(l & PT_V)) {
 		printf("Error: L2PT invalid on 0x%x\n", addr);
 		longjmp(dbg_errjmp, 1);
	}
	return((l & PT_PFN) | (addr & VOFF));
}

/*
 * maploc()
 *	Given address, return a kernel vaddr mapping to it
 */
void *
maploc(ulong off, uint size, int phys)
{
	ulong a;
	int len, pgidx;
	extern char end[];

	/*
	 * If not "upyet", can only use 1:1 mapping of first 4 Mb or
	 * data region.
	 */
	if (!upyet) {
		return((void *)off);
	}

	/*
	 * Physical is all mapped in a row now
	 */
	if (phys) {
		return((void *)ptov(off));
	}

 	/*
 	 * Walk across each page, handle virtual and physical as
	 * appropriate.
 	 */
	pgidx = 0;
	for (a = off; a < off+size; a += len) {
 		ulong o;

 		len = NBPG - (a & VOFF);
 		if (len > ((off + size) - a)) {
 			len = (off + size) - a;
		}
		if (phys) {
			o = a;
		} else {
			o = dbg_vtop(a);
		}
		kern_addtrans(dbg_utl + ptob(pgidx), btop(o));
		pgidx += 1;
	}
	return(dbg_utl + (off & VOFF));
}
#endif /* KDB */
