/*
 * main.c
 *	Main handling loop and startup for semaphore filesystem
 */
#include <sys/namer.h>
#include "sema.h"
#include <hash.h>
#include <llist.h>
#include <stdio.h>
#include <std.h>
#include <syslog.h>

#define NCACHE (16)	/* Roughly, # clients */

static struct hash	/* Map of all active users */
	*filehash;
port_t rootport;	/* Port we receive contacts through */
struct hash *files;	/* Map of integer filename -> openfile */

/*
 * new_client()
 *	Create new per-connect structure
 */
static void
new_client(struct msg *m)
{
	struct file *f;
	struct perm *perms;
	int nperms;

	/*
	 * See if they're OK to access
	 */
	perms = (struct perm *)m->m_buf;
	nperms = (m->m_buflen)/sizeof(struct perm);

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
	f->f_nperm = nperms;
	bcopy(m->m_buf, &f->f_perms, nperms * sizeof(struct perm));
	f->f_perm = ACC_READ|ACC_WRITE;

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

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(filehash, m->m_arg, f)) {
		free(f);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Add ref
	 */
	if (f->f_file) {
		f->f_file->o_refs += 1;
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
	sema_close(f);
	free(f);
}

/*
 * sema_main()
 *	Endless loop to receive and serve requests
 */
static void
sema_main()
{
	struct msg msg;
	int x;
	struct file *f;
	extern int valid_fname(char *, int);

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
	switch (msg.m_op) {
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
		sema_abort(&msg, f);
		break;
	case FS_OPEN:		/* Look up file from directory */
		if ((msg.m_nseg != 1) || !valid_fname(msg.m_buf,
				msg.m_buflen)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		sema_open(&msg, f);
		break;

	case FS_SEEK:		/* Set position */
		sema_seek(&msg, f);
		break;

	case FS_ABSREAD:	/* Read at position */
		if (sema_seek(&msg, f)) {
			break;
		}
		/* VVV fall into VVV */

	case FS_READ:		/* Read file */
		sema_read(&msg, f);
		break;

	case FS_STAT:		/* Tell about file */
		sema_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Set stuff on file */
		sema_wstat(&msg, f);
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
	printf("Usage is: sema <fsname>\n");
	exit(1);
}

/*
 * main()
 *	Startup of a sema filesystem
 *
 * A TMPFS instance expects to start with a command line:
 *	$ sema <filesystem name>
 */
int
main(int argc, char *argv[])
{
	port_name fsname;
	char *namer_name;
	int x;

	/*
	 * Initialize syslog
	 */
	openlog("sema", LOG_PID, LOG_DAEMON);

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
	files = hash_alloc(NCACHE/4);
	if (!filehash || !files) {
		syslog(LOG_ERR, "file/hash not allocated");
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
	sema_main();
	return(0);
}
