/*
 * telnetd.c
 *	Handle incoming telnet sessions via /inet
 */
#include <sys/fs.h>
#include <stdio.h>
#include <syslog.h>
#include <llist.h>
#include <std.h>
#include <paths.h>
#include "../telnet.h"

/*
 * State shared between threads serving a TCP->telnet connection
 */
struct tnserv {
	port_name tn_pn;	/* Name of our stdin/out port */
	pid_t tn_tcp_tid,	/* Thread reading /inet */
		tn_serv_tid;	/* Thread handling stdin/out port */
	port_t tn_server,	/* Server side of stdin/out port */
		tn_read,	/* Reading side of /inet port */
		tn_write;	/*  ...writing side */
	uchar			/* Local/remote negotiated options */
		tn_local[NOPTIONS],
		tn_remote[NOPTIONS];
};

extern port_t path_open(char *, int);
static void queue_data(struct tnserv *, uchar *, uint);

typedef unsigned char lock_t;		/* Our mutex type */

static char *inet = "net/inet";		/* Default port_name for TCP/IP */
static int ip_port = 23;		/* Default TCP port to listen on */
static char buf[128];			/* Utility buffer */
static lock_t readq_lock;		/* Mutex for readq */
static int telstate = TS_DATA;		/* State machine for proto */

struct pendio {
	struct msg p_msg;	/* Message from sender */
	struct llist *p_entry;	/* Where this is queued */
};

static struct llist readq,	/* Queue of readers awaiting data */
	dataq;			/*  ...of data awaiting readers */

/*
 * Our private hack mutex package
 */
inline static void
init_lock(volatile lock_t *p)
{
	*p = 0;
}
inline static void
p_lock(volatile lock_t *p)
{
	while (*p) {
		__msleep(20);
	}
	*p = 1;
}
inline static void
v_lock(volatile lock_t *p)
{
	*p = 0;
}

/*
 * mwrite()
 *	Write buffer via a port_t
 */
static void
mwrite(port_t p, uchar *buf, uint cnt)
{
	struct msg m;

	m.m_op = FS_WRITE;
	m.m_buf = buf;
	m.m_arg = m.m_buflen = cnt;
	m.m_nseg = 1;
	m.m_arg1 = 0;
	(void)msg_send(p, &m);
}

/*
 * tn_read()
 *	Handle all the machinery of a client which wants data
 *
 * This includes a reading client for which no data is available, and
 * thus must be put on readq.  It also includes pulling partial data
 * from the dataq, if more data is available than the client has
 * requested.
 */
static void
tn_read(struct msg *m)
{
	int nseg, size;
	struct llist *l;
	struct pendio *p;
	struct msg *mp;
	seg_t *s, *sp;

	/*
	 * If no data yet, get in line
	 */
	if (LL_EMPTY(&dataq)) {
		p = malloc(sizeof(struct pendio));
		if (p == 0) {
			msg_err(m->m_sender, strerror());
			return;
		}
		p->p_entry = ll_insert(&readq, p);
		if (p->p_entry == 0) {
			msg_err(m->m_sender, strerror());
			return;
		}
		p->p_msg = *m;
		return;
	}

	/*
	 * Get next block of data available
	 */
	l = LL_NEXT(&dataq);
	p = l->l_data;
	mp = &p->p_msg;

	/*
	 * If it appears to be less than or equal to the requested
	 * amount, dump it right back
	 */
	if (mp->m_arg <= m->m_arg) {
		msg_reply(m->m_sender, mp);
		ll_delete(p->p_entry);
		free(p);
		return;
	}

	/*
	 * Otherwise extract data until the read request is
	 * satisfied and adjust the queued data element to reflect
	 * the residual
	 */
	size = m->m_arg;
	nseg = 0;
	s = &m->m_seg[0];
	sp = &mp->m_seg[0];
	while ((size > 0) && (nseg < mp->m_nseg)) {
		*s = *sp;
		if (s->s_buflen > size) {
			sp->s_buflen -= size;
			sp->s_buf += size;
			s->s_buflen = size;
			size = 0;
		} else {
			nseg += 1;
			++s, ++sp;
			size -= s->s_buflen;
		}
		mp->m_arg -= s->s_buflen;
	}

	/*
	 * Send back the data
	 */
	m->m_nseg = nseg;
	msg_reply(m->m_sender, m);

	/*
	 * If we consumed any segments, reap either the whole
	 * transaction (if we used them all) or the slot.
	 */
	if (nseg > 0) {
		if (nseg == mp->m_nseg) {
			ll_delete(p->p_entry);
			free(p);
		} else {
			mp->m_nseg -= nseg;
			bcopy(&mp->m_seg[nseg], &mp->m_seg[0],
				(mp->m_nseg - nseg) * sizeof(seg_t));
		}
	}
}

/*
 * telproto()
 *	Handle telnet protocol stuff in a buffer
 */
static void
telproto(struct tnserv *tn, uchar *buf, uint cnt)
{
	uchar c, *p, *pend, *pdest;
	uchar *dest = malloc(cnt);
	static uchar vsta_ayt[] = "\r\n[VSTa telnetd here]\r\n",
		dont[] = {IAC, DONT, 0},
		will[] = {IAC, WILL, 0},
		wont[] = {IAC, WONT, 0};

	/*
	 * We'll be building a buffer with the data minus telnet
	 * noise.
	 */
	dest = malloc(cnt);
	if (dest == 0) {
		return;
	}

	/*
	 * Walk buffer, running state machine
	 */
	p = buf;
	pend = p + cnt;
	pdest = dest;
	while (p < pend) {
		c = *p++;
		switch (telstate) {
		/*
		 * Saw a CR (perhaps CR-LF)
		 */
		case TS_CR:
			telstate = TS_DATA;
			if ((c == '\0') && (c == '\n')) {
				break;
			}

		/*
		 * Unknown, fall into data
		 */
		default:
			telstate = TS_DATA;
			/* VVV fall into VVV */

		/*
		 * Add data to buffer
		 */
		case TS_DATA:
			if (c == IAC) {
				telstate = TS_DATA;
				break;
			}
			*pdest++ = c;
			if (c == '\r') {
				telstate = TS_CR;
			}
			break;

		/*
		 * Telnet interpret-as-command
		 */
		case TS_IAC:
			switch (c) {
			case WILL:
				telstate = TS_WILL;
				break;
			case WONT:
				telstate = TS_WONT;
				break;
			case DO:
				telstate = TS_DO;
				break;
			case DONT:
				telstate = TS_DONT;
				break;
			case AYT:
				mwrite(tn->tn_write,
					vsta_ayt, sizeof(vsta_ayt)-1);
				telstate = TS_DATA;
				break;
			case IAC:
			default:
				telstate = TS_DATA;
				*pdest++ = c;
				break;
			}
			break;
		case TS_WILL:
			dont[2] = c;
			mwrite(tn->tn_write, dont, sizeof(dont));
			telstate = TS_DATA;
			break;
		case TS_WONT:
			if ((c < NOPTIONS) && tn->tn_remote[c]) {
				tn->tn_remote[c] = 0;
				dont[2] = c;
				mwrite(tn->tn_write, dont, sizeof(dont));
			}
			telstate = TS_DATA;
			break;
		case TS_DO:
			if ((c < NOPTIONS) && !tn->tn_local[c]) {
				switch (c) {
				case TN_ECHO:
				case TN_SUPPRESS_GA:
					tn->tn_local[c] = 1;
					will[2] = c;
					mwrite(tn->tn_write,
						will, sizeof(will));
					break;
				default:
					wont[2] = c;
					mwrite(tn->tn_write,
						wont, sizeof(wont));
					break;
				}
			}
			break;
		case TS_DONT:
			if ((c < NOPTIONS) && tn->tn_local[c]) {
				tn->tn_local[c] = 0;
				wont[2] = c;
				mwrite(tn->tn_write, wont, sizeof(wont));
			}
			break;
		}
	}

	/*
	 * We've walked the whole buffer, and advanced our state
	 * machine accordingly.  If there's any data left, feed it
	 * back into queue_data()
	 */
	if (pdest > dest) {
		int realstate;

		/*
		 * Have to suspend non-data state so queue_data()
		 * will accept it.
		 */
		realstate = telstate;
		telstate = TS_DATA;
		queue_data(tn, dest, pdest - dest);
		telstate = realstate;
	}

	/*
	 * Free up temp buffer
	 */
	free(dest);
}

/*
 * queue_data()
 *	Make data available for consumption via the dataq
 *
 * Actually, feed it out directly to any queued readers.  Place any
 * residual on the dataq.  For the latter, the data must be dup'ed so
 * our caller can reuse his buffer.
 */
static void
queue_data(struct tnserv *tn, uchar *buf, uint cnt)
{
	struct llist *l;
	struct pendio *p;
	uint x;

	/*
	 * Send it directly on its way if possible
	 */
	while (!LL_EMPTY(&readq) && cnt) {
		/*
		 * Get next reader
		 */
		l = LL_NEXT(&readq);
		p = l->l_data;

		/*
		 * We'll be sending from here in any case
		 */
		p->p_msg.m_buf = buf;
		p->p_msg.m_nseg = 1;
		p->p_msg.m_arg1 = 0;

		/*
		 * If he can take it all, away it goes
		 */
		x = p->p_msg.m_arg;
		if (x >= cnt) {
			x = cnt;
			cnt = 0;
		} else {
			/*
			 * Give him the requested amount
			 */
			buf += x;
			cnt -= x;
		}

		/*
		 * Fill in I/O length decided, reply to reader
		 */
		p->p_msg.m_buflen = p->p_msg.m_arg = x;
		msg_reply(p->p_msg.m_sender, &p->p_msg);

		/*
		 * This pending I/O is complete
		 */
		ll_delete(p->p_entry);
		free(p);
	}

	/*
	 * If all data consumed, we're done
	 */
	if (cnt == 0) {
		return;
	}

	/*
	 * Queue the rest for future use
	 */
	p = malloc(sizeof(struct pendio));
	if (p == 0) {
		return;
	}
	p->p_msg.m_buf = malloc(cnt);
	if (p->p_msg.m_buf == 0) {
		free(p);
		return;
	}
	p->p_entry = ll_insert(&dataq, p);
	if (p->p_entry == 0) {
		free(p->p_msg.m_buf);
		free(p);
		return;
	}
	bcopy(buf, p->p_msg.m_buf, cnt);
	p->p_msg.m_arg = p->p_msg.m_buflen = cnt;
	p->p_msg.m_arg1 = 0;
}

/*
 * io_server()
 *	Thread which provides stdin/out to our local processes
 */
static void
io_server(struct tnserv *tn)
{
	struct msg m;
	int x, nrefs = 0;
	struct llist *l;

	for (;;) {
		x = msg_receive(tn->tn_server, &m);
		if (x < 0) {
			syslog(LOG_ERR, "IO server: %s", strerror());
			notify(0, tn->tn_tcp_tid, "kill");
			_exit(1);
		}
		switch (m.m_op) {

		case FS_READ:
			p_lock(&readq_lock);
			tn_read(&m);
			v_lock(&readq_lock);
			break;

		case FS_WRITE:
			m.m_arg = msg_send(tn->tn_write, &m);
			m.m_nseg = m.m_arg1 = 0;
			msg_reply(m.m_sender, &m);
			break;

		case M_DUP:
			nrefs += 1;
			m.m_arg = m.m_arg1 = m.m_nseg = 0;
			msg_reply(m.m_sender, &m);
			break;

		case M_CONNECT:
			nrefs += 1;
			msg_accept(m.m_sender);
			break;

		case M_DISCONNECT:
			nrefs -= 1;
			if (nrefs < 1) {
				syslog(LOG_NOTICE,
					"IO server: all clients done");
				notify(0, tn->tn_tcp_tid, "kill");
				(void)wstat(tn->tn_write,
					"conn=disconnect\n");
				_exit(1);
			}
			break;

		case M_ABORT:
			l = LL_NEXT(&readq);
			while (l != &readq) {
				struct pendio *p;

				p = l->l_data;
				l = LL_NEXT(l);
				if (p->p_msg.m_sender == m.m_sender) {
					ll_delete(p->p_entry);
					free(p);
					break;
				}
			}
			m.m_arg = m.m_arg1 = m.m_nseg = 0;
			msg_reply(m.m_sender, &m);
			break;

		case FS_STAT:
			sprintf(buf, "type=c\n");
			m.m_buf = buf;
			m.m_nseg = 1;
			m.m_arg = m.m_buflen = strlen(buf);
			m.m_arg1 = 0;
			msg_reply(m.m_sender, &m);
			break;

		default:
			msg_err(m.m_sender, EINVAL);
			break;
		}
	}
}

/*
 * inet_reader()
 *	Pull data in an endless loop from /inet, queue to dataq
 */
static void
inet_reader(struct tnserv *tn)
{
	int x;
	struct msg m;
	uchar buf[1024];
	static uchar defaults[] = {
		IAC, WILL, TN_ECHO,
		IAC, WILL, TN_SUPPRESS_GA,
	};

	/*
	 * Kick off telnet negotiation by asking for something
	 * which works well for cbreak-ish remote echo.
	 */
	mwrite(tn->tn_write, defaults, sizeof(defaults));

	for (;;) {
		/*
		 * Set up read, get data
		 */
		m.m_op = M_READ | FS_READ;
		m.m_nseg = 1;
		m.m_buf = buf;
		m.m_arg = m.m_buflen = sizeof(buf);
		m.m_arg1 = 0;
		x = msg_send(tn->tn_read, &m);
		if (x < 0) {
			syslog(LOG_ERR, "/inet gone: %s", strerror());
			notify(getpid(), 0, "kill");
			_exit(1);
		}

		/*
		 * Queue data, with interlock
		 */
		p_lock(&readq_lock);

		/*
		 * If we're involved in telnet protocol handling, call out
		 * to our special routine, otherwise just queue as
		 * straight data.
		 */
		if ((telstate != TS_DATA) ||
				memchr(buf, IAC, x) ||
				memchr(buf, '\r', x)) {
			telproto(tn, buf, x);
		} else {
			queue_data(tn, buf, x);
		}

		v_lock(&readq_lock);
	}
}

/*
 * launch_client()
 *	Run a client on the given port
 *
 * We are in our own process here, with our own connection to the
 * /inet server.  Our "file position" keeps us talking to a particular
 * remote telnet, and we launch a second thread to serve the stdin/stdout
 * for the login process.
 */
static void
launch_client(port_t tn_read)
{
	struct tnserv tn;
	pid_t pid;

	/*
	 * clone() the TCP port so we can do simultaneous reads
	 * and writes
	 */
	bzero(&tn, sizeof(tn));
	tn.tn_read = tn_read;
	tn.tn_write = clone(tn_read);
	if (tn.tn_write < 0) {
		syslog(LOG_WARNING, "Can't clone port: %s", strerror());
		_exit(1);
	}

	/*
	 * Create a port to serve as stdin/out
	 */
	tn.tn_server = msg_port(0, &tn.tn_pn);
	if (tn.tn_server < 0) {
		syslog(LOG_WARNING, "Can't create PTY: %s", strerror());
		_exit(1);
	}

	/*
	 * Launch the login process
	 */
	pid = fork();

	/*
	 * Failure?
	 */
	if (pid < 0) {
		syslog(LOG_ERR, "Can't fork: %s", strerror());
		_exit(1);
	}

	/*
	 * Child--run login
	 */
	if (pid == 0) {
		port_t p;
		int x;
		char buf[16];

		/*
		 * Launch login with our port_name as his
		 * stdin/out
		 */
		sprintf(buf, "%d", tn.tn_pn);
		(void)execl(_PATH_LOGIN, "login", buf, (char *)0);
		syslog(LOG_ERR, "Can't login: %s", strerror());
		_exit(1);
	}

	/*
	 * Run a thread to serve the child's port
	 */
	tn.tn_tcp_tid = gettid();
	tn.tn_serv_tid = tfork(io_server, (ulong)&tn);
	if (tn.tn_serv_tid < 0) {
		syslog(LOG_WARNING, "Can't fork for IO: %s", strerror());
		_exit(1);
	}

	/*
	 * This thread then pulls data from /inet and queues it
	 */
	inet_reader(&tn);
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
		x = fork();
		if (x == 0) {
			/*
			 * Clone off a distinct connection for the client
			 */
			launch_client(p);
		}
		if (x < 0) {
			syslog(LOG_WARNING, "Can't tfork: %s", strerror());
			continue;
		}
	}
}

main(int argc, char **argv)
{
	int x;
	port_t p;

	for (x = 1; x < argc; ++x) {
		if (argv[x][0] != '-') {
			fprintf(stderr, "%s: unknown argument %s\n",
				argv[0], argv[x]);
			exit(1);
		}
		switch (argv[x][1]) {
		case 'p':
			if (argv[++x] == 0) {
				fprintf(stderr, "Missing arg\n");
				exit(1);
			}
			ip_port = atoi(argv[x]);
			break;
		case 'i':
			if (argv[++x] == 0) {
				fprintf(stderr, "Missing arg\n");
				exit(1);
			}
			inet = argv[x];
			break;
		default:
			fprintf(stderr, "%s: unknown option: %s\n",
				argv[0], argv[x]);
			exit(1);
		}
	}

	/*
	 * Access our telnet port
	 */
	sprintf(buf, "%s:tcp/%d", inet, ip_port);
	p = path_open(buf, ACC_READ | ACC_WRITE);
	if (p < 0) {
		perror(buf);
		exit(1);
	}

	/*
	 * Set up for syslogging
	 */
	(void)openlog("telnetd", LOG_PID, LOG_DAEMON);

	/*
	 * Initialize locks and lists
	 */
	init_lock(&readq_lock);
	ll_init(&readq);
	ll_init(&dataq);

	/*
	 * Start serving telnet
	 */
	serve(p);
}
