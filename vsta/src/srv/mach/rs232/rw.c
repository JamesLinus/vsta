/*
 * rw.c
 *	Reads and writes to the rs232 device
 *
 * We violate the internals of the FIFO structure, but all in the
 * name of efficiency.
 */
#include <sys/msg.h>
#include <llist.h>
#include <sys/assert.h>
#include <sys/seg.h>
#include "rs232.h"
#include "fifo.h"

/*
 * Shared with ISR code
 */
struct fifo *inbuf, *outbuf;

/*
 * Our queue for I/O's pending, and count.  ISR will peek at count,
 * but not modify.
 */
static struct llist read_q, write_q;
int txwaiters, rxwaiters;

/*
 * rs232_write()
 *	Write bytes to port
 *
 * To allow concurrency, we bite the bullet and bulk-copy
 * into our private FIFO output buffer.  We still can block
 * if the buffer is full.
 */
void
rs232_write(struct msg *m, struct file *fl)
{
	struct fifo *f = outbuf;
	uint cnt, resid = m->m_arg;
	extern int txbusy;

	/*
	 * While there's room, add data to the buffer
	 */
	while ((resid > 0) && (f->f_cnt < f->f_size)) {
		if (f->f_hd >= f->f_tl) {
			cnt = f->f_size - f->f_hd;
		} else {
			cnt = f->f_tl - f->f_hd;
		}
		if (cnt > resid) {
			cnt = resid;
		}
		bcopy(m->m_buf, &f->f_buf[f->f_hd], cnt);
		f->f_hd += cnt;
		if (f->f_hd >= f->f_size) {
			f->f_hd = 0;
		}
		f->f_cnt += cnt;
		m->m_buf += cnt;
		resid -= cnt;
	}

	/*
	 * Kick UART awake if he's not doing anything currently
	 */
	if (!txbusy) {
		extern void start_tx();

		start_tx();
	}

	/*
	 * If we have more data than the buffer can hold,
	 * queue us for later processing.
	 */
	if (resid) {
		fl->f_count = resid;
		fl->f_sender = m->m_sender;
		fl->f_buf = m->m_buf;
		ll_insert(&write_q, fl);
		txwaiters += 1;
		return;
	}

	/*
	 * Otherwise complete with success
	 */
	m->m_nseg = m->m_arg1 = m->m_buflen = 0;
	msg_reply(m->m_sender, m);
}

/*
 * dequeue_tx()
 *	Buffer has emptied, unleash some new data from our queue
 */
void
dequeue_tx(void)
{
	struct msg m;
	struct llist *l;
	struct file *fl;

	/*
	 * Here's the next sender
	 */
	l = write_q.l_forw;
	ASSERT_DEBUG(l != &write_q, "dequeue_tx: skew");

	/*
	 * Extract the waiter from the list
	 */
	fl = l->l_data;
	ll_delete(l);
	txwaiters -= 1;

	/*
	 * Cobble together a pseudo-message, and re-post the
	 * request.
	 */
	m.m_sender = fl->f_sender;
	m.m_arg = fl->f_count;
	m.m_buf = fl->f_buf;
	fl->f_count = 0;
	fl->f_sender = 0;
	rs232_write(&m, fl);
}

/*
 * rs232_read()
 *	Post a read to the port
 *
 * m_arg specifies how much they want at most.  A value of 0 is
 * special; we will send 0 or more bytes, but will not wait for
 * more bytes.  It is, in a sense, a non-blocking read.
 */
void
rs232_read(struct msg *m, struct file *fl)
{
	struct fifo *f = inbuf;

	/*
	 * Handle easiest first--non-blocking read, no data
	 */
	if ((m->m_arg == 0) && fifo_empty(f)) {
		m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Next easiest: non-blocking read, give'em what we have
	 */
	if (m->m_arg == 0) {
		m->m_buf = &f->f_buf[f->f_tl];

		/*
		 * If all the data's in a row, send with just the
		 * buffer.
		 */
		if (f->f_hd > f->f_tl) {
			m->m_arg = m->m_buflen = f->f_hd - f->f_tl;
			m->m_nseg = 1;
		} else {
			/*
			 * If not, send the second extent using
			 * a second segment.
			 */
			m->m_buflen = f->f_size - f->f_tl;
			m->m_nseg = 2;
			m->m_seg[1] = seg_create(f->f_buf, f->f_hd);
			m->m_arg = m->m_buflen + f->f_hd;
		}
		f->f_hd = f->f_tl = f->f_cnt = 0;
		m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * If we have data, give them up to what they want
	 */
	if (!fifo_empty(f)) {
		/*
		 * See how much we can get in one run
		 */
		m->m_nseg = 1;
		m->m_buf = &f->f_buf[f->f_tl];
		if (f->f_hd > f->f_tl) {
			m->m_buflen = f->f_hd - f->f_tl;
		} else {
			m->m_buflen = f->f_size - f->f_tl;
		}

		/*
		 * Cap at how much they want
		 */
		if (m->m_buflen > m->m_arg) {
			m->m_buflen = m->m_arg;
		}

		/*
		 * Update tail pointer
		 */
		f->f_cnt -= m->m_buflen;
		f->f_tl += m->m_buflen;
		if (f->f_tl >= f->f_size) {
			f->f_tl = 0;
		}

		/*
		 * Send back data
		 */
		m->m_arg = m->m_buflen;
		m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Last but not least, queue the I/O until we can complete
	 * it.
	 */
	fl->f_count = m->m_arg;
	fl->f_sender = m->m_sender;
	ll_insert(&read_q, fl);
	rxwaiters += 1;
}

/*
 * rs232_init()
 *	Set up our read queue structure once
 */
void
rs232_init()
{
	ll_init(&read_q);
	ll_init(&write_q);
	inbuf = fifo_alloc(RS232_MAXBUF);
	outbuf = fifo_alloc(RS232_MAXBUF);
	ASSERT(inbuf && outbuf, "rs232_init: no memory");
}

/*
 * abort_read()
 *	Remove our guy from the waiting queue for data
 */
void
abort_io(struct file *f)
{
	struct llist *l;

	for (l = read_q.l_forw; l != &read_q; l = l->l_forw) {
		if (l->l_data == f) {
			ll_delete(l);
			rxwaiters -= 1;
			return;
		}
	}
	for (l = write_q.l_forw; l != &read_q; l = l->l_forw) {
		if (l->l_data == f) {
			ll_delete(l);
			txwaiters -= 1;
			return;
		}
	}
	ASSERT_DEBUG(0, "abort_io: missing tran");
}

/*
 * dequeue_rx()
 *	New data has arrived, feed the readers
 */
void
dequeue_rx(void)
{
	struct msg m;
	struct llist *l;
	struct file *fl;

	/*
	 * If there's a waiter, just let'em have it
	 */
	l = read_q.l_forw;
	ASSERT_DEBUG(l != &read_q, "dequeue_rx: skew");

	/*
	 * Extract the waiter from the list
	 */
	fl = l->l_data;
	ll_delete(l);
	rxwaiters -= 1;

	/*
	 * Cobble together a pseudo-message, and re-post the
	 * request.
	 */
	m.m_sender = fl->f_sender;
	m.m_arg = fl->f_count;
	fl->f_count = 0;
	fl->f_sender = 0;
	rs232_read(&m, fl);
}
