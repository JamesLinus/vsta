/*
 * stat.c
 *	Do the stat function
 */
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "par.h"
#include "par_port.h"

extern char *perm_print();

extern struct prot par_prot;
extern uint accgen;
extern struct par_port printer;
extern int timeout;

/*
 * par_stat()
 *	Do stat
 */
void
par_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];

	if (!(f->f_flags & ACC_READ)) {
		msg_err(m->m_sender, EPERM);
		return;
	}
	sprintf(buf,
	 "size=0\ntype=c\nowner=0\ninode=0\ngen=%d\n"
	 "timeout=%d\nquiet=%s\n",
	 accgen, timeout, printer.quiet ? "true" : "false");
	strcat(buf, perm_print(&par_prot));
	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * par_wstat()
 *	Allow writing of supported stat messages
 */
void
par_wstat(struct msg *m, struct file *f)
{
	char *field, *val;
	int i;

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &par_prot, f->f_flags, &field, &val) == 0)
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
	} else if (!strcmp(field, "timeout")) {
		if (val) {
			i = atoi(val);
			if (i >= 0) {
				timeout = i;
			} else {
				msg_err(m->m_sender, EINVAL);
				return;
			}
		} else {
			msg_err(m->m_sender, EINVAL);
			return;
		}
	} else if (!strcmp(field, "quiet")) {
		if (!strcmp(val, "true")) {
			printer.quiet = 1;
		} else if (!strcmp(val, "false")) {
			printer.quiet = 0;
		} else {
			msg_err(m->m_sender, EINVAL);
			return;
		}
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
