/*
 * stat.c
 *	Do the stat function
 *
 * We also lump the chmod/chown stuff here as well
 */
#define _NAMER_H_INTERNAL
#include <sys/namer.h>
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <llist.h>
#include <string.h>
#include <stdio.h>

extern char *perm_print(struct prot *);

/*
 * namer_stat()
 *	Do stat
 */
void
namer_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];
	char buf2[8];
	struct node *n = f->f_node;
	int len;
	struct llist *l;

	/*
	 * Verify access
	 */
	if (!(f->f_mode & ACC_READ)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Calculate length
	 */
	if (n->n_internal) {
		len = 0;
		l = n->n_elems.l_forw;
		while (l != &n->n_elems) {
			len += 1;
			l = l->l_forw;
		}
	} else {
		sprintf(buf2, "%d", n->n_port);
		len = strlen(buf2);
	}
	sprintf(buf, "size=%d\ntype=%c\nowner=%d\ninode=%u\n",
		len, n->n_internal ? 'd' : 'f', n->n_owner, n);
	strcat(buf, perm_print(&n->n_prot));
	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * namer_wstat()
 *	Allow writing of supported stat messages
 */
void
namer_wstat(struct msg *m, struct file *f)
{
	char *field, *val;

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &f->f_node->n_prot, f->f_mode, &field, &val) == 0)
		return;

	/*
	 * Not a field we support...
	 */
	msg_err(m->m_sender, EINVAL);
}
