/*
 * open.c
 *	Routines for walking into client or server nodes
 */
#include "selfs.h"

/*
 * selfs_open()
 *	Main entry for processing an open message
 */
void
selfs_open(struct msg *m, struct file *f)
{
	/*
	 * Have to be in root dir to open down into a file
	 */
	if (f->f_mode != MODE_ROOT) {
		msg_err(m->m_sender, ENOTDIR);
		return;
	}

	/*
	 * No subdirs or new files
	 */
	if (m->m_arg & (ACC_DIR | ACC_CREATE)) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Look for "client" or "server"
	 */
	if (!strcmp(m->m_buf, "client")) {
		f->f_mode = MODE_CLIENT;
	} else if (!strcmp(m->m_buf, "server")) {
		f->f_mode = MODE_SERVER;
	} else {
		/*
		 * No other nodes can exist
		 */
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * OK.
	 */
	m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}
