/*
 * dir.c
 *	Routines for messing about with directories
 */
#include <sys/fs.h>
#include "dos.h"
#include <ctype.h>
#include <std.h>
#include <hash.h>
#include <time.h>
#include <sys/assert.h>
#include <syslog.h>

extern struct boot bootb;
extern int blkdev;
extern uint dirents;

struct node *rootdir;		/* Root dir always here */
static uint rootsize;		/* # bytes in root dir data */
static int root_dirty = 0;	/*  ...needs to be flushed to disk */
static struct directory		/* Root dir contents */
	*rootdirents;
static uint cldirs;		/* # dir entries in a cluster */
struct hash *dirhash;		/* Maps dirs to nodes */

/*
 * dir_init()
 *	Set up various tables, read in root directory
 */
void
dir_init(void)
{
	struct node *n;

	/*
	 * Main hash for directories
	 */
	dirhash = hash_alloc(64);

	/*
	 * Get the root dir, fill it in
	 */
	n = rootdir = malloc(sizeof(struct node));
	if (n == 0) {
		perror("dir_init: root");
		exit(1);
	}
	n->n_type = T_DIR;
	n->n_clust = 0;		/* Flag root dir */
	n->n_mode = 0;
	n->n_refs = 1;		/* Always ref'ed */
	n->n_files = hash_alloc(16);

	/*
	 * The root dir is special in many ways.  Its directory
	 * entries exist in data blocks which are not addressible
	 * using the cluster values used by everyone else; in fact,
	 * I can't find any reason to assume the size will even be
	 * in multiples of clusters.  Therefore we read the entire
	 * contents into a malloc()'ed buffer, and flush the buffer
	 * back in units of sectors as needed.
	 */
	rootsize = dirents * sizeof(struct directory);
	rootdirents = malloc(rootsize);
	if (rootdirents == 0) {
		perror("dir_init: rootdirents");
		exit(1);
	}
	lseek(blkdev,
		(bootb.nrsvsect+(bootb.nfat * bootb.fatlen))*(ulong)SECSZ,
		0);
	if (read(blkdev, rootdirents, rootsize) != rootsize) {
		perror("dir_init: read root");
		exit(1);
	}

	/*
	 * Precalculate
	 */
	cldirs = clsize/sizeof(struct directory);
}

/*
 * map_filename()
 *	Convert between a UNIX-ish filename and its DOS counterpart
 */
static
map_filename(char *file, char *f1, char *f2)
{
	char *p, c;
	int len;

	p = f1;
	len = 0;
	strcpy(f1, "        ");
	while (c = *file++) {
		c &= 0x7F;
		if (c == '.') {
			break;
		}
		if (len++ < 8) {
			*p++ = toupper(c);
		}
	}
	strcpy(f2, "   ");
	if (c == '.') {		/* Extension found */
		p = f2;
		len = 0;
		while (c = *file++) {
			c &= 0x7F;
			if (c == '.') {
				return(1);
			}
			if (len++ < 3) {
				*p++ = toupper(c);
			}
		}
	}
	return(0);
}

/*
 * search_dir()
 *	Search an array of "struct directory"s for a filename
 */
static
search_dir(struct directory *d, uint ndir, char *f1, char *f2)
{
	struct directory *dirend;
	int x = 0;

	dirend = d+ndir;
	for ( ; d < dirend; ++d,++x) {
		/*
		 * If we reach a "never used" entry, there are
		 * guaranteed to be no more beyond.
		 */
		if (d->name[0] == 0) {
			return(-1);
		}

		/*
		 * Compare both filename parts
		 */
		if (!bcmp(f1, d->name, 8) && !bcmp(f2, d->ext, 3)) {
			return(x);
		}
	}
	return(-1);
}

/*
 * root_search()
 *	Search the root directory for an entry
 *
 * Return the index, or -1 if it can't be found.
 */
static
root_search(char *f1, char *f2, struct directory *dp)
{
	int x;

	x = search_dir(rootdirents, dirents, f1, f2);
	*dp = rootdirents[x];
	return(x);
}

/*
 * dir_search()
 *	Like root_search(), but have to walk extents
 */
static
dir_search(struct node *n, char *f1, char *f2, struct directory *dp)
{
	void *handle;
	struct clust *c;
	int cluster, x;
	struct directory *d;

	/*
	 * Walk each cluster, searching for this name
	 */
	c = n->n_clust;
	for (cluster = 0; cluster < c->c_nclust; ++cluster) {
		/*
		 * Get next block
		 */
		handle = bget(c->c_clust[cluster]);
		if (!handle) {
			/* I/O error? */
			return(-1);
		}
		d = bdata(handle);

		/*
		 * Search directory block
		 */
		x = search_dir(d, cldirs, f1, f2);
		if (x >= 0) {
			*dp = d[x];
		}
		bfree(handle);
		if (x >= 0) {
			return(x + (cldirs * cluster));
		}
	}
	return(-1);
}

/*
 * dir_look()
 *	Given dir node and filename, look up entry
 */
struct node *
dir_look(struct node *n, char *file)
{
	char f1[9], f2[4];
	struct directory d;
	int x;
	struct node *n2;

	/*
	 * Get a DOS-ish version
	 */
	if (map_filename(file, f1, f2)) {
		return(0);
	}

	/*
	 * Search dir; special case for root
	 */
	if (n == rootdir) {
		x = root_search(f1, f2, &d);
	} else {
		x = dir_search(n, f1, f2, &d);
	}

	/*
	 * If not present, return failure
	 */
	if (x < 0) {
		return(0);
	}

	/*
	 * Have a dir entry, now try for the node itself.  File nodes
	 * are stored under the parent directory node keyed by directory
	 * offset.  All directories are hashed under a single table keyed
	 * by starting cluster number.
	 */
	if (d.attr & DA_DIR) {
		n2 = hash_lookup(dirhash, d.start);
	} else {
		n2 = hash_lookup(n->n_files, x);
	}

	/*
	 * If we found it, add a reference and we're done
	 */
	if (n2) {
		ref_node(n2);
		return(n2);
	}

	/*
	 * Need to create a new node.  Toss it together.
	 */
	n2 = malloc(sizeof(struct node));
	if (n2 == 0) {
		return(0);
	}
	n2->n_type = ((d.attr & DA_DIR) ? T_DIR : T_FILE);
	if ((n2->n_type == T_DIR) && !d.start) {
		syslog(LOG_ERR, "null directory for '%s'\n", file);
		free(n2);
		return(0);
	}
	n2->n_clust = alloc_clust(d.start);
	if (n2->n_clust == 0) {
		free(n2);
		return(0);
	}
	n2->n_mode = ACC_READ |
		((d.attr & DA_READONLY) ? 0 : ACC_WRITE);
	n2->n_refs = 1;
	n2->n_flags = 0;	/* Not dirty to start */
	n2->n_dir = n; ref_node(n);
	n2->n_slot = x;
	if (n2->n_type == T_DIR) {
		/*
		 * Get the file hash for directories
		 */
		n2->n_files = hash_alloc(8);
		if (n2->n_files == 0) {
			free_clust(n2->n_clust);
			free(n2);
			return(0);
		}
		if (hash_insert(dirhash, d.start, n2)) {
			hash_dealloc(n2->n_files);
			free_clust(n2->n_clust);
			free(n2);
			return(0);
		}
	} else {
		/*
		 * Fill in directory slot information for files
		 */
		n2->n_len = d.size;
		if (hash_insert(n->n_files, x, n2)) {
			free_clust(n2->n_clust);
			free(n2);
			return(0);
		}
	}
	return(n2);
}

/*
 * dir_empty()
 *	Tell if given directory is empty
 */
dir_empty(struct node *n)
{
	struct clust *c;
	int cluster, x;
	void *handle;
	struct directory *d;

	ASSERT_DEBUG(n->n_type == T_DIR, "dir_empty: !dir");

	/*
	 * Trust us, root is *never* empty, and you can't remove
	 * it anyway!
	 */
	if (n == rootdir) {
		return(0);
	}

	c = n->n_clust;
	for (cluster = 0; cluster < c->c_nclust; ++cluster) {
		/*
		 * Get next block
		 */
		handle = bget(c->c_clust[cluster]);
		if (!handle) {
			/* I/O error? */
			return(0);
		}
		d = bdata(handle);

		/*
		 * Search directory block
		 */
		for (x = 0; x < cldirs; ++x,++d) {
			uint c;

			/*
			 * Look at first character
			 */
			c = (d->name[0] & 0xFF);

			/*
			 * No more entries--we haven't seen a file,
			 * so it's empty.
			 */
			if (c == 0) {
				bfree(handle);
				return(1);
			}

			/*
			 * "." and ".." are ignored, as are deleted files
			 */
			if ((c == '.') || (c == 0xe5)) {
				continue;
			}

			/*
			 * Oops.  Found a file.  Not empty.
			 */
			bfree(handle);
			return(1);
		}
		bfree(handle);
	}
	return(1);
}

/*
 * get_dirent()
 *	Return pointer to struct directory, also pass back handle
 *
 * The handle is NULL for root directory; otherwise it is a handle
 * to the block containing this directory entry.
 */
static struct directory *
get_dirent(struct node *n, uint idx, void **handlep)
{
	struct directory *d;
	void *handle;
	uint clnum;
	struct clust *c;

	/*
	 * Root is pretty easy
	 */
	if (n == rootdir) {
		if (idx >= dirents) {
			return(0);
		}
		*handlep = 0;
		return(rootdirents + idx);
	}

	/*
	 * Others require that we figure out which cluster
	 * is needed and bget() it.
	 */
	c = n->n_clust;
	clnum = idx / cldirs;
	if (clnum >= c->c_nclust) {
		return(0);
	}
	*handlep = handle = bget(c->c_clust[clnum]);
	d = bdata(handle);
	return (d + (idx % cldirs));
}

/*
 * dir_remove()
 *	Remove given node from its directory
 *
 * The node's storage is assumed to have already been freed
 */
void
dir_remove(struct node *n)
{
	void *handle;
	struct directory *d;

	ASSERT_DEBUG(n != rootdir, "dir_remove: root");

	/*
	 * Get the directory entry and its handle
	 */
	d = get_dirent(n->n_dir, n->n_slot, &handle);

	/*
	 * Flag name as being deleted, mark the directory block
	 * dirty.  Special case (of course) for root.
	 */
	d->name[0] = 0xe5;
	if (handle) {
		bdirty(handle);
		bfree(handle);
	} else {
		root_dirty = 1;
	}

	/*
	 * Unhash node
	 */
	if (n->n_type == T_DIR) {
		hash_delete(dirhash, n->n_clust->c_clust[0]);
	} else {
		hash_delete(n->n_dir->n_files, n->n_slot);
	}
}

/*
 * dir_findslot()
 *	Find an open slot in the directory
 *
 * Returns a struct directory pointer on success, else 0.  The handle
 * is either 0 for root, or a bget() handle for the cluster containing
 * the directory entry.
 */
static struct directory *
dir_findslot(struct node *n, void **handlep)
{
	struct directory *d, *endd;
	uint x;
	struct clust *c = n->n_clust;
	void *handle;
	uint ch;

	if (n == rootdir) {
		/*
		 * Root is a single linear scan
		 */
		*handlep = 0;
		endd = rootdirents+dirents;
		for (d = rootdirents; d < endd; ++d) {
			ch = (d->name[0] & 0xFF);
			if ((ch == 0) || (ch == 0xe5)) {
				return(d);
			}
		}
		return(0);
	}

	/*
	 * Search each existing cluster
	 */
	for (x = 0; x < c->c_nclust; ++x) {
		/*
		 * Get next cluster of directory entries
		 */
		handle = bget(c->c_clust[x]);
		if (!handle) {
			return(0);
		}

		/*
		 * Scan
		 */
		d = bdata(handle);
		endd = d+cldirs;
		for ( ; d < endd; ++d) {
			ch = (d->name[0] & 0xFF);
			if ((ch == 0) || (ch == 0xe5)) {
				*handlep = handle;
				return(d);
			}
		}
		bfree(handle);
	}

	/*
	 * No free entries.  Try to extend.
	 */
	if (clust_setlen(c, (c->c_nclust+1)*clsize)) {
		/*
		 * No more blocks
		 */
		return(0);
	}

	/*
	 * Zero block, return pointer to base
	 */
	*handlep = handle = bget(c->c_clust[c->c_nclust-1]);
	d = bdata(handle);
	bzero(d, clsize);
	bdirty(handle);
	return(d);
}

/*
 * dir_newfile()
 *	Create a directory entry
 *
 * For directories, we also allocate the first cluster and put its
 * "." and ".." entries in place.
 */
struct node *
dir_newfile(struct file *f, char *file, int isdir)
{
	struct directory *d, *dir;
	struct clust *c;
	void *handle, *dirhandle;
	char f1[9], f2[4], ochar0;
	int error = 0;
	struct node *n = f->f_node;

	/*
	 * Get a slot
	 */
	dir = dir_findslot(n, &dirhandle);
	if (dir == 0) {
		/*
		 * Sorry...
		 */
		return(0);
	}

	/*
	 * Get a DOS version of the name, put in place
	 */
	if (map_filename(file, f1, f2)) {
		error = 1;
		goto out;
	}
	ochar0 = dir->name[0];	/* In case we have to undo this */
	bcopy(f1, dir->name, sizeof(dir->name));
	bcopy(f2, dir->ext, sizeof(dir->ext));
	dir->attr = 0;
	dir->start = 0;
	dir->size = 0;

	/*
	 * Have to allocate an empty directory (., ..) for directories
	 */
	if (isdir) {
		/*
		 * Get a single cluster
		 */
		c = alloc_clust(0);
		if (c == 0) {
			error = 1;
			goto out;
		}
		if (clust_setlen(c, 1 * clsize)) {
			free(c);
			error = 1;
			goto out;
		}

		/*
		 * Put "." first, ".." second, and zero out the rest
		 * XXX time/date stuff
		 */
		handle = bget(c->c_clust[0]);
		d = bdata(handle);
		bzero(d, clsize);
		bcopy(".       ", d->name, sizeof(d->name));
		bcopy("   ", d->ext, sizeof(d->ext));
		dir->attr = d->attr = DA_DIR|DA_ARCHIVE;
		dir->start = d->start = c->c_clust[0];
		++d;
		bcopy("..      ", d->name, sizeof(d->name));
		bcopy("   ", d->ext, sizeof(d->ext));
		d->attr = DA_DIR|DA_ARCHIVE;
		if (n == rootdir) {
			d->start = 0;
		} else {
			d->start = n->n_clust->c_clust[0];
		}
		bdirty(handle);
		bfree(handle);
	}
out:
	if (error) {
		dir->name[0] = ochar0;
	} else {
		if (dirhandle) {
			bdirty(dirhandle);
		} else {
			root_dirty = 1;
		}
	}
	if (dirhandle) {
		bfree(dirhandle);
	}
	if (error) {
		return(0);
	}

	/*
	 * Kind of cheating, but saves quite a bit of duplication
	 */
	n = dir_look(n, file);
	if (n) {
		n->n_flags |= N_DIRTY;
	}
	return(n);
}

/*
 * dir_copy()
 *	Get a snapshot of a directory entry
 */
dir_copy(struct node *n, uint pos, struct directory *dp)
{
	struct directory *d;
	void *handle;

	/*
	 * Get pointer to slot
	 */
	d = get_dirent(n, pos, &handle);

	/*
	 * If we got one, copy out its contents
	 */
	if (d) {
		*dp = *d;
	}

	/*
	 * If we hold a buffer, free it now that we have our
	 * copy.
	 */
	if (d && handle) {
		bfree(handle);
	}

	/*
	 * Return error indication
	 */
	return(d == 0);
}

/*
 * timestamp()
 *	Mark current date/time onto given dir entry
 */
static void
timestamp(struct directory *d)
{
	time_t t;
	struct tm *tm;

	/*
	 * Get date/time from system, convert to struct tm so we
	 * can pick apart day, month, etc.  Just zero date/time
	 * if we can't get it.
	 */
	t = 0;
	(void)time(&t);
	tm = localtime(&t);
	if (!t || !tm || (tm->tm_year < 80)) {
		d->date = 0;
		d->time = 0;
		return;
	}

	/*
	 * Pack hours, minutes, seconds into "time" field
	 */
	d->time = (tm->tm_hour << 11) | (tm->tm_min << 5) |
		(tm->tm_sec >> 1);
	d->date = ((tm->tm_year - 80) << 9) |
		((tm->tm_mon + 1) << 5) | tm->tm_mday;
}

/*
 * dir_setlen()
 *	Move the length attribute from the node into the dir entry
 *
 * Used on last close to update the directory entry.  It's not worth
 * scribbling the directory block in the interim.
 */
void
dir_setlen(struct node *n)
{
	struct directory *d;
	void *handle;
	struct clust *c = n->n_clust;

	/*
	 * Non-issue unless it's a file
	 */
	if (n->n_type != T_FILE) {
		return;
	}

	/*
	 * Get pointer to slot
	 */
	d = get_dirent(n->n_dir, n->n_slot, &handle);
	ASSERT(d, "dir_setlen: lost dir entry");

	/*
	 * Update it, mark the block dirty, and return
	 */
	d->size = n->n_len;
	timestamp(d);
	if (c->c_nclust) {
		ASSERT_DEBUG(d->size, "dir_setlen: !len clust");
		d->start = c->c_clust[0];
	} else {
		ASSERT_DEBUG(d->size == 0, "dir_setlen: len !clust");
		d->start = 0;
	}
	if (handle) {
		bdirty(handle);
		bfree(handle);
	} else {
		root_dirty = 1;
	}
}

/*
 * root_sync()
 *	If root directory is modified, flush to disk
 */
void
root_sync(void)
{
	int x;

	if (!root_dirty) {
		return;
	}
	lseek(blkdev,
		(bootb.nrsvsect+(bootb.nfat * bootb.fatlen))*(ulong)SECSZ,
		0);
	x = write(blkdev, rootdirents, rootsize);
	if (x != rootsize) {
		syslog(LOG_ERR,
			"root_sync: write failed, err '%s', value 0x%x\n",
			strerror(), x);
		exit(1);
	}
	root_dirty = 0;
}
