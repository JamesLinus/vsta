#ifndef _CORE_H
#define _CORE_H
/*
 * core.h
 *	Data structures organizing core
 */
#include <sys/types.h>

/*
 * For enumerating current mappings of a physical page
 */
struct atl {
	struct pview *a_pview;
	uint a_idx;
	struct atl *a_next;
};

/*
 * Per-physical-page information
 */
struct core {
	uchar c_flags;		/* Flags */
	uchar c_pad[3];		/* Padding */
	union {
		struct atl *_c_atl;	/* List of mappings */
		ulong _c_long;		/* Word of storage when C_SYS */
	} c_u;
#define c_atl c_u._c_atl
#define c_long c_u._c_long
};

/*
 * Bits in c_flags
 */
#define C_BAD 1		/* Hardware error on page */
#define C_SYS 2		/* Page wired down for kernel use */

#ifdef KERNEL
/*
 * Routines for manipulating attach lists
 */
void atl_add(int, struct pview *, uint);
void atl_del(int, struct pview *, uint);
#endif

#endif /* _CORE_H */
