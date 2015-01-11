/*
 * fat12.c
 *	Routines for FAT-12 FAT's
 */
#include "dos.h"
#include "fat.h"
#include <std.h>
#include <sys/param.h>
#include <sys/assert.h>
#include <fcntl.h>
#include <syslog.h>

/*
 * Values for FAT slots
 */
#define FAT_RESERVED (0xFF0)	/* Start of reserved value range */
#define FAT_DEFECT (0xFF7)	/* Cluster w. defective block */
#define FAT_EOF (0xFF8)		/* Start of EOF range */
#define FAT_END (0xFFF)		/* End of reserved range */

typedef ushort fat12_t;	/* A 12-bit FAT value */

static uchar *fat;	/* Our in-core FAT */
static uint fatlen;	/* Length in bytes */
static uchar *dirtymap;	/* Map of sectors with dirty FAT entries */
static uint		/*  ...size of map */
	dirtymapsize;
static claddr_t
	nxt_clust;	/* Where last cluster search left off */

/*
 * dirty()
 *	Flag a given entry as being dirtied
 *
 * Need to do squirrely math for the 1.5-byte entries
 */
static void
dirty(uint idx)
{
	uint offset;

	/*
	 * Scale the entry size up by 2 (1.5 bytes * 2 == 3) so we can
	 * calculate in integer; scale divisor by 2, also.
	 */
	offset = (idx * 3) / (SECSZ * 2);

	/*
	 * Flag the containing sector as dirty
	 */
	dirtymap[offset] = 1;

	/*
	 * Flag the next slot up, too.  Each entry spans two bytes, and
	 * these two bytes could span sectors.
	 */
	offset = (++idx * 3) / (SECSZ * 2);
	dirtymap[offset] = 1;
}

/*
 * set()
 *	Set a 12-bit slot to the given value
 *
 * Automatically marks this part of the FAT dirty, too.
 */
static void
set(uint idx, fat12_t val)
{
	uchar *pos;

	pos = &fat[(idx * 3) / 2];
	if (idx & 1) {
		*pos = (*pos & 0x0F) | ((val & 0x0F) << 4);
		*++pos = (val >> 4);
	} else {
		*pos++ = val;
		*pos = (*pos & 0xF0) | ((val & 0xF00) >> 8);
	}
	dirty(idx);
}

/*
 * get()
 *	Get a 12-bit slot value
 */
static fat12_t
get(uint idx)
{
	uchar *pos;

	pos = &fat[(idx * 3) / 2];
	if (idx & 1) {
		return ((pos[0] & 0xF0) >> 4) | (pos[1] << 4);
	} else {
		return pos[0] | ((pos[1] & 0x0F) << 8);
	}
}

/*
 * fat_init()
 *	Initialize FAT handling
 */
void
fat12_init(void)
{
	/*
	 * Get map of dirty sectors in FAT.  "nclust" is multipled
	 * by 1.5, by scaling the values by 2 (1.5 bytes * 2 == 3).
	 */
	dirtymapsize = roundup((nclust * 3)/2, SECSZ) / SECSZ;
	dirtymap = malloc(dirtymapsize);
	ASSERT(dirtymap, "fat12_init: dirtymap");
	bzero(dirtymap, dirtymapsize);

	/*
	 * Get memory for FAT table
	 */
	fatlen = bootb.fatlen * SECSZ;
	fat = malloc(fatlen);
	if (fat == 0) {
		perror("fat12_init");
		exit(1);
	}

	/*
	 * Seek to FAT table on disk, read into buffer
	 */
	lseek(blkdev, 1 * SECSZ, 0);
	if (read(blkdev, fat, fatlen) != fatlen) {
		syslog(LOG_ERR, "read (%d bytes) of FAT failed",
			fatlen);
		exit(1);
	}
}

/*
 * fat12_setlen()
 *	Twiddle FAT allocation to match the indicated length
 *
 * Returns 0 if it could be done; 1 if it failed.
 */
int
fat12_setlen(struct clust *c, uint newclust)
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
			set(cl, 0);
		}
		if (newclust > 0) {
			set(cl = c->c_clust[newclust-1], FAT_EOF);
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
		while ((clust_cnt++ < nclust) && get(cl)) {
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
		set(c->c_clust[c->c_nclust-1], c->c_clust[c->c_nclust]);
	}

	/*
	 * Chain all the new clusters together
	 */
	for (x = c->c_nclust; x < newclust-1; ++x) {
		set(c->c_clust[x], c->c_clust[x+1]);
	}

	/*
	 * Mark the EOF cluster for the last one
	 */
	set(c->c_clust[newclust-1], FAT_EOF);
	c->c_nclust = newclust;
	fat_dirty = 1;
	return(0);
}

/*
 * fat12_alloc()
 *	Allocate a description of the given cluster chain
 */
struct clust *
fat12_alloc(struct clust *c, struct directory *d)
{
	uint nclust = 1, x, start;

	/*
	 * Get starting cluster from directory entry
	 */
	start = d->start;

	/*
	 * Scan the chain to get its length
	 */
	for (x = start; get(x) < FAT_RESERVED; x = get(x)) {
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
		x = get(x);
	} while (x < FAT_RESERVED);
	return(c);
}

/*
 * fat12_sync()
 *	Write a FAT12 using the dirtymap to minimize I/O
 */
static void
fat12_sync(void)
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
			lseek(blkdev, SECSZ + x*SECSZ + off, 0);
			if (write(blkdev, &fat[x*SECSZ],
					SECSZ*cnt) != (SECSZ*cnt)) {
				perror("fat12");
				syslog(LOG_ERR, "write of FAT12 #%d failed",
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
struct fatops fat12ops = {
	fat12_init,
	fat12_setlen,
	fat12_alloc,
	fat12_sync,
	NULL
};
