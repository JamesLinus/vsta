/*
 * rw.c
 *	Routines for reading and writing swap
 *
 * Swap only support reads/writes of whole pages.
 */
#include <sys/param.h>
#include <swap/swap.h>

extern char *strerror();
extern struct swapmap *swapent();

/*
 * swap_rw()
 *	Look up underlying swap device, forward operation
 */
void
swap_rw(struct msg *m, struct file *f, uint bytes)
{
	struct swapmap *s;

	/*
	 * Check for permission, page alignment
	 */
	if ((m->m_op == FS_WRITE) && !(f->f_perms & ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
		return;
	}
	if (bytes & (NBPG-1)) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Find entry for next part of I/O
	 */
	if ((s = swapent(f->f_pos)) == 0) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

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
