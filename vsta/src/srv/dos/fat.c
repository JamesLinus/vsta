/*
 * fat.c
 *	Routines for fiddling storage via the File Allocation Table
 *
 * The FAT is read into memory and kept there through the life of
 * the filesystem server, though it is periodically flushed to disk.
 * This file has the generic FAT support code; code for a specific
 * FAT format is in FAT<format>.c.
 */
#include "dos.h"
#include "fat.h"
#include <std.h>
#include <sys/param.h>
#include <sys/assert.h>
#include <fcntl.h>

uint clsize;		/* Size of cluster, in bytes */
uint dirents;		/* # directory entries */
uint nclust;		/* # clusters in filesystem */
claddr_t
	nxt_clust;	/* Where last cluster search left off */
uint fat_size;		/* FAT format... 12, 16, or 32 */
int fat_dirty;		/* Flag that a change has occurred in the FAT */
static struct fatops
	*fatops;	/* Vectors for current FAT type */
ulong data0;		/* Byte offset for data block 0 (cluster 2) */ 

/*
 * fat_init()
 *	Initialize FAT handling
 *
 * Read in the FAT, convert if needed.
 * Extract some useful values from the boot block at the same time.
 */
void
fat_init(void)
{
	/*
	 * Calculate some static values
	 */
	ASSERT((bootb.secsize0 + (bootb.secsize1 << 8)) == SECSZ,
		"fat_init: bad sector size");
	clsize = bootb.clsize * SECSZ;

	/*
	 * Figure out FAT size
	 */
	if (bootb.fatlen == 0) {
		extern struct fatops fat32ops;

		/*
		 * If the FAT size in the base of the boot sector is 0,
		 * we know it's FAT32 (and the actual FAT size is in a
		 * FAT32 extension, which the FAT32 fatops will handle).
		 */
		fat_size = 32;
		fatops = &fat32ops;
	} else {
		/*
		 * Figure out the filesystem size.
		 * This also fills in "nclust" and "data0" on behalf
		 * of the FAT12/16 code.
		 */
		uint x = bootb.psect0 + (bootb.psect1 << 8);
		dirents = bootb.dirents0 + (bootb.dirents1 << 8);
		data0 = bootb.nrsvsect + (bootb.nfat * bootb.fatlen) +
			(dirents * sizeof(struct directory)) / SECSZ;
		if (x > 0) {
			nclust = (x * SECSZ) / clsize;
		} else {
			nclust = (bootb.bigsect - data0) / bootb.clsize;
		}

		/*
		 * Below this threshold, it's FAT12.  Otherwise FAT16
		 */
		if (nclust < MSDOS_FAT12) {
			extern struct fatops fat12ops;

			fat_size = 12;
			fatops = &fat12ops;
		} else {
			extern struct fatops fat16ops;

			fat_size = 16;
			fatops = &fat16ops;
		}
	}

	/*
	 * FAT format-specific init
	 */
	fatops->init();
}

/*
 * clust_setlen()
 *	Twiddle FAT allocation to match the indicated length
 *
 * Returns 0 if it could be done; 1 if it failed.
 */
int
clust_setlen(struct clust *c, ulong newlen)
{
	ulong newclust = roundup(newlen, clsize) / clsize;

	/*
	 * No change, so no problem
	 */
	if (c->c_nclust == newclust) {
		return(0);
	}

	return(fatops->setlen(c, newclust));
}

/*
 * alloc_clust()
 *	Allocate a description of the given cluster chain
 */
struct clust *
alloc_clust(struct directory *d)
{
	struct clust *c, *c2;

	/*
	 * Get the cluster description
	 */
	c = malloc(sizeof(struct clust));
	if (c == 0) {
		return(0);
	}

	/*
	 * Zero-length file is easy
	 */
	if ((d == 0) || (START(d) == 0)) {
		c->c_nclust = 0;
		c->c_clust = 0;
		return(c);
	}

	/*
	 * Let FAT specific code take over
	 */
	c2 = fatops->alloc(c, d);

	/*
	 * On failure, dump the clust struct
	 */
	if (c2 == NULL) {
		free(c);
	}

	return(c2);
}

/*
 * free_clust()
 *	Free a cluster description
 */
void
free_clust(struct clust *c)
{
	if (c->c_clust) {
		free(c->c_clust);
	}
	free(c);
}

/*
 * fat_sync()
 *	Sync out the FAT to disk
 *
 * Both copies are updated; if the first copy can not be written
 * successfully, the second is left alone and the server aborts.
 */
void
fat_sync(void)
{
	/*
	 * Not dirty--no work
	 */
	if (!fat_dirty) {
		return;
	}

	/*
	 * Sync the appropriate type of FAT
	 */
	fatops->sync();

	/*
	 * Now it's clean
	 */
	fat_dirty = 0;
}

/*
 * get_clust()
 *	Get cluster # of given cluster slot
 *
 * Used to fill in the "start" field of dir entries like "..",
 * as well as to support inode number generation.
 */
claddr_t
get_clust(struct clust *c, uint idx)
{
	ASSERT_DEBUG(c->c_nclust > idx, "get_clust: bad index");
	return(c->c_clust[idx]);
}
