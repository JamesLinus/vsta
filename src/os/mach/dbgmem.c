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
#define MALLOC_INTERNAL
#include <sys/malloc.h>
#include "../mach/locore.h"
#include "../mach/vminline.h"

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
dbg_vtop(ulong addr)
{
	pte_t *pt;
	ulong l, pfn;

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
		void *kaddr = dbg_utl + ptob(pgidx);

		/*
		 * Clear out any preceding mapping from the TLB.
		 * Note that we encode here our knowledge that an x86
		 * deletetrans doesn't care about the current PFN.
		 */
		ASSERT_DEBUG(pgidx < DBG_PAGES, "maploc: too many pages");
		kern_deletetrans(kaddr, 0);

		/*
		 * Calculate what to map
		 */
 		len = NBPG - (a & VOFF);
 		if (len > ((off + size) - a)) {
 			len = (off + size) - a;
		}
		if (phys) {
			o = a;
		} else {
			o = dbg_vtop(a);
		}

		/*
		 * Add in the current mapping
		 */
		kern_addtrans(kaddr, btop(o));
		pgidx += 1;
	}
	return(dbg_utl + (off & VOFF));
}

/*
 * dump_buck()
 *	Dump out change from old to new bucket state
 */
static void
dump_buck(struct bucket *n, struct bucket *o)
{
	printf("%d byte pool: ", n->b_size);
	if (n->b_elems < o->b_elems) {
		printf("lost %d elems", o->b_elems - n->b_elems);
	} else {
		printf("gained %d elems", n->b_elems - o->b_elems);
	}
	if (n->b_pages != o->b_pages) {
		if (n->b_pages < o->b_pages) {
			printf(", lost %d pages", o->b_pages - n->b_pages);
		} else {
			printf(", gained %d pages", n->b_pages - o->b_pages);
		}
	}
	printf("\n");
}

#ifdef DEBUG
/*
 * dump_usage()
 *	Tell about changes in usage by memory type
 */
static void
dump_usage(void)
{
	static ulong on_alloc[MALLOCTYPES];
	int x;
	extern ulong n_alloc[];
	extern char *n_allocname[];

	for (x = 0; x < MALLOCTYPES; ++x) {
		if (n_alloc[x] != on_alloc[x]) {
			if (n_alloc[x] > on_alloc[x]) {
				printf("Gained %d type %s\n",
					n_alloc[x] - on_alloc[x],
					n_allocname[x]);
			} else {
				printf("Lost %d type %s\n",
					on_alloc[x] - n_alloc[x],
					n_allocname[x]);
			}
		}
	}
	bcopy(n_alloc, on_alloc, sizeof(on_alloc));
}
#endif

/*
 * memleaks()
 *	Scan for memory leaks
 */
void
memleaks(void)
{
	static struct bucket obuckets[PGSHIFT];
	struct bucket *b, *b2;
	static ulong ofreemem;
	int x;
	extern ulong freemem;

	/*
	 * Global system memory
	 */
	if (ofreemem != freemem) {
		if (ofreemem > freemem) {
			printf("Lost %d pages\n", ofreemem - freemem);
		} else {
			printf("Gained %d pages\n", freemem - ofreemem);
		}
		ofreemem = freemem;
	}

	/*
	 * malloc() pools
	 */
	b = buckets;
	b2 = obuckets;
	for (x = 0; x < PGSHIFT; ++x,++b,++b2) {
		if ((b->b_elems != b2->b_elems) ||
				(b->b_pages != b2->b_pages)) {
			dump_buck(b, b2);
		}
	}
	bcopy(buckets, obuckets, sizeof(obuckets));

#ifdef DEBUG
	/*
	 * Tell about type usage
	 */
	dump_usage();
#endif
}

#endif /* KDB */
