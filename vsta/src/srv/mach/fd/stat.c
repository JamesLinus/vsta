/*
 * stat.c
 *	Do the stat function
 */
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include "fd.h"

extern char *perm_print();
extern struct floppy *unit();

extern struct prot fd_prot;
extern struct fdparms fdparms[];

/*
 * fd_stat()
 *	Do stat
 */
void
fd_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];
	int size, ino;
	char type;

	if (!(f->f_flags & ACC_READ)) {
		msg_err(m->m_sender, EPERM);
		return;
	}
	if (f->f_unit == ROOTDIR) {
		size = NFD;
		ino = 0;
		type = 'd';
	} else {
		struct floppy *fl;

		fl = unit(f->f_unit);
		size = fdparms[fl->f_density].f_size * SECSZ,
		ino = f->f_unit+1;
		type = 's';
	}
	sprintf(buf,
	 "size=%d\ntype=%c\nowner=0\ninode=%d\n", size, type, ino);
	strcat(buf, perm_print(&fd_prot));
	m->m_buf = buf;
	m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * fd_wstat()
 *	Allow writing of supported stat messages
 */
void
fd_wstat(struct msg *m, struct file *f)
{
	char *field, *val;

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &fd_prot, f->f_flags, &field, &val) == 0) {
		return;
	}

	/*
	 * Otherwise forget it
	 */
	msg_err(m->m_sender, EINVAL);
}
