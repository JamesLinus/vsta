#ifndef _CORE_H
#define _CORE_H
/*
 * core.h
 *	Data structures organizing core
 */
#include <sys/types.h>

/*
 * Per-physical-page information
 */
struct core {
	uchar c_flags;		/* Flags */
	uchar c_dummy1;
	ushort c_psidx;		/* Index into pset */
	union {
		struct pset	/* Pset page is used under */
			*_c_pset;
		ulong _c_word;	/* Word of storage when C_SYS */
		struct core	/* Free list link when free */
			*_c_free;
	} _c_u;
#define c_pset _c_u._c_pset
#define c_word _c_u._c_word
#define c_free _c_u._c_free
};

/*
 * Bits in c_flags
 */
#define C_BAD 1		/* Hardware error on page */
#define C_SYS 2		/* Page wired down for kernel use */
#define C_WIRED 4	/* Wired for physical I/O */
#define C_ALLOC 8	/* Allocated from free list */

#ifdef KERNEL

extern struct core *core, *coreNCORE;

/*
 * Access per-page data values
 */
#define PAGE_GETVAL(pg) (core[pg].c_word)
#define PAGE_SETVAL(pg, val) (core[pg].c_word = (val))
#define PAGE_SETSYS(pg) (core[pg].c_flags |= C_SYS)

#endif /* KERNEL */

#endif /* _CORE_H */
