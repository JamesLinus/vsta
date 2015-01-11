/*
 * stat.c
 *	Do the stat function
 *
 * We also lump the chmod/chown stuff here as well
 */
#include <mach/kbd.h>
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>

extern char *perm_print();

extern struct prot kbd_prot;
extern uint accgen, key_nbuf;

/*
 * kbd_stat()
 *	Do stat
 */
void
kbd_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];

	if (!(f->f_flags & ACC_READ)) {
		msg_err(m->m_sender, EPERM);
		return;
	}
	sprintf(buf,
	 "size=%d\ntype=c\nowner=0\ninode=0\ngen=%d\n",
		key_nbuf, accgen);
	strcat(buf, perm_print(&kbd_prot));
	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * kbd_wstat()
 *	Allow writing of supported stat messages
 */
void
kbd_wstat(struct msg *m, struct file *f)
{
	char *field, *val;

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &kbd_prot, f->f_flags, &field, &val) == 0)
		return;

	/*
	 * Process each kind of field we can write
	 */
	if (!strcmp(field, "gen")) {
		/*
		 * Set access-generation field
		 */
		if (val) {
			accgen = atoi(val);
		} else {
			accgen += 1;
		}
		f->f_gen = accgen;
	} else {
		/*
		 * Not a field we support...
		 */
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Return success
	 */
	m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}
