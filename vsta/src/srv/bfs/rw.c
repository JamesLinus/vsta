/*
 * rw.c
 *	Routines for operating on the data in a file
 */
#include <sys/param.h>
#include <bfs/bfs.h>

extern void *bget(), *malloc();
extern char *strerror();

/*
 * do_write()
 *	Local routine to loop over a buffer and write it to a file
 *
 * Returns 0 on success, 1 on error.
 */
static
do_write(int startblk, int pos, char *buf, int cnt)
{
	int x, step, blk, boff;
	void *handle;

	/*
	 * Loop across each block, putting our data into place
	 */
	for (x = 0; x < cnt; ) {
		/*
		 * Calculate how much to take out of current block
		 */
		boff = pos & (BLOCKSIZE-1);
		step = BLOCKSIZE - boff;
		if (step >= cnt) {
			step = cnt;
		}

		/*
		 * Map current block
		 */
		blk = pos / BLOCKSIZE;
		handle = bget(startblk+blk);
		if (!handle)
			return 1;
		memcpy(bdata(handle)+boff, buf+x, step);
		bdirty(handle);
		bfree(handle);

		/*
		 * Advance to next chunk
		 */
		x += step;
	}
	return(0);
}

/*
 * bfs_write()
 *	Write to an open file
 */
void
bfs_write(struct msg *m, struct file *f)
{
	void *handle;
	struct dirent d;
	struct inode *i = f->f_inode;

	/*
	 * Can only write to a true file, and only if open for writing.
	 */
	if ((i == ROOTINO) || !f->f_write) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Get a picture of what the directory entry currently
	 * looks like.  The d_len field will change after new
	 * blocks are allocated, but d_start will remain the
	 * same.
	 */
	if (dir_copy(i->i_num, &d)) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * See if the file's going to be able to hold all the data.  We
	 * do not necessarily need to allocate space if we're rewriting
	 * an existing file.
	 */
	if ((f->f_pos + m->m_buflen) > d.d_len) {
		if (blk_alloc(i, f->f_pos + m->m_buflen)) {
			msg_err(m->m_sender, ENOSPC);
			return;
		}
	}

	/*
	 * Copy out the buffer
	 */
	if (do_write(d.d_start, f->f_pos, m->m_buf, m->m_buflen) {
		msg_err(m->m_sender, strerror());
		return;
	}
	m->m_arg = m->m_buflen;
	m->m_buflen = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * bfs_readdir()
 *	Do reads on directory entries
 */
static void
bfs_readdir(struct msg *m, struct file *f)
{
	struct dirent d;
	char *buf;
	int x, len, err;

	/*
	 * Make sure it's the root directory
	 */
	if (f->f_inode != ROOTINO) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Get a buffer of the requested size, but put a sanity
	 * cap on it.
	 */
	len = m->m_arg;
	if (len > 256) {
		len = 256;
	}
	if ((buf = malloc(len+1)) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Assemble as many names as will fit
	 */
	for (x = 0; x < len; ) {
		/*
		 * Find next directory entry.  Null name means
		 * it's an empty slot.
		 */
		while (((err = dir_copy(f->f_pos, &d)) == 0) &&
				(d.d_name[0] == '\0')) {
			f->f_pos += 1;
		}

		/*
		 * If error or EOF, return what we have
		 */
		if (err) {
			break;
		}

		/*
		 * If the next entry won't fit, back up the file
		 * position and return what we have.
		 */
		if ((x + strlen(d.d_name) + 1) >= len) {
			break;
		}

		/*
		 * Add entry and a newline
		 */
		strcat(buf+x, d.d_name);
		strcat(buf+x, "\n");
		x += (strlen(d.d_name)+1);
		f->f_pos += 1;
	}

	/*
	 * Send back results
	 */
	m->m_buf = buf;
	m->m_arg = m->m_buflen = x;
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	free(buf);
}

/*
 * bfs_read()
 *	Read bytes out of the current file or directory
 *
 * Directories get their own routine.
 */
void
bfs_read(struct msg *m, struct file *f)
{
	int x, step, cnt, blk, boff;
	struct inode *i;
	void *handle;
	struct dirent d;
	char *buf;

	/*
	 * Directory--only one is the root
	 */
	if (f->f_inode == ROOTINO) {
		bfs_readdir(m, f);
		return;
	}

	/*
	 * Get a snapshot of our dir entry.  It can't change (we're
	 * single-threaded), and we won't be modifying it.
	 */
	i = f->f_inode;
	if (dir_copy(i->i_num, &d)) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * EOF?
	 */
	if (f->f_pos >= d.d_len) {
		bfree(handle);
		m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Calculate # bytes to get
	 */
	cnt = m->m_arg;
	if (cnt > (d.d_len - f->f_pos)) {
		cnt = d.d_len - f->f_pos;
	}

	/*
	 * Get a buffer big enough to do the job
	 */
	buf = malloc(cnt);
	if (buf == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Loop across each block, putting our data into place
	 */
	for (x = 0; x < cnt; ) {
		/*
		 * Calculate how much to take out of current block
		 */
		boff = f->f_pos & (BLOCKSIZE-1);
		step = BLOCKSIZE - boff;
		if (step >= cnt) {
			step = cnt;
		}

		/*
		 * Map current block
		 */
		blk = f->f_pos / BLOCKSIZE;
		handle = bget(d.d_start+blk);
		if (!handle) {
			free(buf);
			msg_err(m->m_sender, strerror());
			return;
		}
		memcpy(buf+x, bdata(handle)+boff, step);
		bfree(handle);

		/*
		 * Advance to next chunk
		 */
		x += step;
	}

	/*
	 * Send back reply
	 */
	m->m_buf = buf;
	m->m_arg = m->m_buflen = cnt;
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	free(buf);
}
