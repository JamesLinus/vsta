/*
 * stat.c
 *	Do the stat function
 */
#include "sema.h"
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <string.h>
#include <stdio.h>
#include <std.h>

extern char *perm_print();

/*
 * sema_stat()
 *	Do stat
 */
void
sema_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];
	uint len, owner;
	struct openfile *o;

	/*
	 * Verify access
	 */
	if (!(f->f_perm & (ACC_READ | ACC_CHMOD))) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Calculate length
	 */
	o = f->f_file;
	if (!o) {
		/*
		 * Root dir--# files in dir
		 */
		len = nfiles;
		owner = 0;
	} else {
		/*
		 * File--queue length
		 */
		len = (o->o_count < 0) ? (- o->o_count) : 0;
		owner = o->o_owner;
	}
	sprintf(buf, "size=%d\ntype=%c\nowner=%d\ninode=%lu\n",
		len, o ? 'f' : 'd', owner, (ulong)o);
	if (o) {
		strcat(buf, perm_print(&o->o_prot));
		sprintf(buf + strlen(buf), "count=%d\n", o->o_count);
		sprintf(buf + strlen(buf), "name=%u\n", o->o_iname);
	} else {
		sprintf(buf+strlen(buf), "perm=1\nacc=%d/%d\n",
			ACC_READ | ACC_WRITE, ACC_CHMOD);
	}
	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * sema_wstat()
 *	Allow writing of supported stat messages
 */
void
sema_wstat(struct msg *m, struct file *f)
{
	char *field, *val;
	struct openfile *o = f->f_file;

	/*
	 * Can't fiddle the root dir
	 */
	if (o == 0) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Check permissions
	 */
	if (!(f->f_perm & (ACC_WRITE | ACC_CHMOD))) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &o->o_prot, f->f_perm, &field, &val) == 0)
		return;

	/*
	 * Set semaphore count, let through anybody possible due to
	 * new count
	 */
	if (!strcmp(field, "count")) {
		o->o_count = atoi(val);
		process_queue(o);
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
