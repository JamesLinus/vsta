/*
 * wait.c
 *	Emulation of wait() and waitpid() on top of VSTa waits()
 */
#include <sys/wait.h>
#include <sys/proc.h>
#include <sys/fs.h>
#include <std.h>
#include <signal.h>	/* For wait_child() prototype */

/*
 * For PIDs ignored by waitpid()
 */
static struct exitst *pendhd = 0, *pendtl;

/*
 * queue()
 *	Put another exitst on the queue
 */
static void
queue(struct exitst *e)
{
	struct exitst *ep;

	ep = malloc(sizeof(struct exitst));
	if (ep == 0) {
		return;
	}
	*ep = *e;
	ep->e_next = 0;
	if (pendhd) {
		pendtl->e_next = ep;
		pendtl = ep;
	} else {
		pendhd = pendtl = ep;
	}
}

/*
 * encode()
 *	Convert to wait() stat format
 */
static
encode(struct exitst *e)
{
	int x = 0;

	/*
	 * Killed by event, otherwise voluntary
	 */
	if (e->e_code & _W_EV) {
		x |= _W_EV;
		x |= ((__strtosig(e->e_event) & 0xFF) << 8);
	}

	/*
	 * Get low 8 bits
	 */
	x |= (e->e_code & 0xFF);
	return(x);
}

/*
 * fillpend()
 *	Pull all pending waits() events to local queue
 */
static void
fillpend(void)
{
	struct exitst e;

	/*
	 * Pull while events
	 */
	while (waits(&e, 0) >= 0) {
		/*
		 * Enqueue
		 */
		queue(&e);
	}
}

/*
 * wait()
 *	Wait for next process, return its status
 */
pid_t
wait(int *ip)
{
	struct exitst e, *ep;
	int x;

	/*
	 * Get all pending children
	 */
	fillpend();

	/*
	 * Pull from pending list, or wait
	 */
	if (ep = pendhd) {
		pendhd = ep->e_next;
		bcopy(ep, &e, sizeof(e));
	} else {
		x = waits(&e, 1);
		if (x < 0) {
			/*
			 * If our SIGCHLD thread picked up a completion
			 * for us, return that.
			 * TBD: The current libc is not up to snuff for
			 *  letting this sigchild watcher thread play
			 *  down here.
			 */
			if (ep = pendhd) {
				pendhd = ep->e_next;
				bcopy(ep, &e, sizeof(e));
			} else {
				return(-1);
			}
		}
	}

	/*
	 * Fill in encoded status if requested
	 */
	if (ip) {
		*ip = encode(&e);
	}

	/*
	 * Free queued waits() message if any
	 */
	if (ep) {
		free(ep);
	}

	return(e.e_pid);
}

/*
 * waitpid()
 *	wait(), with more bells and whistles
 */
pid_t
waitpid(pid_t pid, int *ip, int opts)
{
	struct exitst e, *ep, **epp;

	/*
	 * Simple case--use wait()
	 */
	if ((pid == -1) && !opts) {
		return(wait(ip));
	}

	/*
	 * If have a specific PID in mind, go for it
	 */
	fillpend();
	if (pid > 0) {
		/*
		 * Search pending list first
		 */
		epp = &pendhd;
		for (ep = *epp; ep; ep = ep->e_next) {
			if (ep->e_pid == pid) {
				*epp = ep->e_next;
				if (ip) {
					*ip = encode(ep);
				}
				free(ep);
				return(pid);
			}
			epp = &ep->e_next;
		}

		/*
		 * Allow waiting?
		 */
		if (opts & WNOHANG) {
			return(0);
		}

		/*
		 * Now wait for our PID
		 */
		for (;;) {
			if (waits(&e, 1) < 0) {
				return(-1);
			}
			if (e.e_pid == pid) {
				if (ip) {
					*ip = encode(&e);
				}
				return(pid);
			}
			queue(&e);
		}
	}

	/*
	 * If have a queued entry, return it
	 */
	if (ep = pendhd) {
		pendhd = ep->e_next;
		pid = ep->e_pid;
		if (ip) {
			*ip = encode(ep);
		}
		free(ep);
		return(pid);
	}

	/*
	 * Allow waiting?
	 */
	if (opts & WNOHANG) {
		return(0);
	}

	/*
	 * Wait for child
	 */
	if (waits(&e, 1) < 0) {
		return(-1);
	}
	if (ip) {
		*ip = encode(&e);
	}
	return(e.e_pid);
}

/*
 * wait_child()
 *	Wait for a child, and store its exit info in the pending queue
 */
void
wait_child(void)
{
	struct exitst e;

	while (waits(&e, 1) < 0) {
		/*
		 * There are no children to wait() for right now,
		 * but our main thread could certainly fork() new
		 * ones in the future.  Sleep and retry.
		 */
		__msleep(250);
	}
	queue(&e);
}
