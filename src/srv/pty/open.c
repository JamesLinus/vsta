/*
 * open.c
 *	Routines for opening and closing files
 */
#include <std.h>
#include <sys/assert.h>
#include <ctype.h>
#include "pty.h"

/*
 * pty_open()
 *	Main entry for processing an open message
 */
void
pty_open(struct msg *m, struct file *f)
{
	struct pty *pty;
	uint x, idx, master;
	char *p;

	/*
	 * Have to be in root dir to open down into a file
	 */
	if (f->f_file) {
		msg_err(m->m_sender, ENOTDIR);
		return;
	}

	/*
	 * No subdirs in a pty filesystem
	 */
	if (m->m_arg & ACC_DIR) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Look up name
	 */
	p = m->m_buf;
	if ((strncmp(p, "pty", 3) && strncmp(p, "tty", 3)) ||
			(strlen(p) != 4)) {
		msg_err(m->m_sender, ESRCH);
		return;
	}
	if (isdigit(p[3])) {
		idx = p[3] - '0';
	} else if (islower(p[3])) {
		idx = p[3] - 'a';
	} else {
		idx = NPTY;	/* No match */
	}
	if (idx >= NPTY) {
		msg_err(m->m_sender, ESRCH);
		return;
	}
	pty = &ptys[idx];
	master = (p[0] == 'p');

	/*
	 * Can't open a pty if it's still open and being used
	 * by somebody else.
	 */
	if (master) {
		struct prot *prot;

		if (pty->p_nmaster || pty->p_nslave) {
			msg_err(m->m_sender, EBUSY);
			return;
		}
		pty->p_nmaster = 1;
		ioq_init(&pty->p_ioqr);
		ioq_init(&pty->p_ioqw);
		pty->p_rows = 25;	/* Restore defaults */
		pty->p_cols = 80;
		ll_init(&pty->p_selectors);

		/*
		 * Propagate protection from pty's master
		 */
		prot = &pty->p_prot;
		bzero(prot, sizeof(*prot));
		prot->prot_len = PERM_LEN(&f->f_perms[0]);
		bcopy(f->f_perms[0].perm_id, prot->prot_id, PERMLEN);
		prot->prot_bits[prot->prot_len-1] =
			ACC_READ | ACC_WRITE | ACC_CHMOD;
		pty->p_owner = f->f_perms[0].perm_uid;
		f->f_perm = ACC_READ | ACC_WRITE | ACC_CHMOD;
	} else {
		/*
		 * Can't access if there isn't a master
		 */
		if (pty->p_nmaster == 0) {
			msg_err(m->m_sender, EIO);
			return;
		}

		/*
		 * Check permission
		 */
		x = perm_calc(f->f_perms, f->f_nperm, &pty->p_prot);
		if ((m->m_arg & x) != m->m_arg) {
			msg_err(m->m_sender, EPERM);
			return;
		}
		f->f_perm = m->m_arg | ACC_CHMOD;
		pty->p_nslave += 1;
	}


	/*
	 * Move to this file and return success
	 */
	f->f_file = pty;
	f->f_master = master;
	m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * abort_clients()
 *	Wake up anybody waiting for a master who's gone
 */
static void
abort_clients(struct pty *pty)
{
	ioq_abort(&pty->p_ioqr);
	ioq_abort(&pty->p_ioqw);
}

/*
 * pty_close()
 *	Do closing actions on a file
 */
void
pty_close(struct file *f)
{
	struct pty *pty;

	/*
	 * Nothing to be done on directory
	 */
	pty = f->f_file;
	if (pty == 0) {
		return;
	}

	/*
	 * Remove from any pending list
	 */
	if (f->f_q) {
		ll_delete(f->f_q);
	}

	/*
	 * Free a ref.  No more clients--free node.
	 */
	if (f->f_master) {
		/*
		 * Kill off pending I/O when the master departs
		 */
		if (pty->p_nmaster == 0) {
			abort_clients(pty);
		}
	}
}
