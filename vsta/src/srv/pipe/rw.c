/*
 * rw.c
 *	Routines for operating on the data in a file
 *
 * I'm going to try a bizarre tradeoff here:  a writer will block
 * until a reader accepts all the writer's data.  This allows me
 * to do direct buffer->buffer data motion, since the server here
 * will just hold the writer's buffers mapped until a read comes
 * in.  The response will be the writer's buffer, so the reader
 * will then copy it into his address space.
 *
 * The alternative is to copy the written data into an internal
 * buffer.  This allows the writer to dump lots of discrete writes
 * without blocking.  But the server then has to enforce limits on
 * size and buffering, plus the additional copying to an intermediate
 * buffer.  So we'll try this, and come back and do it the other way
 * if it stinks.
 */
#include "pipe.h"
#include <hash.h>
#include <std.h>
#include <sys/assert.h>

extern struct llist files;

/*
 * pipe_abort()
 *	Caller has requested abort of operation
 */
void
pipe_abort(struct msg *m, struct file *f)
{
	/*
	 * Always answer a zero-length message
	 */
	m->m_nseg = m->m_arg = m->m_arg1 = 0;

	/*
	 * Remove any pending I/O
	 */
	if (f->f_q) {
		ll_delete(f->f_q);
		f->f_q = 0;
	}

	/*
	 * Answer completion
	 */
	msg_reply(m->m_sender, m);
}

/*
 * sendseg()
 *	Send data off to requestor, update message segments
 */
static uint
sendseg(long retaddr, struct msg *m, uint nbyte)
{
	struct msg m2;
	uint oseg, total = 0;
	seg_t *s;

	/*
	 * Set up our reply message
	 */
	m2.m_arg = m2.m_arg1 = 0;
	m2.m_nseg = 0;

	/*
	 * Build segments until byte count or segments exhausted
	 */
	oseg = 0;
	s = &m->m_seg[0];
	while ((nbyte > 0) && (m->m_nseg > 0) && (oseg < MSGSEGS)) {
		uint cnt;

		/*
		 * Get count for next segment
		 */
		cnt = s->s_buflen;
		if (cnt > nbyte) {
			cnt = nbyte;
		}

		/*
		 * Set up next outgoing segment
		 */
		m2.m_seg[oseg].s_buf = s->s_buf;
		m2.m_seg[oseg].s_buflen = cnt;

		/*
		 * Advance writing side
		 */
		if (s->s_buflen == cnt) {
			bcopy(&m->m_seg[1], s, (MSGSEGS-1)*sizeof(seg_t));
			m->m_nseg -= 1;
		} else {
			s->s_buf = (char *)s->s_buf + cnt;
			s->s_buflen -= cnt;
		}
		m->m_arg += cnt;
		total += cnt;
		nbyte -= cnt;

		/*
		 * Advance reading side
		 */
		oseg += 1;
		m2.m_arg += cnt;
	}

	/*
	 * Send reply
	 */
	m2.m_nseg = oseg;
	msg_reply(retaddr, &m2);

	/*
	 * Return amount of data taken from queue
	 */
	return(total);
}

/*
 * run_readers()
 *	Move data from next queued writer to next queued reader
 */
static void
run_readers(struct pipe *o)
{
	struct file *r, *w;

	while (!LL_EMPTY(&o->p_readers) && !LL_EMPTY(&o->p_writers)) {

		/*
		 * Point to next reader and writer.  Reader always completes
		 * here, so remove him from the list immediately.  If we
		 * consume all the writer data, we'll remove him later.
		 */
		r = LL_NEXT(&o->p_readers)->l_data;
		ll_delete(r->f_q);
		r->f_q = 0;
		w = LL_NEXT(&o->p_writers)->l_data;

		/*
		 * Copy segments
		 */
		sendseg(r->f_msg.m_sender, &w->f_msg, r->f_msg.m_arg);

		/*
		 * If writer completely finished, dequeue and complete
		 * him as well.
		 */
		if (w->f_msg.m_nseg == 0) {
			ll_delete(w->f_q);
			w->f_q = 0;
			w->f_msg.m_arg1 = 0;
			msg_reply(w->f_msg.m_sender, &w->f_msg);
		}
	}
}

/*
 * pipe_write()
 *	Write to an open file
 */
void
pipe_write(struct msg *m, struct file *f, uint nbyte)
{
	struct pipe *o = f->f_file;

	/*
	 * Can only write to a true file, and only if open for writing.
	 */
	if (!o || !(f->f_perm & ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Is this a writer that's trying to write to what's now a pipe
	 * with no listeners (it's broken)?
	 */
	if (o->p_nread == PIPE_CLOSED_FOR_READS) {
		msg_err(m->m_sender, EPIPE);
		return;
	}

	/*
	 * Queue write, fail if we can't insert list element (VM
	 * exhausted?)
	 */
	ASSERT_DEBUG(f->f_q == 0, "pipe_write: busy");
	if ((f->f_q = ll_insert(&o->p_writers, f)) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}
	f->f_msg = *m;
	f->f_msg.m_arg = 0;

	/*
	 * Now move to pending read requests
	 */
	if (!LL_EMPTY(&o->p_readers)) {
		run_readers(o);
	}
}

/*
 * pipe_readdir()
 *	Do reads on directory entries
 */
static void
pipe_readdir(struct msg *m, struct file *f)
{
	char *buf;
	uint len, pos, bufcnt;
	struct llist *l;

	/*
	 * Get a buffer of the requested size, but put a sanity
	 * cap on it.
	 */
	len = m->m_arg;
	if (len > 256) {
		len = 256;
	}
	if ((buf = malloc(len+1)) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}
	buf[0] = '\0';

	/*
	 * Assemble as many names as will fit, starting at
	 * given byte offset.  We assume the caller's position
	 * always advances in units of a whole directory entry.
	 */
	bufcnt = pos = 0;
	for (l = LL_NEXT(&files); l != &files; l = LL_NEXT(l)) {
		uint slen;
		char buf2[32];

		/*
		 * Point to next file.  Get its length.
		 */
		sprintf(buf2, "%u", l->l_data);
		slen = strlen(buf2)+1;

		/*
		 * If we've reached an offset the caller hasn't seen
		 * yet, assemble the entry into the buffer.
		 */
		if (pos >= f->f_pos) {
			/*
			 * No more room in buffer--return results
			 */
			if (slen >= len) {
				break;
			}

			/*
			 * Put string with newline at end of buffer
			 */
			sprintf(buf + bufcnt, "%s\n", buf2);

			/*
			 * Update counters
			 */
			len -= slen;
			bufcnt += slen;
		}

		/*
		 * Update position
		 */
		pos += slen;
	}

	/*
	 * Send back results
	 */
	m->m_buf = buf;
	m->m_arg = m->m_buflen = bufcnt;
	m->m_nseg = ((bufcnt > 0) ? 1 : 0);
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	free(buf);
	f->f_pos = pos;
}

/*
 * pipe_read()
 *	Read bytes out of the current pipe or directory
 *
 * Directories get their own routine.
 */
void
pipe_read(struct msg *m, struct file *f)
{
	struct pipe *o;

	/*
	 * Directory--only one is the root
	 */
	if ((o = f->f_file) == 0) {
		pipe_readdir(m, f);
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
	 * If all writers have gone, continue to return EOF
	 */
	if (o->p_nwrite == 0) {
		m->m_arg = m->m_arg1 = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Queue as a reader
	 */
	ASSERT_DEBUG(f->f_q == 0, "pipe_read: busy");
	if ((f->f_q = ll_insert(&o->p_readers, f)) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}
	f->f_msg.m_sender = m->m_sender;
	f->f_msg.m_arg = m->m_arg;

	/*
	 * If there's stuff waiting, get it now
	 */
	if (!LL_EMPTY(&o->p_writers)) {
		run_readers(o);
	}
}
