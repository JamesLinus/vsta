/*
 * open.c
 *	Routines for opening, closing, creating  and deleting files
 */
#include <vstafs/vstafs.h>
#include <vstafs/alloc.h>
#include <vstafs/buf.h>
#include <std.h>
#include <sys/param.h>
#include <sys/assert.h>

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
	 * Fill in the fields
	 */
	d->fs_prev = 0;
	d->fs_rev = 1;
	d->fs_len = sizeof(struct fs_file);
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
	 * Dump all but the fs_file
	 */
	file_shrink(o, sizeof(struct fs_file));

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
 * dir_newfile()
 *	Create a new entry in the current directory
 */
static struct openfile *
dir_newfile(struct file *f, char *name, int type)
{
	struct buf *b, *b2;
	struct fs_file *fs;
	uint extent;
	ulong off;
	struct openfile *o;
	struct fs_dirent *d;
	int err = 0;

	/*
	 * Access file structure of enclosing dir
	 */
	fs = getfs(f->f_file, &b);
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
				d = (struct fs_dirent *)
					((char *)d + sizeof(struct fs_file));
				len -= sizeof(struct fs_file);
			}

			/*
			 * Look for our filename
			 */
			dstart = d;
			d = findfree(dstart, len/sizeof(struct fs_dirent));
			if (d) {
				off += ((char *)d - (char *)dstart);
				goto out;
			}
			off += len;
		}
	}

	/*
	 * No luck with existing blocks.  Use bmap() to map in some
	 * more storage.
	 */
	ASSERT_DEBUG(off == fs->fs_len, "dir_newfile: off/len skew");
	{
		uint dummy;

		b2 = bmap(b, fs, fs->fs_len, sizeof(struct fs_dirent),
			(char **)&d, &dummy);
		if (b2 == b) {
			fs = index_buf(b, 0, 1);
		}
	}
	if (b2 == 0) {
		err = 1;
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
	if (off > f->f_file->o_hiwrite) {
		f->f_file->o_hiwrite = off;
	}

	/*
	 * Update dir file's length
	 */
	off += sizeof(struct fs_dirent);
	if (off > fs->fs_len) {
		fs->fs_len = off;
		dirty_buf(b);
	} else {
		off = MAX(off, f->f_file->o_hiwrite);
		if (fs->fs_len > off) {
			file_shrink(f->f_file, off);
		}
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
			file_shrink(o, o->o_hiwrite);
		}
		sync();
	}
	deref_node(o);
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
