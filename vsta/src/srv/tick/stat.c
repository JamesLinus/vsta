/*
 * stat.c
 *	Do the stat function
 */
#include "tick.h"
#include <sys/param.h>
#include <sys/fs.h>
#include <string.h>
#include <stdio.h>
#include <std.h>

/*
 * tick_wstat()
 *	Allow writing of supported stat messages
 */
void
tick_wstat(struct msg *m, struct file *f)
{
	char *field, *val;

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, NULL, ACC_CHMOD, &field, &val) == 0)
		return;

	/*
	 * Start participating in the select() protocol.
	 * TBD: stop ignoring host name; tabulate select server connections
	 * under this name, rather than one connection per client.
	 */
	if (!strcmp(field, "select")) {
		extern port_t path_open();

		if (f->f_mask) {
			msg_err(m->m_sender, EBUSY);
			return;
		}
		if ((sscanf(val, "%u,%ld,%d,%lu,%*s",
				&f->f_mask, &f->f_clid, &f->f_fd,
				&f->f_key) != 4) || (!f->f_mask)) {
			msg_err(m->m_sender, EINVAL);
			return;
		}
		f->f_sentry = ll_insert(&selectors, f);
		if (f->f_sentry == 0) {
			f->f_mask = 0;
			msg_err(m->m_sender, ENOMEM);
			return;
		}
		f->f_selfs = path_open("//fs/select", 0);
		if (f->f_selfs < 0) {
			f->f_mask = 0;
			ll_delete(f->f_sentry);
			msg_err(m->m_sender, strerror());
			return;
		}
		f->f_needsel = 1;
		f->f_iocount = 0;

	/*
	 * Client no longer uses select() on this connection
	 */
	} else if (!strcmp(field, "unselect")) {
		f->f_mask = 0;
		f->f_needsel = 0;
		ll_delete(f->f_sentry);
		f->f_sentry = 0;
		(void)msg_disconnect(f->f_selfs);

	} else {
		/*
		 * Not a field we support
		 */
		msg_err(m->m_sender, EINVAL);
	}

	/*
	 * Common success
	 */
	m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}
