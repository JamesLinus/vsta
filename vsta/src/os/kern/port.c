/*
 * port.c
 *	Routines for handling ports
 */
#include <sys/port.h>
#include <sys/percpu.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/msg.h>
#include <sys/mutex.h>
#include <sys/fs.h>
#include <lib/alloc.h>
#include <sys/assert.h>

/*
 * find_portref()
 *	Find a port given its handle
 *
 * Validate the state of the port also.  This routine handles all
 * of its own locking.
 *
 * We take the spinlock on the portref, and then transfer to the
 * semaphore.  This allows us to release the proc lock before
 * sleeping to be the single I/O thread through the portref.
 * We definitely do *not* want to hold the proc semaphore while
 * waiting our turn for I/O--it would throttle all I/O for all
 * threads in the process.
 *
 * Sets err() and return 0 on failure; otherwise returns pointer
 * to semaphored portref.  On success both p_sema and p_lock are
 * held.
 */
struct portref *
find_portref(struct proc *p, port_t port)
{
	struct portref *ptref;

	/*
	 * Hold proc while we look at open slots
	 */
	if (p_sema(&p->p_sema, PRICATCH)) {
		err(EINTR);
		return(0);
	}

	/*
	 * From the user's perspective, port references run
	 * 0..PROCOPENS-1, and server ports run from
	 * PROCOPENS up.
	 */
	if ((port < 0) || (port >= PROCOPENS) ||
			!(ptref = p->p_open[port]) ||
			(ptref == PORT_RESERVED)) {
		v_sema(&p->p_sema);
		err(EINVAL);
		return(0);
	}

	/*
	 * Take spinlock on portref.  Release proc.
	 */
	p_lock(&ptref->p_lock, SPL0);
	v_sema(&p->p_sema);

	/*
	 * Quick sanity check
	 */
	ASSERT_DEBUG(ptref->p_port, "find_portref: null p_port");
	if (ptref->p_port == 0) {
		v_lock(&ptref->p_lock, SPL0);
		err(EIO);
		return(0);
	}

	/*
	 * Transfer from lock to semaphore.  When we come out of
	 * this we will be the sole thread doing I/O through the
	 * portref.
	 */
	if (p_sema_v_lock(&ptref->p_sema, PRICATCH, &ptref->p_lock)) {
		err(EINTR);
		return(0);
	}
	p_lock(&ptref->p_lock, SPL0);
	if (ptref->p_port == 0) {
		v_lock(&ptref->p_lock, SPL0);
		v_sema(&ptref->p_sema);
		err(EIO);
		return(0);
	}
	return(ptref);
}

/*
 * delete_portref()
 *	Like find_portref, except remove from open portref table also
 *
 * Since this routine is used on exit and such, it is much more
 * tenacious about getting the semaphores--no interrupted system
 * calls allowed!
 */
struct portref *
delete_portref(struct proc *p, port_t port)
{
	struct portref *ptref;

	/*
	 * Hold proc while we look at open slots
	 */
	p_sema(&p->p_sema, PRIHI);

	/*
	 * From the user's perspective, port references run
	 * 0..PROCOPENS-1, and server ports run from
	 * PROCOPENS up.
	 */
	if ((port < 0) || (port >= PROCOPENS) ||
			!(ptref = p->p_open[port]) ||
			(ptref == PORT_RESERVED)) {
		v_sema(&p->p_sema);
		err(EINVAL);
		return(0);
	}

	/*
	 * Delete from proc list
	 */
	p->p_open[port] = 0;
	p->p_nopen -= 1;

	/*
	 * Take spinlock on portref.  Release proc.
	 */
	p_lock(&ptref->p_lock, SPL0);
	v_sema(&p->p_sema);

	ASSERT_DEBUG(ptref->p_port, "find_portref: null p_port");

	/*
	 * Transfer from lock to semaphore.  When we come out of
	 * this we will be the last thread to ever have access
	 * to this portref.
	 */
	p_sema_v_lock(&ptref->p_sema, PRIHI, &ptref->p_lock);
	return(ptref);
}

/*
 * find_port()
 *	Find a port given its handle
 *
 * This routine handles all of its own locking.  On successful return,
 * a pointer to the port is returned and the port is locked.  Otherwise
 * 0 is returned.
 */
struct port *
find_port(struct proc *p, port_t portid)
{
	struct port *port;

	/*
	 * Hold proc while we look at open slots
	 */
	if (p_sema(&p->p_sema, PRICATCH)) {
		err(EINTR);
		return(0);
	}

	/*
	 * From the user's perspective, port references run
	 * PROCOPENS up.
	 */
	portid -= PROCOPENS;
	if ((portid < 0) || (portid >= PROCPORTS) ||
			!(port = p->p_ports[portid])) {
		v_sema(&p->p_sema);
		err(EINVAL);
		return(0);
	}

	/*
	 * Take spinlock on port.  Release proc.
	 */
	p_lock(&port->p_lock, SPLHI);
	v_sema(&p->p_sema);

	/*
	 * Transfer from lock to semaphore.  When we come out of
	 * this we will be the sole thread receiving I/O through the
	 * port.
	 */
	if (p_sema_v_lock(&port->p_sema, PRICATCH, &port->p_lock)) {
		err(EINTR);
		return(0);
	}
	p_lock(&port->p_lock, SPLHI);
	return(port);
}

/*
 * delete_port()
 *	Like find_port, except remove from open port table also
 *
 * This routine handles all of its own locking.  On return we are the
 * last server reference to the port--clients may still hold portrefs.
 */
struct port *
delete_port(struct proc *p, port_t portid)
{
	struct port *port;

	/*
	 * Hold proc while we look at open slots
	 */
	p_sema(&p->p_sema, PRIHI);

	/*
	 * From the user's perspective, port references run
	 * PROCOPENS up.
	 */
	portid -= PROCOPENS;
	if ((portid < 0) || (portid >= PROCPORTS) ||
			!(port = p->p_ports[portid])) {
		v_sema(&p->p_sema);
		err(EINVAL);
		return(0);
	}

	/* 
	 * Remove from process
	 */
	p->p_ports[portid] = 0;

	/*
	 * Take spinlock on port.  Release proc.
	 */
	p_lock(&port->p_lock, SPL0);
	v_sema(&p->p_sema);

	/*
	 * Wait our turn for the port
	 */
	p_sema_v_lock(&port->p_sema, PRIHI, &port->p_lock);
	return(port);
}

/*
 * alloc_portref()
 *	Allocate a portref data structure, initialize it
 */
struct portref *
alloc_portref(void)
{
	struct portref *pr;

	pr = malloc(sizeof(struct portref));
	bzero(pr, sizeof(struct portref));
	init_sema(&pr->p_sema);
	init_lock(&pr->p_lock);
	init_sema(&pr->p_iowait);
	init_sema(&pr->p_svwait);
	set_sema(&pr->p_iowait, 0);
	set_sema(&pr->p_svwait, 0);
	pr->p_state = PS_OPENING;
	return(pr);
}

/*
 * dup_port()
 *	Duplicate a portref
 *
 * Needs to bump reference count on port, and initiate a M_DUP
 * to get new portref registered with server.
 */
struct portref *
dup_port(struct portref *opr)
{
	struct portref *pr;
	long args[2];

	/*
	 * Get our new proposed port, set it up
	 */
	pr = alloc_portref();
	pr->p_port = opr->p_port;
	pr->p_state = PS_IODONE;

	/*
	 * Ask the server to start a new portref
	 */
	args[0] = (long)pr;
	args[1] = 0;
	if (kernmsg_send(opr, M_DUP, args) || (args[0] == -1)) {
		free(pr);
		return(0);
	}
	return(pr);
}

/*
 * fork_ports()
 *	For each open portref, M_DUP it into the new structure
 */
void
fork_ports(struct portref **old, struct portref **new, uint nport)
{
	int x;
	struct portref *pr;

	for (x = 0; x < nport; ++x) {
		if ((pr = old[x]) == 0) {
			new[x] = 0;
			continue;
		}
		p_sema(&pr->p_sema, PRIHI);
		new[x] = dup_port(pr);
		v_sema(&pr->p_sema);
	}
}

/*
 * close_ports()
 *	Shut down a server for each open port in the range
 */
void
close_ports(struct port **ports, uint nport)
{
	int x;

	for (x = 0; x < nport; ++x) {
		if (ports[x]) {
			(void)shut_server(ports[x]);
			ports[x] = 0;
		}
	}
}

/*
 * close_portrefs()
 *	Shut down a client for each open port in the range
 */
void
close_portrefs(struct portref **prs, uint nport)
{
	int x;

	for (x = 0; x < nport; ++x) {
		if (prs[x]) {
			(void)shut_client(prs[x]);
			prs[x] = 0;
		}
	}
}

/*
 * alloc_open()
 *	Allocate an open portref in the p_open[] array
 *
 * Returns p_open[] index on success; -1 on failure.
 */
alloc_open(struct proc *p)
{
	int slot;

	/*
	 * Lock proc, see if we have a slot.  Find next open slot
	 * if we do.
	 */
	p_sema(&p->p_sema, PRIHI);
	if (p->p_nopen >= PROCOPENS) {
		v_sema(&p->p_sema);
		return(err(ENOSPC));
	}
	for (slot = 0; slot < PROCOPENS; ++slot) {
		if (p->p_open[slot] == 0) {
			break;
		}
	}
	ASSERT(slot < PROCOPENS, "msg_connect: wrong p_nopen");
	p->p_open[slot] = PORT_RESERVED;
	ATOMIC_INC(&p->p_nopen);
	v_sema(&p->p_sema);
	return(slot);
}

/*
 * free_open()
 *	Free up a slot allocated with alloc_open()
 *
 * We order our assignments carefully so we can save the cost
 * of taking the proc semaphore.
 */
void
free_open(struct proc *p, int slot)
{
	p->p_open[slot] = 0;
	ATOMIC_DEC(&p->p_nopen);
}

/*
 * clone()
 *	Duplicate port
 *
 * This duplication is visible right out to the server; each port
 * will have its own state (position, etc.) after a successful
 * clone.
 */
clone(port_t arg_port)
{
	struct portref *pr, *newpr;
	int slot;
	struct proc *p = curthread->t_proc;

	/*
	 * Get another slot in p_open[]
	 */
	slot = alloc_open(p);
	if (slot < 0) {
		return(-1);
	}

	/*
	 * Find existing port
	 */
	pr = find_portref(p, arg_port);
	if (!pr) {
		free_open(p, slot);
		return(-1);
	}
	v_lock(&pr->p_lock, SPL0);

	/*
	 * Try to clone it
	 */
	newpr = dup_port(pr);
	v_sema(&pr->p_sema);

	/*
	 * Return success/failure
	 */
	if (newpr) {
		p->p_open[slot] = newpr;	/* Set reserved slot */
		return(slot);
	} else {
		free_open(p, slot);
		return(-1);
	}
}

/*
 * free_portref()
 *	Free a portref
 *
 * We just make sure there's no segments left, then free() it
 */
void
free_portref(struct portref *pr)
{
	unmapsegs(&pr->p_segs);
	free(pr);
}
