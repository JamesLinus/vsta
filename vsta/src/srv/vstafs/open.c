/*
 * open.c
 *	Routines for opening, closing, creating  and deleting files
 */
#include "vstafs.h"
#include "alloc.h"
#include <std.h>
#include <sys/param.h>
#include <sys/assert.h>
#include <hash.h>
#include <time.h>

static struct hash *rename_pending;
static char *parse_name(char *, ulong *);

#define isdigit(c) (((c) >= '0') && ((c) <= '9'))

/*
 * walkto()
 *	Walk to given revision of file, given base
 *
 * Optionally can return pointer to next newer file revision
 */
static struct openfile *
walkto(struct openfile *o, ulong rev)
{
	daddr_t prev;
	struct fs_file *fs;

	/*
	 * Walk backwards in file's revision chain
	 */
	for (;;) {
		/*
		 * Get next fs_file
		 */
		fs = getfs(o, 0);

		/*
		 * Match?  Return it.
		 */
		if (fs->fs_rev == rev) {
			break;
		}

		/*
		 * Walk back a level.  When reach 0, we didn't
		 * find the requested revision number.
		 */
		prev = fs->fs_prev;
		if (prev == 0) {
			return(0);
		}
		deref_node(o);
		o = get_node(prev);
	}
	return(o);
}

/*
 * dir_fillnew()
 *	Zero out new directory space
 *
 * Note "off" is a byte offset, "len" is in sectors
 */
static void
dir_fillnew(struct buf *b, struct fs_file *fs, ulong off, ulong len)
{
	ulong x;
	struct buf *b2;
	void *v;
	uint dummy;

	for (x = 0; x < len; ++x, off += SECSZ) {
		b2 = bmap(b, fs, off, SECSZ, (char **)&v, &dummy);
		ASSERT_DEBUG(dummy == SECSZ, "dir_fillnew: short sector");
		ASSERT(b2, "dir_fillnew: can't fill");
		bzero(v, SECSZ);
		dirty_buf(b2, 0);
	}
	sync_bufs(0);
}

/*
 * partial_trunc()
 *	Trim back the block allocation on an allocation unit
 */
static void
partial_trunc(struct alloc *a, uint newsize)
{
	uint topbase;

	ASSERT_DEBUG(newsize > 0, "partial_trunc: 0 size");
	ASSERT_DEBUG(newsize <= a->a_len, "partial_trunc: grow");

	/*
	 * If not even a single block has been freed, the partial
	 * trunc comes to a no-op from the perspective of block
	 * allocation.
	 */
	if (newsize == a->a_len) {
		return;
	}

	/*
	 * Inval buffer extents beyond last one with data
	 */
	topbase = roundup(newsize, EXTSIZ);
	if (a->a_len > topbase) {
		inval_buf(a->a_start + topbase, a->a_len - topbase);
	}

	/*
	 * Resize extent, freeing trailing data
	 */
	free_block(a->a_start + newsize, a->a_len - newsize);
	a->a_len = newsize;
	topbase = (newsize & ~(EXTSIZ-1));
	if (newsize > topbase) {
		/*
		 * Resize the top buffer unless it's vacuous (zero length)
		 */
		resize_buf(a->a_start + topbase, newsize - topbase, 0);
	}
}

/*
 * file_shrink()
 *	Trim a file down to the specified length
 *
 * Does not handle freeing of actual fs_file data.
 */
static void
file_shrink(struct openfile *o, ulong len)
{
	struct fs_file *fs;
	struct buf *b;
	ulong pos;
	uint idx, y;
	struct alloc *a;

	ASSERT_DEBUG(len >= sizeof(struct fs_file), "file_shrink: too small");

	/*
	 * Get file, apply sanity checks
	 */
	fs = getfs(o, &b);
	ASSERT(fs, "file_shrink: can't map file");
	ASSERT_DEBUG(len <= fs->fs_len, "file_shrink: grow");

	/*
	 * Scan the extents
	 */
	pos = 0;
	a = fs->fs_blks;
	for (idx = 0; idx < fs->fs_nblk; ++idx,++a) {
		ulong npos;

		/*
		 * Continue loop while offset is below
		 */
		npos = pos + stob(a->a_len);
		if (npos <= len) {
			pos = npos;
			continue;
		}

		/*
		 * Trim extent if it's partially truncated
		 */
		y = btors(len - pos);
		if (y > 0) {
			/*
			 * Shave it
			 */
			partial_trunc(a, y);

			/*
			 * If this was extent 0, our buffer data
			 * might move on the resize.  Re-access
			 * it.
			 */
			if (idx == 0) {
				fs = index_buf(b, 0, 1);
				a = fs->fs_blks;
			}

			/*
			 * This extent is finished, advance to next
			 */
			a += 1;
			idx += 1;
		}

		/*
		 * Now dump the remaining extents
		 */
		for (y = idx; y < fs->fs_nblk; ++y,++a) {
			inval_buf(a->a_start, a->a_len);
			free_block(a->a_start, a->a_len);
		}
		fs->fs_nblk = idx;
		break;
	}

	/*
	 * Flag buffer as dirty, update length
	 */
	dirty_buf(b, 0);
	fs->fs_len = len;
	o->o_len = btors(len);
	if (o->o_hiwrite > len) {
		o->o_hiwrite = len;
	}
	sync_buf(b);
}

/*
 * getfs()
 *	Given openfile, return pointer to its fs_file
 *
 * The returned value is in the buffer pool, and can only be
 * used until the next request is made against the buffer pool.
 *
 * If bp is non-zero, the associated buffer header is filled
 * into this pointer.
 */
struct fs_file *
getfs(struct openfile *o, struct buf **bp)
{
	struct fs_file *fs;
	struct buf *b;

	b = find_buf(o->o_file, MIN(o->o_len, EXTSIZ), ABC_FILL);
	if (!b) {
		return(0);
	}
	fs = index_buf(b, 0, 1);
	if (bp) {
		*bp = b;
	}
	return(fs);
}

/*
 * findent()
 *	Given a buffer-full of fs_dirent's, look up a filename
 *
 * Returns a pointer to a dirent on success, 0 on failure.
 */
static struct fs_dirent *
findent(struct fs_dirent *d, uint nent, char *name)
{
	for ( ; nent > 0; ++d,--nent) {
		/*
		 * No more entries in file
		 */
		if (d->fs_name[0] == 0) {
			return(0);
		}

		/*
		 * Deleted file
		 */
		if (d->fs_name[0] & 0x80) {
			continue;
		}

		/*
		 * Keep going while no match
		 */
		if (strcmp(d->fs_name, name)) {
			continue;
		}

		/*
		 * Return matching dir entry
		 */
		return(d);
	}
	return(0);
}

/*
 * dir_lookup()
 *	Given open dir, look for name
 *
 * On success, returns the openfile; on failure, 0.
 *
 * "b" is assumed locked on entry; will remain locked in this routine.
 */
static struct openfile *
dir_lookup(struct buf *b, struct fs_file *fs, char *name,
	struct fs_dirent **dep, struct buf **bp)
{
	struct buf *b2;
	uint extent;
	ulong left = fs->fs_len;

	/*
	 * Walk the directory entries one extent at a time
	 */
	for (extent = 0; extent < fs->fs_nblk; ++extent) {
		uint x, len;
		struct alloc *a = &fs->fs_blks[extent];

		/*
		 * Walk through an extent one buffer-full at a time
		 */
		for (x = 0; x < a->a_len; x += EXTSIZ) {
			struct fs_dirent *d;

			ASSERT_DEBUG(left != 0, "dir_lookup: left == 0");
			/*
			 * Figure out size of next buffer-full
			 */
			len = a->a_len - x;
			if (len > EXTSIZ) {
				len = EXTSIZ;
			}

			/*
			 * Map it in
			 */
			b2 = find_buf(a->a_start+x, len, ABC_FILL);
			if (b2 == 0) {
				return(0);
			}
			d = index_buf(b2, 0, len);
			len = stob(len);

			/*
			 * Ignore unused trailing part of last extent
			 */
			len = MIN(len, left);
			left -= len;

			/*
			 * Special case for initial data in file
			 */
			if ((extent == 0) && (x == 0)) {
				d = (struct fs_dirent *)
					((char *)d + sizeof(struct fs_file));
				len -= sizeof(struct fs_file);
			}

			/*
			 * Look for our filename
			 */
			d = findent(d, len/sizeof(struct fs_dirent), name);
			if (d) {
				struct openfile *o;

				/*
				 * Found it.  Get node, and fill in
				 * our information.
				 */
				o = get_node(d->fs_clstart);
				if (!o) {
					return(0);
				}
				if (dep) {
					*dep = d;
					*bp = b2;
				}
				return(o);
			}
		}
	}
	return(0);
}

/*
 * findfree()
 *	Find the next open directory slot
 */
static struct fs_dirent *
findfree(struct fs_dirent *d, uint nent)
{
	while (nent > 0) {
		if ((d->fs_clstart == 0) || (d->fs_name[0] & 0x80)) {
			return(d);
		}
		nent -= 1;
		d += 1;
	}
	return(0);
}

/*
 * create_file()
 *	Create the initial "file" contents
 */
static struct openfile *
create_file(struct file *f, uint type)
{
	daddr_t da;
	struct buf *b;
	struct fs_file *d, *dirfs;
	struct openfile *o;

	/*
	 * Get the block, map it
	 */
	da = alloc_block(1);
	if (da == 0) {
		return(0);
	}
	b = find_buf(da, 1, ABC_FILL);
	if (b == 0) {
		free_block(da, 1);
		return(0);
	}
	d = index_buf(b, 0, 1);
	lock_buf(b);

	/*
	 * Special handling; dir versus file
	 */
	if (type == FT_DIR) {
		bzero(d, SECSZ);
		d->fs_len = SECSZ;
	} else {
		d->fs_len = sizeof(struct fs_file);
	}

	/*
	 * Fill in the fields
	 */
	d->fs_prev = 0;
	d->fs_rev = 1;
	d->fs_type = type;
	d->fs_nlink = 1;
	d->fs_nblk = 1;
	d->fs_blks[0].a_start = da;
	d->fs_blks[0].a_len = 1;
	time(&d->fs_ctime); d->fs_mtime = d->fs_ctime;

	/*
	 * Default protection, use containing directory's permissions,
	 * and UID of 0th perm.
	 */
	dirfs = getfs(f->f_file, NULL);
	bcopy(&dirfs->fs_prot, &d->fs_prot, sizeof(d->fs_prot));
	d->fs_owner = f->f_perms[0].perm_uid;

	/*
	 * Allocate an openfile to it
	 */
	o = get_node(da);
	if (o == 0) {
		free_block(da, 1);
	}

	/*
	 * Flush out info to disk, and return openfile
	 */
	dirty_buf(b, 0);
	unlock_buf(b);
	sync_buf(b);
	return(o);
}

/*
 * uncreate_file()
 *	Release resources of an openfile
 */
static void
uncreate_file(struct openfile *o)
{
	struct fs_file *fs;
	struct alloc *a;

	ASSERT(o->o_refs == 1, "uncreate_file: refs");

	/*
	 * Access file structure info
	 */
	fs = getfs(o, 0);
	ASSERT(fs, "uncreate_file: buffer access failed");

	/*
	 * Dump all but the fs_file.  The buffer can move; re-map
	 * it.
	 */
	file_shrink(o, sizeof(struct fs_file));
	fs = getfs(o, 0);

	/*
	 * Dump the file header, and free the openfile
	 */
	a = &fs->fs_blks[0];
	ASSERT_DEBUG(a->a_len == 1, "uncreate_file: too many left");
	free_block(a->a_start, 1);
	inval_buf(a->a_start, 1);
	deref_node(o);
}

/*
 * dir_addspace()
 *	Add space to the end of the named directory
 *
 * Return 1 on failure, 0 on success
 */
static int
dir_addspace(struct buf *b, struct fs_file *fs, ulong off)
{
	uint x;
	ulong got, newlen;
	struct alloc *a;

	/*
	 * Shoot for an increment based on how many extents
	 * are consumed.
	 */
	x = fs->fs_nblk - 1;
	newlen = MIN(1 << (DIREXTSIZ + x), EXTSIZ);

	/*
	 * See if we can extend the current dir block
	 */
	a = &fs->fs_blks[x];
	got = take_block(a->a_start + a->a_len, newlen);

	/*
	 * If we could add some space, go with it
	 */
	if (got > 0) {
		uint topbase;

		/*
		 * Add space to extent table in file, update file size
		 */
		a->a_len += got;
		fs->fs_len += stob(got);

		/*
		 * Mark file header modified
		 */
		dirty_buf(b, 0);

		/*
		 * Tell buffer cache to resize buffer containing this,
		 * Calculate "fs" location again.
		 */
		topbase = (a->a_len & ~(EXTSIZ-1));
		resize_buf(a->a_start + topbase, a->a_len - topbase, 0);
		fs = index_buf(b, 0, 1);

		/*
		 * Clear out space
		 */
		dir_fillnew(b, fs, off, got);

		/*
		 * Success
		 */
		return(0);
	}

	/*
	 * Couldn't extend current space, grab a completely new extent
	 */
	if (fs->fs_nblk == MAXEXT) {
		return(1);
	}
	a += 1;
	x += 1;
	newlen = MIN(1 << (DIREXTSIZ + x), EXTSIZ);
	a->a_start = alloc_block(newlen);
	if (a->a_start == 0) {
		return(1);
	}

	/*
	 * Add the space on, and initialize it
	 */
	fs->fs_nblk += 1;
	fs->fs_len += stob(newlen);
	a->a_len = newlen;
	dirty_buf(b, 0);
	dir_fillnew(b, fs, off, a->a_len);

	return(0);
}

/*
 * dir_newfile()
 *	Create a new entry in the current directory
 *
 * On success new openfile will have one reference.
 */
static struct openfile *
dir_newfile(struct file *f, char *name, int type)
{
	struct buf *b, *b2 = 0;
	struct fs_file *fs;
	uint extent, dummy;
	ulong off;
	struct openfile *dirf, *o;
	struct fs_dirent *d;
	int err = 0;

	/*
	 * Access file structure of enclosing dir
	 */
	dirf = f->f_file;
	fs = getfs(dirf, &b);
	if (fs == 0) {
		return(0);
	}
	lock_buf(b);

	/*
	 * Get the openfile first
	 */
	o = create_file(f, type);
	if (o == 0) {
		unlock_buf(b);
		return(0);
	}

	/*
	 * Walk the directory entries one extent at a time
	 */
	off = sizeof(struct fs_file);
	for (extent = 0; extent < fs->fs_nblk; ++extent) {
		uint x;
		ulong len;
		struct alloc *a = &fs->fs_blks[extent];

		/*
		 * Walk through an extent one buffer-full at a time
		 */
		for (x = 0; x < a->a_len; x += EXTSIZ) {
			struct fs_dirent *dstart;

			/*
			 * Figure out size of next buffer-full
			 */
			len = MIN(a->a_len - x, EXTSIZ);

			/*
			 * Map it in
			 */
			b2 = find_buf(a->a_start+x, len, ABC_FILL);
			if (b2 == 0) {
				err = 1;
				goto out;
			}
			d = index_buf(b2, 0, len);
			len = stob(len);

			/*
			 * Special case for initial data in file
			 */
			if ((extent == 0) && (x == 0)) {
				d = (struct fs_dirent *)((char *)d + off);
				len -= sizeof(struct fs_file);
			}

			/*
			 * Look for a free slot
			 */
			dstart = d;
			len = MIN(len, fs->fs_len - off);
			d = findfree(dstart, len/sizeof(struct fs_dirent));
			if (d) {
				off += ((char *)d - (char *)dstart);
				goto out;
			}
			off += len;
		}
	}

	/*
	 * No luck with existing blocks.  Try to get some more, and
	 * use the start of the new space if successful.
	 */
	ASSERT_DEBUG(off == fs->fs_len, "dir_newfile: off/len skew");
	if (dir_addspace(b, fs, off)) {
		err = 1;
	} else {
		fs = index_buf(b, 0, 1);
		b2 = bmap(b, fs, off, sizeof(struct fs_dirent),
			(char **)&d, &dummy);
		ASSERT_DEBUG(b2, "dir_newfile: grow !buf");
	}

out:
	/*
	 * On error, release germinal file allocation, return 0
	 */
	if (err) {
		uncreate_file(o);
		return(0);
	}

	/*
	 * We have a slot, so fill it in & return success
	 */
	strcpy(d->fs_name, name);
	d->fs_clstart = o->o_file;
	if (off > dirf->o_hiwrite) {
		dirf->o_hiwrite = off;
	}

	/*
	 * Update dir file's length
	 */
	off += sizeof(struct fs_dirent);
	if (off > fs->fs_len) {
		fs->fs_len = off;
		dirty_buf(b, 0);
	}
	dirty_buf(b2, 0);
	sync_buf(b2);
	sync_buf(b);
	unlock_buf(b);
	return(o);
}

/*
 * vfs_open()
 *	Main entry for processing an open message
 */
void
vfs_open(struct msg *m, struct file *f)
{
	struct buf *b, *debp;
	struct openfile *o;
	struct fs_file *fs;
	uint x, want;
	struct fs_dirent *de;
	char *nm = 0;
	ulong rev = 0;

	/*
	 * Get file header, but don't wire down
	 */
	fs = getfs(f->f_file, &b);
	if (!fs) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Have to be in dir to open down into a file
	 */
	if (fs->fs_type != FT_DIR) {
		msg_err(m->m_sender, ENOTDIR);
		return;
	}

	/*
	 * Yuck.  If they ask for mode 0, make it mean "read" access.
	 * Delete me when you can relink all your applications with
	 * the new termcap.a
	 */
	if (m->m_arg == 0) {
		m->m_arg = ACC_READ;
	}

	/*
	 * Parse name for version field.  Bounce on illegal
	 * behavior.
	 */
	nm = parse_name(m->m_buf, &rev);
	if (rev && (m->m_arg & ACC_CREATE)) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Look up name, make sure "fs" stays valid
	 */
	lock_buf(b);
	o = dir_lookup(b, fs, nm, &de, &debp);

	/*
	 * If they want a particular revision, find it now
	 */
	if (o && rev) {
		o = walkto(o, rev);
	}

	/*
	 * No such file--do they want to create?
	 */
	if (!o && !(m->m_arg & ACC_CREATE)) {
		msg_err(m->m_sender, ESRCH);
		goto out;
	}

	/*
	 * If it's a new file, allocate the entry now.
	 */
	if (!o) {
		/*
		 * Allowed?
		 */
		if ((f->f_perm & (ACC_WRITE|ACC_CHMOD)) == 0) {
			msg_err(m->m_sender, EPERM);
			goto out;
		}

		/*
		 * Read-only?
		 */
		if (roflag) {
			msg_err(m->m_sender, EROFS);
			goto out;
		}

		/*
		 * Failure?
		 */
		o = dir_newfile(f, m->m_buf, (m->m_arg & ACC_DIR) ?
				FT_DIR : FT_FILE);
		if (o == 0) {
			msg_err(m->m_sender, ENOMEM);
			goto out;
		}

		/*
		 * Move to new node
		 */
		deref_node(f->f_file);
		f->f_file = o;
		f->f_perm = ACC_READ|ACC_WRITE|ACC_CHMOD;
		m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		goto out;
	}

	/*
	 * Check permission.  Hand off "fs" and "b" from the containing
	 * directory to the file being opened.
	 */
	unlock_buf(b);
	lock_buf(debp);
	fs = getfs(o, &b);
	unlock_buf(debp);
	lock_buf(b);
	want = m->m_arg & (ACC_READ|ACC_WRITE|ACC_CHMOD);
	x = perm_calc(f->f_perms, f->f_nperm, &fs->fs_prot);
	if ((want & x) != want) {
		deref_node(o);
		msg_err(m->m_sender, EPERM);
		goto out;
	}
	if (roflag && (want & (ACC_WRITE | ACC_CREATE | ACC_CHMOD))) {
		deref_node(o);
		msg_err(m->m_sender, EROFS);
		goto out;
	}

	/*
	 * If they wanted it truncated, do it now.  Truncation
	 * actually results in a new file revision, pushed down
	 * on top of the current one.
	 */
	if (m->m_arg & ACC_CREATE) {
		struct openfile *o2;
		struct fs_file *fs2;
		struct buf *b2;

		/*
		 * Make sure this entry is writable
		 */
		if (((x & ACC_WRITE) == 0) ||
				(fs->fs_type != FT_FILE)) {
			deref_node(o);
			msg_err(m->m_sender, EPERM);
			goto out;
		}

		/*
		 * Create the new instance of this file
		 * Grab the revision before getfs()'ing as we won't
		 * need fs again, and it saves us a buffer lock.
		 */
		lock_buf(debp);
		o2 = create_file(f, FT_FILE);
		unlock_buf(debp);

		/*
		 * The directory entry points to the new one
		 */
		de->fs_clstart = o2->o_file;
		dirty_buf(debp, 0);

		/*
		 * And the new one points to the older one by way
		 * of the fs_prev field.
		 */
		fs2 = getfs(o2, &b2);
		fs2->fs_prev = o->o_file;
		fs2->fs_rev = fs->fs_rev + 1;

		/*
		 * This new one will be the node of interest
		 * from here on.
		 */
		deref_node(o);
		o = o2;
	} else if (m->m_arg & ACC_WRITE) {
		/*
		 * If opening for writing, update mtime
		 */
		time(&fs->fs_mtime);
		dirty_buf(b, 0);
	}

	/*
	 * Move to this file
	 */
	deref_node(f->f_file);
	f->f_file = o;
	f->f_perm = m->m_arg | (x & ACC_CHMOD);
	m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
out:
	unlock_buf(b);
	if (nm && rev) {
		free(nm);
	}
}

/*
 * vfs_close()
 *	Do closing actions on a file
 */
void
vfs_close(struct file *f)
{
	struct openfile *o;

	o = f->f_file;
	ASSERT_DEBUG(o, "vfs_close: no openfile");
	if (o->o_refs == 1) {
		/*
		 * If delete pending, do it now
		 */
		if (o->o_flags & O_DELETED) {
			uncreate_file(o);
			sync_bufs(0);
			return;
		}

		/*
		 * Files are extended with a length which reflects
		 * the extent pre-allocation.  On final close, we
		 * trim this pre-allocated space back, and update
		 * the file's length to indicate just the true
		 * data.
		 */
		if (f->f_perm & (ACC_WRITE | ACC_CHMOD)) {
			if (f->f_perm & ACC_WRITE) {
				struct fs_file *fs;
				struct buf *b;

				fs = getfs(f->f_file, &b);
				if (fs->fs_type != FT_DIR) {
					file_shrink(o, o->o_hiwrite);
				}
			}
			sync_bufs(0);
		}
	}
	deref_node(o);
}

/*
 * do_unhash()
 *	Function to do the unhash() call from a child thread
 */
static void
do_unhash(ulong unhash_fid)
{
	extern port_t rootport;
	extern void unhash();

	unhash(rootport, unhash_fid);
	_exit(0);
}

/*
 * dir_empty()
 *	Return 1 if dir is empty, 0 otherwise
 *
 * Assumes "b" is locked
 */
static int
dir_empty(struct buf *b, struct fs_file *fs)
{
	ulong off;

	/*
	 * Walk across each dir entry
	 */
	off = sizeof(struct fs_file);
	while (off < fs->fs_len) {
		struct buf *b2;
		struct fs_dirent *de;
		uint dummy;

		/*
		 * Get pointer to next entry
		 */
		b2 = bmap(b, fs, off, sizeof(struct fs_dirent),
			(void *)&de, &dummy);

		/*
		 * If no more non-empty dir entries, we know the whole
		 * dir is empty as well
		 */
		if (de->fs_name[0] == 0) {
			return(1);
		}

		/*
		 * There's a file entry.  Unless it's deleted, we
		 * know it's not empty
		 */
		if ((de->fs_name[0] & 0x80) == 0) {
			return(0);
		}
		off += sizeof(struct fs_dirent);
	}

	/*
	 * Reached the bottom--empty
	 */
	return(1);
}

/*
 * parse_name()
 *	Break name into basename and revision portions
 *
 * If there's a revision, the returned pointer is malloc'ed memory and
 * *revp holds the revision.  Otherwise the original pointer is
 * returned.
 */
static char *
parse_name(char *nm, ulong *revp)
{
	char *p, *buf;
	int len;

	p = strrchr(nm, ',');
	if (p && (p > nm) && (p[-1] == ',') && isdigit(p[1])) {
		*revp = atoi(p+1);
		len = p - nm;
		buf = malloc(len);
		len -= 1;
		bcopy(nm, buf, len);
		buf[len] = '\0';
		return(buf);
	}
	*revp = 0;
	return(nm);
}

/*
 * vfs_remove()
 *	Remove an entry in the current directory
 */
void
vfs_remove(struct msg *m, struct file *f)
{
	struct buf *b, *bdir, *bdirent, *bfile, *brevp;
	struct fs_file *fs, *fsdir, *fsfile;
	struct openfile *odirent, *o;
	uint x;
	struct fs_dirent *de;
	char *nm, *err;
	ulong rev;
	daddr_t da, cur_da, *revp;

	/*
	 * Initialize the pointers which flag various active
	 * data structures
	 */
	bdir = bdirent = bfile = 0;
	err = 0;
	odirent = o = 0;
	nm = 0;
	rev = 0;

	/*
	 * Look at file structure
	 */
	fsdir = getfs(f->f_file, &bdir);
	if (fsdir == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}
	lock_buf(bdir);

	/*
	 * Have to be in a dir
	 */
	if (fsdir->fs_type != FT_DIR) {
		err = ENOTDIR;
		goto out;
	}

	/*
	 * See if we're clearing a particular revision, or all
	 */
	nm = parse_name(m->m_buf, &rev);

	/*
	 * Look up entry.  Bail if no such file.
	 */
	odirent = dir_lookup(bdir, fsdir, nm, &de, &bdirent);
	if (odirent == 0) {
		err = ESRCH;
		goto out;
	}
	lock_buf(bdirent);

	/*
	 * Check permission
	 */
	x = perm_calc(f->f_perms, f->f_nperm, &fsdir->fs_prot);
	if ((x & (ACC_WRITE|ACC_CHMOD)) == 0) {
		err = EPERM;
		goto out;
	}
	if (roflag) {
		err = EROFS;
		goto out;
	}

	/*
	 * If directory, make sure it's empty
	 */
	fsfile = getfs(odirent, &bfile);
	if (fsfile == 0) {
		err = strerror();
		goto out;
	}
	lock_buf(bfile);
	if (fsfile->fs_type == FT_DIR) {
		if (!dir_empty(bfile, fsfile)) {
			err = EBUSY;
			goto out;
		}
	}
	unlock_buf(bfile); bfile = 0;

	/*
	 * Walk the revision chain backwards, taking action based
	 * on the specified name.
	 */
	da = odirent->o_file;
	revp = &de->fs_clstart;
	brevp = bdirent;
	deref_node(odirent); odirent = 0;
	while (da) {
		/*
		 * Access file under this name, prepare for next
		 */
		cur_da = da;
		lock_buf(brevp);
		o = get_node(cur_da);
		fs = getfs(o, &b);
		da = fs->fs_prev;
		unlock_buf(brevp);

		/*
		 * If we're looking for a particular revision, and this
		 * isn't it, continue.
		 */
		if (rev && (fs->fs_rev != rev)) {
			deref_node(o); o = 0;

			/*
			 * If we've passed the requested revision,
			 * end the loop now.
			 */
			if (fs->fs_rev < rev) {
				break;
			}

			/*
			 * Otherwise continue walking backwards
			 */
			revp = &fs->fs_prev;
			brevp = b;
			continue;
		}

		/*
		 * Try unhashing if it might be the only other reference
		 */
		if ((o->o_refs == 2) && (o->o_flags & O_HASHED)) {
			/*
			 * Clear hint
			 */
			o->o_flags &= ~O_HASHED;

			/*
			 * Since a closing portref needs to handshake
			 * with the server, use a child thread to do
			 * the dirty work.
			 */
			(void)tfork(do_unhash, cur_da);

			/*
			 * Release our ref and tell the requestor he
			 * might want to try again.
			 */
			err = EAGAIN;
			goto out;
		}

		/*
		 * Patch this file out of the chain
		 */
		*revp = fs->fs_prev;
		dirty_buf(brevp, 0);

		/*
		 * Zap the blocks, or mark zap pending
		 */
		if (o->o_refs > 1) {
			o->o_flags |= O_DELETED;
			deref_node(o);
		} else {
			uncreate_file(o);
		}
		o = 0;

		/*
		 * Move to next, unless we're done
		 */
		if (rev) {
			break;
		}
	}

	/*
	 * If they asked to delete the file and all its revisions,
	 * and we successfully did so, zap the dir entry.
	 */
	if (!rev) {
		de->fs_name[0] |= 0x80;
		dirty_buf(bdirent, 0);
		sync_buf(bdirent);
	}

	/*
	 * Clean up
	 */
out:
	if (bfile) {
		unlock_buf(bfile);
	}
	if (bdirent) {
		unlock_buf(bdirent);
	}
	if (bdir) {
		unlock_buf(bdir);
	}
	if (o) {
		deref_node(o);
	}
	if (odirent) {
		deref_node(odirent);
	}
	if (nm && rev) {
		free(nm);
	}

	/*
	 * Return success/error
	 */
	if (err) {
		msg_err(m->m_sender, err);
	} else {
		m->m_buflen = m->m_arg = m->m_arg1 = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
	}
}

/*
 * get_dirent()
 *	Look up named entry, return pointer to it
 *
 * On error, returns 0 and puts a string in err.  On success, returns
 * dir entry pointer and buf for it in bp.  Buffer has been locked.
 */
static struct fs_dirent *
get_dirent(struct file *f, char *name, struct buf **bpp, char **errp,
	int create, struct openfile **op)
{
	struct fs_file *fs;
	struct buf *b;
	struct fs_dirent *de;
	struct openfile *o;

	/*
	 * Get file header, make sure it's a directory
	 */
	fs = getfs(f->f_file, &b);
	if (fs == 0) {
		*errp = strerror();
		return(0);
	}

	/*
	 * Have to be in a dir
	 */
	if (fs->fs_type != FT_DIR) {
		*errp = ENOTDIR;
		return(0);
	}

	/*
	 * Look up entry
	 */
	lock_buf(b);
	o = dir_lookup(b, fs, name, &de, bpp);
	unlock_buf(b);

	/*
	 * If not there, see about creating
	 */
	if ((o == 0) && create) {
		/*
		 * Wire down parent buf, create new file
		 */
		lock_buf(b);
		o = dir_newfile(f, name, FT_FILE);
		if (o == 0) {
			unlock_buf(b);
			*errp = EINVAL;
			return(0);
		}

		/*
		 * Get new dir entry, drop ref from dir_newfile()
		 * now that dir_lookup() has taken one.
		 */
		o = dir_lookup(b, fs, name, &de, bpp);
		deref_node(o);
		unlock_buf(b);
		ASSERT(o, "get_dirent: can't find created file");
	}

	/*
	 * Free any storage, bomb on the destination being a directory
	 */
	if (create) {
		fs = getfs(o, &b);
		if (fs->fs_type == FT_DIR) {
			*errp = EISDIR;
			return(0);
		}
		file_shrink(o, sizeof(struct fs_file));
	}

	/*
	 * Give open file back, or release our hold on it
	 */
	if (op) {
		*op = o;
	} else {
		deref_node(o);
	}
	ASSERT_DEBUG(de, "get_dirent: !de");
	lock_buf(*bpp);
	return(de);
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
	struct fs_dirent *desrc, *dedest;
	struct buf *bsrc = 0, *bdest = 0;
	struct openfile *odest;
	char *err;
	daddr_t blktmp;

	/*
	 * Get pointers to source and destination directory
	 * entries.
	 */
	desrc = get_dirent(fsrc, src, &bsrc, &err, 0, 0);
	if (desrc == 0) {
		return(err);
	}
	dedest = get_dirent(fdest, dest, &bdest, &err, 1, &odest);
	if (dedest == 0) {
		unlock_buf(bsrc);
		return(err);
	}

	/*
	 * Swap who holds which file
	 */
	blktmp = desrc->fs_clstart;
	desrc->fs_clstart = dedest->fs_clstart;
	dedest->fs_clstart = blktmp;

	/*
	 * Mark the two directory blocks dirty, and release their locks
	 */
	dirty_buf(bsrc, 0); unlock_buf(bsrc);
	dirty_buf(bdest, 0); unlock_buf(bdest);

	/*
	 * Delete the old one now
	 */
	desrc->fs_name[0] |= 0x80;
	if (odest->o_refs == 1) {
		uncreate_file(odest);
	} else {
		odest->o_flags |= O_DELETED;
		deref_node(odest);
	}

	/*
	 * Success
	 */
	sync_bufs(0);
	return(0);
}

/*
 * vfs_rename()
 *	Rename one dir entry to another
 *
 * Handle registry of pending rename; run actual rename when we
 * have the two halves.
 */
void
vfs_rename(struct msg *m, struct file *f)
{
	struct file *f2;
	char *errstr;
	extern int valid_fname(char *, int);

	/*
	 * Sanity
	 */
	if ((m->m_arg1 == 0) || !valid_fname(m->m_buf, m->m_buflen)) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Read-only filesystem
	 */
	if (roflag) {
		msg_err(m->m_sender, EROFS);
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
		bcopy(m, &f->f_rename_msg, sizeof(struct msg));
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
