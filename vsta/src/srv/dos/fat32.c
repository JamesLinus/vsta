/*
 * fat32.c
 *	Routines for FAT32 format
 */
#include "dos.h"
#include "fat.h"
#include <std.h>
#include <sys/param.h>
#include <sys/assert.h>
#include <fcntl.h>
#include <syslog.h>

/*
 * FAT flag values for FAT-32
 */
#define FAT_RESERVED (0xFFFFFFF0)	/* Start of reserved value range */
#define FAT_DEFECT (0xFFFFFFF7)		/* Cluster w. defective block */
#define FAT_EOF (0xFFFFFFF8)		/* Start of EOF range */
#define FAT_END (0xFFFFFFFF)		/* End of reserved range */

/*
 * Format of our FAT entries
 */
typedef ulong fat32_t;

/*
 * Private storage
 */
static fat32_t *fat;	/* Our in-core FAT */
static uint fatlen;	/*  ...length */
static uchar *dirtymap;	/* Map of sectors with dirty FAT entries */
static uint		/*  ...size of map */
	dirtymapsize;
static claddr_t
	nxt_clust;	/* Where last cluster search left off */
static uint fatbase;	/* Sector # of base of FAT */

/*
 * DIRTY()
 *	Mark dirtymap at given position
 */
#define DIRTY(idx) (dirtymap[(idx * sizeof(fat32_t)) / SECSZ] = 1)

/*
 * fat32_init()
 *	Initialize FAT handling
 *
 * Read in the FAT, convert if needed.
 */
static void
fat32_init(void)
{
	uint x;

	/*
	 * Calculate some global parameters
	 */
	x = bootb.psect0 + (bootb.psect1 << 8);
	data0 = bootb.nrsvsect + bootb.u.fat32.bigFat*bootb.nfat;
	root_cluster = bootb.u.fat32.rootCluster;
	ASSERT_DEBUG(root_cluster != 0, "fat32_init: zero root cluster");
	if (x > 0) {
		nclust = (x * SECSZ) / clsize;
	} else {
		nclust = (bootb.bigsect - data0) / bootb.clsize;
	}

	/*
	 * Get map of dirty sectors in FAT
	 */
	dirtymapsize = roundup(nclust*sizeof(fat32_t), SECSZ) / SECSZ;
	dirtymap = malloc(dirtymapsize);
	ASSERT(dirtymap, "fat32_init: dirtymap");
	bzero(dirtymap, dirtymapsize);

	/*
	 * Get memory for FAT table
	 */
	fatbase = bootb.nrsvsect;
	fatlen = bootb.u.fat32.bigFat * SECSZ;
	fat = malloc(fatlen);
	if (fat == 0) {
		perror("fat32_init");
		exit(1);
	}

	/*
	 * Seek to FAT table on disk, read into buffer
	 */
	lseek(blkdev, fatbase * SECSZ, 0);
	if (read(blkdev, fat, fatlen) != fatlen) {
		syslog(LOG_ERR, "read (%d bytes) of FAT failed",
			fatlen);
		exit(1);
	}
}

/*
 * fat32_setlen()
 *	Twiddle FAT allocation to match the indicated length
 *
 * Returns 0 if it could be done; 1 if it failed.
 */
static int
fat32_setlen(struct clust *c, uint newclust)
{
	uint clust_cnt, x;
	claddr_t cl, *ctmp;

	/*
	 * Getting smaller--free stuff at the end
	 */
	if (c->c_nclust > newclust) {
		nxt_clust = newclust;
		for (x = newclust; x < c->c_nclust; ++x) {
			cl = c->c_clust[x];
#ifdef DEBUG
			if ((cl >= nclust) || (cl < 2)) {
				uint z;

				syslog(LOG_ERR, "bad cluster 0x%x", cl);
				syslog(LOG_ERR, "clusters in file:");
				for (z = 0; z < c->c_nclust; ++z) {
					syslog(LOG_ERR, " %x", c->c_clust[z]);
				}
				ASSERT(0, "fat_setlen: bad clust");
			}
#endif
			fat[cl] = 0;
			DIRTY(cl);
		}
		if (newclust > 0) {
			fat[cl = c->c_clust[newclust-1]] = FAT_EOF;
			DIRTY(cl);
		}
		c->c_nclust = newclust;
		fat_dirty = 1;
		return(0);
	}

	/*
	 * Trying to grow.  See if we can get the blocks.  If we can't,
	 * we bail out.  The allocation is done in two passes, so that
	 * when we bail after running out of space there's nothing which
	 * needs to be undone.  The realloc()'ed c_clust is harmless.
	 */
	ctmp = realloc(c->c_clust, newclust * sizeof(claddr_t));
	if (ctmp == 0) {
		return(1);
	}
	c->c_clust = ctmp;
	cl = nxt_clust;
	clust_cnt = 0;
	for (x = c->c_nclust; x < newclust; ++x) {
		/*
		 * Scan for next free cluster
		 */
		while ((clust_cnt++ < nclust) && fat[cl]) {
			if (++cl >= nclust) {
				cl = 0;
			}
		}

		/*
		 * If we didn't find one, fail
		 */
		if (clust_cnt >= nclust) {
			return(1);
		}

		/*
		 * Tag where last search finished to save time
		 */
		nxt_clust = cl + 1;
		if (nxt_clust >= nclust) {
			nxt_clust = 0;
		}

		/*
		 * Sanity
		 */
		ASSERT_DEBUG((cl >= 2) && (cl < nclust),
			"clust_setlen: bad FAT");

		/*
		 * Otherwise add it to our array.  We will flag it
		 * consumed soon.
		 */
		ctmp[x] = cl++;
	}

	/*
	 * When we get here, the new clusters for the file extension
	 * have been found and filled into the c_clust[] array.
	 * We now go back and (1) flag the FAT entries consumed,
	 * which also builds the cluster chain, and (2) update the
	 * count of clusters for this file.
	 */

	/*
	 * Chain last block which already existed onto this new
	 * space.
	 */
	if (c->c_nclust > 0) {
		fat[cl = c->c_clust[c->c_nclust-1]] = c->c_clust[c->c_nclust];
		DIRTY(cl);
	}

	/*
	 * Chain all the new clusters together
	 */
	for (x = c->c_nclust; x < newclust-1; ++x) {
		fat[cl = c->c_clust[x]] = c->c_clust[x+1];
		DIRTY(cl);
	}

	/*
	 * Mark the EOF cluster for the last one
	 */
	fat[cl = c->c_clust[newclust-1]] = FAT_EOF;
	DIRTY(cl);
	c->c_nclust = newclust;
	fat_dirty = 1;
	return(0);
}

/*
 * fat32_alloc()
 *	Allocate a description of the given cluster chain
 */
struct clust *
fat32_alloc(struct clust *c, struct directory *d)
{
	uint nclust = 1, start, x;

	/*
	 * Get starting cluster from directory entry
	 */
	start = d->start;

	/*
	 * Scan the chain to get its length
	 */
	for (x = start; fat[x] < FAT_RESERVED; x = fat[x]) {
		ASSERT_DEBUG(x >= 2, "alloc_clust: free cluster in file");
		nclust++;
	}

	/*
	 * Allocate the description array
	 */
	c->c_clust = malloc(nclust * sizeof(claddr_t));
	if (c->c_clust == 0) {
		return(0);
	}
	c->c_nclust = nclust;

	/*
	 * Scan again, recording each cluster number
	 */
	x = start;
	nclust = 0;
	do {
		c->c_clust[nclust++] = x;
		x = fat[x];
	} while (x < FAT_RESERVED);
	return(c);
}

/*
 * fat32_sync()
 *	Write a FAT16 using the dirtymap to minimize I/O
 */
static void
fat32_sync(void)
{
	int x, cnt, pass;
	off_t off;

	/*
	 * There are two copies of the FAT, so do them iteratively
	 */
	for (pass = 0; pass <= 1; ++pass) {
		/*
		 * Calculate the offset once per pass
		 */
		off = pass*(long)fatlen;

		/*
		 * Walk across the dirty map, find the next dirty sector
		 * of FAT information to write out.
		 */
		for (x = 0; x < dirtymapsize; ) {
			/*
			 * Not dirty.  Advance to next sector's worth.
			 */
			if (!dirtymap[x]) {
				x += 1;
				continue;
			}

			/*
			 * Now find runs, so we can flush adjacent sectors
			 * in a single operation.
			 */
			for (cnt = 1; ((x+cnt) < dirtymapsize) &&
					dirtymap[x+cnt]; ++cnt) {
				;
			}

			/*
			 * Seek to the right place, and write the data
			 */
			lseek(blkdev, fatbase*SECSZ + x*SECSZ + off, 0);
			if (write(blkdev,
					&fat[x*SECSZ/sizeof(fat32_t)],
					SECSZ*cnt) != (SECSZ*cnt)) {
				perror("fat32");
				syslog(LOG_ERR, "write of FAT16 #%d failed",
					pass);
				exit(1);
			}

			/*
			 * Advance x
			 */
			x += cnt;
		}
	}

	/*
	 * Clear dirtymap--everything's been flushed successfully
	 */
	bzero(dirtymap, dirtymapsize);
}

/*
 * Our registered vectors
 */
struct fatops fat32ops = {
	fat32_init,
	fat32_setlen,
	fat32_alloc,
	fat32_sync
};
