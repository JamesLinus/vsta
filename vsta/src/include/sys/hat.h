#ifndef _HAT_H
#define _HAT_H
/*
 * hat.h
 *	Definitions for Hardware Address Translation routines
 */
#include <sys/types.h>
#include <sys/vas.h>
#include <sys/pview.h>
#include <mach/hat.h>

/*
 * hat_vtop()
 *	Given virtual address and address space, return physical address
 *
 * Returns 0 if a translation does not exist
 */
void *hat_vtop(struct vas *, void *);

/*
 * hat_initvas()
 *	Initialize a vas
 */
void hat_initvas(struct vas *);

/*
 * hat_addtrans()
 *	Add a translation
 */
void hat_addtrans(struct pview *pv, void *vaddr, uint pfn, int write);

/*
 * hat_deletetrans()
 *	Delete a translation
 */
void hat_deletetrans(struct pview *pv, void *vaddr, uint pfn);

/*
 * hat_attach()
 *	Try to attach a new view
 */
int hat_attach(struct pview *pv, void *vaddr);

/*
 * hat_detach()
 *	Detach existing view
 */
void hat_detach(struct pview *pv);

#endif /* _HAT_H */
