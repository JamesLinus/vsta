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
#include <sys/assert.h>

extern port_t path_open(char *, int);

/*
 * State of each distinct client under this proxy server
 */
struct client {
	port_t c_local;		/* Local port reached through proxyd */
	struct msg c_msg;	/* Message from client */
	pid_t c_pid;		/* PID of slave thread */
	lock_t c_lock;		/* Mutext for c_queue */
	struct llist c_queue;	/* Linked list FIFO queue */
	int c_abort;		/* Client was interrupting */
	long c_sender;		/* Record of client m_sender */
};
static void free_cl(struct client *);

/*
 * Maximum message size we accept
 */
#define MAXMSG (32768)

/*
 * All the clients connected through this server
 */
static struct hash *clients;
static int num_clients;

/*
 * Read and write ports into the TCP server delivering our clients
 */
static port_t rxport, txport;

/*
 * cleanup()
 *	Toss out the server ports, arranging for TCP reset back to client
 */
static void
cleanup(void)
{
	msg_disconnect(txport);
	(void)wstat(rxport, "conn=disconnect\n");
	sleep(3);
	msg_disconnect(rxport);
}

/*
 * rxmsg()
 *	Receive next message from the TCP byte stream
 */
struct msg *
rxmsg(void)
{
	struct msg *mp;
	int x, msgleft, bodyleft;
	char *bufp, *clmsgp, *body = 0, *bodyp;
	char clbuf[1024];
	static char *pushback;
	static int pushlen;

	/*
	 * Optimism; get message header and buffer
	 */
	mp = malloc(sizeof(struct msg));

	/*
	 * Initialize state to "receiving message header"
	 */
	msgleft = sizeof(struct msg);
	clmsgp = (void *)mp;

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
			struct msg m;

			m.m_op = FS_READ | M_READ;
			m.m_buf = clbuf;
			m.m_arg = m.m_buflen = sizeof(clbuf);
			m.m_nseg = 1;
			m.m_arg1 = 0;
			x = msg_send(rxport, &m);
			if (x < 0) {
				goto out;
			}
			bufp = clbuf;
		}

		/*
		 * If we're still assembling the message header, take
		 * bytes as needed.
		 */
		if (msgleft) {
			uint seg;

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

			/*
			 * Calculate body size from segments in sender's
			 * view of message.  It'll all be coalesced into
			 * a single message on this side.
			 */
			bodyleft = 0;
			for (seg = 0; seg < mp->m_nseg; ++seg) {
				bodyleft += mp->m_seg[seg].s_buflen;
			}
			if (bodyleft > MAXMSG) {
				syslog(LOG_ERR, "message too large");
				goto out;
			}
			if (bodyleft) {
				mp->m_buf = body = bodyp = malloc(bodyleft);
				mp->m_buflen = bodyleft;
				mp->m_nseg = 1;
			} else {
				mp->m_nseg = 0;
			}
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

		/*
		 * We're at the end of the message, return it
		 */
		return(mp);
	}

out:
	/*
	 * This is only used for error cleanup
	 */
	free(mp);
	if (body) {
		free(body);
	}
	return(0);
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
 *
 * The message is encoded as a dump of the message header itself
 * (self-referential messages, cool, huh?) followed by the strerror()
 * buffer.
 */
static void
send_err(long clid)
{
	struct msg m;

	m.m_sender = clid;
	m.m_op = FS_ERR;
	m.m_seg[0].s_buf = &m;
	m.m_seg[0].s_buflen = sizeof(struct msg);
	m.m_seg[1].s_buf = strerror();
	m.m_seg[1].s_buflen = strlen(m.m_seg[1].s_buf) + 1;
	m.m_arg = m.m_seg[0].s_buflen + m.m_seg[1].s_buflen;
	m.m_arg1 = 0;
	m.m_nseg = 2;
	(void)msg_send(txport, &m);
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
	struct llist *ll;
	char *rxbuf;

	for (;;) {
		/*
		 * Wait for work
		 */
		if (mutex_thread(0) < 0) {
			/*
			 * This would be a signal from the main thread;
			 * since we saw the signal on our thread
			 * mutex, it's a simple race, and we'll get
			 * the real work when we correctly mutex
			 * through.
			 */
			continue;
		}

		/*
		 * Grab next operation message
		 */
		p_lock(&cl->c_lock);
		ll = LL_NEXT(&cl->c_queue);
		mp = ll->l_data;
		(void)ll_delete(ll);
		v_lock(&cl->c_lock);

		/*
		 * Client out on the remote side interrupted an
		 * operation.  The signal from our master will have kicked
		 * us out of our own I/O; when we see the actual
		 * M_ABORT message in the queue, we send back our
		 * acknowledgement.
		 */
		if (mp->m_op == M_ABORT) {
			cl->c_abort = 0;
			mp->m_sender = cl->c_sender;
			mp->m_buf = mp;
			mp->m_arg = mp->m_buflen = sizeof(struct msg);
			mp->m_nseg = 1;
			mp->m_arg1 = 0;
			(void)msg_send(txport, mp);
			free(mp);
			continue;
		}

		/*
		 * A remote peer disconnected
		 */
		if (mp->m_op == M_DISCONNECT) {
			free_cl(cl);
			_exit(0);
		}

		/*
		 * Do the message operation, return error or
		 * result.  For results, try to slip the message
		 * header into the scatter/gather.  If it won't fit,
		 * send it in its own message.
		 */
		rxbuf = (mp->m_nseg ? mp->m_buf : 0);
		cl->c_sender = cl->c_msg.m_sender;
		x = msg_send(cl->c_local, mp);
		if (x < 0) {
			send_err(cl->c_sender);
		} else if (mp->m_nseg == MSGSEGS) {
			struct msg m;

			m.m_sender = cl->c_sender;
			m.m_buf = mp;
			m.m_arg = m.m_buflen = sizeof(struct msg);
			m.m_nseg = 1;
			(void)msg_send(txport, &m);
			(void)msg_send(txport, mp);
		} else {
			bcopy(&mp->m_seg[0], &mp->m_seg[1],
				mp->m_nseg * sizeof(seg_t));
			mp->m_sender = cl->c_sender;
			mp->m_buf = mp;
			mp->m_buflen = sizeof(struct msg);
			mp->m_nseg += 1;
			(void)msg_send(txport, &cl->c_msg);
		}

		/*
		 * Free up memory buffers from rxmsg()
		 */
		free(mp);
		if (rxbuf) {
			free(rxbuf);
		}
	}
}

/*
 * do_msg_send()
 *	Take forwarded message, direct it out to the local server
 */
static void
do_msg_send(struct client *cl, struct msg *mp)
{
	/*
	 * If this is the first I/O, allocate an I/O buffer and
	 * launch a thread to serve
	 */
	if (cl->c_pid == 0) {
		cl->c_pid = tfork(serve_slave, (ulong)cl);
	}

	/*
	 * Buffer the message.  After we take the lock, the client
	 * may have returned to the idle case.
	 */
	p_lock(&cl->c_lock);
	(void)ll_insert(&cl->c_queue, mp);
	v_lock(&cl->c_lock);

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
 * free_cl()
 *	Free a client data structure
 */
static void
free_cl(struct client *cl)
{
	struct llist *ll;
	struct msg *mp;

	while (!LL_EMPTY(&cl->c_queue)) {
		ll = LL_NEXT(&cl->c_queue);
		mp = ll->l_data;
		ll_delete(ll);
		if (mp->m_nseg) {
			free(mp->m_buf);
		}
		free(mp);
	}
	free(cl);
}

/*
 * serve_clients()
 *	Read messages and farm out to clients
 */
static void
serve_clients(void)
{
	struct msg *mp;
	struct client *cl;

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
		mp = rxmsg();
		if (!mp) {
			cleanup();
			exit(1);
		}

		/*
		 * Get client pointer based on client ID
		 */
		cl = hash_lookup(clients, mp->m_sender);
		if (cl == 0) {
			syslog(LOG_ERR, "bad m_sender");
			notify(0, 0, "kill");
		}

		/*
		 * Proxy it based on message type
		 */
		switch (mp->m_op) {
		case M_DUP:
			{
			struct client *newcl;

			/*
			 * Duplicate the port, ignore errors
			 */
			newcl = alloc_cl();
			newcl->c_local = clone(cl->c_local);
			(void)hash_insert(clients, mp->m_arg, newcl);
			num_clients += 1;
			}
			break;

		case M_DISCONNECT:
			do_msg_send(cl, mp); mp = 0;
			(void)hash_delete(clients, cl->c_sender);
			if (--num_clients == 0) {
				cleanup();
				notify(0, 0, "kill");
			}
			break;

		case M_ABORT:
			/*
			 * Tell the slave thread to try and interrupt
			 */
			do_msg_send(cl, mp); mp = 0;
			if (cl->c_abort == 0) {
				cl->c_abort = 1;
				notify(0, cl->c_pid, "wakeup");
			}
			break;

		default:
			/*
			 * Send it via a slave thread
			 */
			do_msg_send(cl, mp); mp = 0;
			break;
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
	struct llist *ll;
	struct msg *mp;

	/*
	 * First, get a client data structure
	 */
	cl = alloc_cl();

	/*
	 * Get a "reply" side TCP handle
	 */
	rxport = clport;
	txport = clone(rxport);

	/*
	 * Receive first message, which must be an FS_OPEN telling
	 * us where to attach locally
	 */
	if (rxmsg() == 0) {
		syslog(LOG_DEBUG, "err on FS_OPEN");
		cleanup();
		exit(1);
	}

	/*
	 * Get the message rxmsg() has received
	 */
	ll = LL_NEXT(&cl->c_queue);
	mp = ll->l_data;
	ll_delete(ll);

	/*
	 * The first message has to be the path to attach
	 */
	if (mp->m_op != FS_OPEN) {
		syslog(LOG_DEBUG, "not an FS_OPEN");
		cleanup();
		exit(1);
	}

	/*
	 * Attach to the indicated location
	 */
	cl->c_local = path_open(mp->m_buf, ACC_READ);
	if (cl->c_local < 0) {
		syslog(LOG_ERR, "client wanted %s", mp->m_buf);
		cleanup();
		exit(1);
	}

	/*
	 * Set up hash of clients multiplexed from here
	 */
	clients = hash_alloc(37);
	(void)hash_insert(clients, mp->m_sender, cl);

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
	p = path_open(tcp_buf, ACC_READ | ACC_WRITE | ACC_CHMOD);
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
