/*
 * open.c
 *	Routines for moving downwards in the hierarchy
 */
#define _NAMER_H_INTERNAL
#include <sys/types.h>
#include <sys/namer.h>
#include <sys/fs.h>
#include <sys/assert.h>
#include <llist.h>
#include <std.h>
#include <unistd.h>

/*
 * lookup()
 *	Look through list for a name
 */
static struct node *
lookup(struct file *f, char *name)
{
	struct node *n = f->f_node, *n2;
	struct llist *l;

	ASSERT_DEBUG(n->n_internal, "namer lookup: not a dir");
	for (l = n->n_elems.l_forw; l != &n->n_elems; l = l->l_forw) {
		n2 = l->l_data;
		if (!strcmp(n2->n_name, name))
			return(n2);
	}
	return(0);
}

/*
 * namer_open()
 *	Look up an entry downward
 */
void
namer_open(struct msg *m, struct file *f)
{
	struct node *n, *nparent;
	struct prot *p;

	/*
	 * Make sure it's a "directory"
	 */
	nparent = f->f_node;
	if (!nparent->n_internal) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * See if you can find an existing entry
	 */
	n = lookup(f, m->m_buf);

	/*
	 * If found, verify access and type of use
	 */
	if (n) {
		if (can_access(f, m->m_arg, &n->n_prot)) {
			msg_err(m->m_sender, EPERM);
			return;
		}

		/*
		 * Move current reference to new node
		 */
		nparent->n_refs -= 1;
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
	if (!(m->m_arg & ACC_CREATE)) {
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
	bzero(n, sizeof(struct node));

	/*
	 * Try inserting it under the current node
	 */
	if (!(n->n_list = ll_insert(&f->f_node->n_elems, n))) {
		free(n);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Fill in its fields.  We allow everybody to read by
	 * default, as they could hunt for the port_name anyway,
	 * and our real protection against them is when they
	 * try to connect to the server.
	 */
	p = &n->n_prot;
	p->prot_len = PERM_LEN(&f->f_perms[0]);
	bcopy(f->f_perms[0].perm_id, p->prot_id, PERMLEN);
	p->prot_default = ACC_READ;
	p->prot_bits[p->prot_len-1] =
		ACC_WRITE|ACC_CHMOD;
	n->n_owner = f->f_perms[0].perm_uid;
	strcpy(n->n_name, m->m_buf);
	n->n_internal = (m->m_arg & ACC_DIR) ? 1 : 0;
	ll_init(&n->n_elems);
	n->n_parent = nparent;
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
 * delete_node()
 *	Remove a node from its directory list, free its storage
 */
void
delete_node(struct node *n)
{
	ASSERT_DEBUG(n->n_parent->n_refs, "delete_node: overflow");
	n->n_parent->n_refs -= 1;
	ll_delete(n->n_list);
	free(n);
}

/*
 * namer_remove()
 *	Delete an entry from the current directory
 */
void
namer_remove(struct msg *m, struct file *f)
{
	struct node *n = f->f_node, *n2;

	/*
	 * Make sure it's a "directory"
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
	 * See if you can find the entry
	 */
	n2 = lookup(f, m->m_buf);

	/*
	 * If not found, forget it
	 */
	if (!n2) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * If the node is busy, mark deletion pending last close
	 */
	if (n2->n_refs > 1) {
		n2->n_deleted = 1;
	} else {
		ASSERT((n2->n_refs == 1) && (n->n_refs > 2),
			"namer_remove: short ref");

		/*
		 * Trim reference counts, remove node from list, free memory.
		 */
		delete_node(n2);
	}
	m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}
