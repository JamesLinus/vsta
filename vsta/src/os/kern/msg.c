/*
 * msg.c
 *	Message handling routines
 *
 * These implement the primary user system service--message sending
 * and receiving.
 *
 * Message passing is done using memory mapping techniques.  Each
 * segment in a user message gets converted to a pview in terms of
 * the existing page set.  Because it's simply another view, it allows
 * process->process memory copying without the need for the kernel
 * to make an intermediate copy or fault in the pages.
 */
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/msg.h>
#include <sys/fs.h>
#include <sys/percpu.h>
#include <sys/port.h>
#include <sys/thread.h>
#include <sys/assert.h>
#include <sys/malloc.h>
#include <sys/misc.h>
#include <hash.h>
#include "msg.h"

/*
 * queue_msg()
 *	Queue a message, external version
 */
void
queue_msg(struct port *port, struct sysmsg *sm, spl_t exit_state)
{
	inline_queue_msg(port, sm, exit_state);
}

/*
 * lqueue_msg()
 *	Locked version, external
 */
void
lqueue_msg(struct port *port, struct sysmsg *sm)
{
	inline_lqueue_msg(port, sm);
}

/*
 * freesegs()
 *	Release all references indicated by the segments of a sysmsg
 */
inline void
freesegs(struct sysmsg *sm)
{
	uint x;

	for (x = 0; x < sm->sm_nseg; ++x) {
		free_seg(sm->sm_seg[x]);
	}
	sm->sm_nseg = 0;
}

/*
 * unmapsegs()
 *	Unmap and free each segment for this segref
 *
 * Assumes caller has exclusive access to the segref
 */
void
unmapsegs(struct segref *segref)
{
	uint x;
	struct seg *s;

	for (x = 0; x < MSGSEGS; ++x) {
		if ((s = segref->s_refs[x]) == 0) {
			break;
		}
		detach_seg(s);
		free_seg(s);
		segref->s_refs[x] = 0;
	}
}

/*
 * mapsegs()
 *	Map each segment in a message into the user process
 *
 * Records the attachment under the segref structure, which
 * will be cleared of old mappings if necessary.
 */
static int
mapsegs(struct proc *p, struct sysmsg *sm, struct segref *segref)
{
	uint x;
	uint cnt = 0;
	struct seg *s;

	for (x = 0; x < sm->sm_nseg; ++x) {
		s = sm->sm_seg[x];
		if (attach_seg(&p->p_vas, s)) {
			uint y;

			for (y = 0; y < x; ++y) {
				s = sm->sm_seg[y];
				segref->s_refs[y] = 0;
				detach_seg(s);
			}
			return(err(ENOMEM));
		}
		segref->s_refs[x] = s;
		cnt += s->s_len;
	}
	if (sm->sm_nseg != MSGSEGS) {
		segref->s_refs[sm->sm_nseg] = 0;
	}
	return(cnt);
}

/*
 * sm_to_m()
 *	Convert the segment information from the sysmsg format to its
 *	internal msg format
 */
static void
sm_to_m(struct sysmsg *sm)
{
	uint x;
	seg_t *s;
	struct seg *seg;
	struct msg *m = &sm->sm_msg;

	m->m_op &= ~M_READ;
	m->m_nseg = sm->sm_nseg;
	for (x = 0, s = m->m_seg; x < sm->sm_nseg; ++x, ++s) {
		seg = sm->sm_seg[x];
		s->s_buf = (char *)(seg->s_pview.p_vaddr) + seg->s_off;
		s->s_buflen = seg->s_len;
	}
	sm->sm_nseg = 0;
}

/*
 * m_to_sm()
 *	Convert from user segments (msg format) to sysmsg format
 *
 * For !M_READ, creates segments for each chunk of user memory,
 * walks across all the "segments" of the user's message,
 * converts to internal format, and places under the sysmsg.
 *
 * For M_READ, only the header part is copied.  The user buffers
 * will be filled out once an answer comes back from the server.
 *
 * Can sleep in make_seg().
 *
 * On error, sets err() and returns -1.  On success, returns 0.
 */
static int
m_to_sm(struct vas *vas, struct sysmsg *sm)
{
	uint x;
	struct seg *seg;
	seg_t *s;
	struct msg *m = &sm->sm_msg;

	/*
	 * Sanity check # segments
	 */
	if (m->m_nseg > MSGSEGS) {
		return(err(EINVAL));
	}

	/*
	 * Walk user segments, construct struct seg's for each part
	 */
	if (!(sm->sm_op & M_READ)) {
		for (x = 0, s = m->m_seg; x < m->m_nseg; ++x, ++s) {
			/*
			 * On error, have to go back and clean up the
			 * segments we've already constructed.  Then
			 * return error.
			 */
			seg = make_seg(vas, s->s_buf, s->s_buflen);
			if (seg == 0) {
				uint y;

				for (y = 0; y < x; ++y) {
					free_seg(sm->sm_seg[y]);
					sm->sm_seg[y] = 0;
				}
				sm->sm_nseg = 0;
				return(err(EFAULT));
			}
			sm->sm_seg[x] = seg;
		}
		sm->sm_nseg = m->m_nseg;
	} else {
		sm->sm_nseg = 0;
	}
	return(0);
}

/*
 * msg_send()
 *	Send a message to a port
 *
 * msg_send() does the job of validating and packing up the
 * message, and queueing it to the appropriate destination.
 * The receiving side maps it into the process address space.
 * When the receiving side msg_reply()'s, this routine picks up
 * any corresponding reply segments and maps them back into
 * this process' address space.
 */
int
msg_send(port_t arg_port, struct msg *arg_msg)
{
	struct portref *pr;
	struct port *port;
	struct sysmsg sm;
	struct proc *p = curthread->t_proc;
	int error = 0;

	/*
	 * Get message body
	 */
	if (copyin(arg_msg, &sm.sm_msg, sizeof(struct msg))) {
		return(err(EFAULT));
	}

	/*
	 * Protect our reserved messages types.  M_TIME is a special
	 * case because I used to think I'd generate these from the
	 * kernel, but currently I do it through a user-space thread.
	 * I could renumber it out of the reserved range--TBD.
	 *
	 * We mask the low bits to avoid being confused by the
	 * M_READ bit in the higher bits.
	 */
	if ((sm.sm_op & 0xFFF) < M_RESVD) {
		if ((sm.sm_op & 0xFFF) != M_TIME) {
			return(err(EINVAL));
		}
	}

	/*
	 * Construct a system message
	 */
	if (m_to_sm(&p->p_vas, &sm)) {
		error = -1;
		goto out2;
	}

	/*
	 * Validate port ID.  On successful non-poisoned port, the
	 * semaphore is held for our handle on the port.
	 */
	pr = find_portref(p, arg_port);
	if (pr == 0) {
		/*
		 * find_portref() sets err() for us
		 */
		error = -1;
		goto out2;
	}

	/*
	 * Get the port, I/O error if the server's gone
	 */
	port = pr->p_port;
	if (port == 0) {
		v_lock(&pr->p_lock, SPL0);
		return(err(EIO));
	}

	/*
	 * Record us as sender
	 */
	sm.sm_sender = pr;

	/*
	 * Set up our message transfer state
	 */
	ASSERT_DEBUG(sema_count(&pr->p_iowait) == 0, "msg_send: p_iowait");
	pr->p_state = PS_IOWAIT;

	/*
	 * Put message on queue
	 */
	inline_queue_msg(port, &sm, SPL0);

	/*
	 * Now wait for the I/O to finish or be interrupted
	 */
	if (p_sema_v_lock(&pr->p_iowait, PRICATCH, &pr->p_lock)) {
		/*
		 * Oops.  Interrupted.  Grapple with the server for
		 * control of the in-progress message.  Re-grab the
		 * port pointer, which may have changed (i.e., been
		 * zeroed) from under us.
		 */
		p_lock_void(&pr->p_lock, SPL0_SAME);
		port = pr->p_port;

		/*
		 * Based on the state, either abort the I/O or
		 * accept (and ignore) the completion.
		 */
		switch (pr->p_state) {
		case PS_IOWAIT: {
			struct sysmsg sm2, *s;

			/*
			 * Server gone--just I/O err
			 */
			if (!port) {
				pr->p_state = PS_IODONE;
				v_lock(&pr->p_lock, SPL0_SAME);
				break;
			}

			/*
			 * If our message has not yet been dequeued,
			 * pull it out of the queue now.
			 */
			p_lock_void(&port->p_lock, SPLHI);

			/*
			 * Head of queue--take out.  Tail was either
			 * this message (queue now empty) or is still
			 * valid.
			 */
			s = port->p_hd;
			if (s == &sm) {
				port->p_hd = sm.sm_next;
			} else {
				/*
				 * Otherwise hunt it down in the queue and
				 * remove it.
				 */
				while (s) {
					if (s->sm_next == &sm) {
						s->sm_next = sm.sm_next;
						if (port->p_tl == &sm) {
							port->p_tl = s;
						}
						break;
					}
					s = s->sm_next;
				}
			}
			v_lock(&port->p_lock, SPL0);

			/*
			 * We found ourselves in the queue, so no need
			 * to interact with the server.
			 */
			if (s) {
				/*
				 * Adjust semaphore count.  We *know*
				 * it's > 0, since our message was
				 * in the queue unconsumed.  All I/O
				 * semaphore access is done holding
				 * the port lock, so we're OK.
				 */
				ASSERT_DEBUG(sema_count(&port->p_wait) > 0,
					"msg_send: qcnt < 1");
				adj_sema(&port->p_wait, -1);
				pr->p_state = PS_IODONE;
				v_lock(&pr->p_lock, SPL0_SAME);
				break;
			}

			/*
			 * Send an M_ABORT and then wait for completion
			 * ignoring further interrupts.
			 */
			sm2.sm_sender = pr;
			sm2.sm_op = M_ABORT;
			sm2.sm_nseg = sm2.sm_arg = sm2.sm_arg1 = 0;
			queue_msg(port, &sm2, SPL0);
			pr->p_state = PS_ABWAIT;
			p_sema_v_lock(&pr->p_iowait, PRIHI, &pr->p_lock);
			break;
		}

		case PS_IODONE:
			/*
			 * We raced with server completion.  The transaction
			 * is complete, but we still return an error.  The
			 * server already V'ed our iowait sema, so clear
			 * it for next time.
			 */
			set_sema(&pr->p_iowait, 0);
			v_lock(&pr->p_lock, SPL0_SAME);
			break;
		default:
			ASSERT(0, "msg_send: illegal PS state");
			break;
		}

		/*
		 * Done with I/O on port.  Return error.
		 */
		error = err(EINTR);
		goto out1;
	}

	/*
	 * If the server indicates error, set it and leave
	 */
	if (sm.sm_arg == -1) {
		error = err(sm.sm_err);
		goto out1;
	}

	if (sm.sm_nseg) {
		struct segref segrefs;

		/*
		 * Attach the returned segments to the user address
		 * space.
		 */
		segrefs.s_refs[0] = 0;
		if (mapsegs(p, &sm, &segrefs) == -1) {
			error = -1;
			goto out1;
		}

		/*
		 * Copy out segments to user's buffer, then let them go
		 */
		error = copyoutsegs(&sm);
		unmapsegs(&segrefs);
		if (error == -1) {
			sm.sm_nseg = 0;	/* unmapsegs() got'em */
			goto out1;
		}

		/*
		 * Translate rest of message to user format, give back
		 * to user.
		 */
		sm_to_m(&sm);
		if (copyout(arg_msg, &sm.sm_msg, sizeof(struct msg))) {
			error = err(EFAULT);
			goto out1;
		}
	}

	/*
	 * If return value is not otherwise indicated, use m_arg
	 */
	if (error == 0) {
		error = sm.sm_arg;
	}

out1:
	/*
	 * Clean up and return success/failure
	 */
	if (sm.sm_msg.m_nseg) {
		v_sema(&pr->p_svwait);
	}
	v_sema(&pr->p_sema);

out2:
	if (sm.sm_nseg) {
		freesegs(&sm);
	}
	return(error);
}

/*
 * new_client()
 *	Record a new client for this server port
 */
static inline void
new_client(struct portref *pr)
{
	struct proc *p = curthread->t_proc;

	p_sema(&p->p_sema, PRIHI);
	ASSERT_DEBUG(!hash_lookup(p->p_prefs, (long)pr),
		"new_client: already hashed");
	hash_insert(p->p_prefs, (long)pr, pr);
	v_sema(&p->p_sema);
}

/*
 * del_client()
 *	Delete an existing client from our hash
 */
inline static void
del_client(struct proc *p, struct portref *pr)
{
	p_sema(&p->p_sema, PRIHI);
	ASSERT_DEBUG(hash_lookup(p->p_prefs, (long)pr),
		"del_client: can't find");
	hash_delete(p->p_prefs, (long)pr);
	v_sema(&p->p_sema);
	unmapsegs(&pr->p_segs);
	free_portref(pr);
}

/*
 * receive_isr()
 *	Copy out an M_ISR message to the user
 */
static inline int
receive_isr(int isr, int nintr, struct msg *arg_msg)
{
	struct msg m;

	/*
	 * Build a user message
	 */
	m.m_op = M_ISR;
	m.m_arg = isr;
	m.m_arg1 = nintr;
	m.m_nseg = 0;
	m.m_sender = 0;

	/*
	 * Send it out to him
	 */
	return(copyout(arg_msg, &m, sizeof(struct msg)));
}

/*
 * msg_receive()
 *	Receive next message from queue
 */
int
msg_receive(port_t arg_port, struct msg *arg_msg)
{
	struct port *port;
	struct proc *p = curthread->t_proc;
	struct sysmsg *sm;
	int error = 0;
	struct portref *pr;

	/*
	 * Look up port, become sole process doing I/O through it
	 */
	port = find_port(p, arg_port);
	if (!port) {
		return(-1);
	}

	/*
	 * Wait for something to arrive for us
	 */
	if (p_sema_v_lock(&port->p_wait, PRICATCH, &port->p_lock)) {
		/*
		 * Interrupted system call.
		 */
		v_sema(&port->p_sema);
		return(err(EINTR));
	}

	p_lock_void(&port->p_lock, SPLHI);
	ASSERT_DEBUG(port->p_hd,
		"msg_receive: p_wait/p_hd disagree");

	/*
	 * Extract next message, then release port
	 */
	sm = port->p_hd;
	port->p_hd = sm->sm_next;

	/*
	 * With lock held, at SPLHI, check for M_ISR.  These are
	 * special messages we handle carefully to avoid losing
	 * an interrupt.
	 */
	if (sm->sm_op == M_ISR) {
		int isr, nintr;

		/*
		 * Record ISR and count before we release the sysmsg
		 * back for further use.
		 */
		isr = sm->sm_arg;
		nintr = sm->sm_arg1;

		/*
		 * Flag sysmsg as "off the queue"
		 */
		sm->sm_op = 0;
		v_lock(&port->p_lock, SPL0);

		/*
		 * Make sure we don't fiddle with the sysmsg any more.
		 * Call our function to pass out an ISR notification.
		 */
		sm = 0;
		error = receive_isr(isr, nintr, arg_msg);
		goto out;
	}

	/*
	 * Have our message, flag that we're running with it.
	 */
	pr = sm->sm_sender;
	pr->p_msg = sm;

	/*
	 * Connect messages are special; the buffer is the array
	 * of permissions which the connector possesses.  The portref
	 * is new and must be added to our hash.
	 */
	if (sm->sm_op == M_CONNECT) {
		extern struct seg *kern_mem();

		ref_port(port, pr);
		v_lock(&port->p_lock, SPL0);
		new_client(pr);

		sm->sm_seg[0] = kern_mem(sm->sm_seg[0], sm->sm_arg1);
		sm->sm_nseg = 1;

	/*
	 * Disconnect message mean we remove the portref from
	 * our hash, tell our server he's gone, and let the
	 * client complete his disconnect and free his portref.
	 */
	} else if (sm->sm_op == M_DISCONNECT) {
		ASSERT_DEBUG(pr->p_state == PS_CLOSING,
			"msg_receive: DISC but not CLOSING");
		ASSERT_DEBUG(pr->p_port == port,
			"msg_receive: DISC pr mismatch");
		deref_port(port, pr);
		v_lock(&port->p_lock, SPL0);
		del_client(p, pr);
		FREE(sm, MT_SYSMSG);
	} else {
		v_lock(&port->p_lock, SPL0);
	}

	/*
	 * We now have a message, and are running under an address space
	 * into which we now may want to map the parts of the message.
	 */
	if (sm->sm_nseg) {
		if (pr->p_segs.s_refs[0]) {
			unmapsegs(&pr->p_segs);
		}
		error = mapsegs(p, sm, &pr->p_segs);
		if (error == -1) {
			goto out;
		}
	}
	sm_to_m(sm);
	if (!copyout(arg_msg, &sm->sm_msg, sizeof(struct msg))) {
		/*
		 * All completed successfully - release the
		 * port semaphore and return
		 */
		v_sema(&port->p_sema);
		return(error);
	} else {
		error = err(EFAULT);
	}

	/*
	 * All done.  Release the port semaphore and report our
	 * failure (we've already returned if we were successful
	 */
out:
	v_sema(&port->p_sema);
	if (sm && sm->sm_nseg) {
		freesegs(sm);
	}
	return(error);
}

/*
 * msg_reply()
 *	Reply to a message received through msg_receive()
 */
int
msg_reply(long arg_who, struct msg *arg_msg)
{
	struct proc *p = curthread->t_proc;
	struct portref *pr;
	struct sysmsg sm, *om;
	int error = 0;

	/*
	 * Get a copy of the user's reply message
	 */
	if (copyin(arg_msg, &sm.sm_msg, sizeof(struct msg))) {
		return(err(EFAULT));
	}

	/*
	 * Try to map segments into sysmsg format
	 */
	if (m_to_sm(&p->p_vas, &sm)) {
		error = -1;
		goto out;
	}

	/*
	 * Lock down proc and try to map arg_who onto a known
	 * connection.  Ironically, arg_who *is* the address of
	 * the portref, but we can't trust it until we've found
	 * it in our hash table.
	 */
	p_sema(&p->p_sema, PRIHI);
	pr = hash_lookup(p->p_prefs, arg_who);
	if (pr) {
		unmapsegs(&pr->p_segs);
		p_lock_void(&pr->p_lock, SPL0_SAME);
		v_sema(&p->p_sema);
	} else {
		/*
		 * If we didn't find the portref, bounce them.
		 */
		v_sema(&p->p_sema);
		error = err(EINVAL);
		goto out;
	}

	/*
	 * We now have the portref locked, and the reply message
	 * in hand.  Take action based on the state of the portref
	 * and the type of message we're delivering.
	 */
	switch (pr->p_state) {
	/*
	 * The usual case--he's waiting for our message.  Give it
	 * to him, and wake him up.  We wait for him to copy out
	 * the message, and let us free.
	 */
	case PS_IOWAIT:
		/*
		 * Make sure we're replying to the right client;
		 * a buggy server could specify the wrong one, which
		 * we detect by p_msg still being NULL.
		 */
		om = pr->p_msg;
		if (!om) {
			error = err(EINVAL);
			v_lock(&pr->p_lock, SPL0_SAME);
			break;
		}
		if ((om->sm_op == M_DUP) && (sm.sm_arg != -1)) {
			struct port *port = pr->p_port;
			struct portref *newpr = (struct portref *)
				(om->sm_arg);

			ASSERT_DEBUG(newpr->p_port == port,
				"msg_reply: newpr != port");
			p_lock_void(&port->p_lock, SPLHI);
			ref_port(port, newpr);
			v_lock(&port->p_lock, SPL0);
			v_lock(&pr->p_lock, SPL0_SAME);
			v_sema(&pr->p_iowait);
			new_client(newpr);
		} else {
			/*
			 * Give him the parts of the sysmsg
			 * that he needs from us
			 */
			om->sm_arg = sm.sm_arg;
			om->sm_arg1 = sm.sm_arg1;
			om->sm_nseg = sm.sm_nseg;
			om->sm_sender = sm.sm_sender;
			om->sm_segs = sm.sm_segs;
			om->sm_errs = sm.sm_errs;
			
			sm.sm_nseg = 0;		/* He has them */
			pr->p_msg = 0;

			/*
			 * Let him run.  We interlock with him if we
			 * have handed segments for him to consume;
			 * otherwise, just release him and return.
			 */
			pr->p_state = PS_IODONE;
			if (om->sm_nseg) {
				set_sema(&pr->p_svwait, 0);
				v_sema(&pr->p_iowait);
				p_sema_v_lock(&pr->p_svwait,
					PRIHI, &pr->p_lock);
			} else {
				v_sema(&pr->p_iowait);
				v_lock(&pr->p_lock, SPL0_SAME);
			}
			return(0);

			/*
			 * As we have no segments now we can take an
			 * early exit out of the function
			 */
			return(0);
		}
		break;

	/*
	 * He's requested an abort.  Flag confirmation if this is
	 * an M_ABORT message response, otherwise discard this
	 * message so he can continue to wait for the M_ABORT
	 * response.
	 */
	case PS_ABWAIT:
		if (sm.sm_op != M_ABORT) {
			error = err(EIO);
			v_lock(&pr->p_lock, SPL0_SAME);
			goto out;
		}
		pr->p_state = PS_ABDONE;
		v_sema(&pr->p_iowait);
		v_lock(&pr->p_lock, SPL0_SAME);
		break;

	/*
	 * The server's hosed.  It would appear he's msg_reply()'ed to
	 * a client who's already finished.
	 */
	case PS_IODONE:
	case PS_ABDONE:
	default:
		error = err(EINVAL);
		v_lock(&pr->p_lock, SPL0_SAME);
		break;
	}

	/*
	 * All done.  Release locks, free memory, return result
	 */
out:
	if (sm.sm_nseg) {
		freesegs(&sm);
	}
	return(error);
}
