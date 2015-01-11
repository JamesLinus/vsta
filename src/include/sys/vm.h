#ifndef _VM_H
#define _VM_H
/*
 * vm.h
 *	Some handy definitions for VM-type stuff
 */
#include <sys/types.h>
#include <sys/vas.h>
#include <sys/pset.h>
#include <sys/hat.h>

/*
 * Linear P->V translation of all memory based here.  Routines are then
 * available to convert p->v on the fly.
 */
extern char *mem_map_base;
#define ptov(paddr) ((void *)(((ulong)paddr) + mem_map_base))

/*
 * Description of memory segments
 */
struct memseg {
	void *m_base;	/* Byte address of start */
	ulong m_len;	/* Memory in bytes */
};
extern struct memseg memsegs[];
extern int nmemsegs;

/*
 * Utility routines
 */
extern void lock_page(uint), unlock_page(uint);
extern int clock_page(uint);
extern uint alloc_page(void);
extern void free_page(uint);
extern void *alloc_pages(uint), free_pages(void *, uint);
extern int vm_unvirt(struct perpage *);

#endif /* _VM_H */
