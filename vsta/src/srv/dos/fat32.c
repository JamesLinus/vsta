/*
 * fat32.c
 *	Routines for FAT32 format
 */
#include "dos.h"
#include "fat.h"
#include <std.h>
#include <unistd.h>
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
 * Contents of FAT32 filesystem information sector
 */
static struct fat32_info {
	ulong signature0;	/* Magic number 0 */
	uchar filler[0x1e0];
	ulong signature1;	/* Magic number 1 */
	ulong free;		/* Free clusters in filesystem */
	ulong last;		/* Last location allocated from */
} *info;
static uint infobase,		/* Sector addr of info sector */
	infosize;		/*  ...its size, rounded up to sector */

/*
 * Values for the signature[01] fields
 */
#define INFOSECT_SIGNATURE0 0x41615252
#define INFOSECT_SIGNATURE1 0x61417272

/*
 * Format of our FAT entries
 */
typedef ulong fat32_t;

/*
 * Private storage
 */
static fat32_t **fatv;	/* Our in-core FAT sections */
static uint fatlen,	/*  ...length of overall FAT */
	nfatv,		/*  ...number of slots in fatv array */
	fatvlen;	/*  ...byte size of fatv array */
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
 * FATSEG()
 *	Calculate segment index for fatv given cluster number
 */
#define FATSEGSIZE (64*1024)
#define FATSEG(idx) (((idx) * sizeof(fat32_t)) / FATSEGSIZE)
#define FATIDX(idx) ((idx) % (FATSEGSIZE / sizeof(fat32_t)))

/*
 * FATSECTSEG()
 *	Convert sector index to segment index
 */
#define FATSECT(sect) ((sect)*SECSZ / sizeof(fat32_t))
#define FATSECTSEG(sect) FATSEG(FATSECT(sect))

/*
 * lookup()
 *	Return pointer to fat32_t slot for this index value
 *
 * Uses fatv[] to permit only active parts of the FAT table
 * to reside in memory.  TBD: remove parts not recently used?
 * Creates any needed chunk of fat32_t entries on demand.
 */
static fat32_t *
lookup(claddr_t idx)
{
	uint seg = FATSEG(idx);
	fat32_t *fatp, *ptr;
	off_t off;

	/*
	 * If it isn't there yet, need to go fetch it now
	 */
	if (idx >= nfatv) {
		syslog(LOG_ERR, "bad seg %d limit %d idx %ld",
			seg, nfatv, idx);
	}
	ASSERT_DEBUG(idx < nfatv, "fat32 lookup: bad index");
	fatp = fatv[seg];
	if (fatp == 0) {
		/*
		 * Allocate this chunk
		 */
		fatp = fatv[seg] = malloc(FATSEGSIZE);

		/*
		 * Seek over in the FAT and read it
		 */
		off = (fatbase * SECSZ) + (seg * FATSEGSIZE);
		lseek(blkdev, off, SEEK_SET);
		if (read(blkdev, fatp, FATSEGSIZE) != FATSEGSIZE) {
			syslog(LOG_ERR,
				"read (%d bytes) of FAT at 0x%lx failed",
				FATSEGSIZE, off);
			ASSERT(0, "FAT-32 get(): FAT fill failed");
		}
	}

	/*
	 * Now return the particular entry within
	 */
	ptr = &fatp[FATIDX(idx)];
	ASSERT_DEBUG((ptr >= fatp) &&
		(ptr < (fat32_t *)((char *)fatp + FATSEGSIZE)),
		"fat32 lookup(): bad index");
	return(ptr);
}

/*
 * get()
 *	Access a FAT-32 entry
 */
static fat32_t
get(claddr_t idx)
{
	return(*lookup(idx));
}

/*
 * set()
 *	Set a FAT-32 entry
 */
static void
set(claddr_t idx, fat32_t val)
{
	*lookup(idx) = val;
	DIRTY(idx);
}

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
	int err;

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
	nfatv = roundup(fatlen, FATSEGSIZE) / FATSEGSIZE;
	fatvlen = nfatv * sizeof(fat32_t *);
	fatv = malloc(fatvlen);
	if (fatv == 0) {
		perror("fat32_init");
		exit(1);
	}
	bzero(fatv, fatvlen);

	/*
	 * If there's an info sector, get it, too
	 */
	infobase = bootb.u.fat32.infoSector;
	if (infobase != 0xFFFF) {
		/*
		 * Read in the sector
		 */
		lseek(blkdev, infobase * SECSZ, 0);
		infosize = roundup(sizeof(struct fat32_info), SECSZ);
		info = malloc(infosize);
		ASSERT_DEBUG(info, "fat32_init: malloc failed");
		err = read(blkdev, info, infosize);
		if (err != infosize) {
			syslog(LOG_ERR,
				"read (%d bytes) of info sector failed"
				": %d(%s)",
				infosize, err, strerror());
			exit(1);
		}

		/*
		 * Sanity check
		 */
		if ((info->signature0 != INFOSECT_SIGNATURE0) ||
				(info->signature1 != INFOSECT_SIGNATURE1)) {
			syslog(LOG_WARNING, "corrupt info sector");
			infobase = 0;
		}
	} else {
		/*
		 * Flag no info sector
		 */
		info = 0;
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
			set(cl, 0);
		}
		if (info) {
			info->free += (c->c_nclust - newclust);
		}
		if (newclust > 0) {
			set(c->c_clust[newclust-1], FAT_EOF);
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
	 * Update free list count
	 */
	if (info) {
		info->free -= (newclust - c->c_nclust);
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
 * fat32_sync()
 *	Write a FAT32 using the dirtymap to minimize I/O
 */
static void
fat32_sync(void)
{
	uint x, cnt, pass, baseSeg;
	off_t off;
	fat32_t *fatp;

	/*
	 * There are two copies of the FAT, so do them iteratively
	 */
	for (pass = 0; pass <= 1; ++pass) {
		/*
		 * Calculate the offset once per pass
		 */
		off = pass*(ulong)fatlen;

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
			baseSeg = FATSECTSEG(x);
			for (cnt = 1; ((x+cnt) < dirtymapsize) &&
					dirtymap[x+cnt]; ++cnt) {
				/*
				 * Can't write() across distinct
				 * chunks of FAT entries.
				 */
				if (FATSECTSEG(x+cnt) != baseSeg) {
					break;
				}
			}

			/*
			 * Seek to the right place, and write the data
			 */
			lseek(blkdev, fatbase*SECSZ + x*SECSZ + off, 0);
			fatp = lookup(FATSECT(x));
			if (write(blkdev, fatp, SECSZ*cnt) != (SECSZ*cnt)) {
				perror("fat32 sync");
				syslog(LOG_ERR, "write of FAT32 #%d failed",
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

	/*
	 * Update info sector, if present
	 */
	if (info) {
		/*
		 * Copy over our rotor for allocation attempt starting point
		 */
		info->last = nxt_clust;

		/*
		 * Write the info sector
		 */
		lseek(blkdev, infobase * SECSZ, 0);
		if (write(blkdev, info, infosize) != infosize) {
			syslog(LOG_ERR, "write of info sector failed");
			exit(1);
		}
	}
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
