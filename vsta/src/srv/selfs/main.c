/*
 * main.c
 *	Main handling loop and startup for select filesystem
 */
#include <sys/namer.h>
#include "selfs.h"
#include <hash.h>
#include <llist.h>
#include <stdio.h>
#include <std.h>
#include <syslog.h>
#include <random.h>

#define NCACHE (16)	/* Roughly, # clients */

struct hash *filehash;	/* Map of all active users */
port_t rootport;	/* Port we receive contacts through */
struct hash *files;	/* Map of integer filename -> openfile */
struct llist		/* List of timeout select() clients */
	time_waiters;
port_name fsname;	/* Port name assigned by OS */

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
	f->f_mode = MODE_ROOT;
	f->f_key = random();
	f->f_sender = m->m_sender;
	ll_init(&f->f_events);

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
	bcopy(fold, f, sizeof(struct file));
	f->f_sender = m->m_arg;

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
 * selfs_main()
 *	Endless loop to receive and serve requests
 */
static void
selfs_main()
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
		selfs_abort(&msg, f);
		break;

	case FS_OPEN:		/* Walk down from root node */
		selfs_open(&msg, f);
		break;

	case FS_READ:		/* Read file */
		selfs_read(&msg, f);
		break;
	case FS_WRITE:		/* Write file */
		selfs_write(&msg, f);
		break;

	case FS_STAT:		/* Tell about node */
		selfs_stat(&msg, f);
		break;

	case SEL_TIME:		/* Timeout interval has occurred */
		selfs_timeout(&msg);
		break;

	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	goto loop;
}

/*
 * main()
 *	Startup of a selfs filesystem
 */
int
main(int argc, char *argv[])
{
	char *namer_name;
	int x;

	/*
	 * Initialize syslog
	 */
	openlog("selfs", LOG_PID, LOG_DAEMON);

	/*
	 * Name we'll offer service as
	 */
	if (argc > 1) {
		namer_name = argv[1];
	} else {
		namer_name = "fs/select";
	}

	/*
	 * Allocate data structures we'll need
	 */
        filehash = hash_alloc(NCACHE/4);
	files = hash_alloc(NCACHE/4);
	if (!filehash || !files) {
		syslog(LOG_ERR, "file/hash not allocated");
		exit(1);
        }
	ll_init(&time_waiters);

	/*
	 * Last check is that we can register with the given name.
	 */
	rootport = msg_port((port_name)0, &fsname);
	x = namer_register(namer_name, fsname);
	if (x < 0) {
		syslog(LOG_ERR, "can't register name '%s'", namer_name);
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	syslog(LOG_INFO, "filesystem established");
	selfs_main();
	return(0);
}
