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
	ASSERT_DEBUG(n->n_internal, "env lookup: not a dir");
	for (; n; n = n->n_up) {
		for (l = n->n_elems.l_forw;
				l != &n->n_elems; l = l->l_forw) {
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
	struct node *n;
	struct prot *p;
	int creating = (m->m_arg & ACC_CREATE);

	/*
	 * Make sure it's a "directory"
	 */
	if (!f->f_node->n_internal) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * See if you can find an existing entry
	 */
	n = lookup(f, m->m_buf, !creating);

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
		if (creating) {
			deref_val(n->n_val);
			n->n_val = alloc_val("");
		}

		/*
		 * Move current reference to new node
		 */
		f->f_node->n_refs -= 1;
		f->f_node = n;
		n->n_refs += 1;
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
	 * malloc the new node
	 */
	if ((n = malloc(sizeof(struct node))) == 0) {
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Try inserting it under the current node
	 */
	if (!(n->n_list = ll_insert(&f->f_node->n_elems, n))) {
		free(n);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Fill in its fields.  Default label is the first user
	 * label, with all the abilities requiring a full match.
	 */
	p = &n->n_prot;
	bzero(p, sizeof(*p));
	p->prot_len = f->f_perms[0].perm_len;
	bcopy(f->f_perms[0].perm_id, p->prot_id, PERMLEN);
	p->prot_bits[p->prot_len-1] =
		ACC_READ|ACC_WRITE|ACC_CHMOD;
	strcpy(n->n_name, m->m_buf);
	n->n_internal = (m->m_arg & ACC_DIR) ? 1 : 0;
	n->n_val = alloc_val("");
	n->n_up = f->f_node;
	ll_init(&n->n_elems);
	n->n_refs = 2;	/* One from f->f_node, another for this open */

	/*
	 * Move "f" down to new node.  Note that we leave a reference
	 * on the parent--it is now the reference of the child node.
	 */
	f->f_node = n;

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
	struct prot *p;

	/*
	 * Make sure we're in a "directory"
	 */
	if (!n->n_internal) {
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
	 * Trim reference counts, remove node from list, free memory.
	 */
	n->n_refs -= 1;
	ll_delete(n2->n_list);

	/*
	 * Release reference to string memory, free this node
	 */
	deref_val(n2->n_val);
	free(n2);

	m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}
