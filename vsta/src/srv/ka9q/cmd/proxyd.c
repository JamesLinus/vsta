/*
 * proxyd.c
 *	Do proxy filesystem actions for a remote client
 *
 * An incoming TCP connection to the proxy TCP port gets this daemon.
 * Each connection is assumed to be a distinct client, who will likely
 * insert this connection into his mount table and have it inherited by
 * all child processes.
 *
 * Each message is formatted as a "struct msg" followed by the appropriate
 * amount of payload.  The very first message is special; its payload
 * is interpreted as a string for path_open(), and makes the initial
 * association between the TCP client and the resource desired.
 *
 * As the client fork()'s and M_DUP's arrive, the proxy daemon clone()'s
 * local ports for each, but all traffic is still carried and multiplexed
 * over the single TCP connection.  m_sender is used to do the mux/demux.
 * While there's some opportunity for interference on the TCP channel, this
 * is better than forcing each client doing a fork/exec to have to eat
 * the full latency of a new TCP connection.
 */
#include <sys/fs.h>
#include <stdio.h>
#include <syslog.h>
#include <llist.h>
#include <hash.h>
#include <std.h>
#include <lock.h>
#include <sys/syscall.h>

extern port_t path_open(char *, int);

/*
 * State of each distinct client under this proxy server
 */
struct client {
	port_t c_local;		/* Local port reached through proxyd */
	char *c_buf;		/* Buffered message body */
	struct msg c_msg;	/* Message from client */
	pid_t c_pid;		/* PID of slave thread */
	int c_busy;		/* Is slave busy on a message now? */
	lock_t c_lock;		/* Mutext for c_queue */
	struct llist c_queue;	/* Linked list FIFO queue */
	int c_abort;		/* Client was interrupting */
	int c_done;		/* Client is gone */
	long c_sender;		/* Record of client m_sender */
};

/*
 * Maximum message size we accept
 */
#define MAXMSG (32768)

/*
 * All the clients connected through this server
 */
static struct hash *clients;
static int num_clients;
static port_t rxport, txport;

/*
 * cleanup()
 *	Toss out the server ports, arranging for TCP reset back to client
 */
static void
cleanup(void)
{
	msg_disconnect(txport);
	wstat(rxport, "conn=disconnect");
	msg_disconnect(rxport);
}

/*
 * rxmsg()
 *	Receive next message from the TCP byte stream
 */
static int
rxmsg(struct msg *msg, char *clbuf)
{
	struct msg m;
	int x, msgleft, bodyleft;
	char *bufp, *clmsgp, *bodyp;
	static char *pushback;
	static int pushlen;

	/*
	 * Initialize state to "receiving message header"
	 */
	msgleft = sizeof(struct msg);
	clmsgp = (void *)msg;

	for (;;) {
		/*
		 * Pushback?  This runs in two phases.  In the first, we
		 * take the bytes here instead of doing an actual message.
		 * We flag this by clearing pushlen.  Next time around,
		 * we see pushback && !pushlen, and free the buffer.
		 */
		if (pushback) {
			if (pushlen == 0) {
				free(pushback);
				pushback = 0;
			} else {
				bufp = pushback;
				x = pushlen;
				pushlen = 0;
			}
		}

		/*
		 * Get next message, clean up on error
		 */
		if (!pushback) {
			m.m_op = FS_READ | M_READ;
			m.m_buf = clbuf;
			m.m_arg = m.m_buflen = MAXMSG;
			m.m_nseg = 1;
			m.m_arg1 = 0;
			x = msg_send(rxport, &m);
			if (x < 0) {
				return(x);
			}
			bufp = clbuf;
		}

		/*
		 * If we're still assembling the message header, take
		 * bytes as needed.
		 */
		if (msgleft) {
			if (x < msgleft) {
				bcopy(bufp, clmsgp, x);
				clmsgp += x;
				msgleft -= x;
				continue;
			}
			bcopy(bufp, clmsgp, msgleft);
			x -= msgleft;
			bufp += msgleft;
			msgleft = 0;
			if (msg->m_arg > MAXMSG) {
				syslog(LOG_ERR, "message too large");
				return(-1);
			}
			bodyleft = msg->m_arg;
			bodyp = clbuf;
		}

		/*
		 * Still assembling body?  Copy bytes and iterate.
		 */
		if (x < bodyleft) {
			bcopy(bufp, bodyp, x);
			bodyp += x;
			bodyleft -= x;
			continue;
		}

		/*
		 * Here's the entire message body
		 */
		bcopy(bufp, bodyp, bodyleft);
		x -= bodyleft;
		bufp += bodyleft;
		bodyleft = 0;

		/*
		 * If we're at the end of the message, return it
		 */
		if (bodyleft == 0) {
			/*
			 * Unconsumed bytes must be held until the
			 * next call.
			 */
			if (x > 0) {
				/*
				 * If there's bytes from the last
				 * pushback buffer, just shuffle them
				 * up to the front.  Otherwise create
				 * a new buffer.
				 */
				pushlen = x;
				if (pushback) {
					bcopy(bufp, pushback, pushlen);
				} else {
					pushback = malloc(pushlen);
					bcopy(bufp, pushback, pushlen);
				}
			}
			return(msg->m_arg);
		}
	}
}

/*
 * no_op()
 *	Event handler
 *
 * Just used to interrupt a sleep
 */
static void
no_op(char *event)
{
}

/*
 * send_err()
 *	Send back a special FS_ERR message to our client
 */
static void
send_err(port_t dest)
{
	struct msg m;

	m.m_op = FS_ERR;
	m.m_buf = strerror();
	m.m_buflen = strlen(m.m_buf);
	m.m_nseg = 1;
	(void)msg_send(dest, &m);
}

/*
 * free_cl()
 *	Release client resources
 */
static void
free_cl(struct client *cl)
{
	if (cl->c_buf) {
		free(cl->c_buf);
	}
	free(cl);
}

/*
 * serve_slave()
 *	Take requests and launch I/O
 *
 * Also handle cleanup on exit, and interrupted I/O
 */
static void
serve_slave(struct client *cl)
{
	struct msg *mp;
	int x;

	for (;;) {
		/*
		 * Wait for work; special cases for being interrupted
		 */
		if (mutex_thread(0) < 0) {
			if (cl->c_done) {
				free_cl(cl);
				_exit(0);
			}
			if (cl->c_abort) {
				cl->c_abort = 0;
				cl->c_msg.m_sender = cl->c_sender;
				cl->c_msg.m_arg = cl->c_msg.m_nseg = 0;
				(void)msg_send(txport, &cl->c_msg);
			}
			continue;
		}

		/*
		 * Ready to get to work.  Flag that we're running.
		 */
		cl->c_busy = 1;

		/*
		 * If the message isn't on the c_msg, dequeue it from
		 * the linked list and put it in place.
		 */
		if (cl->c_msg.m_op == 0) {
			struct llist *ll;

			p_lock(&cl->c_lock);
			ll = LL_NEXT(&cl->c_queue);
			mp = ll->l_data;
			ll_delete(ll);
			v_lock(&cl->c_lock);
			bcopy(mp, &cl->c_msg, sizeof(struct msg));
			bcopy(mp->m_buf, cl->c_buf, mp->m_arg);
			free(mp->m_buf);
			free(mp);
		}

		/*
		 * Do the message operation, return error or
		 * result.  For results, try to slip the message
		 * header into the scatter/gather.  If it won't fit,
		 * send it in its own message.
		 */
		cl->c_sender = cl->c_msg.m_sender;
		x = msg_send(cl->c_local, &cl->c_msg);
		if (x < 0) {
			send_err(txport);
		} else if (cl->c_msg.m_nseg == MSGSEGS) {
			struct msg m;

			m.m_buf = &cl->c_msg;
			m.m_arg = m.m_buflen = sizeof(struct msg);
			m.m_nseg = 1;
			(void)msg_send(txport, &m);
			(void)msg_send(txport, &cl->c_msg);
		} else {
			bcopy(&cl->c_msg.m_seg[0], &cl->c_msg.m_seg[1],
				cl->c_msg.m_nseg * sizeof(seg_t));
			cl->c_msg.m_buf = &cl->c_msg;
			cl->c_msg.m_buflen = sizeof(struct msg);
			cl->c_msg.m_nseg += 1;
			(void)msg_send(txport, &cl->c_msg);
		}
	}
}

/*
 * do_msg_send()
 *	Take forwarded message, direct it out to the local server
 */
static void
do_msg_send(struct client *cl, struct msg *msg, char *buf)
{
	/*
	 * If this is the first I/O, allocate an I/O buffer and
	 * launch a thread to serve
	 */
	if (cl->c_pid == 0) {
		if (cl->c_buf == 0) {
			cl->c_buf = malloc(MAXMSG);
		}
		cl->c_pid = tfork(serve_slave, (ulong)cl);
	}

	/*
	 * Easy/common case: slave is idle, hand him the next message
	 */
	if (cl->c_busy == 0) {
race:		bcopy(msg, &cl->c_msg, sizeof(struct msg));
		bcopy(buf, cl->c_buf, msg->m_arg);
		cl->c_msg.m_buf = cl->c_buf;
	} else {
		struct msg *msgp;
		char *bufp;

		/*
		 * Buffer the message.  After we take the lock, the client
		 * may have returned to the idle case.
		 */
		p_lock(&cl->c_lock);
		if (cl->c_busy == 0) {
			v_lock(&cl->c_lock);
			goto race;
		}
		msgp = malloc(sizeof(struct msg));
		bufp = malloc(msg->m_arg);
		bcopy(msg, msgp, sizeof(struct msg));
		bcopy(buf, bufp, msg->m_arg);
		msgp->m_buf = bufp;
		ll_insert(&cl->c_queue, msgp);
		v_lock(&cl->c_lock);
	}

	/*
	 * Tell the slave that there's more work
	 */
	mutex_thread(cl->c_pid);
}

/*
 * alloc_cl()
 *	Get a new client data structure
 */
static struct client *
alloc_cl(void)
{
	struct client *cl;

	cl = malloc(sizeof(struct client));
	bzero(cl, sizeof(struct client));
	ll_init(&cl->c_queue);
	init_lock(&cl->c_lock);
	return(cl);
}

/*
 * serve_clients()
 *	Read messages and farm out to clients
 */
static void
serve_clients(void)
{
	struct msg clmsg;
	struct client *cl;
	char buf[MAXMSG];
	int err;

	/*
	 * We start out with just the initial connecting client
	 */
	num_clients = 1;

	/*
	 * Watch for events; they're used to interrupt I/O and to
	 * cause cleanup on disconnect.
	 */
	notify_handler(no_op);

	for (;;) {
		/*
		 * Get next message
		 */
		if (rxmsg(&clmsg, buf) < 0) {
			cleanup();
			exit(1);
		}

		/*
		 * Get client pointer based on client ID
		 */
		cl = hash_lookup(clients, clmsg.m_sender);
		if (cl == 0) {
			syslog(LOG_ERR, "bad m_sender");
			notify(0, 0, "kill");
		}

		/*
		 * Proxy it based on message type
		 */
		switch (clmsg.m_op) {
		case M_DUP:
			{
			struct client *newcl;

			/*
			 * Duplicate the port
			 */
			newcl = alloc_cl();
			newcl->c_local = clone(cl->c_local);
			if (newcl->c_local < 0) {
				err = -1;
				free_cl(newcl);
				break;
			}
			(void)hash_insert(clients,
				clmsg.m_arg, newcl);
			err = 0;
			num_clients += 1;
			}
			break;

		case M_DISCONNECT:
			if (cl->c_pid) {
				cl->c_done = 1;
				notify(0, cl->c_pid, "done");
			} else {
				free_cl(cl);
			}
			(void)hash_delete(clients, clmsg.m_sender);
			if (--num_clients == 0) {
				cleanup();
				notify(0, 0, "kill");
			}
			continue;

		case M_ABORT:
			/*
			 * Tell the slave thread to try and interrupt
			 */
			if (cl->c_busy) {
				cl->c_abort = 1;
				notify(0, cl->c_pid, "wakeup");
			}
			err = 0;
			break;

		default:
			/*
			 * Send it via a slave thread
			 */
			do_msg_send(cl, &clmsg, buf);
			continue;
		}

		/*
		 * Special case for error; convert to a special
		 * message flagging it.
		 */
		if (err < 0) {
			send_err(txport);
		} else {
			/*
			 * Otherwise send back the data
			 */
			(void)msg_send(txport, &clmsg);
		}
	}
}

/*
 * serve_client()
 *	Code run as a thread to service proxy filesystem for a given client
 */
static void
serve_client(port_t clport)
{
	struct client *cl;

	/*
	 * First, get a client data structure
	 */
	cl = alloc_cl();
	cl->c_buf = malloc(MAXMSG);

	/*
	 * Get a "reply" side TCP handle
	 */
	rxport = clport;
	txport = clone(rxport);

	/*
	 * Receive first message, which must be an FS_OPEN telling
	 * us where to attach locally
	 */
	if (rxmsg(&cl->c_msg, cl->c_buf) < 0) {
		syslog(LOG_DEBUG, "err on FS_OPEN");
		cleanup();
		exit(1);
	}
	if (cl->c_msg.m_op != FS_OPEN) {
		syslog(LOG_DEBUG, "not an FS_OPEN");
		cleanup();
		exit(1);
	}

	/*
	 * Attach to the indicated location
	 */
	cl->c_local = path_open(cl->c_buf, ACC_READ);
	if (cl->c_local < 0) {
		syslog(LOG_ERR, "client wanted %s", cl->c_buf);
		exit(1);
	}

	/*
	 * Set up hash of clients multiplexed from here
	 */
	clients = hash_alloc(37);
	(void)hash_insert(clients, cl->c_msg.m_sender, cl);

	/*
	 * Fall into main processing loop
	 */
	serve_clients();
}

/*
 * serve()
 *	Endless loop to listen for connection and start a login
 */
static void
serve(port_t p)
{
	struct msg m;
	int x;

	for (;;) {
		/*
		 * Reset to primary channel to listen on clone open
		 */
		m.m_op = FS_SEEK;
		m.m_arg = m.m_arg1 = m.m_nseg = 0;
		(void)msg_send(p, &m);

		/*
		 * Listen for next connection
		 */
		x = wstat(p, "conn=server\n");
		if (x < 0) {
			syslog(LOG_ERR, "Can't listen: %s", strerror());
			exit(1);
		}

		/*
		 * Launch a client for this connection
		 */
		switch (fork()) {
		case 0:
			serve_client(p);
			break;
		case -1:
			syslog(LOG_WARNING, "Can't fork: %s", strerror());
			break;
		default:
			break;
		}
	}
}

int
main(int argc, char **argv)
{
	port_t p;
	static char tcp_buf[] = "net/inet:tcp/11223";

	/*
	 * Access our server port
	 */
	p = path_open(tcp_buf, ACC_READ | ACC_WRITE);
	if (p < 0) {
		perror(tcp_buf);
		exit(1);
	}

	/*
	 * Set up for syslogging
	 */
	(void)openlog("proxyd", LOG_PID, LOG_DAEMON);

	/*
	 * Start serving filesystem requests
	 */
	serve(p);
	return(0);
}
