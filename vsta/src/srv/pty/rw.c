/*
 * rw.c
 *	Routines for operating on the data in a file
 *
 * Reading and writing of data, by either master or slave of PTY.
 * Also listing of directory of PTY nodes.
 */
#include <hash.h>
#include <std.h>
#include <sys/assert.h>
#include "pty.h"

/*
 * pty_abort()
 *	Caller has requested abort of operation
 */
void
pty_abort(struct msg *m, struct file *f)
{
	/*
	 * Always answer a zero-length message
	 */
	m->m_nseg = m->m_arg = m->m_arg1 = 0;

	/*
	 * Remove any pending I/O
	 */
	if (f->f_q) {
		ll_delete(f->f_q);
		f->f_q = 0;
	}

	/*
	 * Answer completion
	 */
	msg_reply(m->m_sender, m);
}

/*
 * pty_write()
 *	Write to an open file
 */
void
pty_write(struct msg *m, struct file *f)
{
	struct pty *pty = f->f_file;

	/*
	 * Can only write to a true file, and only if open for writing.
	 */
	if (!pty || !(f->f_perm & ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Push data onto the appropriate I/O queue
	 */
	ioq_add_data(f, f->f_master ? &pty->p_ioqw : &pty->p_ioqr, m);
}

/*
 * pty_readdir()
 *	Do reads on directory entries
 */
static void
pty_readdir(struct msg *m, struct file *f)
{
	int count;

	/*
	 * If at end of directory, null length
	 */
	if (f->f_pos >= ptydirlen) {
		m->m_arg = m->m_arg1 = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * See how much is left
	 */
	count = ptydirlen - f->f_pos;
	if (count > m->m_arg) {
		count = m->m_arg;
	}

	/*
	 * Send back some data
	 */
	m->m_arg = count;
	m->m_arg1 = 0;
	m->m_nseg = 1;
	m->m_buf = ptydir + f->f_pos;
	m->m_buflen = count;
	msg_reply(m->m_sender, m);

	f->f_pos += count;
}

/*
 * pty_read()
 *	Read bytes out of the current pty or directory
 *
 * Directories get their own routine.
 */
void
pty_read(struct msg *m, struct file *f)
{
	struct pty *pty;

	/*
	 * Directory--only one is the root
	 */
	if ((pty = f->f_file) == NULL) {
		pty_readdir(m, f);
		return;
	}

	/*
	 * Access?
	 */
	if (!(f->f_perm & ACC_READ)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * If master is gone, return EOF
	 */
	if (pty->p_nmaster == 0) {
		m->m_arg = m->m_arg1 = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Queue as a reader
	 */
	ioq_read_data(f, f->f_master ? &pty->p_ioqr : &pty->p_ioqw, m);
}
