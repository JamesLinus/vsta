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
 * process_queue()
 *	Take off elements while permitted
 */
static void
process_queue(struct openfile *o)
{
	struct llist *l;
	struct file *f;
	struct msg m;

	/*
	 * While there are waiters and the sema is not held
	 * exclusively, process requests
	 */
	while (!LL_EMPTY(&o->o_queue) && !(o->o_holder && o->o_writing)) {
		/*
		 * Get next entry off list
		 */
		l = LL_NEXT(&o->o_queue);
		f = l->l_data;

		/*
		 * If it's a writer, he's exclusive, so just
		 * set him up as the holder and let him run
		 */
		if (f->f_writer) {
			/*
			 * If the mutex is held, this guy can't go,
			 * so nobody after him can, either.
			 */
			if (o->o_holder) {
				return;
			}

			/*
			 * One less writer waiting
			 */
			printf("Writer, %d left\n", o->o_nwrite);
			o->o_nwrite -= 1;
		} else {
			/*
			 * Read access.  We know the sema isn't currently
			 * held for write (it's in the loop check), so
			 * we can have it.
			 */
			printf("Reader\n");
		}

		/*
		 * Take the requester, let him through
		 */
		o->o_writing = f->f_writer;
		ll_delete(f->f_entry);		
		f->f_entry = 0;
		f->f_hold += 1;
		o->o_holder += 1;
		printf("%d holding\n", o->o_holder);
		m.m_arg = m.m_arg1 = m.m_nseg = 0;
		msg_reply(f->f_sender, &m);
	}
}

/*
 * sema_seek()
 *	"Set" position
 *
 * For semaphores, this releases the semaphore currently held.  The
 * idea is you "read" or "write" into a semaphore, then "rewind" out
 * of it.
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
	 * If we don't hold it, don't let us release it
	 */
	if (!f->f_hold) {
		msg_err(m->m_sender, EPERM);
		return(1);
	}

	/*
	 * Release a reference
	 */
	o->o_holder -= 1;
	f->f_hold -= 1;
	m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);

	/*
	 * If the hold count has reached zero, see about letting
	 * somebody new through
	 */
	if ((o->o_holder == 0) && !LL_EMPTY(&o->o_queue)) {
		process_queue(o);
	}
	return(0);
}

/*
 * queue()
 *	Put us on the queue for this sema
 *
 * If no error, also run queue
 */
static void
queue(struct msg *m, struct file *f)
{
	struct openfile *o = f->f_file;

	printf("Got a %s\n", f->f_writer ? "writer" : "reader");
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

	/*
	 * Run queue
	 */
	process_queue(o);
}

/*
 * sema_write()
 *	Enter semaphore as exclusive writer
 */
void
sema_write(struct msg *m, struct file *f)
{
	struct openfile *o = f->f_file;

	/*
	 * Make sure in file, and allowed to write
	 */
	if (!o || !(f->f_perm & ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * We're a writer
	 */
	f->f_writer = 1;
	o->o_nwrite += 1;

	/*
	 * Queue and run queue
	 */
	queue(m, f);
}

/*
 * add_dirent()
 *	Add this entry to the list of entries
 */
static int
add_dirent(struct openfile *o, char *buf)
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
	 * We're a reader
	 */
	f->f_writer = 0;

	/*
	 * Queue and run queue
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
		if (f->f_writer) {
			f->f_file->o_nwrite -= 1;
		}
	}
	m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}
