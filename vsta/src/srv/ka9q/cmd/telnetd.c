/*
 * telnetd.c
 *	Handle incoming telnet sessions via /inet
 */
#include <sys/fs.h>
#include <stdio.h>
#include <syslog.h>
#include <llist.h>

extern path_t path_open(char *, int);

static char *inet = "net/inet";		/* Default port_name for TCP/IP */
static int ip_port = 23;		/* Default TCP port to listen on */
static char buf[128];			/* Utility buffer */

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
};

static struct llist readq,	/* Queue of readers awaiting data */
	dataq;			/*  ...of data awaiting readers */

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
			notify(0, tn->tn_tcp_tid, "kill");
			syslog(LOG_ERROR, "IO server: %s", strerror());
			_exit(1);
		}
		switch (m.m_op) {

		case M_READ:
			tn_read(&m);
			break;

		case M_WRITE:
			msg_send(tn->tn_write, &m);
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
				notify(0, tn->tn_tcp_tid, "kill");
				_exit(1);
			}
			break;

		case M_ABORT:
			l = LL_NEXT(&readq);
			while (l != &readq) {
				struct pendio *p;

				p = l->l_data;
				l = LL_NEXT(l);
				if (p->p_sender == m.m_sender) {
					ll_delete(p->p_entry);
					break;
				}
			}
			m.m_arg = m.m_arg1 = m.m_nseg = 0;
			msg_reply(m.m_sender, &m);
			break;

		case FS_STAT:
			sprintf(buf, "type=c\n");
			m.m_arg = m.m_buflen = strlen(buf);
			m.m_arg1 = 0;
			msg_reply(m.m_sender, &m);
			break;

		default:
			msg_err(m.m_sender, EINVAL);
			break;
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


	/*
	 * clone() the TCP port so we can do simultaneous reads
	 * and writes
	 */
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
	 * Initialize some data structures
	 */
	ll_init(&readq);
	ll_init(&dataq);

	/*
	 * Run a thread to serve this port
	 */
	tn.tn_tcp_tid = gettid();
	tn.tn_serv_tid = tfork(io_server, &tn);
	if (tn.tn_serv_tid < 0) {
		syslog(LOG_WARNING, "Can't fork for IO: %s", strerror());
		_exit(1);
	}
}

/*
 * serve()
 *	Endless loop to listen for connection and start a login
 */
static void
serve(port_t p)
{
	struct msg m;

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
		sprintf(buf, "conn=clone\n", buf);
		m.m_op = FS_WSTAT;
		m.m_buflen = m.m_arg = strlen(buf)+1;
		m.m_arg1 = 0;
		m.m_buf = buf;
		m.m_nseg = 1;
		x = msg_send(p, &m);
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
			run_client(p);
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
	 * Close all files; this simplifies what gets inherited by
	 * clients, and we have syslog() to use for complaints from
	 * now on.
	 */
	for (x = 0; x < getdtablesize(); ++x) {
		close(x);
	}
	(void)openlog("telnetd", LOG_PID, LOG_DAEMON);

	serve(p);
}
