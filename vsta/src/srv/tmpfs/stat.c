/*
 * stat.c
 *	Do the stat function
 *
 * We also lump the chmod/chown stuff here as well
 */
#include <tmpfs/tmpfs.h>
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <lib/llist.h>

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
	sprintf(buf, "size=%d\ntype=%c\nowner=%d\ninode=%u\n",
		len, f->f_file ? 'f' : 'd', owner, o);
	strcat(buf, perm_print(&o->o_prot));
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
