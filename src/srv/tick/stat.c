/*
 * stat.c
 *	Do the stat function
 */
#include "tick.h"
#include <sys/fs.h>
#include <stddef.h>

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
	 * Start perhaps participating in the select() protocol.
	 */
	if (sc_wstat(m, &f->f_selfs, field, val) == 0) {
		if (f->f_selfs.sc_mask && !f->f_sentry) {
			f->f_sentry = ll_insert(&selectors, f);
		}
		return;
	}

	/*
	 * Not a field we support
	 */
	msg_err(m->m_sender, EINVAL);
}
