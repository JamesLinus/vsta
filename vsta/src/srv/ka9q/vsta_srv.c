/*
 * vsta_srv.c
 *	Offer a VSTa filesystem interface to the KA9Q TCP/IP engine
 */
#include <sys/fs.h>
#include <sys/perm.h>
#include <hash.h>
#include <llist.h>
#include "mbuf.h"
#include "vsta.h"

static void port_daemon(void);

const int hash_size = 16;	/* Guess, # clients */

extern int32 ip_addr;		/* Our node's IP address */

static struct hash *clients;	/* /inet filesystem clients */
static struct hash *ports;	/* Port # -> tcp_port mapping */
static port_t serv_port;	/* Our port */

/*
 * Per-port TCP state.  Note that there can be numerous distinct
 * client connections
 */
struct tcp_port {
	struct tcb *t_tcb;	/* KA9Q's TCB struct */
	uchar t_mode;		/* TCP_{SERVER/PASSIVE/ACTIVE} */
	uchar t_conn;		/* tcp_open() called yet? */
	ushort t_refs;		/* # clients attached */
	struct llist		/* Reader/writer queues */
		t_readers,
		t_writers;
};

/*
 * Per-client state.  Each client has a TCP port open; multiple
 * clients can have concurrent access to a particular port.
 */
struct client {
	struct tcp_port
		*c_port;	/* Current port open */
	struct msg		/* Pending I/O, if any */
		c_msg;
	struct llist		/* Queue waiting within */
		*c_entry;
	uint c_pos;		/* Position, for dir reads */
};

/*
 * tcpfs_seek()
 *	Set file position
 */
static void
tcpfs_seek(struct msg *m, struct client *c)
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
	struct perm *perms;
	uint x, uperms, nperms;

	/*
	 * See if they're OK to access
	 */
	perms = (struct perm *)m->m_buf;
	nperms = (m->m_buflen)/sizeof(struct perm);
	x = perm_calc(perms, nperms, &tcp_prot);
	if ((x & (ACC_READ | ACC_WRITE)) != (ACC_READ | ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

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
	if ((t->t_refs == 0) && t->t_tcb) {
		close_tcp(t->t_tcb);
		t->t_tcb = 0;
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
		ll_delete(c->c_entry);
		c->c_entry = 0;
	}
}

/*
 * shut_client()
 *	Close a client
 */
static void
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
	t = c->c_port;
	if (t) {
		deref_tcp(t);
		c->c_port = 0;
	}
}

/*
 * dead_client()
 *	Someone has gone away.  Free their info.
 */
static void
dead_client(struct msg *m, struct client *c)
{
	shut_client(m->m_sender, c, 0)
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
 * create_port()
 *	Create new tcp_port
 */
static struct tcp_port *
create_port(uint portnum)
{
	struct tcp_port *t;

	t = malloc(sizeof(struct tcp_port));
	if (t == 0) {
		return(0);
	}
	bzero(t, sizeof(struct tcp_port));
	ll_init(&t->t_readers);
	ll_init(&t->t_writers);
	return(t);
}

/*
 * tcpfs_open()
 */
static void
tcpfs_open(struct msg *m, struct client *c)
{
	uint portnum;
	struct socket lsocket;
	struct tcp_port *t;

	/*
	 * Can only open downward into a file from top dir
	 */
	if (c->c_port) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Numeric chooses a particular TCP port #
	 */
	if (sscanf(m->m_buf, "%d", &portnum) != 1) {
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
	 * Add client to this port
	 */
	c->c_port = t;
	t->t_refs += 1;

	/*
	 * Success
	 */
	m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * proc_msg()
 *	Handle next message to server
 */
static void
proc_msg(struct msg *m)
{
	struct client *c;

	/*
	 * Get state for client
	 */
	c = hash_lookup(clients, m->m_sender);
	if (c == 0) {
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
		dead_client(m, f);
		break;
	case M_DUP:		/* File handle dup during exec() */
		dup_client(m, f);
		break;

	case M_ABORT:		/* Aborted operation */
		abort_op(c);
		m->m_arg = m->m_arg1 = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		break;

	case FS_OPEN:		/* Look up file from directory */
		if ((m->m_nseg != 1) || !valid_fname(m->m_buf,
				m->m_buflen)) {
			msg_err(m->m_sender, EINVAL);
			break;
		}
		tcpfs_open(m, f);
		break;

	case FS_ABSREAD:	/* Set position, then read */
		if (msg.m_arg1 < 0) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		f->f_pos = msg.m_arg1;
		/* VVV fall into VVV */
	case FS_READ:		/* Read file */
		tcpfs_read(m, f);
		break;

	case FS_ABSWRITE:	/* Set position, then write */
		if (msg.m_arg1 < 0) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		f->f_pos = msg.m_arg1;
		/* VVV fall into VVV */
	case FS_WRITE:		/* Write file */
		tcpfs_write(m, f);
		break;

	case FS_SEEK:		/* Set new file position */
		tcpfs_seek(m, f);
		break;

	case FS_REMOVE:		/* Get rid of a file */
		if ((msg.m_nseg != 1) || !valid_fname(msg.m_buf,
				msg.m_buflen)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		tcpfs_remove(m, f);
		break;

	case FS_STAT:		/* Tell about file */
		tcpfs_stat(m, f);
		break;
	case FS_WSTAT:		/* Set stuff on file */
		tcpfs_wstat(m, f);
		break;
	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
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
 *	Start VSTa TCP server
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
	clients = hash_alloc(hash_size);
	ports = hash_alloc(hash_size);
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

/* XXX stuff to listen for a connection */
{
	struct socket lsocket;

	/* Incoming Telnet */
	lsocket.address = ip_addr;

	if (argc < 2) {
		lsocket.port = TELNET_PORT;
	} else {
		lsocket.port = atoi(argv[1]);
	}

	tnix_tcb = open_tcp(&lsocket,NULLSOCK,TCP_SERVER,0,
			tnix_rcv,tnix_xmt,tnix_state,0,(char *)NULL);

	if (tnix_tcb == NULLTCB) {
		fprintf(stderr, "start telunix fails rsn %d.\n",
			net_error);
	} else {
		log(tnix_tcb,"STARTED Telunix - (%d %x)",
			lsocket.port,tnix_tcb);
	}
}

/*
 * tnix0()
 *	Shut down Telnet server
 */
tnix0(void)
{
	cleanup();
}

/* Handle incoming Telnet-Unix connect requests */
static void
tnix_state(struct tcb *tcb, char old, char new)
{
	struct telnet *tn;
	char *ttyname;
	int i, scanindex;

	tn = (struct telnet *)tcb->user;

	switch(new){
	case ESTABLISHED:
		/* Create and initialize a Telnet protocol descriptor */
		if((tn = (struct telnet *)calloc(1,sizeof(struct telnet))) == NULLTN){
			log(tcb,"reject Telunix - no space");
			sndmsg(tcb,"Rejected; no space on remote\n");
			close_tcp(tcb);
			return;
		}
		tn->session = NULLSESSION;
		tn->state = TS_DATA;
		tcb->user = (char *)tn;	/* Upward pointer */
		tn->tcb = tcb;		/* Downward pointer */
		tn->inbuf = NULLBUF;
		tn->outbuf = NULLBUF;
		ttyname = (char *)0;
		if((scanindex = tnix_addscan(tcb)) < 0 ||
		   (tn->fd = OpenPty(&ttyname)) < 3) {	/* barf if <= stderr */
			tnix_rmvscan(tcb);
			log(tcb,"reject Telunix - no Unix ports");
			sndmsg(tcb,
			    "Rejected; no ports available on remote\n");
			close_tcp(tcb);
 			free (ttyname);
			return;
		}
		tnix_fd2tcb[tn->fd] = tcb;
  		log(tcb,"open Telunix - (%d %x %d %d)",tn->fd,tcb,old,new);

		{ /* negotiate connection */
			char data[] = { IAC, WILL, TN_ECHO, IAC, WILL,
				TN_SUPPRESS_GA /*, IAC, WILL, TN_TRANSMIT_BINARY,
				IAC, WILL, TN_STATUS, IAC, DO, TN_LFLOW, IAC,
				DONT, ECHO */};
			send_tcp (tcb, qdata (data, 6 /* 18 */));
		}

		/* spawn login process */
		switch (sessionpid[scanindex] = fork ()) {
		case -1:
			sndmsg (tcb, "fork failed!\n");
			close_tcp (tcb);
			free (ttyname);
			return;
		case 0:
			for (i = 0; i < getdtablesize(); i++) {
				close(i);
			}
			execl("/vsta/bin/login", ttyname);
			exit(1);
		}
		sndmsg(tcb, "\r\nVSTa ka9q telnet server\r\n");
		free (ttyname);
		break;

	case FINWAIT1:
	case FINWAIT2:
	case CLOSING:
	case LAST_ACK:
	case TIME_WAIT:
		if(tn != NULLTN &&
		   tn->fd > 2) {
			log(tcb,"close Telunix - (%d %x %d %d)",
				tn->fd,tcb,old,new);
			close(tn->fd);
			tnix_fd2tcb[tn->fd] = NULL;
			tn->fd = 0;
		}
		tnix_rmvscan(tcb);
		break;

	case CLOSE_WAIT:
		/* flush that last buffer */
		if(tn != NULLTN &&
		   tn->outbuf != NULLBUF &&
		   tn->outbuf->cnt != 0) {
			send_tcp(tcb,tn->outbuf);
			tn->outbuf = NULLBUF;
		}
		close_tcp(tcb);
		break;
	
	case CLOSED:
		if(tn != NULLTN) {
			if(tn->fd > 2) {
				log(tcb,"close Telunix - (%d %x %d %d)",
					tn->fd,tcb,old,new);
				close(tn->fd);
				tnix_fd2tcb[tn->fd] = NULL;
				tn->fd = 0;
			}
			if(tn->inbuf != NULLBUF)
				free_p(tn->inbuf);
			if(tn->outbuf != NULLBUF)
				free_p(tn->outbuf);
			free((char *)tn);
		}
		tnix_rmvscan(tcb);
		del_tcp(tcb);
		if(tcb == tnix_tcb)
			tnix_tcb = NULLTCB;
		break;
	}
}

/* Telnet receiver upcall routine */
void
tnix_rcv(tcb,cnt)
register struct tcb *tcb;
int16 cnt;
{
	register struct telnet *tn;
	extern void tnix_rmvscan();
	extern int recv_tcp();
	extern void tnix_input();

	if((tn = (struct telnet *)tcb->user) == NULLTN || tn->fd < 3) {
		/* Unknown connection - remove it from queue */
		log(tcb,"error Telnet - tnix_try (%d)", tn);
		tnix_rmvscan(tcb);
		return;
	}
	/*
	 * Check if there is any pending io for the pty:
	 */
	if(tn->inbuf != NULLBUF) {
		tnix_input(tn);
	}
	if(tn->inbuf == NULLBUF) {
		if(tcb->rcvcnt > 0 &&
		   recv_tcp(tcb,&(tn->inbuf),0) > 0)
			tnix_input(tn);
	}
}

/* call when select has input from the pty */

tnix_ready(mask)
     unsigned long mask;
{

  int fd;

  for (fd = 0; mask; fd++, mask >>= 1)
    if (mask & 1) {
	if (tnix_fd2tcb[fd])
	    tnix_read(tnix_fd2tcb[fd]);
    }
}

tnix_read(tcb)
register struct tcb *tcb;
{
	extern void tnix_rmvscan();
	extern int send_tcp();

	register struct telnet *tn;
	register int i;

	if((tn = (struct telnet *)tcb->user) == NULLTN || tn->fd < 3) {
		/* Unknown connection - remove it from queue */
		log(tcb,"error Telnet - tnix_try (%d)", tn);
		tnix_rmvscan(tcb);
		return;
	}
	/*
	 * Next, check if there is any io for tcp:
	 */
	do {
		unsigned char c;

		if(tn->outbuf == NULLBUF &&
		   (tn->outbuf = alloc_mbuf(TURQSIZ)) == NULLBUF)
			return;		/* can't do much without a buffer */
	
#ifdef undef
		if(tn->outbuf->cnt < TURQSIZ) {
			if((i = read(tn->fd, tn->outbuf->data + tn->outbuf->cnt,
				(int)(TURQSIZ - tn->outbuf->cnt))) == -1) {
				if (errno == EAGAIN) return;
				log(tcb,"error Telunix - read (%d %d %d)",
					errno, tn->fd,
					TURQSIZ - tn->outbuf->cnt);
				close_tcp(tcb);
				return;
			}
#endif
		if(tn->outbuf->cnt < TURQSIZ - 1) {
		    i = 1;
		    while (i && tn->outbuf->cnt < TURQSIZ - 1) {
			if((i = read(tn->fd, &c, 1)) == -1) {
				if (errno == EAGAIN) break;
				log(tcb,"error Telunix - read (%d %d %d)",
					errno, tn->fd, 1);
				close_tcp(tcb);
				return;
			}
			if (i > 0) {
				if (c == IAC) {
				  tn->outbuf->data[tn->outbuf->cnt] = IAC;
				  tn->outbuf->data[tn->outbuf->cnt+1] = IAC;
				  i = 2;
				} else if (c == '\r') {
				  tn->outbuf->data[tn->outbuf->cnt] = '\r';
				  tn->outbuf->data[tn->outbuf->cnt+1] = 0;
				  i = 2;
				} else
				  tn->outbuf->data[tn->outbuf->cnt] = c;
			}
			tn->outbuf->cnt += i;
		    }
		    if(tn->outbuf->cnt < TURQSIZ - 1)
		        i = 0;	/* didn't fill buffer so don't retry */

		} else {
			i = -1;		/* any nonzero value will do */
		}
		if(tn->outbuf->cnt == 0)
			return;
		if(send_tcp(tcb,tn->outbuf) < 0) {
			log(tcb,"error Telunix - send_tcp (%d %d %d)",
				net_error, tn->fd, tn->outbuf->cnt);
			close_tcp(tcb);
			tn->outbuf = NULLBUF;
			return;
		}
		tn->outbuf = NULLBUF;
	} while(i);
/*
 * If we've already queued enough data, stop reading from pty, to exert
 * backpressure on application.  But we allow plenty of rope -- 2 MSS
 * worth -- in order to keep the data flow smooth.
 */
	if (tcb->sndcnt >= (tcb->snd.wnd + 2 * tcb->mss)) {
		/* XXX ignore client? */
	}
}

/*
 * transmit done upcall.  The primary purpose of this routine is to
 * start reading from the pty again once enough data has been sent
 * over the network.
 */

tnix_xmt(tcb,cnt)
struct tcb *tcb;
int16 cnt;
{
	register struct telnet *tn;
	extern void tnix_rmvscan();
	extern int recv_tcp();
	extern void tnix_input();

	if((tn = (struct telnet *)tcb->user) == NULLTN || tn->fd < 3) {
		/* Unknown connection - remove it from queue */
		log(tcb,"error Telnet - tnix_try (%d)", tn);
		tnix_rmvscan(tcb);
		return;
	}

	/* if we had disabled reading, reconsider */

	if (! (tnixmask & (1 << tn->fd)))
	  if (tcb->sndcnt < (tcb->snd.wnd + 2 * tcb->mss)) {
		/* XXX enable client */
	}
 }

/* Called by SIGCHLD handler to see if the process has died */

void
tnix_try(tcb,sesspid)
register struct tcb *tcb;
int sesspid;
{
	extern void tnix_rmvscan();
	extern int recv_tcp();
	extern int send_tcp();
	extern void tnix_input();

	register struct telnet *tn;
	register int i;

	if((tn = (struct telnet *)tcb->user) == NULLTN || tn->fd < 3) {
		/* Unknown connection - remove it from queue */
		log(tcb,"error Telnet - tnix_try (%d)", tn);
		tnix_rmvscan(tcb);
		return;
	}

#ifdef XXX
	/* check if session process has died */
	{
		int sesspstat;
		if (wait4 (sesspid, &sesspstat, WNOHANG, NULL)) {
			struct utmp *ut, uu;
			int wtmp;

			tnix_fd2tcb[tn->fd] = NULL;

			utmpname (UTMP_FILE);
			setutent ();
			while (ut = getutent ()) {
				if (ut->ut_pid == sesspid) {
					memcpy (&uu, ut, sizeof (struct utmp));
					uu.ut_type = DEAD_PROCESS;
					time(&uu.ut_time);
/*					strcpy (uu.ut_user, "NONE"); */
					pututline (&uu);
					break;
				}
			}
			endutent ();
			if((wtmp = open(WTMP_FILE, O_APPEND|O_WRONLY)) >= 0) {
			  write(wtmp, (char *)&uu, sizeof(uu));
			  close(wtmp);
			}

			close_tcp (tcb);
			return;
		}
	}
#endif
}

/* Process incoming TELNET characters */
void
tnix_input(tn)
register struct telnet *tn;
{
	void dooptx(),dontoptx(),willoptx(),wontoptx(),answer();
	char *memchr();
	register int i;
	register struct mbuf *bp;
	char c;

	bp = tn->inbuf;

	/* Optimization for very common special case -- no special chars */
	if(tn->state == TS_DATA){
		while(bp != NULLBUF &&
 			memchr(bp->data,'\r',(int)bp->cnt) == NULLCHAR &&
			memchr(bp->data,IAC,(int)bp->cnt) == NULLCHAR) {
			if((i = write(tn->fd, bp->data, (int)bp->cnt)) == bp->cnt) {
				tn->inbuf = bp = free_mbuf(bp);
			} else if(i == -1) {
				log(tn->tcb,"error Telunix - write (%d %d %d)",
					errno, tn->fd, bp->cnt);
				close_tcp(tn->tcb);
				return;
			} else {
				bp->cnt -= i;
				bp->data += i;
				return;
			}
		}
		if(bp == NULLBUF)
			return;
	}
	while(pullup(&(tn->inbuf),&c,1) == 1){
		bp = tn->inbuf;
		switch(tn->state){
		case TS_CR:
			tn->state = TS_DATA;
			if (c == 0 || c == '\n') break; /* cr-nul or cr-nl */
		case TS_DATA:
			if(uchar(c) == IAC){
				tn->state = TS_IAC;
			} else if (uchar (c) == '\r') {
				tn->state = TS_CR;
				if(write(tn->fd, &c, 1) != 1) {
					/* we drop a character here */
					return;
				}
			} else {
#ifdef undef
/* yes, the standard says this, but nobody does it and it breaks things */
				if(!tn->remote[TN_TRANSMIT_BINARY])
					c &= 0x7f;
#endif
				if(write(tn->fd, &c, 1) != 1) {
					/* we drop a character here */
					return;
				}
			}
			break;
		case TS_IAC:
			switch(uchar(c)){
			case WILL:
				tn->state = TS_WILL;
				break;
			case WONT:
				tn->state = TS_WONT;
				break;
			case DO:
				tn->state = TS_DO;
				break;
			case DONT:
				tn->state = TS_DONT;
				break;
			case AYT:
				tn->state = TS_DATA;
				sndmsg (tn->tcb,"\r\n[ka9q here]\r\n");
				break;
			case IAC:
				if(write(tn->fd, &c, 1) != 1) {
					/* we drop a character here */
					return;
				}
				tn->state = TS_DATA;
				break;
			default:
				tn->state = TS_DATA;
				break;
			}
			break;
		case TS_WILL:
			willoptx(tn,c);
			tn->state = TS_DATA;
			break;
		case TS_WONT:
			wontoptx(tn,c);
			tn->state = TS_DATA;
			break;
		case TS_DO:
			dooptx(tn,c);
			tn->state = TS_DATA;
			break;
		case TS_DONT:
			dontoptx(tn,c);
			tn->state = TS_DATA;
			break;
		}
	}
}

/* Use our own copy.  It's too confusing to mix this with the client side */
void
willoptx(tn,opt)
struct telnet *tn;
char opt;
{
	int ack;
	void answer();

	if (debug_options) {
		fprintf(trfp, "[Recv: will ");
		if(uchar(opt) <= NOPTIONS)
			fprintf(trfp, "%s]\n",t_options[opt]);
		else
			fprintf(trfp, "%u]\n",opt);
	}
	
	switch(uchar(opt)){
	default:
		ack = DONT;	/* We don't know what he's offering; refuse */
	}
	answer(tn,ack,opt);
}
void
wontoptx(tn,opt)
struct telnet *tn;
char opt;
{
	void answer();

	if (debug_options) {
		fprintf(trfp, "[Recv: wont ");
		if(uchar(opt) <= NOPTIONS)
			fprintf(trfp, "%s]\n",t_options[opt]);
		else
			fprintf(trfp, "%u]\n",opt);
	}

	if(uchar(opt) <= NOPTIONS){
		if(tn->remote[uchar(opt)] == 0)
			return;		/* Already clear, ignore to prevent loop */
		tn->remote[uchar(opt)] = 0;
	}
	answer(tn,DONT,opt);	/* Must always accept */
}
void
dooptx(tn,opt)
struct telnet *tn;
char opt;
{
	void answer();
	int ack;

	if (debug_options) {
		fprintf(trfp, "[Recv: do ");
		if(uchar(opt) <= NOPTIONS)
			fprintf(trfp, "%s]\n",t_options[opt]);
		else
			fprintf(trfp, "%u]\n",opt);
	}

	switch(uchar(opt)){
/* in fact at the moment we always echo -- better fix this */
	case TN_ECHO:
		if(tn->local[uchar(opt)] == 1)
			return;		/* Already set, ignore to prevent loop */
		tn->local[uchar(opt)] = 1;
		ack = WILL;
		break;
	case TN_SUPPRESS_GA:
		if(tn->local[uchar(opt)] == 1)
			return;		/* Already set, ignore to prevent loop */
		tn->local[uchar(opt)] = 1;
		ack = WILL;
		break;
	default:
		ack = WONT;	/* Don't know what it is */
	}
	answer(tn,ack,opt);
}
void
dontoptx(tn,opt)
struct telnet *tn;
char opt;
{
	void answer();

	if (debug_options) {
		fprintf(trfp, "[Recv: dont ");
		if(uchar(opt) <= NOPTIONS)
			fprintf(trfp, "%s]\n",t_options[opt]);
		else
			fprintf(trfp, "%u]\n",opt);
	}

	if(uchar(opt) <= NOPTIONS){
		if(tn->local[uchar(opt)] == 0){
			/* Already clear, ignore to prevent loop */
			return;
		}
		tn->local[uchar(opt)] = 0;
	}
	answer(tn,WONT,opt);
}


/* This is the SIGCHLD trap handler */

void
tnix_scan()
{
	void tnix_try();
	register int i;

	for(i = 0; i < TUMAXSCAN; i += 1)
		if(tnixtcb[i] != NULLTCB)
			tnix_try(tnixtcb[i], sessionpid[i]);
}

int
tnix_addscan(tcb)
struct tcb *tcb;
{
	register int i;
	for(i = 0; i < TUMAXSCAN; i += 1)
		if(tnixtcb[i] == NULLTCB) {
			tnixtcb[i] = tcb;
			sessionpid[i] =  -1;
			return i;
		}
	return -1;
}

void
tnix_rmvscan(tcb)
struct tcb *tcb;
{
	register int i;

	for(i = 0; i < TUMAXSCAN; i += 1)
		if(tnixtcb[i] == tcb) {
			tnixtcb[i] = NULLTCB;
			if (sessionpid[i] > 0) kill (sessionpid[i], SIGHUP);
			sessionpid[i] = -1;
		}
}
