/*
 * rw.c
 *	Reads and writes to the keyboard device
 *
 * Well, actually just reads, since it's a keyboard.  But r.c looked
 * a little strange.
 */
#include "cons.h"
#include <sys/assert.h>
#include <sys/seg.h>
#include <sys/perm.h>
#include <stdio.h>
#include <fcntl.h>

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
	struct screen *s = SCREEN(f->f_screen);

	/*
	 * Handle easiest first--non-blocking read, no data
	 */
	if ((m->m_arg == 0) && (s->s_nbuf == 0)) {
		m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Next easiest: non-blocking read, give'em what we have
	 */
	if (m->m_arg == 0) {
		m->m_buf = &s->s_buf[s->s_tl];

		/*
		 * If all the data's in a row, send with just the
		 * buffer.
		 */
		if (s->s_hd > s->s_tl) {
			m->m_arg = m->m_buflen = s->s_hd - s->s_tl;
			m->m_nseg = 1;
		} else {
			/*
			 * If not, send the second extent using
			 * a segment.
			 */
			m->m_buflen = KEYBD_MAXBUF - s->s_tl;
			m->m_nseg = 2;
			m->m_seg[1].s_buf = s->s_buf;
			m->m_seg[1].s_buflen = s->s_hd;
			m->m_arg = m->m_buflen + s->s_hd;
		}
		s->s_tl = s->s_hd;
		s->s_nbuf = 0;
		m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * If we have data, give them up to what they want
	 */
	if (s->s_nbuf > 0) {
		/*
		 * See how much we can get in one run
		 */
		m->m_nseg = 1;
		m->m_buf = &s->s_buf[s->s_tl];
		if (s->s_hd > s->s_tl) {
			m->m_buflen = s->s_hd - s->s_tl;
		} else {
			m->m_buflen = KEYBD_MAXBUF - s->s_tl;
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
		s->s_nbuf -= m->m_buflen;
		s->s_tl += m->m_buflen;
		if (s->s_tl >= KEYBD_MAXBUF) {
			s->s_tl -= KEYBD_MAXBUF;
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
	f->f_readcnt = m->m_arg;
	f->f_sender = m->m_sender;
	ll_insert(&s->s_readers, f);
}

/*
 * abort_read()
 *	Remove our guy from the waiting queue for data
 */
void
abort_read(struct file *f)
{
	struct llist *l;
	struct screen *s;

	s = SCREEN(f->f_screen);
	for (l = s->s_readers.l_forw; l != &s->s_readers; l = l->l_forw) {
		if (l->l_data == f) {
			ll_delete(l);
			break;
		}
	}
	ASSERT_DEBUG(l != &s->s_readers, "abort_read: missing tran");
}

/*
 * drop_root()
 *	Turn off our root capability
 */
static void
drop_root(void)
{
	struct perm perm;

	zero_ids(&perm, 1);
	perm.perm_len = 0;
	PERM_DISABLE(&perm);
	(void)perm_ctl(1, &perm, (void *)0);
}

/*
 * add_root()
 *	Turn on our root capability
 */
static void
add_root(void)
{
	struct perm perm;

	zero_ids(&perm, 1);
	perm.perm_len = 0;
	(void)perm_ctl(1, &perm, (void *)0);
}

/*
 * send_sig()
 *	Send a signal to a process group
 *
 * We do it via /proc so this can still work when there's more than
 * one node.
 */
static void
send_sig(pid_t pid, char *event)
{
	char buf[128];
	int fd;

	add_root();
	(void) sprintf(buf, "//fs/proc:%lu/notepg", pid);
	fd = open(buf, O_WRITE);
	if (fd >= 0) {
		(void)write(fd, event, strlen(event));
		close(fd);
	}
	drop_root();
}

/*
 * kbd_enqueue()
 *	Get a new character to enqueue
 */
void
kbd_enqueue(struct screen *s, uint c)
{
	char buf[4];
	struct msg m;
	struct file *f;
	struct llist *l;

	/*
	 * If we're currently in "isig" mode, beam a process group
	 * signal to the target.
	 */
	if (s->s_isig && s->s_pgrp) {
		if (c == s->s_intr) {
			send_sig(s->s_pgrp, "intr");
			return;
		} else if (c == s->s_quit) {
			send_sig(s->s_pgrp, "abort");
			return;
		}
	}

	/*
	 * If there's a waiter, just let'em have it
	 */
	l = s->s_readers.l_forw;
	if (l != &s->s_readers) {
		ASSERT_DEBUG(s->s_nbuf == 0, "kbd: waiters with data");

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
		f->f_readcnt = 0;
#ifdef DEBUG
		f->f_sender = 0;
#endif
		return;
	}

	/*
	 * If there's too much queued data, ignore
	 */
	if (s->s_nbuf >= KEYBD_MAXBUF) {
		return;
	}

	/*
	 * Add it to the queue
	 */
	s->s_buf[s->s_hd] = c;
	s->s_hd += 1;
	s->s_nbuf += 1;
	if (s->s_hd >= KEYBD_MAXBUF) {
		s->s_hd = 0;
	}
}
