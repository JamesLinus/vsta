/*
 * rw.c
 *	Routines for doing R/W selfsphore ops
 */
#include "selfs.h"
#include <hash.h>
#include <llist.h>
#include <std.h>
#include <stdio.h>

#define MAXIO (32*1024)		/* Max select_event message size */

/*
 * run_queue()
 *	Take elements off and let him go
 */
static void
run_queue(struct file *f)
{
	struct llist *l;
	struct msg m;
	struct select_complete *buf = 0, *sc;
	struct event *e;
	uint nentry = 0;

	/*
	 * Assemble all events into a buffer
	 */
	while (!LL_EMPTY(&f->f_events) &&
			(sizeof(struct select_complete) <= f->f_size)) {
		/*
		 * Get next, remove from list
		 */
		l = LL_NEXT(&f->f_events);
		e = l->l_data;
		ll_delete(l);

		/*
		 * Assemble completions
		 */
		nentry += 1;
		buf = realloc(buf, sizeof(struct select_complete) * nentry);
		sc = buf + (nentry-1);
		sc->sc_index = e->e_index;
		sc->sc_mask = e->e_mask;
		sc->sc_iocount = e->e_iocount;
		f->f_size -= sizeof(struct select_complete);
	}

	/*
	 * Send back completion to client
	 */
	m.m_buf = buf;
	m.m_arg = m.m_buflen = nentry * sizeof(struct select_complete);
	m.m_nseg = 1;
	m.m_arg1 = 0;
	msg_reply(f->f_sender, &m);
	f->f_size = 0;
}

/*
 * selfs_read()
 *	Request blocking for select() conditions
 */
void
selfs_read(struct msg *m, struct file *f)
{
	/*
	 * Invalid except for clients
	 */
	if ((f->f_mode != MODE_CLIENT) || (m->m_arg < 1) ||
			(m->m_arg > MAXIO)) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * If there's stuff for this client, send it back now
	 */
	f->f_size = m->m_arg;
	if (!LL_EMPTY(&f->f_events)) {
		run_queue(f);
		/* run_queue clears f->f_size */
		return;
	}

	/*
	 * Otherwise begin waiting
	 */
}

/*
 * selfs_write()
 *	Accept new server events, think about completing clients
 *
 * A server can send in more than one event before a client consumes anything;
 * we collapse all these updates into a single entry.
 */
void
selfs_write(struct msg *m, struct file *f)
{
	struct event *e;
	struct select_event *buf, *se;
	uint x, nevent;
	struct file *f2;
	struct llist *l;

	/*
	 * Cap size, and verify alignment
	 */
	if (m->m_arg > MAXIO) {
		msg_err(m->m_sender, E2BIG);
		return;
	}
	if ((m->m_arg % sizeof(struct select_event)) != 0) {
		msg_err(m->m_sender, ENOEXEC);
		return;
	}

	/*
	 * Assemble a buffer if needed
	 */
	if (m->m_nseg > 1) {
		se = buf = malloc(m->m_arg);
		seg_copyin(m->m_seg, m->m_nseg, buf, m->m_arg);
	} else {
		buf = 0;
		se = m->m_buf;
	}

	/*
	 * Process each event
	 */
	nevent = m->m_arg / sizeof(struct select_event);
	for (x = 0; x < nevent; ++x, ++se) {
		/*
		 * Look at the indicated client.  Verify key
		 * and its client status.
		 */
		f2 = hash_lookup(filehash, se->se_clid);
		if ((f2 == 0) || (f2->f_key != se->se_key) ||
				(f2->f_mode != MODE_CLIENT)) {
			msg_err(m->m_sender, EINVAL);
			free(buf);
			return;
		}

		/*
		 * See if we already have something in the queue for
		 * the client; we'll just overwrite it in place.
		 */
		e = 0;
		for (l = LL_NEXT(&f2->f_events); l != &f2->f_events;
				l = LL_NEXT(l)) {
			e = l->l_data;
			if (e->e_index == se->se_index) {
				break;
			}
		}

		/*
		 * If we're not in the queue yet, create an entry
		 */
		if (e == 0) {
			e = malloc(sizeof(struct event));
			ll_insert(&f2->f_events, e);
		}

		/*
		 * Fill in the data
		 */
		e->e_index = se->se_index;
		e->e_iocount = se->se_iocount;
		e->e_mask = se->se_mask;

		/*
		 * If there's a client waiting, get this out to them
		 */
		if (f2->f_size) {
			run_queue(f2);
		}
	}

	/*
	 * Free memory (if any)
	 */
	free(buf);

	/*
	 * Thanks for telling us
	 */
	m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * selfs_abort()
 *	Abort a requested selfsphore operation
 */
void
selfs_abort(struct msg *m, struct file *f)
{
	f->f_size = 0;
	m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}
