/*
 * main.c
 *	Main handling loop and startup for tickphore filesystem
 */
#include <sys/namer.h>
#include "tick.h"
#include <hash.h>
#include <stdio.h>
#include <std.h>
#include <syslog.h>

#define NCACHE (16)	/* Roughly, # clients */
#define TICK (600)	/* m_op for second interval event */

static struct hash	/* Map of all active users */
	*filehash;
port_t rootport;	/* Port we receive contacts through */
static port_name	/*  ...its name */
	fsname;

/*
 * new_client()
 *	Create new per-connect structure
 */
static void
new_client(struct msg *m)
{
	struct file *f;

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct file))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Fill in fields
	 */
	bzero(f, sizeof(struct file));

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(filehash, m->m_sender, f)) {
		free(f);
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
dup_client(struct msg *m, struct file *fold)
{
	struct file *f;

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct file))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Fill in fields
	 */
	bzero(f, sizeof(struct file));

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(filehash, m->m_arg, f)) {
		free(f);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Return acceptance
	 */
	m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * dead_client()
 *	Someone has gone away.  Free their info.
 */
static void
dead_client(struct msg *m, struct file *f)
{
	(void)hash_delete(filehash, m->m_sender);
	free(f);
}

/*
 * tick_main()
 *	Endless loop to receive and serve requests
 */
static void
tick_main()
{
	struct msg msg;
	int x;
	struct file *f;

loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(rootport, &msg);
	if (x < 0) {
		syslog(LOG_ERR, "msg_receive");
		sleep(1);
		goto loop;
	}

	/*
	 * Categorize by basic message operation
	 */
	f = hash_lookup(filehash, msg.m_sender);
	switch (msg.m_op & MSG_MASK) {
	case M_CONNECT:		/* New client */
		new_client(&msg);
		break;
	case M_DISCONNECT:	/* Client done */
		dead_client(&msg, f);
		break;
	case M_DUP:		/* File handle dup */
		dup_client(&msg, f);
		break;
	case M_ABORT:		/* Aborted operation */
		tick_abort(&msg, f);
		break;

	case FS_READ:		/* Read file */
		tick_read(&msg, f);
		break;

	case TICK:		/* Second interval has expired */
		empty_queue();
		msg.m_arg = msg.m_nseg = 0;
		msg_reply(msg.m_sender, &msg);
		break;

	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	goto loop;
}

/*
 * usage()
 *	Tell how to use the thing
 */
static void
usage(void)
{
	printf("Usage is: tick <fsname>\n");
	exit(1);
}

/*
 * send_tick()
 *	Sleep one second, and send a TICK m_op
 */
static void
send_tick(int dummy)
{
	port_t p;
	struct msg m;

	p = msg_connect(fsname, 0);
	if (p < 0) {
		syslog(LOG_ERR, "slave can't connect to main port");
		_exit(1);
	}
	for (;;) {
		sleep(1);
		m.m_op = TICK;
		m.m_arg = m.m_nseg = m.m_arg1 = 0;
		(void)msg_send(p, &m);
	}
}

/*
 * main()
 *	Startup of a tick filesystem
 */
int
main(int argc, char *argv[])
{
	char *namer_name;
	int x;

	/*
	 * Initialize syslog
	 */
	openlog("tick", LOG_PID, LOG_DAEMON);

	/*
	 * Check arguments
	 */
	if (argc != 2) {
		usage();
	}

	/*
	 * Name we'll offer service as
	 */
	namer_name = argv[1];

	/*
	 * Allocate data structures we'll need
	 */
        filehash = hash_alloc(NCACHE/4);
	if (!filehash) {
		syslog(LOG_ERR, "file hash not allocated");
		exit(1);
        }

	/*
	 * Launch a one-second interval thread
	 */
	if (tfork(send_tick, 0) < 0) {
		syslog(LOG_ERR, "can't launch slave thread");
		exit(1);
	}

	/*
	 * Last check is that we can register with the given name.
	 */
	rootport = msg_port((port_name)0, &fsname);
	x = namer_register(namer_name, fsname);
	if (x < 0) {
		syslog(LOG_ERR, "can't register name '%s'", fsname);
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	syslog(LOG_INFO, "filesystem established");
	rw_init();
	tick_main();
	return(0);
}
