/*
 * open.c
 *	Routines for opening, closing, creating  and deleting files
 */
#include "vstafs.h"
#include "alloc.h"
#include "buf.h"
#include <std.h>
#include <sys/param.h>
#include <sys/assert.h>
#include <hash.h>

static struct hash *rename_pending;

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
		dirty_buf(b2);
	}
	sync();
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
	resize_buf(a->a_start + topbase, newsize - topbase, 0);
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
	dirty_buf(b);
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

	b = find_buf(o->o_file, MIN(o->o_len, EXTSIZ));
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
		if (d->fs_clstart == 0) {
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
			b2 = find_buf(a->a_start+x, len);
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
	struct fs_file *d;
	struct prot *p;
	struct openfile *o;

	/*
	 * Get the block, map it
	 */
	da = alloc_block(1);
	if (da == 0) {
		return(0);
	}
	b = find_buf(da, 1);
	if (b == 0) {
		free_block(da, 1);
		return(0);
	}
	d = index_buf(b, 0, 1);

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

	/*
	 * Default protection, use 0'th perm
	 */
	p = &d->fs_prot;
	bzero(p, sizeof(*p));
	p->prot_len = PERM_LEN(&f->f_perms[0]);
	bcopy(f->f_perms[0].perm_id, p->prot_id, PERMLEN);
	p->prot_bits[p->prot_len-1] =
		ACC_READ|ACC_WRITE|ACC_CHMOD;
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
	dirty_buf(b);
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
		dirty_buf(b);

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
	printf("newlen %d DIREXTSIZE %d ext %d EXTSIZ %d\n",
		newlen, DIREXTSIZ, x, EXTSIZ);
	a->a_start = alloc_block(newlen);
	if (a->a_start == 0) {
		return(1);
	}
	printf(" got 0x%x\n", a->a_start);

	/*
	 * Add the space on, and initialize it
	 */
	fs->fs_nblk += 1;
	fs->fs_len += stob(newlen);
	a->a_len = newlen;
	dirty_buf(b);
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
	struct buf *b, *b2;
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
			b2 = find_buf(a->a_start+x, len);
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
		dirty_buf(b);
	}
	dirty_buf(b2);
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
	struct buf *b;
	struct openfile *o;
	struct fs_file *fs;
	uint x, want;

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
	 * Look up name, make sure "fs" stays valid
	 */
	lock_buf(b);
	o = dir_lookup(b, fs, m->m_buf, 0, 0);
	unlock_buf(b);

	/*
	 * No such file--do they want to create?
	 */
	if (!o && !(m->m_arg & ACC_CREATE)) {
		msg_err(m->m_sender, ESRCH);
		return;
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
			return;
		}

		/*
		 * Failure?
		 */
		o = dir_newfile(f, m->m_buf, (m->m_arg & ACC_DIR) ?
				FT_DIR : FT_FILE);
		if (o == 0) {
			msg_err(m->m_sender, ENOMEM);
			return;
		}

		/*
		 * Move to new node
		 */
		deref_node(f->f_file);
		f->f_file = o;
		f->f_perm = ACC_READ|ACC_WRITE|ACC_CHMOD;
		m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Check permission
	 */
	fs = getfs(o, &b);
	want = m->m_arg & (ACC_READ|ACC_WRITE|ACC_CHMOD);
	x = perm_calc(f->f_perms, f->f_nperm, &fs->fs_prot);
	if ((want & x) != want) {
		deref_node(o);
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * If they wanted it truncated, do it now
	 */
	if (m->m_arg & ACC_CREATE) {
		if ((x & ACC_WRITE) == 0) {
			deref_node(o);
			msg_err(m->m_sender, EPERM);
			return;
		}
		file_shrink(o, sizeof(struct fs_file));
	}

	/*
	 * Move to this file
	 */
	deref_node(f->f_file);
	f->f_file = o;
	f->f_perm = m->m_arg | (x & ACC_CHMOD);
	m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
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
		 * Files are extended with a length which reflects
		 * the extent pre-allocation.  On final close, we
		 * trim this pre-allocated space back, and update
		 * the file's length to indicate just the true
		 * data.
		 */
		if (f->f_perm & ACC_WRITE) {
			struct fs_file *fs;
			struct buf *b;

			fs = getfs(f->f_file, &b);
			if (fs->fs_type != FT_DIR) {
				file_shrink(o, o->o_hiwrite);
			}
			sync();
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
 * vfs_remove()
 *	Remove an entry in the current directory
 */
void
vfs_remove(struct msg *m, struct file *f)
{
	struct buf *b, *b2;
	struct fs_file *fs;
	struct openfile *o;
	uint x;
	struct fs_dirent *de;

	/*
	 * Look at file structure
	 */
	fs = getfs(f->f_file, &b);
	if (fs == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Have to be in a dir
	 */
	if (fs->fs_type != FT_DIR) {
		msg_err(m->m_sender, ENOTDIR);
		return;
	}

	/*
	 * Look up entry.  Bail if no such file.
	 */
	lock_buf(b);
	o = dir_lookup(b, fs, m->m_buf, &de, &b2);
	unlock_buf(b);
	if (o == 0) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * Check permission
	 */
	x = perm_calc(f->f_perms, f->f_nperm, &fs->fs_prot);
	if ((x & (ACC_WRITE|ACC_CHMOD)) == 0) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Try unhashing if it might be the only other reference
	 */
	if (o->o_refs == 2) {
		/*
		 * Since a closing portref needs to handshake
		 * with the server, use a child thread to do
		 * the dirty work.
		 */
		(void)tfork(do_unhash, o->o_file);

		/*
		 * Release our ref and tell the requestor he
		 * might want to try again.
		 */
		msg_err(m->m_sender, EAGAIN);
		return;
	}

	/*
	 * Can't be any other users
	 */
	if (o->o_refs > 1) {
		msg_err(m->m_sender, EBUSY);
		return;
	}

	/*
	 * Zap the dir entry, then blocks
	 */
	de->fs_name[0] |= 0x80; dirty_buf(b2); sync_buf(b2);
	uncreate_file(o);

	/*
	 * Return success
	 */
	m->m_buflen = m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
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
	dirty_buf(bsrc); unlock_buf(bsrc);
	dirty_buf(bdest); unlock_buf(bdest);

	/*
	 * Delete the old one now
	 */
	ASSERT_DEBUG(odest->o_refs == 1, "do_rename: o_refs != 1");
	desrc->fs_name[0] |= 0x80;
	uncreate_file(odest);

	/*
	 * Success
	 */
	sync();
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
