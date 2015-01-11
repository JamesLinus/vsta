/*
 * stat.c
 *	Do the stat function
 *
 * We also lump the chmod/chown stuff here as well
 */
#include "tmpfs.h"
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <llist.h>
#include <string.h>
#include <stdio.h>

extern char *perm_print();

/*
 * tmpfs_stat()
 *	Do stat
 */
void
tmpfs_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];
	uint len, owner;
	struct openfile *o;

	/*
	 * Calculate length
	 */
	o = f->f_file;
	if (!o) {
		struct llist *l;
		extern struct llist files;

		/*
		 * Root dir--# files in dir
		 */
		len = 0;
		for (l = LL_NEXT(&files); l != &files; l = LL_NEXT(l)) {
			len += 1;
		}
		owner = 0;
	} else {
		/*
		 * File--its byte length
		 */
		len = o->o_len;
		owner = o->o_owner;
	}
	sprintf(buf, "size=%d\ntype=%c\nowner=%d\ninode=%lu\n",
		len, f->f_file ? 'f' : 'd', owner, (ulong)o);
	if (o) {
		strcat(buf, perm_print(&o->o_prot));
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
 * tmpfs_wstat()
 *	Allow writing of supported stat messages
 */
void
tmpfs_wstat(struct msg *m, struct file *f)
{
	char *field, *val;

	/*
	 * Can't fiddle the root dir
	 */
	if (f->f_file == 0) {
		msg_err(m->m_sender, EINVAL);
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

/*
 * tmpfs_fid()
 *	Return file ID/size for kernel caching of mappings
 */
void
tmpfs_fid(struct msg *m, struct file *f)
{
	struct openfile *o = f->f_file;

	/*
	 * Only files get an ID
	 */
	if (o == 0) {
		msg_err(m->m_sender, EINVAL);
		return;
	}
	m->m_arg = (ulong)o;
	m->m_arg1 = o->o_len;
	m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}
