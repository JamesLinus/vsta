/*
 * Filename:	rw.c
 * Developed:	Dave Hudson <dave@humbug.demon.co.uk>
 * Originated:	Andy Valencia
 * Last Update: 11th February 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description: Routines for operating on the data in a file
 */


#include <std.h>
#include <stdio.h>
#include <sys/param.h>
#include "bfs.h"


extern struct super *sblock;


/*
 * do_write()
 *	Local routine to loop over a buffer and write it to a file
 *
 * Returns 0 on success, 1 on error.
 */
static int
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
		boff = pos & (BLOCKSIZE - 1);
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
		memcpy((char *)bdata(handle) + boff, buf + x, step);
		pos += step;
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
	struct inode *i = f->f_inode;

	/*
	 * Can only write to a true file, and only if open for writing.
	 */
	if ((i->i_num == ROOTINODE) || !f->f_write) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * See if the file's going to be able to hold all the data.  We
	 * do not necessarily need to allocate space if we're rewriting
	 * an existing file.
	 */
	if ((f->f_pos + m->m_buflen) > i->i_fsize) {
		if (blk_alloc(i, f->f_pos + m->m_buflen)) {
			msg_err(m->m_sender, ENOSPC);
			return;
		}
	}

	/*
	 * Copy out the buffer
	 */
	if (do_write(i->i_start, f->f_pos, m->m_buf, m->m_buflen)) {
		msg_err(m->m_sender, strerror());
		return;
	}
	m->m_arg = m->m_buflen;
	f->f_pos += m->m_buflen;
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
	struct inode *i;
	char *buf;
	int x, len, err, ok = 1;

	/*
	 * Make sure it's the root directory
	 */
	if (f->f_inode->i_num != ROOTINODE) {
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
	if ((buf = malloc(len + 1)) == 0) {
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
		while (((err = (f->f_pos >= sblock->s_ndirents)) == 0)
			&& ok) {
			i = ino_find(f->f_pos);
			if ((i != NULL) && (i->i_name[0] != '\0')) {
				ok = 0;
			} else {
				f->f_pos += 1;
			}
		}
		ok = 1;

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
		if ((x + strlen(i->i_name) + 1) >= len) {
			break;
		}

		/*
		 * Add entry and a newline
		 */
		strcat(buf + x, i->i_name);
		strcat(buf + x, "\n");
		x += (strlen(i->i_name) + 1);
		f->f_pos += 1;
	}

	/*
	 * Send back results
	 */
	m->m_buf = buf;
	m->m_arg = m->m_buflen = x;
	m->m_nseg = ((x > 0) ? 1 : 0);
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
	char *buf;

	/*
	 * Directory--only one is the root
	 */
	if (f->f_inode->i_num == ROOTINODE) {
		bfs_readdir(m, f);
		return;
	}

	i = f->f_inode;

	/*
	 * EOF?
	 */
	if (f->f_pos >= i->i_fsize) {
		m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Calculate # bytes to get
	 */
	cnt = m->m_arg;
	if (cnt > (i->i_fsize - f->f_pos)) {
		cnt = i->i_fsize - f->f_pos;
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
		boff = f->f_pos & (BLOCKSIZE - 1);
		step = BLOCKSIZE - boff;
		if (step >= cnt) {
			step = cnt;
		}

		/*
		 * Map current block
		 */
		blk = f->f_pos / BLOCKSIZE;
		handle = bget(i->i_start + blk);
		if (!handle) {
			free(buf);
			msg_err(m->m_sender, strerror());
			return;
		}
		memcpy(buf + x, (char *)bdata(handle) + boff, step);
		f->f_pos += step;
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
