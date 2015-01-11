/*
 * rw.c
 *	Reads and writes to the 3c509
 */
#include <sys/fs.h>
#include <sys/assert.h>
#include <std.h>
#include <llist.h>
#include "el3.h"
#ifdef DEBUG
#include <stdio.h>	/* For printf */
#endif

#define ntohs(x) ((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF))
#define MAXQUEUE (64)		/* Max packets we buffer for our clients */

/*
 * Queued data for later readers
 */
struct bufq {
	void *q_buf;
	int q_len;
	struct llist *q_queue;
};

/*
 * Busy/waiter flags
 */
int tx_busy[NEL3];		/* Tx Busy for each unit */
struct llist writers[NEL3];	/* Who's waiting on each unit */
struct llist readers;		/* Who's waiting */
struct llist files;
ulong dropped;			/* # packets without reader */
static struct llist rxqueue;	/* Queued receive packets */
static int nrxqueue;		/*  ...# in queue */
void *pak_pool;			/* Packet memory for queueing receive */

/*
 * run_queue()
 *	If there's stuff in writers queue, launch next
 */
void
run_queue(int unit)
{
	struct file *f;
	struct msg m;

	/*
	 * Nobody waiting--adapter falls idle
	 */
	if (writers[unit].l_forw == &writers[unit]) {
		return;
	}

	/*
	 * Remove next from list
	 */
	f = writers[unit].l_forw->l_data;
	ASSERT_DEBUG(f->f_io, "run_queue: on queue !f_io");
	ll_delete(f->f_io);
	f->f_io = 0;

	/*
	 * Launch I/O
	 */
	tx_busy[unit] = 1;
	el3_start(&adapters[unit], f);

	/*
	 * Success.
	 */
	m.m_arg = 0;
	m.m_nseg = 0;
	m.m_arg1 = 0;
	msg_reply(f->f_msg.m_sender, &m);
}

/*
 * rw_init()
 *	Initialize our queue data structures
 */
void
rw_init(void)
{
	int unit;

	for(unit = 0; unit < NEL3; unit++) {
		ll_init(&writers[unit]);
	}
	ll_init(&readers);
	ll_init(&rxqueue);
}

/*
 * el3_write()
 *	Write to an open file
 */
void
el3_write(struct msg *m, struct file *f)
{
	struct attach *o = f->f_file;
	int unit = o->a_unit;

	/*
	 * Can only write to a true file, and only if open for writing.
	 */
	if (!o || !(f->f_perm & ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Queue write, fail if we can't insert list element (VM
	 * exhausted?)
	 */
	ASSERT_DEBUG(f->f_io == 0, "el3_write: busy");
	f->f_io = ll_insert(&writers[unit], f);
	if (f->f_io == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}
	f->f_msg = *m;
	f->f_msg.m_arg = 0;

	if (!tx_busy[unit]) {
		run_queue(unit);
	}

	return;
}

/*
 * el3_read()
 *	Read bytes out of the current attachment or directory
 *
 * Directories get their own routine.
 */
void
el3_read(struct msg *m, struct file *f)
{
	struct attach *o;

	/*
	 * Directory--only one is the root
	 */
	if ((o = f->f_file) == 0) {
		el3_readdir(m, f);
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
	 * Queue as a reader
	 */
	ASSERT_DEBUG(f->f_io == 0, "el3_read: busy");
	f->f_io = ll_insert(&readers, f);
	if (f->f_io == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}
	bcopy(m, &f->f_msg, sizeof(struct msg));

	/*
	 * If there's queued packets, run through them
	 */
	if (nrxqueue) {
		struct llist *l = LL_NEXT(&rxqueue);
		struct bufq *q = l->l_data;

		/*
		 * If we pushed it up, free the resources
		 */
		if (!el3_send_up(q->q_buf, q->q_len, 1)) {
			/*
			 * Put the packet body on our local pool
			 */
			*(void **)q->q_buf = pak_pool;
			pak_pool = q->q_buf;

			/*
			 * Delete us from the rxqueue list
			 */
			ll_delete(l);
			free(q);

			/*
			 * And update the length of that list
			 */
			nrxqueue -= 1;
		}
	}
}

/*
 * el3_send_up()
 *	Send received packet to requestor(s)
 *
 * Given a buffer containing a received packet, walk the list of pending
 * readers and return packet if a matching type is found.  If none is
 * found, the packet is dropped.
 * len is length of packet data, excluding header, type and checksum.
 *
 * Returns 0 if buffer may be reused; 1 if a it will be held and
 * free()'ed later.
 */
int
el3_send_up(char *buf, int len, int retrans)
{
	struct llist *l, *ln;
	struct ether_header *eh;
	ushort etype;
	int sent;
	struct bufq *q;

	/*
	 * Fast discard for null packets
	 */
	if (len == 0) {
		return(0);
	}

	/*
	 * Walk pending reader list
	 */
	eh = (struct ether_header *)buf;
	etype = ntohs(eh->ether_type);
	sent = 0;
	for (l = LL_NEXT(&readers); l != &readers; l = ln) {
		struct file *f;
		ushort t2;

		/*
		 * Get next pointer now, before we might delete
		 */
		ln = LL_NEXT(l);

		/*
		 * Skip those who only have the directory itself
		 * open.
		 */
		f = l->l_data;
		ASSERT_DEBUG(f->f_file, "el3_send_up: dir");

		/*
		 * Give him the packet if he wants all (type 0) or
		 * has the right type open.
		 */
		t2 = f->f_file->a_type;
		if (!t2 || (t2 == etype)) {
			struct msg *m;

			m = &f->f_msg;
			if (m->m_nseg == 0) {
				/*
				 * He didn't provide a buffer, send
				 * him ours.
				 */
				m->m_nseg = 1;
				m->m_buf = buf;
				m->m_buflen = len;
			} else {
				/*
				 * Otherwise copy out our buffer to
				 * his (we're a DMA server) and go
				 * on.
				 */
				m->m_nseg = 0;
				bcopy(buf, m->m_buf, len);
			}
			m->m_arg = len;
			m->m_arg1 = 0;
			msg_reply(m->m_sender, m);
			ll_delete(l);
			ASSERT_DEBUG(f->f_io == l, "el3_send_up: mismatch");
			f->f_io = 0;
			sent += 1;
		}
	}

	/*
	 * It was consumed; let him continue with the buffer.
	 */
	if (sent) {
		return(0);
	}

	/*
	 * If this is already in our queue, don't bother re-queueing
	 */
	if (retrans) {
		return(1);
	}

	/*
	 * Nobody consumed it; make a copy unless we're too far
	 * ahead.
	 */
	if (nrxqueue > MAXQUEUE) {
		dropped += 1;
		return(0);
	}

	/*
	 * Queue it.
	 */
	nrxqueue += 1;
	q = malloc(sizeof(struct bufq));
	q->q_buf = buf;
	q->q_len = len;
	q->q_queue = ll_insert(&rxqueue, q);

	/*
	 * We're keeping it.
	 */
	return(1);
}
