/*
 * stat.c
 *	Do the stat function
 *
 * We also lump the chmod/chown stuff here as well
 */
#include "env.h"
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>

extern char *perm_print();

/*
 * env_stat()
 *	Do stat
 */
void
env_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];
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
	if (DIR(n)) {
		len = 0;
		l = LL_NEXT(&n->n_elems);
		while (l != &n->n_elems) {
			len += 1;
			l = LL_NEXT(l);
		}
	} else {
		len = strlen(n->n_val->s_val);
	}
	sprintf(buf, "size=%d\ntype=%c\nowner=%d\ninode=%u\n",
		len, DIR(n) ? 'd' : 'f', n->n_owner, n);
	strcat(buf, perm_print(&n->n_prot));
	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * env_fork()
 *	Spawn off a copy-on-write node
 */
static void
env_fork(struct file *f)
{
	struct node *oldhome = f->f_home;

	/*
	 * Leave current group, start a new one
	 */
	f->f_back->f_forw = f->f_forw;
	f->f_forw->f_back = f->f_back;
	f->f_forw = f->f_back = f;

	/*
	 * Set up copy-on-write of home node if present
	 */
	if (oldhome) {
		f->f_home = clone_node(oldhome);
		deref_node(oldhome);
		/* clone_node has put first reference in place */
	}

	/*
	 * Set current node as needed.  If the current node of our
	 * old node was his "home" node, then we switch to our *own*
	 * home node instead of pointing at his.
	 */
	if (f->f_node == oldhome) {
		deref_node(f->f_node);
		if (f->f_home) {
			f->f_node = f->f_home;
		} else {
			extern struct node rootnode;

			/*
			 * Couldn't copy, just switch to root
			 */
			f->f_node = &rootnode;
		}
		ref_node(f->f_node);
	}
}

/*
 * env_wstat()
 *	Allow writing of supported stat messages
 */
void
env_wstat(struct msg *m, struct file *f)
{
	char *field, *val;

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &f->f_node->n_prot, f->f_mode, &field, &val) == 0) {
		return;
	}

	/*
	 * Clone off our home node?
	 */
	if (!strcmp(field, "fork")) {
		env_fork(f);
		m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Not a field we support...
	 */
	msg_err(m->m_sender, EINVAL);
}
