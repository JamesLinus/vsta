/*
 * rw.c
 *	Reads and writes to the keyboard device
 *
 * Well, actually just reads, since it's a keyboard.  But r.c looked
 * a little strange.
 */
#include <sys/msg.h>
#include <llist.h>
#include <mach/kbd.h>
#include <sys/assert.h>
#include <sys/seg.h>

/*
 * A circular buffer for keystrokes
 */
static char key_buf[KEYBD_MAXBUF];
static uint key_hd = 0,
	key_tl = 0;
uint key_nbuf = 0;

/*
 * Our queue for I/O's pending
 */
static struct llist read_q;

/*
 * kbd_read()
 *	Post a read to the keyboard
 *
 * m_arg specifies how much they want at most.  A value of 0 is
 * special; we will send 0 or more bytes, but will not wait for
 * more bytes.  It is, in a sense, a non-blocking read.
 */
void
kbd_read(struct msg *m, struct file *f)
{
	/*
	 * Handle easiest first--non-blocking read, no data
	 */
	if ((m->m_arg == 0) && (key_nbuf == 0)) {
		m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Next easiest: non-blocking read, give'em what we have
	 */
	if (m->m_arg == 0) {
		m->m_buf = &key_buf[key_tl];

		/*
		 * If all the data's in a row, send with just the
		 * buffer.
		 */
		if (key_hd > key_tl) {
			m->m_arg = m->m_buflen = key_hd-key_tl;
			m->m_nseg = 1;
		} else {
			/*
			 * If not, send the second extent using
			 * a segment.
			 */
			m->m_buflen = KEYBD_MAXBUF-key_tl;
			m->m_nseg = 2;
			m->m_seg[1] = seg_create(key_buf, key_hd);
			m->m_arg = m->m_buflen + key_hd;
		}
		key_tl = key_hd;
		key_nbuf = 0;
		m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * If we have data, give them up to what they want
	 */
	if (key_nbuf > 0) {
		/*
		 * See how much we can get in one run
		 */
		m->m_nseg = 1;
		m->m_buf = &key_buf[key_tl];
		if (key_hd > key_tl) {
			m->m_buflen = key_hd-key_tl;
		} else {
			m->m_buflen = KEYBD_MAXBUF-key_tl;
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
		key_nbuf -= m->m_buflen;
		key_tl += m->m_buflen;
		if (key_tl >= KEYBD_MAXBUF) {
			key_tl -= KEYBD_MAXBUF;
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
	f->f_count = m->m_arg;
	f->f_sender = m->m_sender;
	ll_insert(&read_q, f);
}

/*
 * kbd_init()
 *	Set up our read queue structure once
 */
void
kbd_init()
{
	ll_init(&read_q);
}

/*
 * abort_read()
 *	Remove our guy from the waiting queue for data
 */
void
abort_read(struct file *f)
{
	struct llist *l;

	for (l = read_q.l_forw; l != &read_q; l = l->l_forw) {
		if (l->l_data == f) {
			ll_delete(l);
			break;
		}
	}
	ASSERT_DEBUG(l != &read_q, "abort_read: missing tran");
}

/*
 * kbd_enqueue()
 *	Get a new character to enqueue
 */
void
kbd_enqueue(uint c)
{
	char buf[4];
	struct msg m;
	struct file *f;
	struct llist *l;

	/*
	 * If there's a waiter, just let'em have it
	 */
	l = read_q.l_forw;
	if (l != &read_q) {
		ASSERT_DEBUG(key_nbuf == 0, "kbd: waiters with data");

		/*
		 * Extract the waiter from the list
		 */
		f = l->l_data;
		ll_delete(l);

		/*
		 * Put char in buffer, send message
		 */
		buf[0] = c;	/* Don't assume endianness in an int */
		m.m_buf = buf;
		m.m_arg = m.m_buflen = sizeof(char);
		m.m_nseg = 1;
		m.m_arg1 = 0;
		msg_reply(f->f_sender, &m);

		/*
		 * Clear pending transaction
		 */
		f->f_count = 0;
#ifdef DEBUG
		f->f_sender = 0;
#endif
		return;
	}

	/*
	 * If there's too much queued data, ignore
	 */
	if (key_nbuf >= KEYBD_MAXBUF) {
		return;
	}

	/*
	 * Add it to the queue
	 */
	key_buf[key_hd] = c;
	key_hd += 1;
	key_nbuf += 1;
	if (key_hd >= KEYBD_MAXBUF) {
		key_hd = 0;
	}
}
