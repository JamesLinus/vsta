/*
 * rw.c
 *	Routines for doing R/W ticktock ops
 *
 * Actually, just read.  Writing isn't supported.
 */
#include "tick.h"
#include <hash.h>
#include <llist.h>
#include <std.h>
#include <stdio.h>
#include <time.h>

/*
 * Sorted queue of sleepers waiting for completion
 */
static struct llist waiting;

/*
 * empty_queue()
 *	Take elements off and complete them
 */
void
empty_queue(void)
{
	struct llist *l, *next;
	struct file *f;
	struct msg m;
	time_t t;

	time(&t);
	for (l = LL_NEXT(&waiting); l != &waiting; l = next) {
		f = l->l_data;
		next = LL_NEXT(l);

		/*
		 * Take the requester, let him through
		 */
		ll_delete(f->f_entry);
		f->f_entry = 0;
		m.m_buf = &t;
		m.m_buflen = m.m_arg = sizeof(t);
		m.m_nseg = 1;
		m.m_arg1 = 0;
		msg_reply(f->f_sender, &m);
	}
}

/*
 * tick_read()
 *	Put us on the queue for the next tick
 */
void
tick_read(struct msg *m, struct file *f)
{
	/*
	 * Try to queue up
	 */
	f->f_entry = ll_insert(&waiting, f);
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
 * tick_abort()
 *	Abort a requested tickphore operation
 */
void
tick_abort(struct msg *m, struct file *f)
{
	if (f->f_entry) {
		ll_delete(f->f_entry);
		f->f_entry = 0;
	}
	m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * rw_init()
 *	Set up our linked list
 */
void
rw_init(void)
{
	ll_init(&waiting);
}
