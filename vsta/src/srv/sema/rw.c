/*
 * rw.c
 *	Routines for doing R/W semaphore ops
 */
#include "sema.h"
#include <hash.h>
#include <llist.h>
#include <std.h>
#include <stdio.h>

/*
 * run_one()
 *	Take next element off and let him go
 */
static void
run_one(struct openfile *o)
{
	struct llist *l;
	struct file *f;
	struct msg m;

	l = LL_NEXT(&o->o_queue);
	f = l->l_data;

	/*
	 * Take the requester, let him through
	 */
	ll_delete(f->f_entry); f->f_entry = 0;
	o->o_count += 1;
	m.m_arg = m.m_arg1 = m.m_nseg = 0;
	msg_reply(f->f_sender, &m);
}

/*
 * process_queue()
 *	Take off elements while permitted
 */
void
process_queue(struct openfile *o)
{
	/*
	 * While there are waiters and the sema is not held
	 * exclusively, process requests
	 */
	while ((o->o_count > 0) && !LL_EMPTY(&o->o_queue)) {
		/*
		 * Get next entry off list
		 */
		run_one(o);
	}
}

/*
 * sema_seek()
 *	"Set" position
 *
 * For semaphores, this releases the semaphore currently held.  The
 * idea is you "read" into a semaphore, then "rewind" out of it.
 */
int
sema_seek(struct msg *m, struct file *f)
{
	struct openfile *o = f->f_file;

	/*
	 * If dir, just set position and be done
	 */
	if (!o) {
		f->f_pos = m->m_arg1;
		m->m_arg = m->m_arg1 = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return(0);
	}

	/*
	 * Release a reference
	 */
	m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
	if (!LL_EMPTY(&o->o_queue)) {
		run_one(o);
	}

	return(0);
}

/*
 * queue()
 *	Put us on the queue for this sema
 *
 * This routine assumes that the request must block.
 */
static void
queue(struct msg *m, struct file *f)
{
	struct openfile *o = f->f_file;

	/*
	 * Try to queue up
	 */
	f->f_entry = ll_insert(&o->o_queue, f);
	if (f->f_entry == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Record the appropriate data
	 */
	f->f_sender = m->m_sender;
}

/*
 * add_dirent()
 *	Add this entry to the list of entries
 */
static int
add_dirent(ulong key, struct openfile *o, char *buf)
{
	sprintf(buf + strlen(buf), "%u\n", o->o_iname);
	return(0);
}

/*
 * sema_readdir()
 *	Do reads on directory entries
 */
static void
sema_readdir(struct msg *m, struct file *f)
{
	char *buf;
	uint len;

	/*
	 * Allocate buffer big enough to hold whole list
	 */
	buf = malloc(nfiles * 12);
	if (buf == 0) {
		msg_err(m->m_sender, strerror());
	}

	/*
	 * Assemble names into buffer
	 */
	buf[0] = '\0';
	hash_foreach(files, add_dirent, buf);

	/*
	 * Return no data if their position is beyond end
	 */
	len = strlen(buf);
	if (f->f_pos >= len) {
		free(buf);
		m->m_arg = m->m_arg1 = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Adjust size for position and requested amount
	 */
	len -= f->f_pos;
	if (len > m->m_arg) {
		len = m->m_arg;
	}

	/*
	 * Send back results
	 */
	m->m_buf = buf + f->f_pos;
	m->m_arg = m->m_buflen = len;
	m->m_nseg = ((len > 0) ? 1 : 0);
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	free(buf);
	f->f_pos += len;
}

/*
 * sema_read()
 *	Read bytes out of the current file or directory
 *
 * Directories get their own routine.
 */
void
sema_read(struct msg *m, struct file *f)
{
	struct openfile *o;

	/*
	 * Directory--only one is the root
	 */
	if ((o = f->f_file) == 0) {
		sema_readdir(m, f);
		return;
	}

	/*
	 * Access?
	 */
	if (!(f->f_perm & ACC_READ)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Short circuit--you can have the slot
	 */
	o->o_count -= 1;
	if (o->o_count >= 0) {
		m->m_arg = m->m_arg1 = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Get in line
	 */
	queue(m, f);
}

/*
 * sema_abort()
 *	Abort a requested semaphore operation
 */
void
sema_abort(struct msg *m, struct file *f)
{
	if (f->f_entry) {
		ll_delete(f->f_entry); f->f_entry = 0;
		f->f_file->o_count += 1;
	}
	m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}
