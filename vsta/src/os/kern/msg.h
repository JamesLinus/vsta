#ifndef SYS_MSG_H
#define SYS_MSG_H
/*
 * sys/msg.h
 *	Inline functions for message queue manipulation
 */
#include <sys/types.h>
#include <sys/port.h>
#include "../mach/mutex.h"

/*
 * inline_lqueue_msg()
 *	Queue a message when port is already locked, inline version
 */
inline extern void
inline_lqueue_msg(struct port *port, struct sysmsg *sm)
{
	sm->sm_next = 0;
	if (port->p_hd) {
		/*
		 * If we already have a head entry then
		 * make the tail point at us as the new tail
		 */ 
		port->p_tl->sm_next = sm;
	} else {
		/*
		 * If we don't have a tail entry then we don't
		 * have a head either so we're the new head
		 */
		port->p_hd = sm;
	}
	port->p_tl = sm;
	v_sema(&port->p_wait);
}

/*
 * inline_queue_msg()
 *	Queue a message to the given port's queue, inline version
 *
 * This routine handles all locking of the given port.
 *
 * The source may look complex here, but the compiler will be able to
 * reduce most of the options down as exit_state will be passed as a
 * constant and thus give simple matching characteristics.
 */
inline extern void
inline_queue_msg(struct port *port, struct sysmsg *sm, spl_t exit_state)
{
	spl_t s;

	/*
	 * Lock down destination port, put message in queue, and
	 * bump its sleeping semaphore.
	 */
	if ((exit_state == SPLHI) || (exit_state == SPLHI_SAME)) {
		p_lock_void(&port->p_lock, SPLHI_SAME);
	} else {
		p_lock_void(&port->p_lock, SPLHI);
	}
	inline_lqueue_msg(port, sm);
	if ((exit_state == SPLHI) || (exit_state == SPLHI_SAME)) {
		v_lock(&port->p_lock, SPLHI_SAME);
	} else {
		v_lock(&port->p_lock, SPL0);
	}
}

#endif /* SYS_MSG_H */
