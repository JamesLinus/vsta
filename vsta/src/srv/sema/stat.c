/*
 * stat.c
 *	Do the stat function
 */
#include "sema.h"
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <llist.h>
#include <string.h>
#include <stdio.h>

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
	if (!(f->f_perm & ACC_READ)) {
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
		struct llist *l;

		/*
		 * File--queue length
		 */
		len = 0;
		for (l = LL_NEXT(&o->o_queue); l != &o->o_queue;
				l = LL_NEXT(l)) {
			len += 1;
		}
		owner = o->o_owner;
	}
	sprintf(buf, "size=%d\ntype=%c\nowner=%d\ninode=%u\n",
		len, f->f_file ? 'f' : 'd', owner, o);
	if (o) {
		strcat(buf, perm_print(&o->o_prot));
		sprintf(buf + strlen(buf),
			"held=%d\nqwrite=%d\nwriting=%d\n",
			o->o_holder, o->o_nwrite, o->o_writing);
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

	/*
	 * Can't fiddle the root dir
	 */
	if (f->f_file == 0) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &f->f_file->o_prot, f->f_perm, &field, &val) == 0)
		return;

	/*
	 * Not a field we support...
	 */
	msg_err(m->m_sender, EINVAL);
}
