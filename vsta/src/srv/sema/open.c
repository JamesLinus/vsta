/*
 * open.c
 *	Routines for opening, closing, and creating files
 *
 * The namespace is a flat directory of files with integer filenames.
 */
#include "sema.h"
#include <hash.h>
#include <std.h>
#include <stdio.h>

uint nfiles;	/* # semaphore files which exist */

/*
 * dir_newfile()
 *	Create new entry in filesystem
 */
struct openfile *
dir_newfile(struct file *f, uint iname)
{
	struct openfile *o;
	struct prot *p;

	/*
	 * Get new node
	 */
	o = malloc(sizeof(struct openfile));
	if (o == 0) {
		return(0);
	}
	bzero(o, sizeof(struct openfile));
	ll_init(&o->o_queue);

	/*
	 * Insert in dir chain
	 */
	if (hash_insert(files, iname, o)) {
		free(o);
		return(0);
	}

	/*
	 * Use 0'th perm as our prot, require full match
	 */
	p = &o->o_prot;
	bzero(p, sizeof(*p));
	p->prot_len = PERM_LEN(&f->f_perms[0]);
	bcopy(f->f_perms[0].perm_id, p->prot_id, PERMLEN);
	p->prot_bits[p->prot_len-1] =
		ACC_READ|ACC_WRITE|ACC_CHMOD;
	o->o_owner = f->f_perms[0].perm_uid;

	/*
	 * Record our name and count
	 */
	o->o_iname = iname;
	nfiles += 1;

	return(o);
}

/*
 * sema_open()
 *	Main entry for processing an open message
 */
void
sema_open(struct msg *m, struct file *f)
{
	struct openfile *o;
	uint x, want, iname;

	/*
	 * Have to be in root dir to open down into a file
	 */
	if (f->f_file) {
		msg_err(m->m_sender, ENOTDIR);
		return;
	}

	/*
	 * No subdirs in a sema filesystem
	 */
	if (m->m_arg & ACC_DIR) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Look up name
	 */
	if (sscanf(m->m_buf, "%u", &iname) != 1) {
		msg_err(m->m_sender, EINVAL);
		return;
	}
	o = hash_lookup(files, iname);

	/*
	 * No such file--do they want to create?
	 */
	if (!o && !(m->m_arg & ACC_CREATE)) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * If it's a new file, allocate the entry now.
	 */
	if (!o) {
		/*
		 * Failure?
		 */
		if ((o = dir_newfile(f, iname)) == 0) {
			msg_err(m->m_sender, ENOMEM);
			return;
		}

		/*
		 * Move to new node
		 */
		f->f_file = o; o->o_refs += 1;
		f->f_perm = ACC_READ|ACC_WRITE|ACC_CHMOD;
		m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Check permission
	 */
	x = perm_calc(f->f_perms, f->f_nperm, &o->o_prot);
	want = m->m_arg & (ACC_READ|ACC_WRITE|ACC_CHMOD);
	if ((want & x) != want) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Move to this file
	 */
	f->f_file = o; o->o_refs += 1;
	f->f_perm = want | (x & ACC_CHMOD);
	m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * sema_close()
 *	Do closing actions on a file
 */
void
sema_close(struct file *f)
{
	struct openfile *o;

	if (o = f->f_file) {
		o->o_refs -= 1;
		if (o->o_refs == 0) {
			(void)hash_delete(files, o->o_iname);
			free(o);
			nfiles -= 1;
		}
	}
}
