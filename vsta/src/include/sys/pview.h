/*
 * pview.h
 *	A "view" of some pages
 *
 * One or more of these exist under a vas struct
 */
#ifndef _PVIEW_H
#define _PVIEW_H
#include <sys/types.h>

struct pview {
	struct pset *p_set;	/* Physical pages under view */
	void *p_vaddr;		/* Virtual address of view */
	uint p_len;		/* # pages in view */
	uint p_off;		/* Offset within p_set */
	struct vas *p_vas;	/* VAS we're located under */
	struct pview		/* For listing under vas */
		*p_next;
	uchar p_prot;		/* Protections on view */
};

/*
 * Bits for protection
 */
#define PROT_RO (1)		/* Read only */
#define PROT_KERN (2)		/* Kernel only */
#define PROT_MMAP (4)		/* Created by mmap() */
#define PROT_FORK (8)		/* View is in process of fork() */

#ifdef KERNEL
/*
 * Routines
 */
extern void free_pview(struct pview *);
extern struct pview *dup_pview(struct pview *),
	*copy_pview(struct pview *);
extern struct pview *alloc_pview(struct pset *);
extern void remove_pview(struct vas *, void *);
extern void attach_valid_slots(struct pview *);

#endif /* KERNEL */
#endif /* _PVIEW_H */
