/*
 * vas.h
 *	Definitions of a Virtual Address Space
 */
#ifndef _VAS_H
#define _VAS_H
#include <sys/mutex.h>
#include <mach/hat.h>
#include <sys/pview.h>

struct vas {
	struct pview		/* List of views in address space */
		*v_views;
	lock_t v_lock;		/* Mutex */
	uint v_flags;		/* Flags */
	struct hatvas
		v_hat;		/* Hardware-dependent stuff */
};

/*
 * Bits in v_flags
 */
#define VF_DMA 1		/* VAS used by DMA server */
#define VF_MEMLOCK 2		/* Don't page from this VAS */

#ifdef KERNEL
extern struct pview *detach_pview(struct vas *, void *),
	*find_pview(struct vas *, void *);
extern void *attach_pview(struct vas *, struct pview *);
extern void remove_pview(struct vas *, void *);
extern struct pview *find_ivew(struct vas *, void *);
extern void free_vas(struct vas *);
extern void *alloc_zfod(struct vas *, uint),
	*alloc_zfod_vaddr(struct vas *, uint, void *);
#endif /* KERNEL */

#endif /* _VAS_H */
