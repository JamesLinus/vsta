/*
 * msgkern.c
 *	Routines to support message passing within the kernel
 */
#include <sys/msg.h>
#include <sys/port.h>
#include <sys/malloc.h>
#include <sys/assert.h>
#include "msg.h"

/*
 * kernmsg_send()
 *	Send a message to a port, within the kernel
 *
 * Only useful for messages without segments.  Assumes caller has
 * already interlocked on the use of the portref.
 *
 * args[] receives m_arg, m_arg1, and m_op.  It must have room
 * for these 3 longs.
 */
kernmsg_send(struct portref *pr, int op, long *args)
{
	struct sysmsg sm;

	/*
	 * Construct a system message
	 */
	sm.sm_sender = pr;
	sm.sm_op = op;
	sm.sm_nseg = 0;
	sm.sm_arg = args[0];
	sm.sm_arg1 = args[1];
	pr->p_msg = &sm;

	/*
	 * Interlock with server
	 */
	p_lock_fast(&pr->p_lock, SPL0);

	/*
	 * If port gone, I/O error
	 */
	if (pr->p_port == 0) {
		v_lock(&pr->p_lock, SPL0_SAME);
		return(1);
	}

	/*
	 * Set up our message transfer state
	 */
	ASSERT_DEBUG(sema_count(&pr->p_iowait) == 0, "kernmsg_send: p_iowait");
	pr->p_state = PS_IOWAIT;

	/*
	 * Put message on queue
	 */
	queue_msg(pr->p_port, &sm, SPL0);

	/*
	 * Now wait for the I/O to finish or be interrupted
	 */
	p_sema_v_lock(&pr->p_iowait, PRIHI, &pr->p_lock);

	/*
	 * Release the server
	 */
	v_sema(&pr->p_svwait);

	/*
	 * If the server indicates error, set it and leave
	 */
	if (sm.sm_arg == -1) {
		return(1);
	}
	ASSERT(sm.sm_nseg == 0, "kernmsg_send: got segs back");
	args[0] = sm.sm_arg;
	args[1] = sm.sm_arg1;
	args[2] = sm.sm_op;

	/*
	 * Return success!
	 */
	return(0);
}
