/*
 * rw.c
 *	Routines for doing R/W ops
 */
#include "selfs.h"
#include <hash.h>
#include <llist.h>
#include <std.h>
#include <stdio.h>
#include <sys/assert.h>
#include <time.h>
#include <sys/syscall.h>
#include <syslog.h>

#define MAXIO (32*1024)		/* Max select_event message size */

long timer_handle;		/* Handle for waking up timer slave */
pid_t timer_tid;		/*  ...its thread ID */

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
	m.m_op = FS_READ | M_READ;
	m.m_buf = buf;
	m.m_arg = m.m_buflen = nentry * sizeof(struct select_complete);
	m.m_nseg = 1;
	m.m_arg1 = 0;
	if (msg_reply(f->f_sender, &m) < 0) {
		perror("msg_reply");
	}
	f->f_size = 0;
	free(buf);

	/*
	 * Take them off the timer list if necessary
	 */
	if (f->f_timeq) {
		ll_delete(f->f_timeq);
		f->f_timeq = NULL;
	}
}

/*
 * soonest()
 *	Tell if the named time is sooner than any on the list
 */
static int 
soonest(struct time *t)
{
	struct llist *l;
	struct file *f;
	struct time *tp;

	for (l = LL_NEXT(&time_waiters); l != &time_waiters;
			l = LL_NEXT(l)) {
		f = l->l_data;
		tp = &f->f_time;
		if ((t->t_sec > tp->t_sec) ||
				((t->t_sec == tp->t_sec) &&
				 (t->t_usec >= tp->t_usec))) {
			 return(0);
		 }
	}
	return(1);
}

/*
 * timer_slave()
 *	Code which the slave timing thread executes
 *
 * Basically, wait for the required interval, then send a SEL_TIME message.
 */
static void
timer_slave(ulong arg)
{
	static port_t p = -1;
	struct msg m;
	int x;

	/*
	 * Connect to our server
	 */
	if (p < 0) {
		p = msg_connect(fsname, ACC_WRITE);
		if (p < 0) {
			syslog(LOG_CRIT, "timer thread can't connect");
			_exit(1);
		}
	}

	for (;;) {
		/*
		 * Wait for the required time
		 */
		__msleep(arg);

		/*
		 * Send the message
		 */
		m.m_op = SEL_TIME;
		m.m_arg = m.m_arg1 = m.m_nseg = 0;
		x = msg_send(p, &m);
		if (x == -1) {
			syslog(LOG_CRIT,
				"timer thread can't deliver event");
			_exit(1);
		}

		/*
		 * We get back the next interval
		 */
		arg = (uint)x;
	}
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
	 * If they only wanted to poll, complete with no
	 * events.
	 */
	if (m->m_arg1 == -1) {
		f->f_size = 0;
		m->m_arg = m->m_arg1 = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Otherwise begin waiting
	 */
	if (m->m_arg1) {
		/*
		 * Convert to absolute time value
		 */
		(void)time_get(&f->f_time);
		f->f_time.t_sec += m->m_arg1 / 1000;
		f->f_time.t_usec += ((m->m_arg1 % 1000) * 1000);
		while (f->f_time.t_usec > 1000000) {
			f->f_time.t_sec += 1;
			f->f_time.t_usec -= 1000000;
		}

		/*
		 * If we have a timer thread idle, wake it up on this
		 * interval.
		 */
		if (timer_handle) {
			m->m_arg = m->m_arg1;
			m->m_nseg = 0;
			msg_reply(timer_handle, m);
			timer_handle = 0;

		/*
		 * Not idle; we need to figure out if our timeout is
		 * sooner than what he's currently looking at.  If it
		 * is, we have to kill the thread and launch it afresh.
		 */
		} else if (timer_tid) {
			if (soonest(&f->f_time)) {
				notify(0, timer_tid, "kill");
				timer_tid = tfork(timer_slave, m->m_arg1);
			}

		/*
		 * Otherwise we need to kick off the thread in the
		 * first place.
		 */
		} else {
			timer_tid = tfork(timer_slave, m->m_arg1);
		}

		/*
		 * Timed wait, so list them on the timed queue
		 */
		ASSERT_DEBUG(f->f_timeq == NULL,
			"selfs_read: already queued");
		f->f_timeq = ll_insert(&time_waiters, f);
	}
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
 *	Abort a requested select() operation
 */
void
selfs_abort(struct msg *m, struct file *f)
{
	f->f_size = 0;
	m->m_arg = m->m_arg1 = m->m_nseg = 0;
	if (f->f_timeq) {
		ll_delete(f->f_timeq);
		f->f_timeq = NULL;
	}
	msg_reply(m->m_sender, m);
}

/*
 * timeout()
 *	Do timeout action for a client
 */
static void
timeout(struct file *f)
{
	struct msg m;

	/*
	 * Wake up client with no select() events
	 */
	m.m_op = m.m_arg = m.m_arg1 = m.m_nseg = 0;
	msg_reply(f->f_sender, &m);

	/*
	 * Remove him from our sleeping list
	 */
	ll_delete(f->f_timeq);
	f->f_timeq = NULL;
	f->f_size = 0;
}

/*
 * selfs_timeout()
 *	A timeout interval has passed
 */
void
selfs_timeout(struct msg *m)
{
	struct llist *l, *ln;
	struct time t, *tp, next;
	struct file *f;

	/*
	 * Record the current time, clear our note of the next
	 */
	(void)time_get(&t);
	next.t_sec = 0; next.t_usec = 0;

	/*
	 * Scan our timeout-waiting clients
	 */
	for (l = LL_NEXT(&time_waiters); l != &time_waiters; l = ln) {
		/*
		 * Look at next client
		 */
		ln = LL_NEXT(l);
		f = l->l_data;
		ASSERT_DEBUG(f->f_timeq,
			"selfs_timeout: on queue !f_timeq");

		/*
		 * See if he's expired
		 */
		tp = &f->f_time;
		if ((tp->t_sec < t.t_sec) || ((tp->t_sec == t.t_sec) &&
				(tp->t_usec <= t.t_usec))) {
			timeout(f);

		/*
		 * If not, note the next nearest expiration time
		 */
		} else if ((tp->t_sec < next.t_sec) ||
				((tp->t_sec == next.t_sec) &&
				 (tp->t_usec <= next.t_usec))) {
		 	next = t;
		}
	}

	/*
	 * Tell our slave timer thread when next to interrupt...
	 * just leave the SEL_TIME request uncompleted if we don't
	 * have a timed request right now.
	 */
	if ((next.t_sec == 0) && (next.t_usec == 0)) {
		timer_handle = m->m_sender;
	} else {
		timer_handle = 0;
		m->m_arg = (next.t_sec - t.t_sec) * 1000 +
			(next.t_usec - t.t_usec) / 1000;
		m->m_arg1 = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
	}
}
