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
	ushort c_flags;		/* Flags */
	ushort c_psidx;		/* Index into pset */
	union {
	struct pset *_c_pset;	/* Pset page is used under */
	ulong _c_long;		/* Word of storage when C_SYS */
	struct core *_c_free;	/* Free list link when free */
	} _c_u;
#define c_pset _c_u._c_pset
#define c_long _c_u._c_long
#define c_free _c_u._c_free
};

/*
 * Bits in c_flags
 */
#define C_BAD 1		/* Hardware error on page */
#define C_SYS 2		/* Page wired down for kernel use */
#define C_WIRED 4	/* Wired for physical I/O */

#endif /* _CORE_H */
