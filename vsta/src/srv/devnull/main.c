/*
 * main.c
 *	Main processing loop for /dev/null or /dev/full
 */
#include <sys/fs.h>
#include <sys/namer.h>
#include <string.h>
#include <syslog.h>

char null_name[NAMESZ];		/* What we're known as */
int being_null = 1;		/* Are we being null or full? */

/*
 * devnull_main()
 *	Endless loop to receive and serve requests
 */
static void
devnull_main(port_t rootport)
{
	struct msg m;
	int x;
	char statstr[80];

loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(rootport, &m);
	if (x < 0) {
		syslog(LOG_ERR, "msg_receive");
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
		if (being_null) {
			/* m.m_arg unchanged */
			m.m_nseg = m.m_arg1 = 0;
			msg_reply(m.m_sender, &m);
		} else {
			msg_err(m.m_sender, ENOSPC);
		}
		break;

	case FS_STAT:		/* Tell about file */
		m.m_nseg = 1;
		sprintf(statstr,
			"perm=1/1\nacc=7/0/0\nsize=0\ntype=%s\n"
			"owner=0\ninode=0\n", null_name);
		m.m_buf = statstr;
		m.m_buflen = strlen(statstr);
		m.m_arg = m.m_arg1 = 0;
		msg_reply(m.m_sender, &m);
		break;

	default:		/* Unknown */
		msg_err(m.m_sender, EINVAL);
		break;
	}
	goto loop;
}

/*
 * usage()
 *	Display the correct usage of the devnull server
 */
static void
usage(void)
{
	printf("usage: devnull [-null | -full]\n");
	exit(1);
}

/*
 * main()
 *	Startup of devnull server
 */
void
main(int argc, char **argv)
{
	port_t rootport;
	port_name fsname;
	int x;

	/*
	 * Initialize syslog
	 */
	openlog("devnull", LOG_PID, LOG_DAEMON);

	/*
	 * Check the server usage
	 */
	if (argc > 2) {
		usage();
	} else if (argc == 2) {
		if (strcmp(argv[1], "-full") == 0) {
			being_null = 0;
		} else if (strcmp(argv[1], "-null")) {
			usage();
		}
	}

	/*
	 * Work out our service name :-)
	 */
	strcpy(null_name, (being_null ? "null" : "full"));

	/*
	 * Last check is that we can register with the given name.
	 */
	rootport = msg_port((port_name)0, &fsname);
	x = namer_register(null_name, fsname);
	if (x < 0) {
		syslog(LOG_ERR,
		       "unable to register '%s' with namer", null_name);
		exit(1);
	}

	syslog(LOG_INFO, "%s service started", null_name);

	/*
	 * Start serving requests for the filesystem
	 */
	devnull_main(rootport);
}
