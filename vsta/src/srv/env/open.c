/*
 * open.c
 *	Routines for moving downwards in the hierarchy
 */
#include <sys/types.h>
#include <env/env.h>
#include <sys/fs.h>
#include <sys/assert.h>
#include <lib/llist.h>
#include <std.h>

/*
 * lookup()
 *	Look through list for a name
 */
static struct node *
lookup(struct file *f, char *name, int searchup)
{
	struct node *n, *n2;
	struct llist *l;

	n = f->f_node;
	ASSERT_DEBUG(DIR(n), "env lookup: not a dir");
	for (; n; n = n->n_up) {
		for (l = LL_NEXT(&n->n_elems);
				l != &n->n_elems; l = LL_NEXT(l)) {
			n2 = l->l_data;
			if (!strcmp(n2->n_name, name)) {
				return(n2);
			}
		}

		/*
		 * If not allowed to search upwards in tree, fail
		 * now.
		 */
		if (!searchup) {
			return(0);
		}
	}
	return(0);
}

/*
 * env_open()
 *	Look up an entry downward
 */
void
env_open(struct msg *m, struct file *f)
{
	struct node *n, *nold = f->f_node;
	int creating = (m->m_arg & ACC_CREATE), home;
	char *nm;

	/*
	 * Make sure it's a "directory", cap length.
	 */
	if (!DIR(nold) || (strlen(m->m_buf) >= NAMESZ)) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * "#" refers to a "magic" copy-on-write invisible node
	 */
	nm = m->m_buf;
	if (!strcmp(nm, "#")) {
		home = 1;
	} else {
		home = 0;
	}

	/*
	 * See if we can find an existing entry
	 */
	if (!home) {
		n = lookup(f, nm, !creating);
	} else {
		n = f->f_home;
	}

	/*
	 * If found, verify access and type of use
	 */
	if (n) {
		if (access(f, m->m_arg, &n->n_prot)) {
			msg_err(m->m_sender, EPERM);
			return;
		}

		/*
		 * If creating on an existing file, truncate
		 */
		if ((creating) && !DIR(n)) {
			deref_val(n->n_val);
			n->n_val = alloc_val("");
		}

		/*
		 * Move current reference to new node
		 */
		deref_node(nold);
		f->f_node = n;
		ref_node(n);
		f->f_pos = 0L;
		m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * If not intending to create, error
	 */
	if (!creating) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * If we don't have write access to the current node,
	 * error.
	 */
	if (!(f->f_mode & ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Get the new node
	 */
	n = alloc_node(f);
	if (m->m_arg & ACC_DIR) {
		n->n_flags |= N_INTERNAL;
	} else {
		n->n_val = alloc_val("");
	}

	/*
	 * Try inserting it under the current node
	 */
	if (!home) {
		strcpy(n->n_name, nm);
		ref_node(n);
		if (!(n->n_list = ll_insert(&nold->n_elems, n))) {
			deref_node(n);
			deref_node(nold);
			msg_err(m->m_sender, ENOMEM);
			return;
		}
		/* Reference to nold done in alloc_node (n->n_up) */
	} else {
		struct file *f2;

		/*
		 * Switch home for all in this group
		 */
		f2 = f;
		do {
			deref_node(f2->f_home);
			f2->f_home = n;
			ref_node(n);
			f2 = f2->f_forw;
		} while (f2 != f);
	}

	/*
	 * Move "f" down to new node
	 */
	deref_node(nold);
	f->f_node = n;
	ref_node(n);

	m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * env_remove()
 *	Delete an entry from the current directory
 */
void
env_remove(struct msg *m, struct file *f)
{
	struct node *n = f->f_node, *n2;

	/*
	 * Make sure we're in a "directory"
	 */
	if (!DIR(n)) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * See if we have write access
	 */
	if (!(f->f_mode & ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * See if we can find the entry
	 */
	n2 = lookup(f, m->m_buf, 0);

	/*
	 * If not found, forget it
	 */
	if (!n2) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * If the node is busy, forget it
	 */
	if (n2->n_refs > 1) {
		msg_err(m->m_sender, EBUSY);
		return;
	}
	ASSERT((n2->n_refs == 1) && (n->n_refs > 2),
		"env_remove: short ref");

	/*
	 * Let the node disappear
	 */
	deref_node(n2);

	m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}
