/*
 * msgcon.c
 *	Routines involved with opening/closing message connections
 */
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/fs.h>
#include <hash.h>
#include <sys/percpu.h>
#include <sys/proc.h>
#include <sys/assert.h>
#include <sys/port.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/malloc.h>
#include <alloc.h>

#define START_ROTOR (1024)	/* Where we start searching for an open # */

extern void disable_isr();

static sema_t name_sema;	/* Mutex for portnames and rotor */
static struct hash		/* Map port names->addresses */
	*portnames;
static ulong rotor =		/* Rotor for searching next ID */
	START_ROTOR;

/*
 * ref_port()
 *	Add a reference to a port.
 */
void
ref_port(struct port *port, struct portref *pr)
{
	struct portref *ref = port->p_refs;

	if (ref == 0) {
		port->p_refs = pr;
		pr->p_next = pr->p_prev = pr;
	} else {
		pr->p_next = ref->p_next;
		ref->p_next->p_prev = pr;
		ref->p_next = pr;
		pr->p_prev = ref;
	}
}

/*
 * deref_port()
 *	Release a reference to a port.
 *
 * Remove a portref from the linked list of references under the port.
 */
void
deref_port(struct port *port, struct portref *pr)
{
	struct portref *ref = port->p_refs;

#ifdef DEBUG
	ASSERT(ref, "deref_port: no ref");
	{
		struct portref *prstart, *pr2;

		prstart = ref;
		pr2 = prstart->p_next;
		while (pr2 != pr) {
			ASSERT(pr2 != prstart, "deref_port: ref not found");
			pr2 = pr2->p_next;
		}
		if (pr->p_next == pr) {
			struct sysmsg *sm;

			ASSERT(ref == pr, "deref_port: ref not in port");
			ASSERT(!blocked_sema(&port->p_wait),
				"deref_port: waiters");
			for (sm = port->p_hd; sm; sm = sm->sm_next) {
				ASSERT((sm->sm_op == M_ISR) ||
					(sm->sm_op == M_CONNECT),
					"deref_port: messages");
			}
		}
	}
#endif
	if (pr->p_next != pr) {
		pr->p_next->p_prev = pr->p_prev;
		pr->p_prev->p_next = pr->p_next;
		if (ref == pr) {
			port->p_refs = pr->p_next;
		}
	} else {
		port->p_refs = 0;
	}
#ifdef DEBUG
	pr->p_next = pr->p_prev = 0;
#endif
}

/*
 * msg_port()
 *	Create a new port with the given global ID
 *
 * If port_name is 0, we generate one for them.
 */
port_t
msg_port(port_name arg_port, port_name *arg_portp)
{
	struct port *port;
	struct proc *p = curthread->t_proc;
	int slot;

	/*
	 * Get new port
	 */
	port = alloc_port();

	/*
	 * Lock the process and see if we have a free slot
	 */
	p_sema(&p->p_sema, PRIHI);
	for (slot = 0; slot < PROCPORTS; ++slot) {
		if (p->p_ports[slot] == 0)
			break;
	}
	if (slot >= PROCPORTS) {
		v_sema(&p->p_sema);
		FREE(port, MT_PORT);
		return(err(ENOSPC));
	}

	/*
	 * On first open for this process, allocate the portref
	 * hash.
	 */
	if (!p->p_prefs) {
		p->p_prefs = hash_alloc(NPROC/4);
	}

	/*
	 * If needed, pick a new number
	 */
	p_sema(&name_sema, PRIHI);
	if (arg_port == 0) {
		while (hash_lookup(portnames, rotor)) {
			++rotor;
			if (rotor == 0) {
				rotor = START_ROTOR;
			}
		}
		arg_port = rotor++;
	} else {
		/*
		 * Otherwise verify that it doesn't already
		 * exist.
		 */
		if (hash_lookup(portnames, arg_port)) {
			v_sema(&name_sema);
			v_sema(&p->p_sema);
			FREE(port, MT_PORT);
			return(err(EBUSY));
		}
	}

	/*
	 * Insert our entry, then release the name interlock
	 */
	hash_insert(portnames, arg_port, port);
	v_sema(&name_sema);
	port->p_name = arg_port;

	/*
	 * Fill in our proc entry, return success
	 */
	p->p_ports[slot] = port;
	v_sema(&p->p_sema);

	/*
	 * If non-NULL second argument, return name to caller
	 */
	if (arg_portp) {
		(void)copyout(arg_portp, &arg_port, sizeof(arg_port));
	}
	return(slot+PROCOPENS);
}

/*
 * msg_connect()
 *	Given a global ID, connect to that port
 *
 * TBD: think about allowing connects to be interrupted.  It makes
 * for some hellishly complex state transitions as you race with
 * the server.
 */
port_t
msg_connect(port_name arg_port, uint arg_mode)
{
	struct proc *p = curthread->t_proc;
	int slot;
	struct portref *pr;
	struct port *port;
	int error = 0;
	struct sysmsg sm;

	/*
	 * Allocate a portref data structure, set it up
	 */
	pr = alloc_portref();

	/*
	 * Set up nodup flag now while it's safe
	 */
	if (arg_mode & ACC_NOCLONE) {
		pr->p_flags |= PF_NODUP;
		arg_mode &= ~ACC_NOCLONE;
	}

	/*
	 * Allocate a system message, fill it in.
	 */
	sm.sm_op = M_CONNECT;
	sm.sm_sender = pr;
	sm.sm_seg[0] = (void *)(p->p_ids);
	sm.sm_nseg = 0;
	sm.sm_arg = arg_mode;
	sm.sm_arg1 = sizeof(p->p_ids);

	/*
	 * Get an open slot
	 */
	if ((slot = alloc_open(p)) < 0) {
		error = -1;
		goto out;
	}

	/*
	 * Now lock name semaphore, and look up our port
	 */
	p_sema(&name_sema, PRIHI);
	port = hash_lookup(portnames, arg_port);
	if (!port) {
		v_sema(&name_sema);
		error = err(ESRCH);
		goto out;
	}

	/*
	 * Lock port
	 */
	p_lock_void(&port->p_lock, SPLHI);

	/*
	 * Fill in port we're trying to attach
	 */
	pr->p_port = port;

	/*
	 * Free up semaphores we don't need to hold any more
	 */
	v_sema(&name_sema);

	/*
	 * Queue our message onto the server's queue, kick him awake
	 */
	lqueue_msg(port, &sm);

	/*
	 * Release lock and fall asleep on our own semaphore
	 */
	p_sema_v_lock(&pr->p_iowait, PRIHI, &port->p_lock);

	/*
	 * If p_port went away, we raced with him closing
	 */
	if (!pr->p_port) {
		error = err(EIO);
	}

	/*
	 * If p_state hasn't been set to IODONE, then the connect failed.
	 * The reason has been placed in m_err field of our sysmsg.
	 */
	else if (pr->p_state != PS_IODONE) {
		error = err(sm.sm_err);
	}

	/*
	 * If no error, move port state to PS_IODONE and let
	 * it go into service
	 */
	if (error == 0) {
		p->p_open[slot] = pr;
		error = slot;
		pr = 0;		/* So not freed below */
		slot = -1;
	}

	/*
	 * All done.  Release references as needed, return
	 */
out:
	if (slot >= 0) {
		free_open(p, slot);
	}
	if (pr) {
		FREE(pr, MT_PORTREF);
	}
	return(error);
}

/*
 * tran_find()
 *	Given transaction #, find a portref for the current process
 */
static struct portref *
tran_find(long arg_tran)
{
	struct proc *p = curthread->t_proc;
	struct portref *pr;

	p_sema(&p->p_sema, PRIHI);
	pr = hash_lookup(p->p_prefs, arg_tran);
	if (!pr) {
		err(EINVAL);
	} else {
		p_lock_void(&pr->p_lock, SPL0);
	}
	v_sema(&p->p_sema);
	return(pr);
}

/*
 * msg_accept()
 *	Accept a connection with ID "arg_tran"
 */
int
msg_accept(long arg_tran)
{
	struct portref *pr;
	int error;

	/*
	 * Look up the transaction, sanity check it
	 */
	pr = tran_find(arg_tran);
	if (!pr) {
		return(-1);
	}
	if (pr->p_state != PS_OPENING) {
		/*
		 * Our caller's hosed.  He's not connecting.
		 */
		error = err(EINVAL);
	} else {

		/*
		 * Flag success, wake him.
		 */
		pr->p_state = PS_IODONE;
		v_sema(&pr->p_iowait);
		error = 0;
	}
	v_lock(&pr->p_lock, SPL0);
	return(error);
}

/*
 * shut_client()
 *	Shut down a connection from a client to a server
 */
void
shut_client(struct portref *pr, int sema_held)
{
	struct sysmsg *sm;
	struct port *port;
	ulong refs;

	/*
	 * Decrement the reference count, just return if
	 * it's not zero yet.
	 * TBD: consider compare-and-exchange
	 */
	p_lock_void(&pr->p_lock, SPL0);
	refs = pr->p_refs;
	pr->p_refs -= 1;
	v_lock(&pr->p_lock, SPL0_SAME);
	if (refs > 1) {
		if (sema_held) {
			v_sema(&pr->p_sema);
		}
		return;
	}

	/*
	 * Get a system message
	 */
	sm = MALLOC(sizeof(struct sysmsg), MT_SYSMSG);
	sm->sm_sender = pr;
	sm->sm_op = M_DISCONNECT;
	sm->sm_arg = (long)pr;
	sm->sm_arg1 = sm->sm_nseg = 0;

	/*
	 * If he's closed on us at the same time, no problem.
	 */
	p_lock_void(&pr->p_lock, SPL0);
	if (!(port = pr->p_port)) {
		v_lock(&pr->p_lock, SPL0_SAME);
		free_portref(pr);
		FREE(sm, MT_SYSMSG);
		return;
	}

	/*
	 * Put disconnect message on port's queue.  Flag that we've
	 * sent our final message.  He will clean up from here.
	 */
	pr->p_state = PS_CLOSING;
	queue_msg(port, sm, SPL0);
	if (!sema_held) {
		p_sema_v_lock(&pr->p_sema, PRIHI, &pr->p_lock);
	} else {
		v_lock(&pr->p_lock, SPL0_SAME);
	}
}

/*
 * close_client()
 *	Dump a client when the server closes its port
 *
 * Returns 1 if it detects messages in the port's queue; no action
 * is taken on the portref.  Otherwise zeroes the p_port field of
 * the portref, flagging that the server's gone.  It also removes
 * the portref from the port's list, and finally returns 0.
 */
static int
close_client(struct port *port, struct portref *pr)
{
	int err;

	unmapsegs(&pr->p_segs);
	p_lock_void(&pr->p_lock, SPL0);
	p_lock_void(&port->p_lock, SPLHI);
	if (port->p_hd) {
		err = 1;
	} else {
		pr->p_port = 0;
		if (blocked_sema(&pr->p_iowait)) {
			v_sema(&pr->p_iowait);
		}
		deref_port(port, pr);
		err = 0;
	}
	v_lock(&port->p_lock, SPL0);
	v_lock(&pr->p_lock, SPL0_SAME);
	return(err);
}

/*
 * bounce_msgs()
 *	Take the messages off a port, and bounce each of them
 *
 * We need to do this as sysmsg's can have segments within them,
 * and we need to clean those segments up.  This routine is called
 * with the port locked, and returns with it released.
 */
static void
bounce_msgs(struct port *port)
{
	struct sysmsg *msgs, *sm, *smn;
	struct portref *pr;

	/*
	 * Pull whatever messages are on the port out of the queue.
	 * We'll walk the list once we've released our lock.  We can't
	 * hold the port lock and go for the portrefs, as we lock in
	 * the other order, and would deadlock.
	 */
	p_lock_void(&port->p_lock, SPL0);
	msgs = port->p_hd;
	port->p_hd = 0;
	v_lock(&port->p_lock, SPL0);

	/*
	 * Step through the linked list.  As our clients will be waking
	 * up and discarding messages, we record the next pointer field
	 * into a local variable.
	 */
	for (sm = msgs; sm; sm = smn) {
		/*
		 * Get pointers, apply sanity check
		 */
		pr = sm->sm_sender;
		smn = sm->sm_next;
#ifdef DEBUG
		sm->sm_next = 0;
#endif

		/*
		 * ISR messages are somewhat special.  They don't have
		 * a portref, and there's no connection to break.
		 * We have already de-registered, so it can't come
		 * back.
		 */
		if (pr == 0) {
			ASSERT_DEBUG(sm->sm_op == M_ISR,
				"bounce_msgs: !pr !M_ISR");
			continue;
		}

		ASSERT_DEBUG(pr->p_port == port,
			"bounce_msgs: msg in queue not for port");

		/*
		 * Lock portref, then port
		 */
		p_lock_void(&pr->p_lock, SPL0_SAME);
		p_lock_void(&port->p_lock, SPLHI);

		/*
		 * If any segments in the message, discard them
		 */
		if (sm->sm_nseg > 0) {
			freesegs(sm);
		}

		/*
		 * If the client has tried to abort the operation,
		 * ignore anything but the abort message itself.
		 */
		if ((pr->p_state == PS_ABWAIT) &&
				(sm->sm_op != M_ABORT)) {
			/* nothing */ ;
		} else {

			/*
			 * Fill in sysmsg as an I/O error, clear
			 * p_port to preclude further I/O, and
			 * kick client awake.
			 */
			sm->sm_arg1 = sm->sm_arg = -1;
			strcpy(sm->sm_err, EIO);
			pr->p_port = 0;
			v_sema(&pr->p_iowait);
		}

		/*
		 * Release locks, and remove the portref from our list
		 */
		v_lock(&port->p_lock, SPL0);
		v_lock(&pr->p_lock, SPL0_SAME);
	}
}

/*
 * shut_server()
 *	Shut down a server side
 */
int
shut_server(struct port *port)
{
	struct portref *pr;
	struct proc *p = curthread->t_proc;

	/*
	 * Flag that it's going down
	 */
	port->p_flags |= P_CLOSING;

	/*
	 * Remove the port's name from the system table.  No
	 * new clients after this.
	 */
	p_sema(&name_sema, PRIHI);
	hash_delete(portnames, port->p_name);
	v_sema(&name_sema);

	/*
	 * If we have an ISR tied to this port, disable
	 * that before the port goes away.
	 */
	if (port->p_flags & P_ISR) {
		disable_isr(port);
		port->p_flags &= ~P_ISR;
	}

	/*
	 * Let the mmap() layer clean anything up it might have.
	 */
	mmap_cleanup(port);

	/*
	 * Dump any work left from clients in our queue
	 */
	do {
		bounce_msgs(port);

		/*
		 * Enumerate our current clients, shut them down
		 */
		while (pr = port->p_refs) {
			/*
			 * If traffic has slipped in, re-bounce messages.
			 * Forward progress is guaranteed, because each
			 * time this happens we close off their access to
			 * the port, and new connections aren't possible.
			 */
			if (close_client(port, pr)) {
				break;
			} else {
				p_sema(&p->p_sema, PRIHI);
				ASSERT(hash_delete(p->p_prefs, (ulong)pr) == 0,
					"shut_server: pr not hashed");
				v_sema(&p->p_sema);
			}
		}
	} while (port->p_refs);

	/*
	 * Take this semaphore to flush out any lagging folks trying
	 * to fiddle with mappings.
	 */
	p_sema(&port->p_mapsema, PRIHI);
	ASSERT((port->p_maps == 0) || (port->p_maps == NO_MAP_HASH),
		"shut_server: maps");

	FREE(port, MT_PORT);
	return(0);
}

/*
 * msg_disconnect()
 *	Shut down a message queue
 *
 * Works for both server and client sides, with somewhat different
 * effects.
 */
int
msg_disconnect(port_t arg_port)
{
	struct proc *p = curthread->t_proc;
	struct portref *pr;
	struct port *port;

	if (arg_port >= PROCOPENS) {
		/*
		 * Get port, and delete from proc list.  After this we are
		 * the last server thread to access the port as a server.
		 */
		port = delete_port(p, arg_port);
		if (!port) {
			return(-1);
		}

		/*
		 * Delete all current clients
		 */
		return(shut_server(port));
	} else {
#ifdef PROC_DEBUG
		/*
		 * Prevent miscreant from shutting our debug session
		 */
		if (p->p_dbg.pd_name && (arg_port == p->p_dbg.pd_port)) {
			return(err(EINVAL));
		}
#endif
		/*
		 * Get the portref, or error.  The slot is now deleted from
		 * the "open ports" list in the proc.
		 */
		pr = delete_portref(p, arg_port, 0);
		if (!pr) {
			return(-1);
		}
		shut_client(pr, 0);
		return(0);
	}
}

/*
 * msg_err()
 *	Return error reply to message
 *
 * Mostly because I didn't think it out too well, msg_err() returns
 * errors to both regular operations received with msg_receive(), as
 * well as connection requests.  A connection is accepted with
 * msg_accept(), but rejected here.
 */
int
msg_err(long arg_tran, const char *arg_why, int arg_len)
{
	struct portref *pr;
	struct port *port;
	struct proc *p = curthread->t_proc;
	char errmsg[ERRLEN];

	/*
	 * Validate error string, copy it in.  It's small, so we
	 * just stash it on the stack.
	 */
	if ((arg_len < 1) || (arg_len >= ERRLEN)) {
		return(err(EINVAL));
	}
	if (get_ustr(errmsg, ERRLEN, arg_why, arg_len)) {
		return(-1);
	}

	/*
	 * Validate transaction ID.   If we don't find it, this
	 * system call returns an error immediately.  For failed
	 * opens, delete it from the hash before we release
	 * the mutex.
	 */
	if (p_sema(&p->p_sema, PRICATCH)) {
		return(err(EINTR));
	}
	pr = hash_lookup(p->p_prefs, arg_tran);
	if (pr) {
		p_lock_void(&pr->p_lock, SPL0);
		if (pr->p_state == PS_OPENING) {
			hash_delete(p->p_prefs, arg_tran);
		}
	}
	v_sema(&p->p_sema);
	if (!pr) {
		return(err(EINVAL));
	}

	/*
	 * If the server port's on its way down, never mind
	 * this.  If there's not a request outstanding from
	 * this client, error that as well.
	 */
	port = pr->p_port;
	if (!port) {
		v_lock(&pr->p_lock, SPL0);
		return(err(EIO));
	}
	if (pr->p_msg == 0) {
		v_lock(&pr->p_lock, SPL0);
		return(err(EINVAL));
	}

	/*
	 * Figure out if this is an open, an I/O, or if our caller's
	 * just jerking us around.
	 */
	switch (pr->p_state) {
	case PS_IOWAIT:
		/*
		 * The usual--he sent a request, and here's the error
		 * response.  Flag an error using the m_arg field of
		 * his sysmsg; put the error string in the m_err part.
		 * Wake him up.
		 */
		pr->p_state = PS_IODONE;
		pr->p_msg->sm_arg = -1;
		strcpy(pr->p_msg->sm_err, errmsg);
		v_sema(&pr->p_iowait);
		break;
	case PS_OPENING:
		/*
		 * A failed open needs the portref cleared
		 */
		p_lock_void(&port->p_lock, SPLHI);
		deref_port(port, pr);
		v_lock(&port->p_lock, SPL0);

		/*
		 * An open failed.  We already deleted him from our
		 * hash above.  Flag open failure by setting his
		 * p_state to anything but PS_IODONE.  Wake him up.
		 */
		pr->p_state = PS_ABDONE;
		strcpy(pr->p_msg->sm_err, errmsg);
		v_sema(&pr->p_iowait);
		break;
	default:
		v_lock(&pr->p_lock, SPL0);
		return(err(EINVAL));
	}
	v_lock(&pr->p_lock, SPL0);
	return(0);
}

/*
 * init_msg()
 *	Initialize message stuff
 */
void
init_msg(void)
{
	init_sema(&name_sema);
	portnames = hash_alloc(64);
}

/*
 * msg_portname()
 *	Tell server port name associated with portref
 */
port_name
msg_portname(port_t arg_port)
{
	struct portref *pr;
	port_name pn;

	/*
	 * Access our open port reference.
	 */
	pr = find_portref(curthread->t_proc, arg_port);
	if (pr) {
		struct port *port;

		/*
		 * It looked good.  Take the portref spinlock so we
		 * don't race with the server trying to exit.  If we're
		 * still connected to a server, get his port_name.
		 */
		port = pr->p_port;
		if (port) {
			pn = port->p_name;
		} else {
			/*
			 * He bombed
			 */
			pn = err(EIO);
		}
		v_lock(&pr->p_lock, SPL0);
		v_sema(&pr->p_sema);
	} else {
		/*
		 * Bad port requested.  find_portref() sets err().
		 */
		pn = -1;
	}
	return(pn);
}
