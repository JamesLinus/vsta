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
#include <fcntl.h>
#include <abc.h>
#include <sys/assert.h>
#include <syslog.h>

struct node *rootdir,		/* Flag special root handling: FAT12/16 */
	*procroot;		/* Starting CWD for new attachees */
static uint rootsize;		/* # bytes in root dir data */
static int root_dirty = 0;	/*  ...needs to be flushed to disk */
static struct directory		/* Root dir contents */
	*rootdirents;
static uint cldirs,		/* # dir entries in a cluster */
	clshift;		/*  ...# bits to shift this value */
struct hash *dirhash;		/* Maps dirs to nodes */
static struct hash
	*rename_pending;	/* Tabulate pending renames */
claddr_t root_cluster;		/* Root cluster #, if FAT32 */

static int mystrcasecmp(const char *s1, const char *s2);

/*
 * ddirty()
 *	Mark a directory handle dirty
 *
 * Handles case of null handle, which means root
 */
static void
ddirty(void *handle)
{
	if (handle) {
		dirty_buf(handle, 0);
	} else {
		root_dirty = 1;
	}
}

/*
 * dfree()
 *	Free up a directory handle
 */
static void
dfree(void *handle)
{
	if (handle) {
		unlock_buf(handle);
	}
}

/*
 * dir_init()
 *	Set up various tables, read in root directory
 */
void
dir_init(void)
{
	struct node *n;
	uint x;

	/*
	 * Main hash for directories
	 */
	dirhash = hash_alloc(64);

	/*
	 * Get the root dir, fill it in
	 */
	n = procroot = malloc(sizeof(struct node));
	if (n == 0) {
		syslog(LOG_ERR, "dir_init: root");
		exit(1);
	}
	bzero(n, sizeof(struct node));
	n->n_type = T_DIR;
	n->n_refs = 1;			/* Always ref'ed */
	n->n_files = hash_alloc(16);

	if (!root_cluster) {
		/*
		 * For FAT12 and FAT16:
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
			syslog(LOG_ERR, "dir_init: rootdirents");
			exit(1);
		}
		lseek(blkdev,
			(bootb.nrsvsect+(bootb.nfat * bootb.fatlen))*(ulong)SECSZ,
			0);
		if (read(blkdev, rootdirents, rootsize) != rootsize) {
			syslog(LOG_ERR, "dir_init: unable to read root");
			exit(1);
		}

		/*
		 * This root directory pointer keys all the special cases
		 * for dealing with a non-cluster allocated root.
		 */
		n->n_clust = 0;
		rootdir = n;
	} else {
		/*
		 * The root directory is a normal cluster-based entity
		 * in FAT32.  We'll always hold the root node in core,
		 * but we need a pseudo directory entry in order to set
		 * up the cluster chain.
		 */
		struct directory d;

		SETSTART(&d, root_cluster);
		n->n_clust = alloc_clust(&d);
	}

	/*
	 * Precalculate
	 */
	cldirs = clsize/sizeof(struct directory);
	for (x = cldirs-1; x; x >>= 1) {
		clshift += 1;
	}
}

/*
 * map_filename()
 *	Convert between a UNIX-ish filename and its DOS counterpart
 *
 * We map an all-uppercase 8.3 name to lower case.  Others we leave
 * alone, and use VFAT support to handle it.
 *
 * Returns 0 on valid short filename, 1 on valid long filename,
 * 2 for an invalid filename.
 */
static int
map_filename(char *file, char *f1, char *f2)
{
	char *p, c;
	int len;
	static const char illegal[] = ";+=[]',\"*\\<>/?:|";

	/*
	 * Scan filename for illegal characters.
	 */
	for (p = file; (c = (*p & 0x7F)); ++p) {
		if ((c < ' ') || strchr(illegal, c)) {
			return(2);
		}
	}

	/*
	 * Assemble 8.3 filename
	 */
	p = f1;
	len = 0;
	strcpy(f1, "        ");

	/*
	 * Map .<file> to _<file>
	 */
	if (*file == '.') {
		len += 1;
		file += 1;
		*p++ = '_';
	}

	/*
	 * Copy <file> part of <file>.<ext>
	 */
	while ((c = *file++)) {
		c &= 0x7F;
		if (c == '.') {
			break;
		}
		if (isupper(c)) {
			return(1);
		}
		if (len++ < 8) {
			*p++ = toupper(c);
		} else {
			return(1);
		}
	}

	/*
	 * Now <ext> part, if any
	 */
	strcpy(f2, "   ");
	if (c == '.') {		/* Extension found */
		p = f2;
		len = 0;
		while ((c = *file++)) {
			c &= 0x7F;
			if (isupper(c)) {
				return(1);
			}
			if (len++ < 3) {
				*p++ = toupper(c);
			} else {
				return(1);
			}
		}
	}
	return(0);
}

/*
 * my_bcmp()
 *	Compare, binary style
 */
static int
my_bcmp(const void *s1, const void *s2, unsigned int n)
{
	const char *p = s1, *q = s2;

	while (n-- > 0) {
		if (*p++ != *q++) {
			return(1);
		}
	}
	return(0);
}

/*
 * This is the state kept for our callback to iterate VSE entries
 */
struct dirstate {
	struct directory *st_dir1;	/* Directory to search */
	struct directory *st_dir2;	/*  ...overflow */
	int st_pos;			/* Current position */
	int st_ndir;			/* # entries available */
};

/*
 * next_dir()
 *	Callback function to provide next directory entry in VSE assembly
 */
static int
next_dir(struct directory *dp, void *statep)
{
	struct dirstate *state = statep;

	/*
	 * We've walked off the end of the first directory buffer;
	 * if there's a second, use those.  Otherwise return failure.
	 */
	if (state->st_pos >= state->st_ndir) {
		if (state->st_dir2 == NULL) {
			return(1);
		}
		bcopy(state->st_dir2 + (state->st_pos - state->st_ndir),
			dp, sizeof(struct directory));
	} else {
		/*
		 * Provide next dir entry
		 */
		bcopy(state->st_dir1 + state->st_pos,
			dp, sizeof(struct directory));
	}

	/*
	 * Advance state and return success
	 */
	state->st_pos += 1;
	return(0);
}

/*
 * mystrcasecmp()
 *	Case independent string compare
 *
 * Implemented locally due to its absence from the boot server
 * support library.
 */
static int
mystrcasecmp(const char *s1, const char *s2)
{
	char c1, c2;

	for (;;) {
		/*
		 * Get next pair of chars
		 */
		c1 = *s1++;
		c2 = *s2++;

		/*
		 * If match, return successful match on null termination
		 */
		if (c1 == c2) {
			if (c1 == '\0') {
				return(0);
			}
			continue;
		}

		/*
		 * Map both from lower to upper case
		 */
		if ((c1 >= 'a') && (c1 <= 'z')) {
			c1 = (c1 - 'a') + 'A';
		}
		if ((c2 >= 'a') && (c2 <= 'z')) {
			c2 = (c2 - 'a') + 'A';
		}

		/*
		 * If they still don't match, return a mismatch
		 */
		if (c1 != c2) {
			return(1);
		}

		/*
		 * Otherwise just continue the loop
		 */
	}
}

/*
 * search_vfat()
 *	Scan VSE entries to try and match the filename
 */
static int
search_vfat(char *name, struct directory *d, int ndir, struct directory *d2)
{
	int x;
	uint c;
	struct dirstate state;
	char buf[VSE_MAX_NAME+2];

	for (x = 0; x < ndir; ++x, ++d) {
		/*
		 * Null char in filename means no entries
		 * beyond this point; search failed.
		 */
		c = d->name[0] & 0xFF;
		if (c == 0) {
			return(-1);
		}

		/*
		 * Deleted file; ignore
		 */
		if (c == DN_DEL) {
			continue;
		}

		/*
		 * If not a VSE, ignore
		 */
		if (d->attr != DA_VFAT) {
			continue;
		}

		/*
		 * Assemble VSE's to build up filename
		 */
		state.st_dir1 = d+1;
		state.st_dir2 = d2;
		state.st_pos = 0;
		state.st_ndir = (ndir-x)-1;
		if (assemble_vfat_name(buf, d, next_dir, &state)) {
			continue;
		}

		/*
		 * Get position after building from VSE's
		 */
		x += state.st_pos;
		d += state.st_pos;

		/*
		 * Return if matched
		 */
		if (!mystrcasecmp(name, buf)) {
			return(x);
		}
	}
	return(-1);
}

/*
 * search_dir()
 *	Search an array of "struct directory"s for a filename
 *
 * If "f2" is NULL, this is a search of long-filename VFAT entries.
 * Otherwise it's a search of the 8.3 upper case names.
 *
 * Return value is an offset into the directory to the entry.  If
 * "d2" is non-NULL, this offset may be beyond the end of "d",
 * indicating an offset in "d2" instead.  On failure, -1 is returned.
 */
static int
search_dir(int ndir, struct directory *d, struct directory *d2,
	char *f1, char *f2)
{
	/*
	 * If we have f2 as well as f1 (filename f1.f2), we
	 * search for traditional entries, otherwise long
	 * filename ones
	 */
	if (f2) {
		int x;
		struct directory *dirend = d + ndir;

		for (x = 0; d < dirend; ++d,++x) {
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
			if (!my_bcmp(f1, d->name, 8) &&
					!my_bcmp(f2, d->ext, 3)) {
				return(x);
			}
		}
	} else {
		int off;

		/*
		 * Search VSE entries.  The offset to the "real"
		 * entry must always be beyond the starting point,
		 * since at a minimum there must be the VSE,
		 * followed by the short name "real" entry.
		 */
		off = search_vfat(f1, d, ndir, d2);
		if (off > 0) {
			return (off);
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
static int
root_search(char *f1, char *f2, struct directory *dp)
{
	int x;

	x = search_dir(dirents, rootdirents, NULL, f1, f2);
	if (x >= 0) {
		*dp = rootdirents[x];
	}
	return(x);
}

/*
 * dir_search()
 *	Like root_search(), but have to walk extents
 */
static int
dir_search(struct node *n, char *f1, char *f2, struct directory *dp)
{
	void *handle, *handle2 = NULL;
	struct clust *c;
	int x;
	uint cluster;
	struct directory *d, *d2 = NULL;

	/*
	 * Walk each cluster, searching for this name
	 */
	c = n->n_clust;
	for (cluster = 0; cluster < c->c_nclust; ++cluster) {
		/*
		 * Get next block.  If we already grabbed it
		 * on the previous pass through, just use that
		 * value.
		 */
		if (d2) {
			d = d2;
			handle = handle2;
			d2 = NULL;
		} else {
			handle = find_buf(BOFF(c->c_clust[cluster]),
				CLSIZE, ABC_FILL);
			if (!handle) {
				/* I/O error? */
				return(-1);
			}
			lock_buf(handle);
			d = index_buf(handle, 0, CLSIZE);
		}

		/*
		 * If there's a following block, get it, too.
		 * This is needed for long filename searches, where
		 * the VSE's may straddle clusters.
		 */
		if (!f2 && (cluster < c->c_nclust-1)) {
			handle2 = find_buf(BOFF(c->c_clust[cluster]),
				CLSIZE, ABC_FILL);
			if (handle2) {
				lock_buf(handle2);
				d2 = index_buf(handle2, 0, CLSIZE);
			} else {
				d2 = NULL;
			}
		} else {
			d2 = NULL;
		}

		/*
		 * Search directory block
		 */
		x = search_dir(cldirs, d, d2, f1, f2);
		if (x >= 0) {
			if (x >= cldirs) {
				*dp = d2[x-cldirs];
			} else {
				*dp = d[x];
			}
		}
		unlock_buf(handle);
		if (x >= 0) {
			/*
			 * If we have a lock on the following block
			 * (and we're not going to use it because the
			 * loop has terminated), release it now.
			 */
			if (d2) {
				unlock_buf(handle2);
			}

			/*
			 * This returns the correct value even when
			 * x > cldirs (i.e., the entry was found
			 * in handle2's cluster).  x has "+cldirs"
			 * implicit in it, and "cluster" is one
			 * short.
			 */
			return(x + (cldirs * cluster));
		}
	}
	ASSERT_DEBUG(d2 == NULL, "dir_search: d2 != NULL");


	return(-1);
}

/*
 * map_type()
 *	Convert a DOS attribute into a file type
 */
static int
map_type(int dos_attr)
{
	if (dos_attr & DA_DIR)
		return(T_DIR);
	if (dos_attr & DA_HIDDEN)
		return(T_SYM);
	return(T_FILE);
}

/*
 * get_inum()
 *	Calculate inode value from containing dir and dir entry offset
 *
 * While not apparent, generating an inode number is both more important
 * and harder than it appears.  Because inode numbers are used by both
 * executable page caching as well as utilities like du, they need to
 * be unique, even after file close.
 *
 * So you can't use, say, the "struct node" address as it.  After the file
 * closes, the node gets free()'ed, and will pretty likely be reused for
 * a new open file.  But if it was, say, "du", then this would look like
 * the same file with a different name (hard links).  And thus "du" would
 * ignore the file, and report strangely small values.  This happened.
 *
 * Another "easy" fix is to use the storage address of the first block
 * of a file.  The problem is with zero-length files, which need an inode
 * number before they have any contents.  In UFS, the inode block is
 * allocated whether there's file contents or not, but this is not the
 * case in DOS.  So we're left with the directory entry itself, which
 * is what we use to create an inode value with the needed properties.
 */
static uint
get_inum(struct node *dir, int idx)
{
	claddr_t a;

	/*
	 * Root isn't a "real" cluster, so just use 0 base on entry offset
	 */
	if (dir == rootdir) {
		return (idx);
	}

	/*
	 * Get the cluster address of the cluster holding this
	 * directory entry.
	 */
	a = get_clust(dir->n_clust, idx / cldirs);

	/*
	 * Offset it by the entry index within that cluster
	 */
	return ((a << clshift) | (idx % cldirs));
}

/*
 * dir_look()
 *	Given dir node and filename, look up entry
 */
struct node *
dir_look(struct node *n, char *file)
{
	char *fname, *fname2, f1[9], f2[4];
	struct directory d;
	int x;
	struct node *n2;

	/*
	 * Get a DOS-ish version
	 */
	switch (map_filename(file, f1, f2)) {
	case 0:
		fname = f1;
		fname2 = f2;
		break;
	case 1:
		fname = file;
		fname2 = NULL;
		break;
	default:
		return(NULL);
	}

	/*
	 * Search dir; special case for root
	 */
	if (n == rootdir) {
		x = root_search(fname, fname2, &d);
	} else {
		x = dir_search(n, fname, fname2, &d);
	}

	/*
	 * If not present, return failure
	 */
	if (x < 0) {
		return(NULL);
	}

	/*
	 * Have a dir entry, now try for the node itself.  File nodes
	 * are stored under the parent directory node keyed by directory
	 * offset.  All directories are hashed under a single table keyed
	 * by starting cluster number.
	 */
	if (d.attr & DA_DIR) {
		n2 = hash_lookup(dirhash, START(&d));
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
	n2->n_type = map_type(d.attr);
	if ((n2->n_type == T_DIR) && !START(&d)) {
		syslog(LOG_ERR, "null directory for '%s'", file);
		free(n2);
		return(0);
	}
	n2->n_clust = alloc_clust(&d);
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
	n2->n_inum = get_inum(n, x);

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
		if (hash_insert(dirhash, START(&d), n2)) {
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
int
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
		handle = find_buf(BOFF(c->c_clust[cluster]),
			CLSIZE, ABC_FILL);
		if (!handle) {
			/* I/O error? */
			return(0);
		}
		lock_buf(handle);
		d = index_buf(handle, 0, CLSIZE);

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
				unlock_buf(handle);
				return(1);
			}

			/*
			 * "." and ".." are ignored, as are deleted files
			 */
			if ((c == '.') || (c == DN_DEL)) {
				continue;
			}

			/*
			 * Oops.  Found a file.  Not empty.
			 */
			unlock_buf(handle);
			return(0);
		}
		unlock_buf(handle);
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
	 * is needed and getit.
	 */
	c = n->n_clust;
	clnum = idx / cldirs;
	if (clnum >= c->c_nclust) {
		return(0);
	}
	*handlep = handle =
		find_buf(BOFF(c->c_clust[clnum]), CLSIZE, ABC_FILL);
	lock_buf(handle);
	d = index_buf(handle, 0, CLSIZE);
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
	d->name[0] = DN_DEL;
	ddirty(handle);
	dfree(handle);

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
 * is either 0 for root, or a get handle for the cluster containing
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
			if ((ch == 0) || (ch == DN_DEL)) {
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
		handle = find_buf(BOFF(c->c_clust[x]), CLSIZE, ABC_FILL);
		if (!handle) {
			return(0);
		}
		lock_buf(handle);

		/*
		 * Scan
		 */
		d = index_buf(handle, 0, CLSIZE);
		endd = d+cldirs;
		for ( ; d < endd; ++d) {
			ch = (d->name[0] & 0xFF);
			if ((ch == 0) || (ch == DN_DEL)) {
				*handlep = handle;
				return(d);
			}
		}
		unlock_buf(handle);
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
	*handlep = handle = find_buf(BOFF(c->c_clust[c->c_nclust-1]),
		CLSIZE, ABC_FILL);
	lock_buf(handle);
	d = index_buf(handle, 0, CLSIZE);
	bzero(d, clsize);
	ddirty(handle);
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
	ochar0 = dir->name[0];	/* In case we have to undo this */

	/*
	 * Get a DOS version of the name, put in place
	 */
	if (map_filename(file, f1, f2)) {
		error = 1;
		goto out;
	}
	bcopy(f1, dir->name, sizeof(dir->name));
	bcopy(f2, dir->ext, sizeof(dir->ext));
	dir->attr = 0;
	SETSTART(dir, 0);
	dir->size = 0;
	timestamp(dir, 0);

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
		 */
		handle = find_buf(BOFF(c->c_clust[0]), CLSIZE, ABC_FILL);
		lock_buf(handle);
		d = index_buf(handle, 0, CLSIZE);
		bzero(d, clsize);
		bcopy(".       ", d->name, sizeof(d->name));
		bcopy("   ", d->ext, sizeof(d->ext));
		dir->attr = d->attr = DA_DIR|DA_ARCHIVE;
		SETSTART(dir, c->c_clust[0]);
		SETSTART(d, c->c_clust[0]);
		timestamp(dir, 0);
		++d;
		bcopy("..      ", d->name, sizeof(d->name));
		bcopy("   ", d->ext, sizeof(d->ext));
		d->attr = DA_DIR|DA_ARCHIVE;
		timestamp(dir, 0);
		if (n == rootdir) {
			SETSTART(d, 0);
		} else {
			SETSTART(d, n->n_clust->c_clust[0]);
		}
		ddirty(handle);
		dfree(handle);
	}
out:
	if (error) {
		dir->name[0] = ochar0;
	} else {
		ddirty(dirhandle);
	}
	if (dirhandle) {
		unlock_buf(dirhandle);
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
int
dir_copy(struct node *n, uint pos, struct directory *dp)
{
	struct directory *d;
	void *handle;

	/*
	 * Get pointer to slot
	 */
	d = get_dirent(n, pos, &handle);

	/*
	 * If we got one, copy out its contents.  Free real buffer
	 * once we have the copy.
	 */
	if (d) {
		*dp = *d;
		dfree(handle);
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
void
timestamp(struct directory *d, time_t t)
{
	struct tm *tm;

	if (t == 0) {
		/*
		 * Get date/time from system, convert to struct tm so we
		 * can pick apart day, month, etc.  Just zero date/time
		 * if we can't get it.
		 */
		t = 0;
		(void)time(&t);
	}
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
 * dir_timestamp()
 *	Set timestamp on a file
 *
 * Just gets dir entry field and uses timestamp()
 */
void
dir_timestamp(struct node *n, time_t t)
{
	struct directory *d;
	void *handle;

	/*
	 * Access dir entry
	 */
	d = get_dirent(n->n_dir, n->n_slot, &handle);
	ASSERT_DEBUG(d, "dir_timestamp: lost handle");

	/*
	 * Stamp it with the requested time
	 */
	timestamp(d, t);

	/*
	 * Mark it modified
	 */
	ddirty(handle);
	dfree(handle);
	n->n_flags |= N_DIRTY;
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
	 * Update it, mark the block dirty, log change time, and return
	 */
	if (d->size != n->n_len) {
		d->size = n->n_len;
		if (c->c_nclust) {
			ASSERT_DEBUG(d->size, "dir_setlen: !len clust");
			SETSTART(d, c->c_clust[0]);
		} else {
			ASSERT_DEBUG(d->size == 0, "dir_setlen: len !clust");
			SETSTART(d, 0);
		}
		timestamp(d, 0);
		ddirty(handle);
	}
	dfree(handle);
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
			"root_sync: write failed, err '%s', value 0x%x",
			strerror(), x);
		exit(1);
	}
	root_dirty = 0;
}

/*
 * tree_contains()
 *	Tell if one dir contains another node within its directory tree
 */
static int
tree_contains(struct node *tree, struct node *n)
{
	struct node *nprev;

	/*
	 * Walk the tree upwards
	 */
	while (n != tree) {
		/*
		 * Look up next node upwards.  Done when get same value
		 * (loop at root) or NULL (shouldn't happen?)
		 */
		nprev = n;
		n = dir_look(nprev, "..");
		if ((n == 0) || (n == nprev)) {
			return(0);
		}

		/*
		 * If we run into our root, return true
		 */
		deref_node(n);
		if (n == tree) {
			return(1);
		}
	}

	/*
	 * If we find the node on the way up, this is a no-no
	 */
	return(1);
}

/*
 * fix_dotdot()
 *	Update the ".." entry in a node for a new value
 */
static void
fix_dotdot(struct node *n, struct node *ndir)
{
	struct directory *de;
	void *handle;
	struct buf *b;

	/*
	 * Get the entry, convert the handle back into a buf pointer.
	 * Index 0 for a dir is always "."; 1 is always "..".
	 */
	de = get_dirent(n, 1, &handle);
	if (de == 0) {
		return;
	}
	b = handle;

	/*
	 * Point to new parent dir
	 */
	SETSTART(de, get_clust(ndir->n_clust, 0));
	ddirty(b);
}

/*
 * check_busy()
 *	See if the given node's busy with a page cache reference
 */
static char *
check_busy(struct node *n)
{
	 if ((n->n_refs > 1) && (n->n_flags & N_FID)) {
		(void)tfork(do_unhash, inum(n));
		__msleep(10);
		n->n_flags &= ~N_FID;
		deref_node(n);
		return(EAGAIN);
	 }
	 return(0);
}

/*
 * do_rename()
 *	Given open directories and filenames, rename an entry
 *
 * Returns an error string or 0 for success.
 */
static char *
do_rename(struct file *fsrc, char *src, struct file *fdest, char *dest)
{
	struct node *ndsrc, *nddest, *nsrc, *ndest;
	struct directory *dsrc, *ddest, dtmp;
	void *handsrc, *handdest;
	char name[8], ext[3], *p;

	/*
	 * Get directory nodes, verify that they're directories
	 */
	ndsrc = fsrc->f_node;
	nddest = fdest->f_node;
	if ((ndsrc->n_type != T_DIR) || (nddest->n_type != T_DIR)) {
		return(ENOTDIR);
	}

	/*
	 * Look up each file within the directories
	 */
	nsrc = dir_look(ndsrc, src);
	if (nsrc == 0) {
		return(ESRCH);
	}

	/*
	 * If we're moving a directory, make sure we're not moving to
	 * a point within our own tree
	 */
	if ((nsrc->n_type == T_DIR) && tree_contains(nsrc, nddest)) {
		return(EINVAL);
	}

	/*
	 * Make sure we're the only ones using this file
	 */
	 if ((p = check_busy(nsrc))) {
	 	return(p);
	 }

	/*
	 * If our destination exists, truncate if it's a file, error
	 * if it's a directory.
	 */
	ndest = dir_look(nddest, dest);
	if (ndest) {
		if (ndest->n_type == T_DIR) {
			deref_node(ndest);
			deref_node(nsrc);
			return(EISDIR);
		}
		if ((p = check_busy(ndest))) {
			deref_node(nsrc);
			return(p);
		}
		clust_setlen(ndest->n_clust, 0L);
	} else {
		/* 
		 * Otherwise, create a new entry for the file
		 */
		ndest = dir_newfile(fdest, dest, 0);
		if (ndest == 0) {
			deref_node(nsrc);
			return(strerror());
		}
	}

	/*
	 * Ok... we have our source and destination.  Swap the
	 * directory attributes (but not the names).
	 */
	dsrc = get_dirent(nsrc->n_dir, nsrc->n_slot, &handsrc);
	ddest = get_dirent(ndest->n_dir, ndest->n_slot, &handdest);
	dtmp = *ddest;
	*ddest = *dsrc;
	bcopy(dtmp.name, ddest->name, 8);
	bcopy(dtmp.ext, ddest->ext, 3);
	bcopy(dsrc->name, name, 8); bcopy(dsrc->ext, ext, 3);
	*dsrc = dtmp;
	bcopy(name, dsrc->name, 8); bcopy(ext, dsrc->ext, 3);

	/*
	 * Mark dir blocks containing entries as dirty.  Root dir
	 * has a NULL handle; root_dirty gets flagged instead.
	 */
	ddirty(handsrc);
	dfree(handsrc);
	ddirty(handdest);
	dfree(handdest);

	/*
	 * File nodes are keyed under their parent dir.  Since this
	 * has changed, move them about.
	 */
	if (nsrc->n_type == T_FILE) {
		/*
		 * Unhash each node.  Re-hash it under its new parent.
		 * Node pointers and all are swapped a little later.
		 */
		hash_delete(ndsrc->n_files, nsrc->n_slot);
		hash_delete(nddest->n_files, ndest->n_slot);
		ASSERT(hash_insert(ndsrc->n_files, nsrc->n_slot, ndest) == 0,
			"do_rename: hash1 failed");
		ASSERT(hash_insert(nddest->n_files, ndest->n_slot, nsrc) == 0,
			"do_rename: hash2 failed");
	} else {
		/*
		 * Dir nodes are hashed under starting sector,
		 * which should not change.  Fix up ".." for its
		 * new parent dir.
		 */
		hash_delete(nddest->n_files, ndest->n_slot);
		ASSERT(hash_insert(ndsrc->n_files, nsrc->n_slot, ndest) == 0,
			"do_rename: hash1 failed");
		fix_dotdot(nsrc, nddest);
	}

	/*
	 * Now swap the node information on what directory
	 * entry it occupies.
	 */
	{
		struct node *ntmp;
		uint tmp;
		ulong inum;

		/*
		 * The dir entries
		 */
		ntmp = nsrc->n_dir;
		nsrc->n_dir = ndest->n_dir;
		ndest->n_dir = ntmp;

		/*
		 * Slot # (since we're now referenced by the
		 * other guy's directory/directory-slot)
		 */
		tmp = nsrc->n_slot;
		nsrc->n_slot = ndest->n_slot;
		ndest->n_slot = tmp;

		/*
		 * And inode number (derived from dir & slot)
		 */
		inum = nsrc->n_inum;
		nsrc->n_inum = ndest->n_inum;
		ndest->n_inum = inum;
	}

	/*
	 * Remove the old filename entry (which actually now resides
	 * in the source filename's directory slot).
	 */
	dir_remove(ndest);

	/*
	 * Release references to the nodes we have fiddled.  The
	 * directory nodes are help open by our client, who will
	 * close them on his own.
	 */
	deref_node(ndest);
	deref_node(nsrc);

	/*
	 * Flush dir entries
	 */
	sync();

	/*
	 * Success
	 */
	return(0);
}

/*
 * dos_rename()
 *	Rename one dir entry to another
 *
 * For move of directory, just protect against cycles and update
 * the ".." entry in the directory after the move.
 */
void
dos_rename(struct msg *m, struct file *f)
{
	struct file *f2;
	char *errstr;

	/*
	 * Sanity
	 */
	if ((m->m_arg1 == 0) || !valid_fname(m->m_buf, m->m_buflen)) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * On first use, create the rename-pending hash
	 */
	if (rename_pending == 0) {
		rename_pending = hash_alloc(16);
		if (rename_pending == 0) {
			msg_err(m->m_sender, strerror());
			return;
		}
	}

	/*
	 * Phase 1--register the source of the rename
	 */
	if (m->m_arg == 0) {
		/*
		 * Transaction ID collision?
		 */
		if (hash_lookup(rename_pending, m->m_arg1)) {
			msg_err(m->m_sender, EBUSY);
			return;
		}

		/*
		 * Insert in hash
		 */
		if (hash_insert(rename_pending, m->m_arg1, f)) {
			msg_err(m->m_sender, strerror());
			return;
		}

		/*
		 * Flag open file as being involved in this
		 * pending operation.
		 */
		f->f_rename_id = m->m_arg1;
		f->f_rename_msg = *m;
		return;
	}

	/*
	 * Otherwise it's the completion
	 */
	f2 = hash_lookup(rename_pending, m->m_arg1);
	if (f2 == 0) {
		msg_err(m->m_sender, ESRCH);
		return;
	}
	(void)hash_delete(rename_pending, m->m_arg1);

	/*
	 * Do our magic
	 */
	errstr = do_rename(f2, f2->f_rename_msg.m_buf, f, m->m_buf);
	if (errstr) {
		msg_err(m->m_sender, errstr);
		msg_err(f2->f_rename_msg.m_sender, errstr);
	} else {
		m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		msg_reply(f2->f_rename_msg.m_sender, m);
	}

	/*
	 * Clear state
	 */
	f2->f_rename_id = 0;
}

/*
 * cancel_rename()
 *	A client exit()'ed before completing a rename.  Clean up.
 */
void
cancel_rename(struct file *f)
{
	(void)hash_delete(rename_pending, f->f_rename_id);
	f->f_rename_id = 0;
}

/*
 * dir_set_type()
 *	Set type of entry
 *
 * Only switching between symlinks and files is allowed.  Returns 1
 * on error, 0 on success.
 */
int
dir_set_type(struct file *f, char *newtype)
{
	void *handle;
	struct directory *d;
	struct node *n = f->f_node;
	int ntype;

	/*
	 * Make sure desired type is recognized
	 */
	if (!strcmp(newtype, "f")) {
		ntype = T_FILE;
	} else if (!strcmp(newtype, "symlink")) {
		ntype = T_SYM;
	} else {
		return(1);
	}

	/*
	 * If type is not changing, return success on no-op
	 */
	if (n->n_type == ntype) {
		return(0);
	}

	/*
	 * Make sure node is either symlink or file
	 */
	if ((n->n_type != T_FILE) && (n->n_type != T_DIR)) {
		return(1);
	}

	/*
	 * Access dir slot
	 */
	d = get_dirent(n->n_dir, n->n_slot, &handle);
	ASSERT_DEBUG(d, "dir_set_type: lost handle");

	/*
	 * Set type of node
	 */
	n->n_type = ntype;

	/*
	 * Update DOS version of information in directory node.  Note our
	 * slimy use of HIDDEN to mean symlink.
	 */
	if (ntype == T_SYM) {
		d->attr |= DA_HIDDEN;
	} else {
		d->attr &= ~DA_HIDDEN;
	}

	/*
	 * Flag dir entry as needing a flush, and return success
	 */
	ddirty(handle);
	dfree(handle);
	n->n_flags |= N_DIRTY;
	return(0);
}

/*
 * dir_readonly()
 *	Set/clear dir readonly bit
 */
void
dir_readonly(struct file *f, int readonly)
{
	struct directory *d;
	void *handle;
	struct node *n = f->f_node;

	/*
	 * Flag bit in dir entry header and cache of
	 * it in n_mode.  Don't dirty anything if the
	 * protection isn't changing.
	 */
	d = get_dirent(n->n_dir, n->n_slot, &handle);
	if (readonly) {
		d->attr |= DA_READONLY;
		n->n_mode &= ~ACC_WRITE;
	} else {
		d->attr &= ~DA_READONLY;
		n->n_mode |= ACC_WRITE;
	}
	ddirty(handle);
	dfree(handle);
	n->n_flags |= N_DIRTY;
}
