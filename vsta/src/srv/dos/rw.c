/*
 * rw.c
 *	Routines for operating on the data in a file
 */
#include <sys/fs.h>
#include <dos/dos.h>
#include <std.h>
#include <ctype.h>
#include <sys/assert.h>

/*
 * do_write()
 *	Local routine to loop over a buffer and write it to a file
 *
 * Returns 0 on success, 1 on error.
 */
static
do_write(struct clust *c, int pos, char *buf, int cnt)
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
		handle = bget(c->c_clust[blk]);
		if (!handle) {
			return 1;
		}
		memcpy((char *)bdata(handle)+boff, buf+x, step);
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
 * dos_write()
 *	Write to an open file
 */
void
dos_write(struct msg *m, struct file *f)
{
	void *handle;
	struct node *n = f->f_node;
	ulong newlen;

	/*
	 * Can only write to a true file, and only if open for writing.
	 */
	if ((n->n_type == T_DIR) || !(f->f_perm & ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * See if the file's going to be able to hold all the data.  We
	 * do not necessarily need to allocate space if we're rewriting
	 * an existing file.
	 */
	newlen = f->f_pos + m->m_buflen;
	if (newlen > n->n_len) {
		if (clust_setlen(n->n_clust, newlen)) {
			msg_err(m->m_sender, ENOSPC);
			return;
		}
		n->n_len = newlen;
	}
	n->n_flags |= N_DIRTY;

	/*
	 * Copy out the buffer
	 */
	if (do_write(n->n_clust, f->f_pos, m->m_buf, m->m_buflen)) {
		msg_err(m->m_sender, strerror());
		return;
	}
	m->m_arg = m->m_buflen;
	f->f_pos += m->m_buflen;
	m->m_buflen = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * pack_name()
 *	Pack a DOS name into a UNIX-ish format
 */
void
pack_name(char *name, char *ext, char *file)
{
	char *p;

	for (p = name; (p < name+8) && (*p != ' '); ++p) {
		*file++ = tolower(*p);
	}
	*file++ = '.';
	for (p = ext; (p < ext+3) && (*p != ' '); ++p) {
		*file++ = tolower(*p);
	}
	*file++ = '\0';
}

/*
 * dos_readdir()
 *	Do reads on directory entries
 */
static void
dos_readdir(struct msg *m, struct file *f)
{
	char *buf;
	uint len, x;
	struct directory d;
	struct node *n = f->f_node;
	char file[14];

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
	buf[0] = '\0';

	/*
	 * Skip "." and "..", which exist only in non-root directories
	 */
	if ((n != rootdir) && (f->f_pos == 0)) {
		f->f_pos = 2;
	}

	/*
	 * Assemble as many names as will fit
	 */
	for (x = 0; x < len; ) {
		uint c;

		/*
		 * Look at the slot at f_pos.  For reads of directories
		 * f_pos is simply the struct directory index.  Leave
		 * loop on failure, presumably from EOF.
		 */
		if (dir_copy(n, f->f_pos++, &d)) {
			break;
		}

		/*
		 * Leave after last entry, skip deleted entries
		 */
		c = (d.name[0] & 0xFF);
		if (!c) {
			break;
		}
		if (c == 0xe5) {
			continue;
		}

		/*
		 * If the next entry won't fit, back up the file
		 * position and return what we have.
		 */
		pack_name(d.name, d.ext, file);
		if ((x + strlen(file) + 1) >= len) {
			f->f_pos -= 1;
			break;
		}

		/*
		 * Add entry and a newline
		 */
		strcat(buf+x, file);
		strcat(buf+x, "\n");
		x += (strlen(file)+1);
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
 * dos_read()
 *	Read bytes out of the current file or directory
 *
 * Directories get their own routine.
 */
void
dos_read(struct msg *m, struct file *f)
{
	int x, step, cnt, blk, boff;
	struct node *n = f->f_node;
	void *handle;
	char *buf;
	struct clust *c = n->n_clust;

	/*
	 * Directory
	 */
	if (n->n_type == T_DIR) {
		dos_readdir(m, f);
		return;
	}

	/*
	 * EOF?
	 */
	if (f->f_pos >= n->n_len) {
		m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}
	ASSERT_DEBUG(c->c_clust, "dos_read: len !clust");
	ASSERT_DEBUG(c->c_nclust > 0, "dos_read: clust !nclust");

	/*
	 * Calculate # bytes to get
	 */
	cnt = m->m_arg;
	if (cnt > (n->n_len - f->f_pos)) {
		cnt = n->n_len - f->f_pos;
	}

	/*
	 * Get a buffer big enough to do the job
	 * XXX user scatter-gather, this is a waste
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
		if (step >= (cnt-x)) {
			step = (cnt-x);
		}

		/*
		 * Map current block
		 */
		blk = f->f_pos / BLOCKSIZE;
		ASSERT_DEBUG(blk < c->c_nclust, "dos_read: bad blk");
		handle = bget(c->c_clust[blk]);
		if (!handle) {
			free(buf);
			msg_err(m->m_sender, strerror());
			return;
		}
		bcopy((char *)bdata(handle)+boff, buf+x, step);
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
	m->m_nseg = (cnt ? 1 : 0);
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	free(buf);
}
