/*
 * stat.c
 *	Do the stat function
 */
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <server.h>
#include <stdio.h>
#include <string.h>
#include <std.h>
#include "pty.h"

/*
 * update_select()
 *	Send out select() notifications as appropriate
 */
void
update_select(struct pty *pty)
{
	uint event;
	struct llist *start, *l;
	struct file *f;

	/*
	 * Short circuit if select() not active
	 */
	start = &pty->p_selectors;
	if (LL_EMPTY(start)) {
		return;
	}

	/*
	 * Generate events for the master first
	 */
	event = 0;
	if (pty->p_ioqr.ioq_flags & IOQ_READABLE) {
		event |= ACC_READ;
		pty->p_ioqr.ioq_flags &= ~IOQ_READABLE;
	}
	if (pty->p_ioqw.ioq_flags & IOQ_WRITABLE) {
		event |= ACC_WRITE;
		pty->p_ioqw.ioq_flags &= ~IOQ_WRITABLE;
	}
	if (event) {
		for (l = LL_NEXT(start); l != start;
				l = LL_NEXT(l)) {
			f = l->l_data;
			if (f->f_master) {
				sc_event(&f->f_selfs, event);
			}
		}
	}

	/*
	 * Now slave events
	 */
	if (pty->p_ioqw.ioq_flags & IOQ_READABLE) {
		event |= ACC_READ;
		pty->p_ioqw.ioq_flags &= ~IOQ_READABLE;
	}
	if (pty->p_ioqr.ioq_flags & IOQ_WRITABLE) {
		event |= ACC_WRITE;
		pty->p_ioqr.ioq_flags &= ~IOQ_WRITABLE;
	}
	if (event) {
		for (l = LL_NEXT(start); l != start;
				l = LL_NEXT(l)) {
			f = l->l_data;
			if (!f->f_master) {
				sc_event(&f->f_selfs, event);
			}
		}
	}
}

/*
 * pty_stat()
 *	Do stat
 */
void
pty_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];
	uint len, owner;
	struct pty *pty;

	/*
	 * Verify access
	 */
	if (!(f->f_perm & ACC_READ)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Calculate length
	 */
	pty = f->f_file;
	if (!pty) {
		len = NPTY;
		owner = 0;
	} else {
		len = 0;
		owner = pty->p_owner;
	}
	sprintf(buf, "size=%d\ntype=%s\nowner=%d\ninode=%lu\n",
		len, pty ? "c" : "d", owner, (ulong)pty);
	if (pty) {
		char buf2[128];

		strcat(buf, perm_print(&pty->p_prot));
		sprintf(buf2, "inbuf=%u\noutbuf=%u\nrows=%u\ncols=%u\n",
			pty->p_ioqw.ioq_nbuf,
			pty->p_ioqr.ioq_nbuf,
			pty->p_rows, pty->p_cols);
		strcat(buf, buf2);
	} else {
		sprintf(buf + strlen(buf), "perm=1\nacc=%d/%d\n",
			ACC_READ | ACC_WRITE, ACC_CHMOD);
	}
	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * pty_wstat()
 *	Allow writing of supported stat messages
 */
void
pty_wstat(struct msg *m, struct file *f)
{
	char *field, *val;
	struct pty *pty = f->f_file;

	/*
	 * Can't fiddle the root dir
	 */
	if (pty == NULL) {
		msg_err(m->m_sender, EINVAL);
	}

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &pty->p_prot, f->f_perm, &field, &val) == 0)
		return;

	/*
	 * select() setup?
	 */
	if (sc_wstat(m, &f->f_selfs, field, val) == 0) {
		f->f_sentry = ll_insert(&pty->p_selectors, f);
		update_select(pty);
		return;
	}

	/*
	 * Set geometry
	 */
	if (!strcmp(field, "rows")) {
		pty->p_rows = atoi(val);
	} else if (!strcmp(field, "cols")) {
		pty->p_cols = atoi(val);

	} else {
		/*
		 * Not a field we support...
		 */
		msg_err(m->m_sender, EINVAL);
	}

	/*
	 * Ok.
	 */
	m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}
