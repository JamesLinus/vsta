/*
 * fat.h
 *	Common definitions for all FAT types
 */
#ifndef FAT_H
#define FAT_H
#include "dos.h"

/*
 * FAT operation vectors
 */
struct fatops {
	void (*init)(void);			/* Initialize */
	int (*setlen)(struct clust *, uint);	/* Set length */
	struct clust *((*alloc)			/* Allocate new */
		(struct clust *, struct directory *));
	void (*sync)(void);			/* Flush to disk */
};

/*
 * Shared variabls among different FAT implementations
 */
extern int fat_dirty;		/* A change has occurred in the FAT */
extern uint fat_size;		/* FAT format (12, 16, or 32) */
extern uint nclust;		/* # clusters in filesystem */

#endif /* FAT_H */
