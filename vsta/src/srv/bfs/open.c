/*
 * open.c
 *	Routines for opening, closing, creating  and deleting files
 *
 * Note that we don't allow subdirectories for BFS, which simplifies
 * things.
 */
#include <bfs/bfs.h>
#include <sys/assert.h>

extern struct super *sblock;
extern void *bdata();
extern struct inode *ifind();
extern char *strerror();

static int nwriters = 0;	/* # writers active */

/*
 * move_file()
 *	Trasfer the current file of a struct file to the given inode
 */
static void
move_file(struct file *f, struct inode *i, int writing)
{
	ASSERT_DEBUG(f->f_inode == ROOTINO, "move_file: not root");
	f->f_inode = i;
	f->f_pos = 0L;
	if (f->f_write = writing)
		nwriters += 1;
}

/*
 * bfs_open()
 *	Main entry for processing an open message
 */
void
bfs_open(struct msg *m, struct file *f)
{
	int newfile, off;
	void *handle;
	struct inode *i;
	struct dirent *d;
	char *p;

	/*
	 * Have to be in root dir to open down into a file
	 */
	if (f->f_inode != ROOTINO) {
		msg_err(m->m_sender, ENOTDIR);
		return;
	}

	/*
	 * Check for permission
	 */
	if (m->m_arg & (ACC_WRITE|ACC_CREATE|ACC_DIR)) {
		/*
		 * No subdirs in a boot filesystem
		 */
		if (m->m_arg & ACC_DIR) {
			msg_err(m->m_sender, EINVAL);
			return;
		}

		/*
		 * Only one writer at a time
		 */
		if (nwriters > 0) {
			msg_err(m->m_sender, EBUSY);
			return;
		}

		/*
		 * Insufficient priveleges
		 */
		if (f->f_write == 0) {
			msg_err(m->m_sender, EPERM);
			return;
		}
	}

	/*
	 * Look up name
	 */
	newfile = dir_lookup(m->m_buf, &handle, &off);

	/*
	 * No such file--do they want to create?
	 */
	if (newfile && !(m->m_arg & ACC_CREATE)) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * If it's a new file, allocate the entry now.
	 */
	if (newfile) {
		if (dir_newfile(m->m_buf, &handle, &off)) {
			msg_err(m->m_sender, strerror());
			return;
		}
	}

	/*
	 * Point to affected directory entry
	 */
	p = bdata(handle);
	d = (struct dirent *)(p + off);

	/*
	 * Get the inode for the directory entry
	 */
	i = ifind(d->d_inum);
	if (!i) {
		bfree(handle);
		msg_err(m->m_sender, ENOMEM);
	}

	/*
	 * If they want to use the existing file, set up the
	 * inode and let them go for it.  Note that this case
	 * MUST be !newfile, or it would have been caught above.
	 */
	if (!(m->m_arg & ACC_CREATE)) {
		move_file(f, i, m->m_arg & ACC_WRITE);
		goto success;
	}

	/*
	 * Creation is desired.  If there's an existing file, free
	 * its storage.
	 */
	if (!newfile) {
		extern void blk_trunc();

		blk_trunc(d);
		/* Marked bdirty() below */
	}

	/*
	 * Move pointers down to next free storage block
	 */
	d->d_start = sblock->s_nextfree;
	d->d_len = 0;
	bdirty(handle);
	move_file(f, i, m->m_arg & ACC_WRITE);
success:
	m->m_buf = 0;
	m->m_buflen = 0;
	m->m_nseg = 0;
	m->m_arg1 = m->m_arg = 0;
	msg_reply(m->m_sender, m);
	if (handle) {
		bfree(handle);
	}
}

/*
 * bfs_close()
 *	Do closing actions on a file
 *
 * There is no FS_CLOSE message; this is entered in response to the
 * connection being terminated.
 */
void
bfs_close(struct file *f)
{
	extern void bsync();

	/*
	 * A reference to the root dir needs no action
	 */
	if (!f->f_inode)
		return;

	/*
	 * Free inode reference, decrement writers count
	 * if it was a writer.
	 */
	ifree(f->f_inode);
	if (f->f_write) {
		nwriters -= 1;
		bsync();
	}
}

/*
 * bfs_remove()
 *	Remove an entry in the current directory
 */
void
bfs_remove(struct msg *m, struct file *f)
{
	int off;
	void *handle;
	struct dirent *d;
	struct inode *i;

	/*
	 * Have to be in root dir, and have permission
	 */
	if (f->f_inode != ROOTINO) {
		msg_err(m->m_sender, EINVAL);
		return;
	}
	if (!f->f_write) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Look up entry.  Bail if no such file.
	 */
	if (dir_lookup(m->m_buf, &handle, &off)) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * Get a handle on the entry
	 */
	d = (struct dirent *)((char *)bdata(handle) + off);

	/*
	 * Make sure nobody's using it
	 */
	i = ifind(d->d_inum);
	if (i) {
		if (i->i_refs > 1) {
			msg_err(m->m_sender, EBUSY);
			ifree(i);
			return;
		}
		ifree(i);
	}

	/*
	 * Zap the blocks
	 */
	blk_trunc(d);

	/*
	 * Flag it as an inactive entry
	 */
	d->d_name[0] = '\0';
#ifdef DEBUG
	d->d_start = d->d_len = 0;
#endif
	/*
	 * Flag the dir entry as dirty, and finish up.  Update
	 * the affected blocks to minimize damage from a crash.
	 */
	bdirty(handle);
	bfree(handle);
	bsync();

	/*
	 * Return success
	 */
	m->m_buflen = m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}
