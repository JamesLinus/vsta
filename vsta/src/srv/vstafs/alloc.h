#ifndef ALLOC_H
#define ALLOC_H
/*
 * alloc.h
 *	Definitions for managing the free block list
 */
#include <vstafs/vstafs.h>

/*
 * We always keep the free list blocks in-core, and just flush out
 * the appropriate ones as needed.  This data structure tabulates
 * the in-core copy.
 */
struct freelist {
	daddr_t fr_this;	/* Sector # for this free list block */
	struct free fr_free;	/* Image of free list block on disk */
	struct freelist		/* Core version of f_free.f_next */
		*fr_next;
};

/*
 * Externally-usable routines
 */
extern void init_block(void);
extern daddr_t alloc_block(uint);
extern void free_block(daddr_t, uint);
extern ulong take_block(daddr_t, ulong);

#endif /* ALLOC_H */
