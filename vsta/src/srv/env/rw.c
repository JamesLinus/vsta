/*
 * rw.c
 *	Read and write functions
 *
 * Our objects hold strings of up to a certain length, defined below.
 */
#include <lib/llist.h>
#include <env/env.h>
#include <sys/fs.h>
#include <std.h>

#define MAX_STRING (1024)	/* 1K should be enough? */

/*
 * env_write()
 *	Write to an open file
 */
void
env_write(struct msg *m, struct file *f, int len)
{
	struct node *n = f->f_node;
	uint newlen, oldlen, x;
	char *buf;
	struct string *str;

	/*
	 * Can only write to a true file, and only if open for writing.
	 */
	if ((n->n_internal) || !(f->f_mode & ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Have to have buffer, make sure it's null-terminated
	 */
	newlen = f->f_pos + len + 1;
	oldlen = strlen(n->n_val->s_val)+1;
	if (newlen >= MAX_STRING) {
		msg_err(m->m_sender, E2BIG);
		return;
	}
	if (newlen < oldlen) {
		newlen = oldlen;
	}
	if ((buf = malloc(newlen)) == 0) {
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Transfer old contents, tack new stuff on end
	 */
	strcpy(buf, n->n_val->s_val);
	seg_copyin(m->m_seg, m->m_nseg,
		buf + f->f_pos, newlen - f->f_pos);
	buf[newlen] = '\0';

	/*
	 * Free old string storage, put ours in its place
	 */
	free(n->n_val->s_val);
	n->n_val->s_val = buf;

	/*
	 * Success
	 */
	m->m_buflen = m->m_arg1 = m->m_nseg = 0;
	m->m_arg = len;
	msg_reply(m->m_sender, m);
	f->f_pos += len;
}

/*
 * env_readdir()
 *	Do reads on directory entries
 */
static void
env_readdir(struct msg *m, struct file *f)
{
	int x, len;
	char *buf;
	struct node *n = f->f_node;
	struct llist *l;

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
	 * Find next directory entry.  We use our position
	 * as a count for scanning forward, and consider
	 * EOF to happen when we wrap to our start.
	 */
	x = 0;
	buf[0] = '\0';
	l = n->n_elems.l_forw;
	while (x < f->f_pos) {
		/*
		 * Check "EOF"
		 */
		if (l == &n->n_elems)
			break;

		/*
		 * Advance
		 */
		l = l->l_forw;
		++x;
	}

	/*
	 * Assemble as many names as we have and will fit
	 */
	for (x = 0; x < len; ) {
		struct node *n2;


		/*
		 * If EOF, return what we have
		 */
		if (l == &n->n_elems) {
			break;
		}

		/*
		 * If the next entry won't fit, back up the file
		 * position and return what we have.
		 */
		n2 = l->l_data;
		if ((x + strlen(n2->n_name) + 1) >= len) {
			break;
		}

		/*
		 * Add entry and a newline
		 */
		strcat(buf+x, n2->n_name);
		strcat(buf+x, "\n");
		x += (strlen(n2->n_name)+1);
		f->f_pos += 1;
		l = l->l_forw;
	}

	/*
	 * Send back results
	 */
	m->m_buf = buf;
	m->m_arg = m->m_buflen = x;
	m->m_nseg = (x ? 1 : 0);
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	free(buf);
}

/*
 * env_read()
 *	Read bytes out of the current file or directory
 *
 * Internal nodes get their own routine.
 */
void
env_read(struct msg *m, struct file *f, int len)
{
	int cnt;
	char *buf;
	struct node *n = f->f_node;

	/*
	 * Directory
	 */
	if (n->n_internal) {
		env_readdir(m, f);
		return;
	}

	/*
	 * Generate our "contents"
	 */
	buf = n->n_val->s_val;
	buf += f->f_pos;

	/*
	 * Calculate # bytes to get
	 */
	cnt = m->m_arg;
	if (cnt > strlen(buf)) {
		cnt = strlen(buf);
	}

	/*
	 * EOF?
	 */
	if (cnt <= 0) {
		m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Send back reply
	 */
	m->m_buf = buf;
	m->m_arg = m->m_buflen = cnt;
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	f->f_pos += cnt;
}
