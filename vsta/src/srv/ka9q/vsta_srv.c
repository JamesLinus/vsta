/*
 * vsta_srv.c
 *	Offer a VSTa filesystem interface to the KA9Q TCP/IP engine
 *
 * There's more to this than meets the eye.  For outgoing connections
 * and non-clone incoming connections,
 * the client can simply open a port and complete their
 * single point-to-point connection.  Destination IP address and port
 * are specified using FS_WSTAT.
 *
 * However, for servers, a single receiving CLONE port can have many
 * attached clients.  To handle this, an open connection can have
 * a list of connections, with a particular one being the current;
 * the seek position indicates which one.  The position is not
 * changed due to I/O, but can be set using FS_ABS{READ,WRITE}
 * to allow switch-and-I/O in a single message operation.
 *
 * m_arg1 is used to provide
 * standard I/O compatibility if 0, and an efficient read interface
 * for scalable servers if non-zero:
 *
 * A read with m_arg1 == 0 causes the reader to block until data
 * is available.
 *
 * A read with m_arg1 != 0 causes the next available data from any
 * connection to be returned, and m_arg1 is filled in with the connection
 * number.
 *
 * For writes, the current position determines which connection
 * receives the data.
 *
 * Connections are numbered 0..max.  Note that there can be
 * "holes" in the numbering if a client disconnects while other clients
 * exist above.
 */
#include <sys/fs.h>
#include <sys/perm.h>
#include <sys/assert.h>
#include <hash.h>
#include <llist.h>
#include <std.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "timer.h"
#include "tcp.h"
#include "vsta.h"

static void port_daemon(void),
	inetfs_rcv(struct tcb *, int16),
	inetfs_xmt(struct tcb *, int16),
	inetfs_state(struct tcb *, char, char);

#define HASH_SIZE (16)		/* Guess, # clients */

extern int32 ip_addr;		/* Our node's IP address */

static struct hash *clients;	/* /inet filesystem clients */
static struct hash *ports;	/* Port # -> tcp_port mapping */
static port_t serv_port;	/* Our port */
static uint nport;		/* # ports being served */

/*
 * Protection of privileged TCP port numbers
 */
static struct prot port_priv = {
	2,
	0,
	{1,	1},
	{0,	ACC_READ|ACC_WRITE|ACC_CHMOD}
};

/*
 * Per-connection TCP state
 */
struct tcp_conn {
	struct tcb *t_tcb;	/* KA9Q's TCB struct */
	struct tcp_port		/* Back-pointer to tcp_port */
		*t_port;
	uint t_conn;		/*  ...and index in tcp_port */
	struct llist		/* Reader/writer queues */
		t_readers,
		t_writers;
	struct socket		/* Foreign TCP socket */
		t_fsock;
	struct mbuf *t_readq;	/* Queue of mbufs of data to be consumed */
	uint t_mode;		/* Current connect mode of port */
};

/*
 * Per-port TCP state.  Note that there can be numerous distinct
 * client connections, enumerated via t_conns/t_maxconn
 */
struct tcp_port {
	uint t_refs;		/* # clients attached */
	struct prot t_prot;	/* Protection on this TCP port */
	struct socket
		t_lsock;	/* Local TCP socket */
	struct tcp_conn
		**t_conns;	/* Connected remote clients */
	uint t_maxconn;		/* # of slots in t_conns */
	struct llist t_any;	/* Reader for "any" connection */
};

/*
 * Per-client state.  Each client has a TCP port open; multiple
 * clients can have concurrent access to a particular port.  Each
 * client also has a particular connection opened; for the case of
 * a single remote connection, this is always 1.
 */
struct client {
	struct tcp_port
		*c_port;	/* Current port open */
	struct msg		/* Pending I/O, if any */
		c_msg;
	struct llist		/* Queue waiting within */
		*c_entry;
	enum {DIR_ROOT = 0, DIR_TCP, DIR_PORT}
		c_dir;		/* Current dir position */
	uint c_pos;		/* Position, for dir reads */
				/* Connection #, for TCP mode */
};

/*
 * msg_to_mbuf()
 *	Convert VSTa message to mbuf chain
 */
static struct mbuf *
msg_to_mbuf(struct msg *m)
{
	uint x;
	struct mbuf *mb, *mtmp;

	mb = NULLBUF;
	for (x = 0; x < m->m_nseg; ++x) {
		seg_t *s;

		mtmp = alloc_mbuf(0);
		if (mtmp == NULLBUF) {
			(void)free_p(mb);
			return(0);
		}
		s = &m->m_seg[x];
		mtmp->data = s->s_buf;
		mtmp->size = s->s_buflen;
		append(&mb, mtmp);
	}
	return(mb);
}

/*
 * inetfs_seek()
 *	Set file position
 */
static void
inetfs_seek(struct msg *m, struct client *c)
{
	if (m->m_arg < 0) {
		msg_err(m->m_sender, EINVAL);
		return;
	}
	c->c_pos = m->m_arg;
	m->m_buflen = m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * new_client()
 *	Create new per-connect structure
 */
static void
new_client(struct msg *m)
{
	struct client *c;

	/*
	 * Get data structure
	 */
	if ((c = malloc(sizeof(struct client))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Initialize fields
	 */
	bzero(c, sizeof(struct client));

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(clients, m->m_sender, c)) {
		free(c);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Return acceptance
	 */
	msg_accept(m->m_sender);
}

/*
 * dup_client()
 *	Duplicate current file access onto new session
 */
static void
dup_client(struct msg *m, struct client *cold)
{
	struct client *c;

	/*
	 * Get data structure
	 */
	if ((c = malloc(sizeof(struct client))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Fill in fields
	 */
	*c = *cold;

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(clients, m->m_arg, c)) {
		free(c);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Add ref
	 */
	if (c->c_port) {
		c->c_port->t_refs += 1;
	}

	/*
	 * Return acceptance
	 */
	m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * deref_tcp()
 *	Deref a tcp_port, clean up on last ref
 */
static void
deref_tcp(struct tcp_port *t)
{
	t->t_refs -= 1;
	if (t->t_refs == 0) {
		uint x;

		/*
		 * Clean up each connection under the port
		 */
		for (x = 0; x < t->t_maxconn; ++x) {
			struct tcp_conn *c;

			c = t->t_conns[x];
			if (c) {
				ASSERT_DEBUG(LL_EMPTY(&c->t_readers),
					"deref_tcp: readers");
				ASSERT_DEBUG(LL_EMPTY(&c->t_writers),
					"deref_tcp: writers");
				if (c->t_tcb) {
					del_tcp(c->t_tcb); c->t_tcb = 0;
					nport -= 1;
				}
				free(c); t->t_conns[x] = 0;
			}
		}

		/*
		 * Clear out port itself
		 */
		ASSERT_DEBUG(LL_EMPTY(&t->t_any), "deref_tcp: any");
		(void)hash_delete(ports, t->t_lsock.port);
		if (t->t_conns) {
			free(t->t_conns); t->t_conns = 0;
		}
		free(t);
	}
}

/*
 * abort_op()
 *	Abort a pending I/O operation
 */
static void
abort_op(struct client *c)
{
	if (c->c_entry) {
		/* XXX what if the buffer's active? */
		ll_delete(c->c_entry); c->c_entry = 0;
	}
}

/*
 * shut_client()
 *	Close a client
 */
static int
shut_client(long client, struct client *c, int arg)
{
	struct tcp_port *t;

	/*
	 * Remove from queued I/O list
	 */
	abort_op(c);

	/*
	 * Deref TCP port
	 */
	if (t = c->c_port) {
		deref_tcp(t); c->c_port = 0;
	}
	return(0);
}

/*
 * dead_client()
 *	Someone has gone away.  Free their info.
 */
static void
dead_client(struct msg *m, struct client *c)
{
	shut_client(m->m_sender, c, 0);
	(void)hash_delete(clients, m->m_sender);
	free(c);
}

/*
 * cleanup()
 *	Clean up whatever resources are allocated
 */
static void
cleanup(void)
{
	if (serv_port > 0) {
		msg_disconnect(serv_port);
		serv_port = 0;
	}
	if (clients) {
		hash_foreach(clients, shut_client, 0);
		hash_dealloc(clients);
		clients = 0;
	}
	if (ports) {
		hash_dealloc(ports);
		ports = 0;
	}
	vsta_daemon_done(port_daemon);
}

/*
 * find_port()
 *	Get existing tcp_port
 */
static struct tcp_port *
find_port(uint portnum)
{
	return(hash_lookup(ports, portnum));
}

/*
 * uncreate_conn()
 *	Clean up a connection under a tcp_port
 */
static void
uncreate_conn(struct tcp_port *t, struct tcp_conn *c)
{
	uint idx;

	for (idx = 0; idx < t->t_maxconn; ++idx) {
		if (t->t_conns[idx] == c) {
			t->t_conns[idx] = 0;
			free(c);
			return;
		}
	}
	ASSERT(0, "uncreate_conn: not found");
}

/*
 * create_conn()
 *	Create a new connection under a tcp_port
 */
static struct tcp_conn *
create_conn(struct tcp_port *t)
{
	struct tcp_conn *c, **cp;
	uint idx;

	/*
	 * Make room for another connection
	 */
	for (idx = 0; idx < t->t_maxconn; ++idx) {
		if (t->t_conns[idx] == 0) {
			break;
		}
	}
	if (idx == t->t_maxconn) {
		cp = realloc(t->t_conns,
			(t->t_maxconn + 1) * sizeof(struct tcp_conn *));
		if (cp == 0) {
			return(0);
		}
		t->t_conns = cp;
		t->t_maxconn += 1;
	}

	/*
	 * Get connection data structure
	 */
	t->t_conns[idx] = c = malloc(sizeof(struct tcp_conn));
	if (c == 0) {
		/*
		 * Yes, this leaves t_conns resized, but that's harmless.
		 * The NULL return also cleared the slot out correctly.
		 */
		return(0);
	}

	/*
	 * Initialize
	 */
	bzero(c, sizeof(struct tcp_conn));
	ll_init(&c->t_readers);
	ll_init(&c->t_writers);

	return(c);
}

/*
 * create_port()
 *	Create new tcp_port
 */
static struct tcp_port *
create_port(int16 portnum)
{
	struct tcp_port *t;

	t = malloc(sizeof(struct tcp_port));
	if (t == 0) {
		return(0);
	}
	if (hash_insert(ports, portnum, t)) {
		free(t);
		return(0);
	}
	bzero(t, sizeof(struct tcp_port));
	t->t_lsock.address = ip_addr;
	t->t_lsock.port = portnum;
	ll_init(&t->t_any);
	return(t);
}

/*
 * inetfs_open()
 */
static void
inetfs_open(struct msg *m, struct client *cl)
{
	uint portnum;
	struct socket lsocket;
	struct tcp_port *t;

	/*
	 * Handle open request based on current dir level
	 */
	switch (cl->c_dir) {
	case DIR_ROOT:
		if (strcmp(m->m_buf, "tcp")) {
			msg_err(m->m_sender, ESRCH);
			return;
		}
		cl->c_dir = DIR_TCP;
		break;
	case DIR_TCP:
		/*
		 * Numeric chooses a particular TCP port #
		 */
		if (sscanf(m->m_buf, "%d", &portnum) != 1) {
			extern int16 lport;

			/*
			 * Only other entry is "clone"
			 */
			if (!strcmp(m->m_buf, "clone")) {
				msg_err(m->m_sender, ESRCH);
				return;
			}

			/*
			 * Clone open; pick an unused port number
			 */
			portnum = lport++;
		}

		/*
		 * Have a port #, connect to existing or create new
		 */
		t = find_port(portnum);
		if (t == 0) {
			t = create_port(portnum);
			if (t == 0) {
				msg_err(m->m_sender, strerror());
				return;
			}
		}

		/*
		 * Add client to this port, move to this node
		 */
		cl->c_port = t;
		cl->c_dir = DIR_PORT;
		t->t_refs += 1;
		break;

	default:
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Success
	 */
	m->m_arg = m->m_arg1 = m->m_nseg = 0;
	cl->c_pos = 0;
	msg_reply(m->m_sender, m);
}

/*
 * inetfs_stat()
 *	Handle status query operations
 */
static void
inetfs_stat(struct msg *m, struct client *cl)
{
	msg_err(m->m_sender, EINVAL);
}

/*
 * cleario()
 *	Clean up pending I/O on the given llist queue
 */
static void
cleario(struct llist *l, char *err)
{
	struct client *cl;

	while (!LL_EMPTY(l)) {
		struct msg *m;

		cl = LL_NEXT(l)->l_data;
		ll_delete(cl->c_entry); cl->c_entry = 0;
		m = &cl->c_msg;
		if (err) {
			msg_err(m->m_sender, err);
		} else {
			m->m_nseg = m->m_arg = m->m_arg1 = 0;
			msg_reply(m->m_sender, m);
		}
	}
}
/*
 * inetfs_state()
 *	Callback for connection state change
 */
static void
inetfs_state(struct tcb *tcb, char old, char new)
{
	struct tcp_conn *c, *c2;

	/*
	 * For all but new connections, these links exist
	 */
	c = tcb->user;

	/*
	 * Demux for new state
	 */
	switch (new) {
	case ESTABLISHED:
		if (tcb->flags & CLONE) {
			c2 = create_conn(c->t_port);
			c2->t_tcb = tcb;
			c2->t_conn = c->t_conn;
			tcb->user = c;
		} else {
			cleario(&c->t_writers, 0);
		}
		break;

	case FINWAIT1:
	case FINWAIT2:
	case CLOSING:
	case LAST_ACK:
	case TIME_WAIT:
		if (c) {
			cleario(&c->t_readers, EIO);
			cleario(&c->t_writers, EIO);
		}
		break;

	case CLOSE_WAIT:
		/* flush last buffers */
		while (!LL_EMPTY(&c->t_writers)) {
			struct mbuf *mb;
			struct client *cl;

			cl = LL_NEXT(&c->t_writers)->l_data;
			ll_delete(cl->c_entry); cl->c_entry = 0;
			mb = msg_to_mbuf(&cl->c_msg);
			if (mb) {
				send_tcp(tcb, mb);
			}
		}
		close_tcp(tcb);
		break;
	
	case CLOSED:
		del_tcp(tcb);
		if (c) {
			c->t_tcb = 0;
		}
		nport -= 1;
		break;
	}
}

/*
 * inetfs_conn()
 *	Try to change connection mode for this TCP port
 */
static void
inetfs_conn(struct msg *m, struct client *cl, char *val)
{
	struct tcp_port *t = cl->c_port;
	struct tcp_conn *c;
	uint mode;

	/*
	 * Disconnection--shut TCP if open, return success
	 */
	if (!strcmp(val, "disconnect")) {
		if (c->t_tcb) {
			close_tcp(c->t_tcb); c->t_tcb = 0;
			tcb->user = 0;
		}
		(void)msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Decode mode of connection
	 */
	if (!strcmp(val, "server")) {
		mode = TCP_SERVER;
	} else if (!strcmp(val, "passive")) {
		mode = TCP_PASSIVE;
	} else if (!strcmp(val, "active")) {
		/*
		 * Require a destination ip/port pair
		 */
		if (!c->t_fsock.address || !c->t_fsock.port) {
			msg_err(m->m_sender, "ip/no addr");
			return;
		}
		mode = TCP_ACTIVE;
	} else {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Don't permit if connection already initiated
	 */
	if (c->t_tcb) {
		msg_err(m->m_sender, EBUSY);
		return;
	}

	/*
	 * Get tcp_conn first
	 */
	c = create_conn(t);
	c->t_conn = cl->c_pos;

	/*
	 * Request connection, with upcall connections
	 */
	c->t_tcb = open_tcp(&t->t_lsock, NULLSOCK, mode, 0,
		inetfs_rcv, inetfs_xmt, inetfs_state, 0, t);
	if (c->t_tcb == NULLTCB) {
		msg_err(m->m_sender, err2str(net_error));
		uncreate_conn(t, c);
		return;
	}
	c->t_mode = mode;

	/*
	 * Queue as a "writer" for kicking off the connection
	 */
	cl->c_entry = ll_insert(&c->t_writers, cl);
	if (cl->c_entry == 0) {
		msg_err(m->m_sender, strerror());
		close_tcp(c->t_tcb); c->t_tcb = 0;
		uncreate_conn(t, c);
		return;
	}

	/*
	 * Update port tally
	 */
	nport += 1;

	/*
	 * Record client message, wait for connection
	 */
	cl->c_msg = *m;
}

/*
 * get_conn()
 *	Given client, return tcp_conn currently selected, or 0
 */
static struct tcp_conn *
get_conn(struct client *cl)
{
	ulong pos;
	struct tcp_port *t = cl->c_port;

	/*
	 * Pick session based on current file position
	 */
	pos = cl->c_pos;
	if (pos >= t->t_maxconn) {
		return(0);
	}
	return(cl->c_port->t_conns[pos]);
}

/*
 * inetfs_wstat()
 *	Handle status setting operations
 */
static void
inetfs_wstat(struct msg *m, struct client *cl)
{
	char *field, *val;
	struct tcp_port *t = cl->c_port;
	struct tcp_conn *c;

#ifdef LATER
	if (do_wstat(m, &c->c_port->t_prot, c->c_perm, &field, &val)) {
		return;
	}
#endif
	switch (cl->c_dir) {
	case DIR_PORT:
		ASSERT_DEBUG(t, "inetfs_wstat: PORT null port");
		c = get_conn(cl);
		if (c == 0) {
			; /* Fall into error case below */
		}
		if (!strcmp(field, "dest")) {
			c->t_fsock.address = aton(val);
			break;
		} else if (!strcmp(field, "destsock")) {
			c->t_fsock.port = atoi(val);
			break;
		} else if (!strcmp(field, "conn")) {
			inetfs_conn(m, cl, val);
			return;
		}

		/* VVV fall into error case VVV */

	default:
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Return success
	 */
	m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * add_dir()
 *	Add next entry to dir listing
 */
static int
add_dir(long port, struct tcp_port *t, char *buf)
{
	char *p;

	p = buf + strlen(buf);
	sprintf(p, "%d\n", t->t_lsock.port);
	return(0);
}

/*
 * inetfs_read_dir()
 *	Handle read requests when in a directory
 */
static void
inetfs_read_dir(struct msg *m, struct client *cl)
{
	char *buf;
	int len;

	switch (cl->c_dir) {
	case DIR_ROOT:
		buf = "tcp\n"; len = 4;
		break;
	case DIR_TCP:
		buf = alloca(nport * 6);
		buf[0] = '\0';
		hash_foreach(ports, add_dir, buf);
		len = strlen(buf);
		break;
	default:
		len = 0;
		break;
	}

	/*
	 * Adjust for what's been read
	 */
	buf += cl->c_pos;
	len -= cl->c_pos;

	/*
	 * Send back result, with buffer if any
	 */
	if (len > 0) {
		m->m_buf = buf;
		m->m_buflen = len;
		m->m_nseg = 1;
	} else {
		len = 0;
		m->m_nseg = 0;
	}
	m->m_arg = len;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	cl->c_pos += len;
}

/*
 * inetfs_read()
 *	Read data from TCP stream into VSTa client
 */
static void
inetfs_read(struct msg *m, struct client *cl)
{
	struct tcp_port *t = cl->c_port;
	struct tcp_conn *c;

	/*
	 * If not in a port, it's a directory
	 */
	if (cl->c_dir != DIR_PORT) {
		inetfs_read_dir(m, cl);
		return;
	}

	/*
	 * If he doesn't care, pick current stream based on
	 * availability
	 */
	if (m->m_arg1) {
		uint idx = cl->c_pos, found = 0;

		/*
		 * Scan for any TCB with data.  We use a "rover"
		 * to try and be fair about which connection gets
		 * processed next
		 */
		do {
			if (++idx > t->t_maxconn) {
				idx = 0;
			}
			c = t->t_conns[idx];
			if (c && c->t_tcb && c->t_tcb->rcvcnt) {
				found = 1;
				break;
			}
		} while (idx != cl->c_pos);
		cl->c_pos = idx;

		/*
		 * Nothing found; queue under the "any" queue.  Otherwise
		 * we fall down into the regular receive path, with our
		 * current position now reflecting a TCB with data.
		 */
		if (!found) {
			cl->c_entry = ll_insert(&t->t_any, cl);
			if (cl->c_entry == 0) {
				msg_err(m->m_sender, ENOMEM);
				return;
			}
			cl->c_msg = *m;
			return;
		}
	}

	/*
	 * Get tcp_conn, validate
	 */
	c = get_conn(cl);
	if (c == 0) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Queue as a reader, then see if we can get data yet
	 */
	cl->c_entry = ll_insert(&c->t_readers, cl);
	if (cl->c_entry == 0) {
		msg_err(m->m_sender, ENOMEM);
		return;
	}
	inetfs_rcv(c->t_tcb, 0);
}

/*
 * inetfs_write()
 *	Write message from VSTa client onto TCP stream
 */
static void
inetfs_write(struct msg *m, struct client *cl)
{
	struct tcp_port *t = cl->c_port;
	struct mbuf *mb;
	struct tcp_conn *c;
	struct tcb *tcb;
	uint size;

	/*
	 * Output is only legal once you have a particular TCP port open,
	 * and only if you have selected a valid connection index
	 */
	if (!t || !(c = get_conn(cl))) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Convert to mbuf format
	 */
	mb = msg_to_mbuf(m);
	if (mb == 0) {
		msg_err(m->m_sender, ENOMEM);
		return;
	}
	size = len_mbuf(mb);

	/*
	 * Get a a linked list entry, queue us
	 */
	cl->c_entry = ll_insert(&c->t_writers, cl);
	if (cl->c_entry == 0) {
		free_p(mb);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Record our message parameters
	 */
	cl->c_msg = *m;

	/*
	 * If there's nothing outbound on this TCP port, send.
	 * Otherwise queueing is sufficient; our transmit upcall
	 * will send it later.
	 */
	tcb = c->t_tcb;
	if (tcb->sndcnt == 0) {
		if (send_tcp(tcb, mb) < 0) {
			msg_err(m->m_sender, err2str(net_error));
			ll_delete(cl->c_entry); cl->c_entry = 0;
			return;
		}
	}
}

/*
 * proc_msg()
 *	Handle next message to server
 */
static void
proc_msg(struct msg *m)
{
	struct client *cl;

	/*
	 * Get state for client
	 */
	cl = hash_lookup(clients, m->m_sender);
	if (cl == 0) {
		msg_err(m->m_sender, EIO);
		return;
	}

	/*
	 * Switch based on request
	 */
	switch (m->m_op) {
	case M_CONNECT:		/* New client */
		new_client(m);
		break;
	case M_DISCONNECT:	/* Client done */
		dead_client(m, cl);
		break;
	case M_DUP:		/* File handle dup during exec() */
		dup_client(m, cl);
		break;

	case M_ABORT:		/* Aborted operation */
		abort_op(cl);
		m->m_arg = m->m_arg1 = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		break;

	case FS_OPEN:		/* Look up file from directory */
		if ((m->m_nseg != 1) || !valid_fname(m->m_buf,
				m->m_buflen)) {
			msg_err(m->m_sender, EINVAL);
			break;
		}
		inetfs_open(m, cl);
		break;

	case FS_ABSREAD:	/* Set position, then read */
		if (m->m_arg1 < 0) {
			msg_err(m->m_sender, EINVAL);
			break;
		}
		cl->c_pos = m->m_arg1;
		/* VVV fall into VVV */
	case FS_READ:		/* Read file */
		inetfs_read(m, cl);
		break;

	case FS_ABSWRITE:	/* Set position, then write */
		if (m->m_arg1 < 0) {
			msg_err(m->m_sender, EINVAL);
			break;
		}
		cl->c_pos = m->m_arg1;
		/* VVV fall into VVV */
	case FS_WRITE:		/* Write file */
		inetfs_write(m, cl);
		break;

	case FS_SEEK:		/* Set new file position */
		inetfs_seek(m, cl);
		break;

	case FS_STAT:		/* Tell about file */
		inetfs_stat(m, cl);
		break;
	case FS_WSTAT:		/* Set stuff on file */
		inetfs_wstat(m, cl);
		break;
	default:		/* Unknown */
		msg_err(m->m_sender, EINVAL);
		break;
	}
}

/*
 * port_daemon()
 *	Handler for TCP port access from VSTa filesystem
 */
static void
port_daemon(void)
{
	struct msg m;
	int x;

	/*
	 * Receive next message
	 */
	for (;;) {
		x = msg_receive(serv_port, &m);
		p_lock(&ka9q_lock);

		/*
		 * Port error, clean up and finish daemon
		 */
		if (x < 0) {
			perror("TCP port server");
			cleanup();
			v_lock(&ka9q_lock);
			_exit(1);
		}

		/*
		 * Run the message through
		 */
		proc_msg(&m);

		/*
		 * Release lock and loop
		 */
		v_lock(&ka9q_lock);
	}
}

/*
 * vsta1()
 *	Start VSTa /inet server
 */
void
vsta1(int argc, char **argv)
{
	port_name pn;

	/*
	 * Get client & port hash
	 */
	if (clients) {
		printf("Already running\n");
		return;
	}
	clients = hash_alloc(HASH_SIZE);
	ports = hash_alloc(HASH_SIZE);
	if (!clients || !ports) {
		printf("Start failed, no memory\n");
		cleanup();
		return;
	}

	/*
	 * Get server port, advertise it
	 */
	serv_port = msg_port(0, &pn);
	if (serv_port < 0) {
		printf("Can't allocate port\n");
		cleanup();
		return;
	}
	if (namer_register("net/inet", pn) < 0) {
		printf("Can't register port name\n");
		cleanup();
		return;
	}

	/*
	 * Start watcher daemon for this service
	 */
	vsta_daemon(port_daemon);
}

/*
 * vsta0()
 *	Shut down VSTa /inet server
 */
vsta0(void)
{
	cleanup();
}

/*
 * send_data()
 *	Move data from mbufs out to VSTa clients
 */
static void
send_data(struct tcp_conn *c, struct llist *q)
{
	struct msg *m;
	struct client *cl;
	uint mb_pullup = 0;

	/*
	 * While there's data and readers, move data
	 */
	while (c->t_readq && !LL_EMPTY(q)) {
		uint nseg, count, req;
		struct mbuf *mb;

		/*
		 * Dequeue next request
		 */
		cl = LL_NEXT(q)->l_data;
		ll_delete(cl->c_entry); cl->c_entry = 0;

		/*
		 * Fill up scatter/gather until it's maxed or
		 * until we've satisfied the client read.
		 */
		m = &cl->c_msg;
		nseg = 0;
		count = 0;
		req = m->m_arg;
		mb = c->t_readq;
		while (req && mb && (nseg < MSGSEGS)) {
			uint step;

			m->m_seg[nseg].s_buf = mb->data;
			if (mb->size > req) {
				mb_pullup = step = req;
			} else {
				step = mb->size;
			}
			m->m_seg[nseg].s_buflen = step;
			req -= step;
			count += step;
			nseg += 1;
			mb = mb->next;
		}

		/*
		 * Send back to requestor
		 */
		m->m_arg = count;
		m->m_arg1 = c->t_conn;
		m->m_nseg = nseg;
		msg_reply(m->m_sender, m);

		/*
		 * Clean up consumed data
		 */
		while (nseg--) {
			/*
			 * For the last mbuf consumed, it may be
			 * a pullup if we couldn't use all the data
			 */
			if ((nseg == 0) && mb_pullup) {
				pullup(&c->t_readq, NULLCHAR, mb_pullup);
			} else {
				/*
				 * Otherwise just free the mbuf
				 */
				c->t_readq = free_mbuf(c->t_readq);
			}
		}
	}
}

/*
 * inetfs_rcv()
 *	More data has arrived for a connection
 */
static void
inetfs_rcv(struct tcb *tcb, int16 cnt)
{
	struct tcp_conn *c = tcb->user;

	/*
	 * If we consumed all the current data, take a new batch from TCP
	 */
	if (c->t_readq == 0) {
		recv_tcp(tcb, &c->t_readq, 0);
	}

	/*
	 * Send data off to clients
	 */
	send_data(c, &c->t_readers);

	/*
	 * If there's still data, see if an "any" reader is present
	 */
	if (c->t_readq) {
		struct tcp_port *t = c->t_port;

		if (!LL_EMPTY(&t->t_any)) {
			send_data(c, &t->t_any);
		}
	}
}

/*
 * inetfs_xmt()
 *	Upcall when TCP finds room for more data
 */
static void
inetfs_xmt(struct tcb *tcb, int16 count)
{
	struct tcp_port *t = tcb->user;
	struct msg *m;
	struct llist *l;
	struct client *cl;
	struct mbuf *mb;
	struct tcp_conn *c;

	/*
	 * If previous write isn't complete, wait until it is.
	 * Also no-op if there is no writer traffic active.
	 */
	if (tcb->sndcnt || LL_EMPTY(&c->t_writers)) {
		return;
	}

	/*
	 * The first entry in the list will be the most
	 * recent writer
	 */
	c = tcb->user;
	l = LL_NEXT(&c->t_writers);
	cl = l->l_data;
	ll_delete(cl->c_entry); cl->c_entry = 0;

	/*
	 * Complete his write
	 */
	m = &cl->c_msg;
	m->m_arg1 = c->t_conn;
	m->m_nseg = 0;
	msg_reply(m->m_sender, m);

	/*
	 * If no further writes, done
	 */
	if (LL_EMPTY(&c->t_writers)) {
		return;
	}

	/*
	 * Get next writer
	 */
	l = LL_NEXT(&c->t_writers);
	cl = l->l_data;

	/*
	 * Start his data out the door
	 */
	mb = msg_to_mbuf(&cl->c_msg);
	if (send_tcp(tcb, mb) < 0) {
		msg_err(m->m_sender, err2str(net_error));
		ll_delete(cl->c_entry); cl->c_entry = 0;
	}
}
