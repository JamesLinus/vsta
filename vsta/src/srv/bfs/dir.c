/*
 * dir.c
 *	Routines for managing directory entries
 *
 * We want to access the actual directory entries through the
 * block cache so that the usual sync and cache behavior is
 * followed.  For simplicity, we don't want a single directory
 * entry to cross a block boundary.  These routines follow a
 * table which maps each directory slot into a block and offset.
 */
#include <bfs/bfs.h>
#include <sys/assert.h>
extern void *realloc(), *bget(), *bdata();

static struct dirmap	/* The map */
	*dirmap;
static int ndir;	/*  ...# entries in it */

/*
 * dir_lookup()
 *	Scan directory entries for given name
 *
 * Return value is 1 for error, 0 for success.  The block has a
 * reference, which the caller will have to release after using
 * the directory entry appropriately.
 */
dir_lookup(char *name, void **handlep, int *off)
{
	int x;
	struct dirent *d;
	char *p;
	struct dirmap *dm;
	void *handle;

	for (x = 0, dm = dirmap; x < ndir; ++x, ++dm) {
		handle = bget(dm->d_blkno);
		if (!handle)
			continue;
		p = bdata(handle);
		d = (struct dirent *)(p + dm->d_off);
		if (!strcmp(name, d->d_name)) {
			*handlep = handle;
			*off = dm->d_off;
			return(0);
		}
		bfree(handle);
	}
	return(1);
}

/*
 * dir_init()
 *	Initialize directory stuff
 */
void
dir_init(void)
{
	uint x;
	struct dirmap *d;

	/*
	 * Generate block #/block offset map for each directory slot
	 */
	ndir = 0;
	dirmap = 0;
	x = sizeof(struct super);
	x -= sizeof(struct dirent);	/* Pad dirent in superblock */
	while (x < NDIRBLOCKS*BLOCKSIZE) {
		int curblk, endblk;

		curblk = (x / BLOCKSIZE);
		endblk = ((x + sizeof(struct dirent) - 1) / BLOCKSIZE);

		/*
		 * This one spans a block--forget it
		 */
		if (curblk != endblk) {
			x = endblk*BLOCKSIZE;
			continue;
		}

		/*
		 * Add an entry to the map
		 */
		ndir += 1;
		dirmap = realloc(dirmap, ndir*sizeof(struct dirmap));
		if (dirmap == 0) {
			perror("dir_init");
			exit(1);
		}
		d = &dirmap[ndir-1];
		d->d_blkno = curblk;
		d->d_off = (x % BLOCKSIZE);
		x += sizeof(struct dirent);
	}
}

/*
 * dir_map()
 *	Given inode #, treat as index and calculate block/offset
 */
void
dir_map(int inum, struct dirmap *dm)
{
	ASSERT((inum >= 0) && (inum < ndir), "dir_map: bad inum");
	*dm = dirmap[inum];
}

/*
 * dir_copy()
 *	Given inode #, make a snapshot of the current dir entry
 *
 * Returns 0 on success, 1 on bad inode #, 2 for all other errors.
 */
dir_copy(int inum, struct dirent *d)
{
	struct dirmap *dm;
	void *handle;

	if ((inum < 0) || (inum >= ndir))
		return(1);
	dm = &dirmap[inum];
	handle = bget(dm->d_blkno);
	if (!handle)
		return(2);
	*d = *(struct dirent *)((char *)bdata(handle) + dm->d_off);
	bfree(handle);
	return(0);
}

/*
 * dir_newfile()
 *	Create a new entry in the directory
 *
 * Return 1 on failure, 0 on success.
 */
dir_newfile(char *name, void **handlep, int *off)
{
	struct dirent *d;
	struct dirmap *dm;
	int x;
	void *handle;
	char *p;

	for (x = 0, dm = dirmap; x < ndir; ++x, ++dm) {
		handle = bget(dm->d_blkno);
		if (!handle)
			continue;
		p = bdata(handle);
		d = (struct dirent *)(p + dm->d_off);
		if (d->d_name[0] == '\0') {
			strcpy(d->d_name, name);
			*handlep = handle;
			*off = dm->d_off;
			return(0);
		}
		bfree(handle);
	}
	return(1);
}
