/*
 * ioq.c
 *	Implement I/O queues (sleeping reader/writer, buffered data)
 */
#include <sys/fs.h>
#include <stddef.h>
#include <string.h>
#include <std.h>
#include "pty.h"

/*
 * ioq_init()
 *	Initialize an I/O queue
 */
void
ioq_init(struct ioq *i)
{
	i->ioq_buf = malloc(IOQ_MAXBUF);
	i->ioq_nbuf = 0;
	ll_init(&i->ioq_read);
	ll_init(&i->ioq_write);
}

/*
 * abortem()
 *	Abort sleepers from a single queue
 */
static void
abortem(struct llist *list)
{
	struct llist *l, *ln;
	struct file *f;

	for (l = LL_NEXT(list); l != list; l = ln) {
		ln = LL_NEXT(l);
		f = l->l_data;
		msg_err(f->f_msg.m_sender, EIO);
		ll_delete(f->f_q);
		f->f_q = NULL;
	}
}

/*
 * ioq_abort()
 *	Abort readers and writers on this I/O queue
 */
void
ioq_abort(struct ioq *i)
{
	abortem(&i->ioq_read);
	abortem(&i->ioq_write);
}

/*
 * queue_data()
 *	Pull data from a "struct msg" and queue to IOQ buffer
 */
static void
queue_data(struct msg *m, struct ioq *i)
{
	uint count;

	/*
	 * Walk across elements of the scatter/gather data
	 */
	while (m->m_nseg > 0) {
		/*
		 * See how much to pull in now
		 */
		count = IOQ_MAXBUF - i->ioq_nbuf;
		if (count > m->m_buflen) {
			count = m->m_buflen;
		}

		/*
		 * Copy it over into place
		 */
		bcopy(m->m_buf, i->ioq_buf + i->ioq_nbuf, count);
		i->ioq_nbuf += count;

		/*
		 * And advance message buffer state
		 */
		if (count == m->m_buflen) {
			if ((m->m_nseg -= 1) > 0) {
				bcopy(&m->m_seg[1], &m->m_seg[0],
					m->m_nseg * sizeof(seg_t));
			}
		} else {
			m->m_buf += count;
			m->m_buflen -= count;
			return;
		}
	}
}

/*
 * dequeue_data()
 *	Shovel data out to a reader
 */
static void
dequeue_data(struct msg *m, struct ioq *i)
{
	uint count = m->m_arg;

	if (count > i->ioq_nbuf) {
		count = i->ioq_nbuf;
	}
	m->m_buf = i->ioq_buf;
	m->m_nseg = 1;
	m->m_buflen = m->m_arg = count;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);

	/*
	 * If there's more data, shuffle it down to the base of
	 * the buffer.
	 */
	if (count < i->ioq_nbuf) {
		i->ioq_nbuf -= count;
		bcopy(i->ioq_buf + count, i->ioq_buf, i->ioq_nbuf);
	} else {
		/*
		 * Otherwise just flag the buffer empty
		 */
		i->ioq_nbuf = 0;
	}
}

/*
 * run_readers()
 *	Take reads pending in ioq_read and complete as many as possible
 */
static void
run_readers(struct ioq *i)
{
	struct msg *m;
	struct file *f;

	/*
	 * While there's readers and data, move data to readers
	 */
	while (i->ioq_nbuf && !LL_EMPTY(&i->ioq_read)) {
		/*
		 * Next reader
		 */
		f = LL_NEXT(&i->ioq_read)->l_data;
		m = &f->f_msg;

		/*
		 * Let them consume what they can.  Their read
		 * completes in all cases here.
		 */
		dequeue_data(m, i);
		ll_delete(f->f_q);
		f->f_q = NULL;
	}
}

/*
 * ioq_add_data()
 *	Queue data; wake up consumers if any are sleeping for data
 */
void
ioq_add_data(struct file *f, struct ioq *i, struct msg *m)
{
	uint oldnbuf = i->ioq_nbuf;

	/*
	 * Directly buffer as much as possible.
	 */
retry:	queue_data(m, i);

	/*
	 * If we consume all data, acknowledge completion now.
	 * We make sure m_arg is preserved, so they'll get the
	 * correct I/O count.
	 */
	if (m->m_nseg == 0) {
		msg_reply(m->m_sender, m);
		f->f_selfs.sc_needsel = 1;

		/*
		 * Let sleeping readers take what they can
		 */
		run_readers(i);
	} else {
		/*
		 * We couldn't buffer it all.  If there's a consumer
		 * waiting in the wings, run them, then retry.
		 */
		if (!LL_EMPTY(&i->ioq_read)) {
			run_readers(i);
			goto retry;
		}

		/*
		 * The writer must be
		 * put to sleep until there's more room.
		 */
		bcopy(m, &f->f_msg, sizeof(*m));
		f->f_q = ll_insert(&i->ioq_write, f);
	}

	/*
	 * If data has become available, wake up any select()
	 * clients.
	 */
	if ((oldnbuf == 0) && (i->ioq_nbuf > 0)) {
		i->ioq_flags |= IOQ_READABLE;
		update_select(f->f_file);
	}
}

/*
 * ioq_read_data()
 *	Consume data; sleep if none available
 */
void
ioq_read_data(struct file *f, struct ioq *i, struct msg *m)
{
	/*
	 * If there's data waiting, just send it on over
	 */
	if (i->ioq_nbuf) {
		dequeue_data(m, i);
		f->f_selfs.sc_needsel = 1;

		/*
		 * When we transition to empty buffer, wake up
		 * select clients waiting to write.
		 */
		if (i->ioq_nbuf == 0) {
			i->ioq_flags |= IOQ_WRITABLE;
			update_select(f->f_file);
		}

		return;
	}

	/*
	 * Otherwise queue the reader
	 */
	f->f_q = ll_insert(&i->ioq_read, f);
	bcopy(m, &f->f_msg, sizeof(*m));
}
