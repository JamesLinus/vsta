/*
 * open.c
 *	Routines for opening, closing, creating  and deleting files
 */
#include <vstafs/vstafs.h>
#include <vstafs/alloc.h>
#include <std.h>

/*
 * findent()
 *	Given a buffer-full of fs_dirent's, look up a filename
 *
 * Returns a pointer to an openfile on success, 0 on failure.
 */
static struct openfile *
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
		 * Found it!  Let our node layer take it, and return the
		 * result.
		 */
		return(get_node(d->fs_clstart));
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
dir_lookup(struct buf *b, struct fs_file *fs, char *name)
{
	ulong off;
	struct buf *b2;
	uint extent;

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

			/*
			 * Special case for initial data in file
			 */
			if (x == 0) {
				d = (struct fs_dirent *)
					((char *)d + sizeof(struct fs_file));
				len -= sizeof(struct fs_file);
			}

			/*
			 * Look for our filename
			 */
			o = findent(d, len/sizeof(struct fs_dirent), name);
			if (o) {
				return(o);
			}
		}
	}
	return(0);
}

/*
 * create_file()
 *	Create the initial "file" contents
 *
 *
static struct openfile *
create_file(struct file *f, uint type)
{
	daddr_t da;
	struct buf *b;
	struct fs_dirent *d;
	struct prot *p;

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
	d->fs_clstart = da;
	d->fs_prev = 0;
	d->fs_rev = 1;
	d->fs_len = 0;
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
	uint x;
	struct buf *b;
	struct fs_file *fs;

	ASSERT(o->o_refs == 1, "uncreate_file: refs");

	/*
	 * Access file structure info
	 */
	b = find_buf(o->o_file, o->o_len);
	fs = index_buf(b, 0, 1);

	/*
	 * Free all allocated blocks, then free openfile itself
	 */
	for (x = 0; x < fs->fs_nblk; ++x) {
		struct alloc *a = &fs->fs_blks[x];
		uint y;

		/*
		 * Invalidate out any buffers associated with
		 * the file's data.
		 */
		for (y = 0; y < a->a_len; y += EXTSIZ) {
			ulong len;

			len = a->a_len - y;
			if (len > EXTSIZ)
				len = EXTSIZ;
			inval_buf(a->a_start + y, (uint)y);
		}

		/*
		 * Now release the physical storage
		 */
		free_block(a->a_start, a->a_len);
	}
	free(o);
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
	uint off, extent;
	struct openfile *o;
	struct fs_dirent *d;
	int err = 0;

	/*
	 * Access file structure of enclosing dir
	 */
	b = find_buf(f->f_file->o_file, f->f_file->o_len);
	if (b == 0) {
		return(0);
	}
	lock_buf(b);
	fs = index_buf(b, 0, 1);

	/*
	 * Get the openfile first
	 */
	o = create_file(f, type);

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
				err = 1;
				goto out;
			}
			d = index_buf(b2, 0, len);

			/*
			 * Special case for initial data in file
			 */
			if (x == 0) {
				d = (struct fs_dirent *)
					((char *)d + sizeof(struct fs_file));
				len -= sizeof(struct fs_file);
			}

			/*
			 * Look for our filename
			 */
			d = findfree(d, len/sizeof(struct fs_dirent));
			if (d) {
				goto out;
			}

		}
	}

	/*
	 * No luck with existing blocks.  Use bmap() to map in some
	 * more storage.
	 */
	b2 = bmap(fs, fs->fs_len, sizeof(struct fs_dirent),
		&d, &off);
	if (b2 == 0) {
		err = 1;
		goto out;
	}
	ASSERT_DEBUG(off >= sizeof(struct fs_dirent),
		"dir_newfile: bad growth");

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
	strcpy(d->d_name, name);
	d->d_clstart = o->o_file;
	dirty_buf(b2);
	sync_buf(b2)
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

	/*
	 * Get file header, but don't wire down
	 */
	o = f->f_file;
	b = find_buf(o->o_file, o->o_len);
	if (!b || !(fs = index_buf(b, 0, 1))) {
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
	o = dir_lookup(b, fs, m->m_buf, findent);
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
	x = perm_calc(f->f_perms, f->f_nperm, &o->o_prot);
	if ((m->m_arg & x) != m->m_arg) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * If they wanted it truncated, do it now
	 */
	if (m->m_arg & ACC_CREATE) {
		blk_trunc(o);
	}

	/*
	 * Move to this file
	 */
	f->f_file = o; o->o_refs += 1;
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

	if (o = f->f_file) {
		deref_node(o);
	}
}

/*
 * vfs_remove()
 *	Remove an entry in the current directory
 */
void
vfs_remove(struct msg *m, struct file *f)
{
	struct openfile *o;
	uint x;

	/*
	 * Have to be in root dir
	 */
	if (f->f_file) {
		msg_err(m->m_sender, ENOTDIR);
		return;
	}

	/*
	 * Look up entry.  Bail if no such file.
	 */
	o = dir_lookup(m->m_buf);
	if (o == 0) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * Check permission
	 */
	x = perm_calc(f->f_perms, f->f_nperm, &o->o_prot);
	if ((x & (ACC_WRITE|ACC_CHMOD)) == 0) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Can't be any other users
	 */
	if (o->o_refs > 0) {
		msg_err(m->m_sender, EBUSY);
		return;
	}

	/*
	 * Zap the blocks
	 */
	blk_trunc(o);

	/*
	 * Free the node memory
	 */
	freeup(o);

	/*
	 * Return success
	 */
	m->m_buflen = m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}
