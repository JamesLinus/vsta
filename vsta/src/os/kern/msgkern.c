/*
 * msgkern.c
 *	Routines to support message passing within the kernel
 */
#include <sys/msg.h>
#include <sys/port.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/assert.h>

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
	struct sysmsg *sm;
	int holding_pr = 0, error = 0;

	/*
	 * Construct a system message
	 */
	sm = MALLOC(sizeof(struct sysmsg), MT_SYSMSG);
	sm->m_sender = pr;
	sm->m_op = op;
	sm->m_nseg = 0;
	sm->m_arg = args[0];
	sm->m_arg1 = args[1];
	pr->p_msg = sm;

	/*
	 * Interlock with server
	 */
	p_lock(&pr->p_lock, SPL0); holding_pr = 1;

	/*
	 * If port gone, I/O error
	 */
	if (pr->p_port == 0) {
		error = 1;
		goto out;
	}

	/*
	 * Set up our message transfer state
	 */
	set_sema(&pr->p_iowait, 0);
	pr->p_state = PS_IOWAIT;

	/*
	 * Put message on queue
	 */
	queue_msg(pr->p_port, sm);

	/*
	 * Now wait for the I/O to finish or be interrupted
	 */
	p_sema_v_lock(&pr->p_iowait, PRIHI, &pr->p_lock);
	holding_pr = 0;

	/*
	 * Release the server
	 */
	v_sema(&pr->p_svwait);

	/*
	 * If the server indicates error, set it and leave
	 */
	if (sm->m_arg == -1) {
		error = 1;
		goto out;
	}
	ASSERT(sm->m_nseg == 0, "kernmsg_send: got segs back");
	args[0] = sm->m_arg;
	args[1] = sm->m_arg1;
	args[2] = sm->m_op;

out:
	/*
	 * Clean up and return success/failure
	 */
	if (holding_pr) {
		v_lock(&pr->p_lock, SPL0);
	}
	if (sm) {
		FREE(sm, MT_SYSMSG);
	}
	return(error);
}
