/*
 * rw.c
 *	Routines for reading and writing swap
 *
 * Swap only support reads/writes of whole pages.
 */
#include <sys/param.h>
#include <sys/swap.h>
#include <std.h>

extern struct swapmap *swapent();

/*
 * swap_rw()
 *	Look up underlying swap device, forward operation
 */
void
swap_rw(struct msg *m, struct file *f, uint bytes)
{
	struct swapmap *s;
	uint blk;

	/*
	 * Check for permission, page alignment
	 */
	if (((m->m_op == FS_WRITE) || (m->m_op == FS_ABSWRITE)) &&
			!(f->f_perms & ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
		return;
	}
	if ((m->m_nseg != 1) || (bytes & (NBPG-1)) ||
			(f->f_pos & (NBPG-1))) {
		msg_err(m->m_sender, EINVAL);
		return;
	}
	blk = btop(f->f_pos);

	/*
	 * Find entry for next part of I/O
	 */
	if ((s = swapent(blk)) == 0) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Convert offset relative to beginning of this chunk of
	 * swap space.
	 */
	m->m_arg1 = ptob(blk - s->s_block + s->s_off);

	/*
	 * Send off the I/O
	 */
	if (msg_send(s->s_port, m) < 0) {
		msg_err(m->m_sender, strerror());
		return;
	}
	m->m_buflen = m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}
