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
#include <hash.h>
#include <std.h>
#include <lock.h>
#include <sys/syscall.h>
#include <sys/assert.h>
#include <time.h>

extern port_t path_open(char *, int);

/*
 * A linked list of messages
 */
struct cmsg {
	struct cmsg *c_next;	/* Next pointer */
	struct msg c_msg;	/* The message */
};

/*
 * State of each distinct client under this proxy server
 */
struct client {
	port_t c_local;		/* Local port reached through proxyd */
	struct msg c_msg;	/* Message from client */
	pid_t c_pid;		/* PID of slave thread */
	lock_t c_lock;		/* Mutext for c_queue */
	struct cmsg *c_next;	/* Queue of messages to process */
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
 * Stuff which the slave threads would like to have free()'ed
 */
void **pending_free;
lock_t pending_lock;

/*
 * q_free()
 *	Queue something to be free()'ed
 */
static void
q_free(void *ptr)
{
	p_lock(&pending_lock);
	*(void **)ptr = pending_free;
	pending_free = ptr;
	v_lock(&pending_lock);
}

/*
 * unq_free()
 *	Free up everything which is pending
 */
static void
unq_free(void)
{
	void *ptr, *next;

	p_lock(&pending_lock);
	ptr = pending_free;
	pending_free = 0;
	v_lock(&pending_lock);
	while (ptr) {
		next = *(void **)ptr;
		free(ptr);
		ptr = next;
	}
}

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
 * do_reply()
 *	Send back a simple reply message to complete the remote I/O
 */
static void
do_reply(long sender, int arg)
{
	struct msg m, m2;

	m.m_sender = sender;
	m.m_op = m.m_arg1 = m.m_nseg = 0;
	m.m_arg = arg;
	m2.m_op = FS_WRITE;
	m2.m_arg = m2.m_buflen = sizeof(m);
	m2.m_nseg = 1;
	m2.m_buf = &m;
	(void)msg_send(txport, &m2);
}

/*
 * rxmsg()
 *	Receive next message from the TCP byte stream
 */
struct cmsg *
rxmsg(void)
{
	struct msg *mp;
	struct cmsg *cm;
	uint x, msgleft, bodyleft;
	char *bufp, *clmsgp, *body = 0, *bodyp;
	char clbuf[1024];
	static char *pushback;
	static int pushlen;

	/*
	 * Optimism; get message header and buffer
	 */
	cm = malloc(sizeof(struct cmsg));
	mp = &cm->c_msg;

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
				printf("resid of %d bytes\n", pushlen);
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
			{ uint y;
			printf("Got %d bytes:", x);
			for (y = 0; y < x; ++y) printf(" %02x",
				bufp[y] & 0xFF);
			printf("\n");
			}
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
			if (mp->m_nseg > MSGSEGS) {
				syslog(LOG_ERR, "corrupt message header");
				goto out;
			}
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
		return(cm);
	}

out:
	/*
	 * This is only used for error cleanup
	 */
	free(cm);
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
 */
static void
send_err(long clid)
{
	struct msg m, m2;

	/*
	 * Build the FS_ERR message the remote client sees
	 */
	m2.m_sender = clid;
	m2.m_op = FS_ERR;
	m2.m_nseg = 1;
	m2.m_buf = strerror();
	m2.m_arg = m2.m_buflen = strlen(m2.m_buf) + 1;
	m2.m_arg1 = 0;

	/*
	 * Construct the FS_WRITE of this message
	 */
	m.m_op = FS_WRITE;
	m.m_nseg = 2;
	m.m_buf = &m2;
	m.m_buflen = sizeof(struct msg);
	m.m_seg[1].s_buf = m2.m_buf;
	m.m_seg[1].s_buflen = m2.m_buflen;
	m.m_arg = m.m_buflen + m.m_seg[1].s_buflen;
	m.m_arg1 = 0;
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
	struct cmsg *cm;
	struct msg *mp;
	int x;
	char *rxbuf;
	struct msg m;

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
		cm = cl->c_next;
		cl->c_next = cm->c_next;
		v_lock(&cl->c_lock);
		mp = &cm->c_msg;

		/*
		 * Client out on the remote side interrupted an
		 * operation.  The signal from our master will have kicked
		 * us out of our own I/O; when we see the actual
		 * M_ABORT message in the queue, we send back our
		 * acknowledgement.  Once that's done, we flag that
		 * we've caught up with the interrupt request by
		 * clearing the m_op field; out master is watching
		 * this and will free the message at that point.
		 */
		if ((mp->m_op & MSG_MASK) == M_ABORT) {
			do_reply(cl->c_sender, 0);
			mp->m_op = 0;
			continue;
		}

		/*
		 * A remote peer disconnected
		 */
		if ((mp->m_op & MSG_MASK) == M_DISCONNECT) {
			q_free(cm);
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
		} else {
			m.m_op = FS_WRITE;
			m.m_sender = cl->c_sender;
			m.m_buf = mp;
			m.m_arg = m.m_buflen = sizeof(struct msg);
			m.m_nseg = 1;
			(void)msg_send(txport, &m);
			if (mp->m_nseg) {
				mp->m_op = FS_WRITE;
				(void)msg_send(txport, mp);
			}
		}

		/*
		 * Free up memory buffers from rxmsg()
		 */
		if (rxbuf) {
			q_free(rxbuf);
		}
		q_free(cm);
	}
}

/*
 * do_msg_send()
 *	Take forwarded message, direct it out to the local server
 */
static void
do_msg_send(struct client *cl, struct cmsg *cm)
{
	/*
	 * If this is the first I/O, launch a thread to serve
	 */
	if (cl->c_pid == 0) {
		cl->c_pid = tfork(serve_slave, (ulong)cl);
	}

	/*
	 * Buffer the message.
	 */
	p_lock(&cl->c_lock);
	cm->c_next = cl->c_next;
	cl->c_next = cm;
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
	struct msg *mp;
	struct cmsg *cm;

	while ((cm = cl->c_next)) {
		cl->c_next = cm->c_next;
		mp = &cm->c_msg;
		if (mp->m_nseg) {
			q_free(mp->m_buf);
		}
		q_free(cm);
	}
	q_free(cl);
}

/*
 * serve_clients()
 *	Read messages and farm out to clients
 */
static void
serve_clients(void)
{
	struct cmsg *cm;
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
		 * Hook for releasing pending memory
		 */
		if (pending_free) {
			unq_free();
		}

		/*
		 * Get next message
		 */
		cm = rxmsg();
		if (!cm) {
			cleanup();
			exit(1);
		}
		mp = &cm->c_msg;

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
		switch (mp->m_op & MSG_MASK) {
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
			free(cm);
			break;

		case M_DISCONNECT:
			do_msg_send(cl, cm);
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
			do_msg_send(cl, cm);
			do {
				notify(0, cl->c_pid, "wakeup");
				__msleep(100);
			} while ((mp->m_op & MSG_MASK) == M_ABORT);
			free(mp);
			break;

		default:
			/*
			 * Send it via a slave thread
			 */
			do_msg_send(cl, cm);
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
	struct msg *mp;
	struct cmsg *cm;

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
	cm = rxmsg();
	if (cm == 0) {
		cleanup();
		exit(1);
	}
	mp = &cm->c_msg;

	/*
	 * The first message has to be the path to attach
	 */
	if ((mp->m_op & MSG_MASK) != FS_OPEN) {
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
	 * Tell them we're happy
	 */
	printf("Reply to 0x%lx\n", mp->m_sender);
	do_reply(mp->m_sender, 0);

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
	pid_t pid;

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
		pid = fork();
		switch (pid) {
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
	 * Memory lock
	 */
	init_lock(&pending_lock);

	/*
	 * Start serving filesystem requests
	 */
	serve(p);
	return(0);
}
