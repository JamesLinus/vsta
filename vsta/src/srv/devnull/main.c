/*
 * main.c
 *	Main processing loop for /dev/null
 */
#include <sys/fs.h>

/*
 * devnull_main()
 *	Endless loop to receive and serve requests
 */
static void
devnull_main(port_t rootport)
{
	struct msg m;
	int x;

loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(rootport, &m);
	if (x < 0) {
		perror("devnull: msg_receive");
		goto loop;
	}

	/*
	 * Categorize by basic message operation
	 */
	switch (m.m_op) {

	case M_CONNECT:		/* New client */
		msg_accept(m.m_sender);
		break;

	case M_DISCONNECT:	/* Client done */
		break;

	case M_DUP:		/* File handle dup during exec() */
	case M_ABORT:		/* Aborted operation */
	case FS_SEEK:		/* Set new file position */
		m.m_nseg = m.m_arg = m.m_arg1 = 0;
		msg_reply(m.m_sender, &m);
		break;

	case FS_OPEN:		/* Look up file from directory */
	case FS_REMOVE:		/* Get rid of a file */
		msg_err(m.m_sender, ENOTDIR);
		break;

	case FS_ABSREAD:	/* Set position, then read */
	case FS_READ:		/* Read file */
		m.m_nseg = m.m_arg = m.m_arg1 = 0;
		msg_reply(m.m_sender, &m);
		break;

	case FS_ABSWRITE:	/* Set position, then write */
	case FS_WRITE:		/* Write file */
		/* m.m_arg unchanged */
		m.m_nseg = m.m_arg1 = 0;
		msg_reply(m.m_sender, &m);
		break;

	case FS_STAT:		/* Tell about file */
		m.m_nseg = 1;
		m.m_buf =
"perm=1/1\nacc=7/0/0\nsize=0\ntype=null\nowner=0\ninode=0\n";
		m.m_buflen = strlen(m.m_buf)+1;
		msg_reply(m.m_sender, &m);
		break;

	default:		/* Unknown */
		msg_err(m.m_sender, EINVAL);
		break;
	}
	goto loop;
}

/*
 * main()
 *	Startup of devnull server
 */
main()
{
	port_t rootport;
	port_name fsname;
	int x;

	/*
	 * Last check is that we can register with the given name.
	 */
	rootport = msg_port((port_name)0, &fsname);
	x = namer_register("null", fsname);
	if (x < 0) {
		perror("null");
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	devnull_main(rootport);
}
