#ifndef _PSET_H
#define _PSET_H
/*
 * pset.h
 *	A "set" of pages
 *
 * This data structure organizes an array which provides an overall
 * grouping plus per-page state information.
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/mutex.h>

/*
 * For enumerating current mappings of a physical page
 */
struct atl {
	struct pview *a_pview;
	uint a_idx;
	struct atl *a_next;
};

/*
 * Per-page-slot information.  The per-physical-page information is
 * a "struct core", in core.h.  This one describes a virtual page
 * slot within a pset.
 */
struct perpage {
	uchar pp_flags;		/* Flags */
	uchar pp_lock;		/* Locking bits */
	ushort pp_refs;		/* # views active through this slot */
	uint pp_pfn;		/* When valid, physical page # */
	struct atl *pp_atl;	/* List of views of this page */
};

/*
 * Bits for pp_flags
 */
#define PP_V 1		/* Page is valid (pp_pfn is page #) */
#define PP_COW 2	/* Page is currently read-only; copy on write */
#define PP_SWAPPED 4	/* Page has been swapped; an image is on swap */
#define PP_BAD 8	/* Page push failed; contents lost */
#define PP_M 16		/* Mod/ref for underlying page */
#define PP_R 32

/*
 * Bits for pp_lock
 */
#define PP_LOCK 1	/* Page is busy with in-transit I/O */
#define PP_WANT 2	/*  ...someone waiting for it */

/*
 * Page set operations.  Used to spare us big gnarly switch
 * statements.
 */
struct psetops {
	intfun psop_fillslot,		/* Fill slot with contents */
		psop_writeslot,		/* Write slot to destination */
		psop_init,		/* Called once on setup */
		psop_deinit;		/*  ...on close */
};

/*
 * Page set information.  At this level there is no concept of
 * virtual address or virtual address space.  We are merely organizing
 * a set of pages, some which have yet to be filled, some of which
 * are valid, and some of which have images on swap.
 */
struct pset {
	uint p_len;		/* # pages in set */
	uint p_off;		/*  ...offset into source */
	ushort p_type;		/* Type of pages */
	ushort p_locks;		/* # pages with PP_LOCK */
	union {
		struct pset *_p_cow;	/* Set we COW from if PT_COW */
		struct portref *_p_pr;	/* Do FS ops here if PT_FILE */
	} p_u;
	ulong p_swapblk;	/* Block # on swapdev */
#define p_cow p_u._p_cow
#define p_pr p_u._p_pr
	lock_t p_lock;		/* Mutex */
	uint p_refs;		/* # views using this set */
	struct perpage		/* Our array of per-page-slot data */
		*p_perpage;
	struct pset		/* List of sets which COW from us */
		*p_cowsets;
	struct psetops		/* Operations on pset */
		*p_ops;
	uint p_flags;		/* Flag bits */
	sema_t p_lockwait;	/* Waiters for a PP_LOCK to go away */
};

/*
 * Values for p_type
 */
#define PT_UNINIT 1	/* Uninitialized data */
#define PT_ZERO 2	/* Initiialize to zeroes */
#define PT_FILE 3	/* Initial from file */
#define PT_COW 4	/* Copy-on-write, shadowing from another pset */
#define PT_MEM 5	/* View of physical memory */

/*
 * Bits in p_flags
 */
#define PF_SHARED 1	/* All views share (shared memory, etc.) */

#ifdef KERNEL
extern struct perpage *find_pp(struct pset *, uint);
extern void lock_slot(struct pset *, struct perpage *),
	unlock_slot(struct pset *, struct perpage *);
extern int clock_slot(struct pset *, struct perpage *);
extern void deref_pset(struct pset *), ref_pset(struct pset *);
extern struct pset *alloc_pset_zfod(uint);
extern struct pview *alloc_pview(struct pset *);
extern struct pset *copy_pset(struct pset *);
extern struct pset *physmem_pset(uint, int);
extern void add_atl(struct perpage *, struct pview *, uint);
extern int delete_atl(struct perpage *, struct pview *, uint);
extern void cow_write(struct pset *, struct perpage *, uint);
extern void ref_slot(struct pset *, struct perpage *, uint),
	deref_slot(struct pset *, struct perpage *, uint);
extern void set_core(uint, struct pset *, uint);
extern struct pset *alloc_pset_cow(struct pset *, uint, uint);
extern ulong alloc_swap(uint);
extern void free_swap(ulong, uint);
extern struct pset *alloc_pset_fod(struct portref *, uint);
#endif /* KERNEL */

#endif /* _PSET_H */
