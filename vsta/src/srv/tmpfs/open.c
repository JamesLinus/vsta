/*
 * open.c
 *	Routines for opening, closing, creating  and deleting files
 *
 * Note that we don't allow subdirectories for BFS, which simplifies
 * things.
 */
#include <tmpfs/tmpfs.h>
#include <lib/llist.h>
#include <lib/hash.h>
#include <std.h>
#include <sys/assert.h>

extern struct llist files;	/* All files in FS */

/*
 * dir_lookup()
 *	Look up name in list of all files in this FS
 */
struct openfile *
dir_lookup(char *name)
{
	struct llist *l;
	struct openfile *o;

	for (l = LL_NEXT(&files); l != &files; l = LL_NEXT(l)) {
		o = l->l_data;
		if (!strcmp(name, o->o_name)) {
			return(o);
		}
	}
	return(0);
}

/*
 * freeup()
 *	Free all memory associated with a file
 */
static void
freeup(struct openfile *o)
{
	if (o == 0) {
		return;
	}
	if (o->o_name) {
		free(o->o_name);
	}
	if (o->o_blocks) {
		hash_dealloc(o->o_blocks);
	}
	if (o->o_entry) {
		ll_delete(o->o_entry);
	}
	free(o);
}

/*
 * dir_newfile()
 *	Create new entry in filesystem
 */
struct openfile *
dir_newfile(struct file *f, char *name)
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

	/*
	 * Name
	 */
	o->o_name = strdup(name);
	if (o->o_name == 0) {
		free(o);
		return(0);
	}

	/*
	 * Block # hash
	 */
	o->o_blocks = hash_alloc(16);
	if (o->o_blocks == 0) {
		freeup(o);
		return(0);
	}

	/*
	 * Insert in dir chain
	 */
	o->o_entry = ll_insert(&files, o);
	if (o->o_entry == 0) {
		freeup(o);
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
	return(o);
}

/*
 * do_free()
 *	Given a block, free it
 */
static
do_free(void *block, void *dummy)
{
	free(block);
	return(0);
}

/*
 * blk_trunc()
 *	Throw away all the blocks in the current file
 */
static void
blk_trunc(struct openfile *o)
{
	hash_foreach(o->o_blocks, do_free, 0);
	hash_dealloc(o->o_blocks);
	o->o_blocks = hash_alloc(16);
	/* Shouldn't fail, since we just freed a bunch... */
	ASSERT(o->o_blocks, "tmpfs_open: lost free space");
	o->o_len = 0;
}

/*
 * tmpfs_open()
 *	Main entry for processing an open message
 */
void
tmpfs_open(struct msg *m, struct file *f)
{
	struct openfile *o;
	uint x;

	/*
	 * Have to be in root dir to open down into a file
	 */
	if (f->f_file) {
		msg_err(m->m_sender, ENOTDIR);
		return;
	}

	/*
	 * No subdirs in a tmpfs filesystem
	 */
	if (m->m_arg & ACC_DIR) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Look up name
	 */
	o = dir_lookup(m->m_buf);

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
		if ((o = dir_newfile(f, m->m_buf)) == 0) {
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
	if ((m->m_arg & x) != m->m_arg) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * If they wanted it truncated, do it now
	 */
	if (m->m_arg & ACC_CREATE) {
		blk_trunc(o);
	}

	/*
	 * Move to this file
	 */
	f->f_file = o; o->o_refs += 1;
	f->f_perm = m->m_arg | (x & ACC_CHMOD);
	m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * tmpfs_close()
 *	Do closing actions on a file
 */
void
tmpfs_close(struct file *f)
{
	struct openfile *o;

	if (o = f->f_file) {
		o->o_refs -= 1;
	}
}

/*
 * tmpfs_remove()
 *	Remove an entry in the current directory
 */
void
tmpfs_remove(struct msg *m, struct file *f)
{
	struct openfile *o;
	uint x;

	/*
	 * Have to be in root dir
	 */
	if (f->f_file) {
		msg_err(m->m_sender, ENOTDIR);
		return;
	}

	/*
	 * Look up entry.  Bail if no such file.
	 */
	o = dir_lookup(m->m_buf);
	if (o == 0) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * Check permission
	 */
	x = perm_calc(f->f_perms, f->f_nperm, &o->o_prot);
	if ((x & (ACC_WRITE|ACC_CHMOD)) == 0) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Can't be any other users
	 */
	if (o->o_refs > 0) {
		msg_err(m->m_sender, EBUSY);
		return;
	}

	/*
	 * Zap the blocks
	 */
	blk_trunc(o);

	/*
	 * Free the node memory
	 */
	freeup(o);

	/*
	 * Return success
	 */
	m->m_buflen = m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}
