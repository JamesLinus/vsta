/*
 * open.c
 *	Routines for opening, closing, creating  and deleting files
 */
#include <vstafs/vstafs.h>
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
 * dir_newfile()
 *	Create a new entry in the current directory
 */
static struct openfile *
dir_newfile(struct buf *b, struct fs_file *fs, char *name, int type)
{
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
	o = dir_lookup(b, fs, m->m_buf);
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
		o = dir_newfile(b, fs, m->m_buf, (m->m_arg & ACC_DIR) ?
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
